#include "program.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "matmul.h"
#include "modules/Core.h"

namespace {

std::string worker_name(int chiplet) {
	return "chiplet" + std::to_string(chiplet);
}

void print_result(const int *result) {
	std::cout << "Temporary Matrix Result" << std::endl;
	for (int row = 0; row < MATRIX_SIZE; row++) {
		std::stringstream ss;
		for (int col = 0; col < MATRIX_SIZE; col++) {
			ss << result[row * MATRIX_SIZE + col] << " ";
		}
		std::cout << ss.str() << std::endl;
	}
	std::cout << std::endl;
}

unsigned count_mismatches(const int *result) {
	unsigned fails = 0;
	for (int row = 0; row < MATRIX_SIZE; row++) {
		for (int col = 0; col < MATRIX_SIZE; col++) {
			int sum = 0;
			for (int k = 0; k < MATRIX_SIZE; k++) {
				sum += element_a(row, k) * element_b(k, col);
			}
			if (result[row * MATRIX_SIZE + col] != sum) {
				fails++;
			}
		}
	}
	return fails;
}

// -------------------------------------------------------
// Manager
// -------------------------------------------------------
void manager_main(Core &core) {
	// One chunk per worker. Static, so every buffer outlives the transfer that
	// reads it: the writes below are issued back to back and only the last is
	// waited on.
	static std::vector<int> chunks(NUM_CHIPLETS * (CHUNK_BYTES / ELEMENT_SIZE));

	LOG_ASSERT(static_cast<unsigned>(MAX_TRANSFER_BYTES) == core.MAX_INCR_BURST_SIZE,
	           "AXI_WIDTH_BITS in matmul.h gives a " << MAX_TRANSFER_BYTES << "-byte transfer limit, but the bus "
	                                                 << "allows " << core.MAX_INCR_BURST_SIZE
	                                                 << ". Set it to axi.width from system.yaml.");

	std::shared_ptr<RequestHandle> handle = nullptr;
	for (int chiplet = 0; chiplet < NUM_CHIPLETS; chiplet++) {
		int *chunk = chunks.data() + chiplet * (CHUNK_BYTES / ELEMENT_SIZE);
		fill_chunk(chunk, chiplet);

		auto req = AxiRequest(chiplet, reinterpret_cast<unsigned char *>(chunk), CHUNK_BYTES)
		               .to_via(worker_name(chiplet), "memory", "interconnect");
		handle   = core.write(req);
	}

	handle->wait();
}

void manager_irq(Core &core, const IRQ &irq) {
	static std::vector<int> result(MATRIX_SIZE * MATRIX_SIZE);
	static int              blocks = 0;

	// The worker tagged its write with its own index and placed its rows at the
	// offset they have in C, so the block needs no header to be filed.
	const int chiplet = static_cast<int>(irq.request_id);
	int      *rows    = result.data() + chiplet * ROWS_PER_CHIPLET * MATRIX_SIZE;

	auto req =
	    AxiRequest(irq.request_id, reinterpret_cast<unsigned char *>(rows), BLOCK_BYTES).set_addr(irq.target_address);
	core.read(req)->wait();

	print_result(result.data());

	if (++blocks < NUM_CHIPLETS) {
		return;
	}

	const unsigned checks = MATRIX_SIZE * MATRIX_SIZE;
	std::cout << "Result: " << (checks - count_mismatches(result.data())) << " / " << checks << " checks passed"
	          << std::endl;

	sc_stop();
}

// -------------------------------------------------------
// Worker
// -------------------------------------------------------
void worker_irq(Core &core, const IRQ &irq, int chiplet) {
	std::vector<int> chunk(CHUNK_BYTES / ELEMENT_SIZE);
	std::vector<int> rows(BLOCK_BYTES / ELEMENT_SIZE);

	auto req =
	    AxiRequest(chiplet, reinterpret_cast<unsigned char *>(chunk.data()), CHUNK_BYTES).set_addr(irq.target_address);
	core.read(req)->wait();

	multiply_block(chunk.data(), rows.data());
	core.wait_cycles("matmul");

	// The rows go to their offset in C, tagged with this worker's index.
	auto back = AxiRequest(chiplet, reinterpret_cast<unsigned char *>(rows.data()), BLOCK_BYTES)
	                .to_via("io", "memory", "interconnect")
	                .set_addr(chiplet * BLOCK_BYTES);
	core.write(back)->wait();
}

ModuleCodeMap build_program_code() {
	ModuleCodeMap code;

	code[{"io", "core0"}] = CPUCode{.main = manager_main, .irq = manager_irq};

	for (int chiplet = 0; chiplet < NUM_CHIPLETS; chiplet++) {
		code[{worker_name(chiplet), "core0"}] =
		    CPUCode{.main = [](Core &core) {},
		            .irq  = [chiplet](Core &core, const IRQ &irq) { worker_irq(core, irq, chiplet); }};
	}

	return code;
}

} // namespace

ModuleCodeMap *get_program_code() {
	static ModuleCodeMap code = build_program_code();
	return &code;
}
