#include "program.h"

#include "logging.h"

#include "modules/Core.h"

ModuleCodeMap *get_program_code() {
	static ModuleCodeMap code = {
	    {{"chiplet0", "core0"},
	     {CPUCode{.main =
	                  [](Core &core) {
		                  SC_LOG_INFO(&core, "Run this setup with debug logging enabled to see the crypto extension output!");

		                  size_t   num_bytes = 256;
		                  uint8_t *data      = new uint8_t[num_bytes];

		                  for (size_t i = 0; i < num_bytes; ++i) {
			                  data[i] = static_cast<uint8_t>(i);
		                  }

		                  auto reqw = AxiRequest(1, reinterpret_cast<unsigned char *>(data), num_bytes)
		                                  .to_via("chiplet2", "memory", "interconnect")
		                                  .use_ext(SmartExtension::CRYPTO);

		                  auto h = core.write(reqw);

		                  auto reqr = AxiRequest(2, reinterpret_cast<unsigned char *>(data), num_bytes)
		                                  .to_via("chiplet2", "memory", "interconnect")
		                                  .set_addr(0x0)
		                                  .use_ext(SmartExtension::CRYPTO);

		                  h = core.read(reqr);
		                  h->wait();

		                  delete[] data;

		                  sc_stop();
	                  },
	              .irq = [](Core &core, const IRQ &irq) {}}}}
    };
	return &code;
}