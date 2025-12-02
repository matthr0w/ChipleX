#include "program.h"

#include "modules/Core.h"

ModuleCodeMap *get_program_code() {
  static ModuleCodeMap code = {
      {{"chiplet0", "core0"},
       {CPUCode{.main =
                    [](Core &core) {
                      size_t num_bytes = 256;
                      uint8_t *data = new uint8_t[num_bytes];

                      for (size_t i = 0; i < num_bytes; ++i)
                        data[i] = static_cast<uint8_t>(i);

                      auto reqw =
                          AxiRequest(1, reinterpret_cast<unsigned char *>(data),
                                     num_bytes)
                              .to_via("chiplet2", "memory", "interconnect")
                              .skip_cache();

                      auto h = core.write(reqw);

                      auto reqr =
                          AxiRequest(2, reinterpret_cast<unsigned char *>(data),
                                     num_bytes)
                              .to_via("chiplet2", "memory", "interconnect")
                              .set_addr(0x0)
                              .skip_cache();

                      h = core.read(reqr);
                      h->wait();

                      std::cout << "Data Buffer" << std::endl;
                      for (size_t i = 0; i < num_bytes; ++i) {
                        std::cout << std::hex << std::setw(2)
                                  << std::setfill('0')
                                  << static_cast<int>(data[i]);
                        if ((i + 1) % 16 == 0)
                          std::cout << "\n";
                        else
                          std::cout << " ";
                      }

                      delete[] data;

                      sc_stop();
                    },
                .irq = [](Core &core, const IRQ &irq) {}}}}};
  return &code;
}