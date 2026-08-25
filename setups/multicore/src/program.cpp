#include "program.h"

#include "blur.h"
#include "modules/Core.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Cores the program supplies code for. Must match cores.num in system.yaml.
// Both are kept in sync by set_cores.sh.
constexpr unsigned NUM_CORES = 4;

// Relative to the simulator's working directory.
const char *INPUT_PATH  = "setups/multicore/data/input.png";
const char *OUTPUT_PATH = "setups/multicore/data/output.png";

// Both frames sit in the memory chiplet's off-chip window, so memory.size must
// leave room for 2 x FRAME_BYTES there.
constexpr uint32_t FRAME_BYTES = IMAGE_HEIGHT * ROW_BYTES;
constexpr uint32_t INPUT_BASE  = 0x0;
constexpr uint32_t RESULT_BASE = FRAME_BYTES;

// Interval at which a core re-checks whether the frame has been staged.
constexpr unsigned POLL_CYCLES = 500;

// Concurrent transactions need distinct ids, so every core gets its own pair.
uint32_t fetch_id(unsigned core_index) {
	return 2 * core_index;
}

uint32_t store_id(unsigned core_index) {
	return 2 * core_index + 1;
}

// Shared work queue over the bands. Claiming never suspends, so no locking.
unsigned next_band  = 0;
unsigned bands_done = 0;

// Cores that started, so the report holds when cores.num is overridden.
unsigned cores_running = 0;

// Set once the input frame is in the memory chiplet and bands can be claimed.
bool frame_staged = false;

sc_time stage_end;
sc_time filter_end;

unsigned claim_band() {
	return next_band < BANDS ? next_band++ : BANDS;
}

// Move `length` bytes to or from the memory chiplet, split into bursts of the
// largest size the AXI port accepts.
void transfer(Core &core, bool write, uint32_t request_id, unsigned char *data, uint32_t address, unsigned length) {
	const unsigned max_burst = core.MAX_INCR_BURST_SIZE;

	for (unsigned done = 0; done < length;) {
		const unsigned burst = std::min(length - done, max_burst);
		auto           request =
		    AxiRequest(request_id, data + done, burst).to_via("memchip", "memory", "slink").set_addr(address + done);
		if (write) {
			core.write(request)->wait();
		} else {
			core.read(request)->wait();
		}
		done += burst;
	}
}

// Rows of band `band`, and the frame rows to fetch: its own plus a halo row on
// each side, clamped to the image.
unsigned band_rows(unsigned band) {
	return std::min(BAND_ROWS, IMAGE_HEIGHT - band * BAND_ROWS);
}

unsigned first_source_row(unsigned band) {
	return (band == 0) ? 0 : band * BAND_ROWS - 1;
}

unsigned source_rows(unsigned band) {
	const unsigned last = std::min(band * BAND_ROWS + band_rows(band), IMAGE_HEIGHT - 1);
	return last - first_source_row(band) + 1;
}

void stage_frame(Core &core) {
	int   width = 0, height = 0, channels = 0;
	auto *image = stbi_load(INPUT_PATH, &width, &height, &channels, CHANNELS);
	if (!image) {
		std::cout << "Cannot read " << INPUT_PATH << ": " << stbi_failure_reason() << std::endl;
		sc_stop();
		return;
	}
	if (unsigned(width) != IMAGE_WIDTH || unsigned(height) != IMAGE_HEIGHT) {
		std::cout << INPUT_PATH << " is " << width << "x" << height << ", but blur.h declares " << IMAGE_WIDTH << "x"
		          << IMAGE_HEIGHT << "." << std::endl;
		stbi_image_free(image);
		sc_stop();
		return;
	}

	transfer(core, true, fetch_id(0), image, INPUT_BASE, FRAME_BYTES);
	stbi_image_free(image);

	stage_end    = sc_time_stamp();
	frame_staged = true;
}

void collect_frame(Core &core) {
	std::vector<unsigned char> frame(FRAME_BYTES);
	transfer(core, false, fetch_id(0), frame.data(), RESULT_BASE, FRAME_BYTES);

	if (!stbi_write_png(OUTPUT_PATH, IMAGE_WIDTH, IMAGE_HEIGHT, CHANNELS, frame.data(), ROW_BYTES)) {
		std::cout << "Cannot write " << OUTPUT_PATH << std::endl;
	}
}

void report() {
	const sc_time total = sc_time_stamp();
	std::cout << "Blurred " << IMAGE_WIDTH << "x" << IMAGE_HEIGHT << " in " << BANDS << " band(s) of " << BAND_ROWS
	          << " row(s) on " << cores_running << " core(s)." << std::endl;
	std::cout << "  stage   " << stage_end << std::endl;
	std::cout << "  filter  " << filter_end - stage_end << std::endl;
	std::cout << "  collect " << total - filter_end << std::endl;
	std::cout << "  total   " << total << std::endl;
	std::cout << "  result  " << OUTPUT_PATH << std::endl;
}

CPUCode core_code(unsigned core_index) {
	return CPUCode{.main = [core_index](Core &core) {
		++cores_running;

		if (core_index == 0) {
			stage_frame(core);
		}
		while (!frame_staged) {
			core.wait_cycles(POLL_CYCLES);
		}

		std::vector<unsigned char> input(BAND_IN_BYTES);
		std::vector<unsigned char> output(BAND_OUT_BYTES);

		for (unsigned band = claim_band(); band < BANDS; band = claim_band()) {
			const unsigned rows    = band_rows(band);
			const unsigned fetched = source_rows(band);

			// At the top and bottom edge the missing halo row is replicated rather
			// than fetched, so the kernel always sees rows + 2 rows.
			const unsigned offset = (band == 0) ? ROW_BYTES : 0;
			transfer(core, false, fetch_id(core_index), input.data() + offset,
			         INPUT_BASE + first_source_row(band) * ROW_BYTES, fetched * ROW_BYTES);
			if (offset > 0) {
				std::copy_n(input.begin() + ROW_BYTES, ROW_BYTES, input.begin());
			}
			if (offset + fetched * ROW_BYTES < BAND_IN_BYTES) {
				const unsigned last = offset + (fetched - 1) * ROW_BYTES;
				std::copy_n(input.begin() + last, ROW_BYTES, input.begin() + last + ROW_BYTES);
			}

			blur_band(input.data(), rows, output.data());
			core.wait_cycles("blur");

			transfer(core, true, store_id(core_index), output.data(), RESULT_BASE + band * BAND_ROWS * ROW_BYTES,
			         rows * ROW_BYTES);

			if (++bands_done == BANDS) {
				filter_end = sc_time_stamp();
				collect_frame(core);
				report();
				sc_stop();
			}
		}
	}};
}

} // namespace

ModuleCodeMap *get_program_code() {
	static ModuleCodeMap code = [] {
		ModuleCodeMap map;
		for (unsigned index = 0; index < NUM_CORES; ++index) {
			map[{"compute", "core" + std::to_string(index)}] = core_code(index);
		}
		return map;
	}();
	return &code;
}
