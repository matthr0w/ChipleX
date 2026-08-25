#include "program.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>

#include "modules/Core.h"

namespace {

constexpr unsigned REPEATS = 16;
constexpr unsigned N_SMALL = 8;
constexpr unsigned N_LARGE = 64;
constexpr unsigned WORD    = 8;

constexpr int N_LINKS   = 2;
constexpr int LINK_SCI  = 0;
constexpr int N_TESTS   = 12;
constexpr int FIRST_DMA = 6;

// Must match cores.clk_cycle of the manager chiplet in system.yaml.
const sc_time CORE_CLK(2, SC_NS);

const char *const link_chiplet[N_LINKS] = {"worker", "memchip0"};
const char *const link_via[N_LINKS]     = {"slink", "spi"};

constexpr uint32_t LINK_OFF = 0x0;    // far-side window
constexpr uint32_t SRC_ADDR = 0x1000; // DMA source, local
constexpr uint32_t DST_ADDR = 0x2000; // DMA destination, local

const char *const test_name[N_TESTS] = {" 1 cpu_wr_1 ", " 2 cpu_rd_1 ", " 3 cpu_wr_8 ", " 4 cpu_rd_8 ",
                                        " 5 cpu_wr_64", " 6 cpu_rd_64", " 7 dma_wr_1 ", " 8 dma_rd_1 ",
                                        " 9 dma_wr_8 ", "10 dma_rd_8 ", "11 dma_wr_64", "12 dma_rd_64"};

unsigned long cc[N_LINKS][N_TESTS];
unsigned long cc_link[N_LINKS][N_TESTS];

unsigned char buf[N_LARGE * WORD];
unsigned char sink[N_LARGE * WORD];

sc_event &dma_done() {
	static sc_event event;
	return event;
}

bool dma_busy = false;

unsigned long cycles_between(const sc_time &start, const sc_time &end) {
	return static_cast<unsigned long>(std::llround((end - start) / CORE_CLK));
}

// A completion landing between two edges is only observed at the next one.
sc_time sample_time() {
	return CORE_CLK * std::ceil(sc_time_stamp() / CORE_CLK);
}

AxiRequest far_request(uint32_t id, unsigned char *data, unsigned length, int link, uint32_t offset) {
	return AxiRequest(id, data, length).to_via(link_chiplet[link], "memory", link_via[link]).set_addr(offset);
}

AxiDMARequest dma_out(unsigned length, int link) {
	return AxiDMARequest(0, length)
	    .from("manager", "memory", SRC_ADDR)
	    .to_via(link_chiplet[link], "memory", LINK_OFF, link_via[link]);
}

AxiDMARequest dma_in(unsigned length, int link) {
	return AxiDMARequest(0, length)
	    .from_via(link_chiplet[link], "memory", LINK_OFF, link_via[link])
	    .to("manager", "memory", DST_ADDR);
}

// Returns the full cost incl. descriptor programming; the part from descriptor
// accepted to the completion interrupt goes to *cc_start_done.
unsigned long dma_copy_split(Core &core, const AxiDMARequest &request, unsigned long *cc_start_done) {
	const sc_time t0 = sc_time_stamp();

	dma_busy    = true;
	auto handle = core.dma(request);
	handle->wait();

	const sc_time t1 = sc_time_stamp();

	while (dma_busy) {
		sc_core::wait(dma_done());
	}

	const sc_time t2 = sample_time();

	*cc_start_done = cycles_between(t1, t2);
	return cycles_between(t0, t2);
}

unsigned long burst_writes(Core &core, int link, unsigned count) {
	const sc_time t0 = sc_time_stamp();
	for (unsigned i = 0; i < count; i++) {
		core.write(far_request(i, buf + i * WORD, WORD, link, LINK_OFF + i * WORD))->wait();
	}
	return cycles_between(t0, sc_time_stamp());
}

unsigned long burst_reads(Core &core, int link, unsigned count) {
	const sc_time t0 = sc_time_stamp();
	for (unsigned i = 0; i < count; i++) {
		core.read(far_request(i, sink + i * WORD, WORD, link, LINK_OFF + i * WORD))->wait();
	}
	return cycles_between(t0, sc_time_stamp());
}

void pass(Core &core, int link) {
	unsigned long start_done = 0;

	cc[link][0] += burst_writes(core, link, 1);
	cc[link][1] += burst_reads(core, link, 1);
	cc[link][2] += burst_writes(core, link, N_SMALL);
	cc[link][3] += burst_reads(core, link, N_SMALL);
	cc[link][4] += burst_writes(core, link, N_LARGE);
	cc[link][5] += burst_reads(core, link, N_LARGE);

	cc[link][6]      += dma_copy_split(core, dma_out(WORD, link), &start_done);
	cc_link[link][6] += start_done;

	cc[link][7]      += dma_copy_split(core, dma_in(WORD, link), &start_done);
	cc_link[link][7] += start_done;

	cc[link][8]      += dma_copy_split(core, dma_out(N_SMALL * WORD, link), &start_done);
	cc_link[link][8] += start_done;

	cc[link][9]      += dma_copy_split(core, dma_in(N_SMALL * WORD, link), &start_done);
	cc_link[link][9] += start_done;

	cc[link][10]      += dma_copy_split(core, dma_out(N_LARGE * WORD, link), &start_done);
	cc_link[link][10] += start_done;

	cc[link][11]      += dma_copy_split(core, dma_in(N_LARGE * WORD, link), &start_done);
	cc_link[link][11] += start_done;
}

void report(int test) {
	std::cout << test_name[test];
	for (int link = 0; link < N_LINKS; link++) {
		std::cout << (link == LINK_SCI ? "  SCI=" : "  SPI=") << cc[link][test] / REPEATS;
		if (test >= FIRST_DMA) {
			std::cout << '(' << cc_link[link][test] / REPEATS << ')';
		}
	}
	std::cout << '\n';
}

} // namespace

ModuleCodeMap *get_program_code() {
	static ModuleCodeMap code = {
	    {{"manager", "core0"},
	     {CPUCode{.main =
	                  [](Core &core) {
		                  for (unsigned i = 0; i < N_LARGE; i++) {
			                  const uint64_t value = 0x37UL | static_cast<uint64_t>(i + 1) << 32;
			                  std::memcpy(buf + i * WORD, &value, WORD);
		                  }
		                  core.local_write(buf, sizeof(buf), SRC_ADDR);

		                  const sc_time t_start = sc_time_stamp();
		                  for (unsigned r = 0; r < REPEATS; r++) {
			                  for (int link = 0; link < N_LINKS; link++) {
				                  pass(core, link);
			                  }
		                  }
		                  const sc_time t_end = sc_time_stamp();

		                  std::cout << "== Cycle Counts (average per transaction, REPEATS=" << REPEATS << ")\n";
		                  std::cout << "   DMA bursts: total incl. register programming, (start->done) only\n";
		                  for (int test = 0; test < N_TESTS; test++) {
			                  report(test);
		                  }
		                  std::cout << "== Total=" << cycles_between(t_start, t_end) << '\n' << std::flush;

		                  sc_stop();
	                  },
	              .irq =
	                  [](Core &core, const IRQ &irq) {
		                  dma_busy = false;
		                  dma_done().notify(SC_ZERO_TIME);
	                  }}}	                                                                                },

	    {{"worker", "core0"},  {CPUCode{.main = [](Core &core) {}, .irq = [](Core &core, const IRQ &irq) {}}}}
    };
	return &code;
}
