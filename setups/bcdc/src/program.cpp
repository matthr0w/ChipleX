#include "program.h"

#include "modules/Core.h"
#include "modules/HWAccel.h"

unsigned total_runs = 3;
unsigned total_bytes = 512;
unsigned chunk_bytes = 64;
sc_event next_run;

ModuleCodeMap *get_program_code() {
  static ModuleCodeMap code = {
      {{"fpga", "core0"},
       {CPUCode{.main =
                    [](Core &core) {
                      static unsigned run = 0;
                      while (run < total_runs) {
                        // For now, we just send dummy data
                        size_t num_bytes = total_bytes;
                        uint8_t *data = new uint8_t[num_bytes];
                        for (size_t i = 0; i < num_bytes; ++i)
                          data[i] = static_cast<uint8_t>(1);

                        auto request =
                            AxiRequest(1,
                                       reinterpret_cast<unsigned char *>(data),
                                       num_bytes)
                                .to_via("mem_chiplet1", "memory", "pulp")
                                .set_addr(0x0)
                                .skip_cache();
                        auto handle = core.write(request);
                        handle->wait();

                        delete[] data;

                        wait(next_run);
                        run++;
                      }
                    },
                .irq =
                    [](Core &core, const IRQ &irq) {
                      static unsigned run = 0;
                      size_t num_bytes = total_bytes;
                      uint8_t *data = new uint8_t[num_bytes];

                      auto request =
                          AxiRequest(1, reinterpret_cast<unsigned char *>(data),
                                     num_bytes)
                              .set_addr(irq.target_address)
                              .skip_cache();
                      auto handle = core.read(request);
                      handle->wait();

                      std::cout << "\nResult on FPGA:" << std::endl;
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

                      next_run.notify(SC_ZERO_TIME);
                      run++;

                      if (run == 3)
                        sc_stop();
                    }}}},
      {{"chiplet1", "core0"},
       {CPUCode{.main = [](Core &core) {},
                .irq =
                    [](Core &core, const IRQ &irq) {
                      enum class Operation {
                        None,
                        FetchFromMem,
                        TransToDFP,
                        TransToAILC
                      };
                      Operation op = Operation::None;

                      static unsigned total_buffer_size = 0;
                      static unsigned offset = 0;

                      switch (irq.request_id) {
                      case 1:
                        // New run
                        total_buffer_size = irq.data_length;
                        offset = 0;
                        op = Operation::FetchFromMem;
                        break;
                      case 2:
                        op = Operation::TransToDFP;
                        break;
                      case 4:
                        op = Operation::TransToAILC;
                        break;
                      case 6:
                        // Fetch next data and repeat until whole data
                        // processed
                        if (total_buffer_size >= chunk_bytes) {
                          offset += chunk_bytes;
                          op = Operation::FetchFromMem;
                        }
                        break;
                      default:
                        break;
                      }

                      switch (op) {
                      case Operation::FetchFromMem: {
                        SC_LOG_INFO(&core, "FetchFromMem");
                        // Path 2: mem_chiplet1 -> SRAM with DMA Engine
                        auto dma =
                            AxiDMARequest(2, chunk_bytes) // chunk by chunk
                                .from_via("mem_chiplet1", "memory",
                                          0x0 + offset, "spi")
                                .to("chiplet1", "memory", 0x0);
                        core.dma(dma);
                        total_buffer_size -= chunk_bytes;
                      } break;
                      case Operation::TransToDFP: {
                        SC_LOG_INFO(&core, "TransToDFP");
                        // Path 3: SRAM -> DFP with DMA Engine
                        auto dma = AxiDMARequest(3, chunk_bytes)
                                       .from("chiplet1", "memory", 0x0)
                                       .to("chiplet1", "hw_accel0", 0x0);
                        core.dma(dma);
                      } break;
                      case Operation::TransToAILC: {
                        SC_LOG_INFO(&core, "TransToAILC");
                        // Path 5: SRAM -> AI-LC with DMA Engine
                        auto dma =
                            AxiDMARequest(5, chunk_bytes)
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
                                .to_via("chiplet2", "hw_accel1", "pulp");
                        auto handle = accel.write(request);
                        handle->wait();
                      }}}},
      {{"chiplet2", "core0"},
       {CPUCode{.main = [](Core &core) {},
                .irq =
                    [](Core &core, const IRQ &irq) {
                      enum class Operation {
                        None,
                        TransToDFP,
                        TransToMem,
                        FetchFromMem
                      };
                      Operation op = Operation::None;

                      static unsigned total_buffer_size = total_bytes;
                      static unsigned offset = 0;

                      switch (irq.request_id) {
                      case 7:
                        op = Operation::TransToDFP;
                        break;
                      case 9:
                        op = Operation::TransToMem;
                        break;
                      case 10:
                        if (total_buffer_size == 0)
                          op = Operation::FetchFromMem;
                        break;
                      default:
                        break;
                      }

                      switch (op) {
                      case Operation::TransToDFP: {
                        SC_LOG_INFO(&core, "TransToDFP");
                        // Path 8: SRAM -> DFP with DMA Engine
                        auto dma = AxiDMARequest(8, irq.data_length)
                                       .from("chiplet2", "memory", 0x0)
                                       .to("chiplet2", "hw_accel0", 0x0);
                        core.dma(dma);
                      } break;
                      case Operation::TransToMem: {
                        SC_LOG_INFO(&core, "TransToMem");
                        // Path 10: SRAM -> mem_chiplet2
                        auto dma =
                            AxiDMARequest(10, irq.data_length)
                                .from("chiplet2", "memory", irq.target_address)
                                .to_via("mem_chiplet2", "memory", 0x0 + offset,
                                        "spi");
                        core.dma(dma);
                        total_buffer_size -= chunk_bytes;
                        offset += chunk_bytes;
                      } break;
                      case Operation::FetchFromMem: {
                        SC_LOG_INFO(&core, "FetchFromMem");
                        // Path 11: mem_chiplet2 -> FPGA
                        auto dma =
                            AxiDMARequest(11, total_bytes)
                                .from_via("mem_chiplet2", "memory", 0x0, "spi")
                                .to_via("fpga", "memory", 0x0, "pulp");
                        core.dma(dma);
                        total_buffer_size = total_bytes;
                        offset = 0;
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