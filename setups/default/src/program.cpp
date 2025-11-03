#include "program.h"

#include "modules/Core.h"

CoreCodeMap *get_program_code() {
  static CoreCodeMap code = {
      {{"chiplet0", 0},
       {[](Core &core) {
          size_t num_bytes = 256;
          uint8_t *data = new uint8_t[num_bytes];

          for (size_t i = 0; i < num_bytes; ++i)
            data[i] = static_cast<uint8_t>(i);

          auto reqw = Core::WriteRequest(
                          1, reinterpret_cast<unsigned char *>(data), num_bytes)
                          .set_dest(2)
                          .skip_cache();

          auto h = core.write(reqw);

          auto reqr =
              Core::ReadRequest(2, 0x0, reinterpret_cast<unsigned char *>(data),
                                num_bytes)
                  .set_dest(2)
                  .skip_cache();

          h = core.read(reqr);
          h->wait();

          std::cout << "Data Buffer" << std::endl;
          for (size_t i = 0; i < num_bytes; ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(data[i]);
            if ((i + 1) % 16 == 0)
              std::cout << "\n";
            else
              std::cout << " ";
          }

          delete[] data;

          sc_stop();
        },
        [](Core &core, tlm_generic_payload *irq) {}}}};
  return &code;
}