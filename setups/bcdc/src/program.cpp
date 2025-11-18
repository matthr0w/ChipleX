#include "program.h"

#include "modules/Core.h"
#include "modules/HWAccel.h"

ModuleCodeMap *get_program_code() {
  static ModuleCodeMap code = {
      {{"fpga", "core0"},
       {CPUCode{.main =
                    [](Core &core) {
                      // For now, we just send dummy data
                      size_t num_bytes = 256;
                      uint8_t *data = new uint8_t[num_bytes];
                      for (size_t i = 0; i < num_bytes; ++i)
                        data[i] = static_cast<uint8_t>(1);

                      auto request =
                          AxiRequest(1, reinterpret_cast<unsigned char *>(data),
                                     num_bytes)
                              .to_module("pulp")
                              .to_target("mem_chiplet1")
                              .skip_cache();

                      auto handle = core.write(request);
                      handle->wait();

                      delete[] data;
                    },
                .irq =
                    [](Core &core, const IRQ &irq) {
                      size_t num_bytes = irq.data_length;
                      uint8_t *data = new uint8_t[num_bytes];

                      auto request =
                          AxiRequest(1, reinterpret_cast<unsigned char *>(data),
                                     num_bytes)
                              .set_addr(irq.target_address)
                              .skip_cache();

                      auto handle = core.read(request);
                      handle->wait();

                      std::cout << "Final Data Buffer" << std::endl;
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
                    }}}},
      {{"chiplet1", "core0"},
       {CPUCode{.main = [](Core &core) {},
                .irq =
                    [](Core &core, const IRQ &irq) {
                      SC_LOG_INFO(&core, "Received interrupt:");
                      SC_LOG_INFO(&core, "Request ID: " << irq.request_id);
                      SC_LOG_INFO(&core,
                                  "Target module: " << int(irq.target_module));
                      SC_LOG_INFO(&core, "Target address: "
                                             << std::hex << irq.target_address
                                             << std::dec);
                      SC_LOG_INFO(&core, "Bytes: " << irq.data_length);

                      switch (irq.request_id) {
                      case 1: {
                        // Path 2: mem_chiplet1 -> SRAM with DMA Engine
                        auto dma =
                            AxiDMARequest(2, 64) // only 64 bytes
                                .from_via("mem_chiplet1", "memory", 0x0, "spi")
                                .to("chiplet1", "memory", 0x0);
                        core.dma(dma);
                      } break;
                      case 2: {
                        // Path 3: SRAM -> DFP with DMA Engine
                        auto dma = AxiDMARequest(3, irq.data_length)
                                       .from("chiplet1", "memory", 0x0)
                                       .to("chiplet1", "hw_accel0", 0x0);
                        core.dma(dma);
                      } break;
                      case 4: {
                        // Path 5: SRAM -> AI-LC with DMA Engine
                        auto dma =
                            AxiDMARequest(5, irq.data_length)
                                .from("chiplet1", "memory", irq.target_address)
                                .to("chiplet1", "hw_accel1", 0x0);
                        core.dma(dma);
                      } break;
                      default:
                        break;
                      }
                    }}}},
      {{"chiplet1", "hw_accel0"}, // DFP
       {AccelCode{.main =
                      [](HWAccel &accel, uint8_t *data, size_t size) {
                        for (size_t i = 0; i < size; ++i)
                          data[i] += 1;

                        // Path 4: DFP -> SRAM with DMA Engine
                        auto request = AxiRequest(
                            4, reinterpret_cast<unsigned char *>(data), size);
                        auto handle = accel.write(request);
                        handle->wait();
                      }}}},
      {{"chiplet1", "hw_accel1"}, // AI-LC
       {AccelCode{.main =
                      [](HWAccel &accel, uint8_t *data, size_t size) {
                        for (size_t i = 0; i < size; ++i)
                          data[i] += 1;

                        // Path 6: AI-LC -> Chiplet 2 AI-LC with DMA Engine
                        auto request =
                            AxiRequest(6,
                                       reinterpret_cast<unsigned char *>(data),
                                       size)
                                .to_module("pulp")
                                .to_target("chiplet2", "hw_accel1");
                        auto handle = accel.write(request);
                        handle->wait();
                      }}}},
      {{"chiplet2", "core0"},
       {CPUCode{.main = [](Core &core) {},
                .irq =
                    [](Core &core, const IRQ &irq) {
                      SC_LOG_INFO(&core, "Received interrupt:");
                      SC_LOG_INFO(&core, "Request ID: " << irq.request_id);
                      SC_LOG_INFO(&core,
                                  "Target module: " << int(irq.target_module));
                      SC_LOG_INFO(&core, "Target address: "
                                             << std::hex << irq.target_address
                                             << std::dec);
                      SC_LOG_INFO(&core, "Bytes: " << irq.data_length);

                      switch (irq.request_id) {
                      case 7: {
                        // Path 8: SRAM -> DFP with DMA Engine
                        auto dma = AxiDMARequest(8, irq.data_length)
                                       .from("chiplet2", "memory", 0x0)
                                       .to("chiplet2", "hw_accel0", 0x0);
                        core.dma(dma);
                      } break;
                      case 9: {
                        // Path 10: SRAM -> mem_chiplet2
                        auto dma =
                            AxiDMARequest(10, irq.data_length)
                                .from("chiplet2", "memory", irq.target_address)
                                .to_via("mem_chiplet2", "memory", 0x0, "spi");
                        core.dma(dma);
                      } break;
                      case 10: {
                        // Path 11: mem_chiplet2 -> FPGA
                        auto dma = AxiDMARequest(11, irq.data_length)
                                       .from_via("mem_chiplet2", "memory",
                                                 0x0, "spi")
                                       .to_via("fpga", "memory", 0x0, "pulp");
                        core.dma(dma);
                      } break;
                      default:
                        break;
                      }
                    }}}},
      {{"chiplet2", "hw_accel0"}, // DFP
       {AccelCode{.main =
                      [](HWAccel &accel, uint8_t *data, size_t size) {
                        for (size_t i = 0; i < size; ++i)
                          data[i] += 1;

                        // Path 9: DFP -> SRAM with DMA Engine
                        auto request = AxiRequest(
                            9, reinterpret_cast<unsigned char *>(data), size);
                        auto handle = accel.write(request);
                        handle->wait();
                      }}}},
      {{"chiplet2", "hw_accel1"}, // AI-LC
       {AccelCode{.main = [](HWAccel &accel, uint8_t *data, size_t size) {
         for (size_t i = 0; i < size; ++i)
           data[i] += 1;

         // Path 7: AI-LC -> SRAM with DMA Engine
         auto request =
             AxiRequest(7, reinterpret_cast<unsigned char *>(data), size);
         auto handle = accel.write(request);
         handle->wait();
       }}}}};
  return &code;
}