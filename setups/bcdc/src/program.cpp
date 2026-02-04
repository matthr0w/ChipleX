#include "program.h"

#include "modules/Core.h"
#include "modules/HWAccel.h"

const size_t TOTAL_RUNS = 3;
const size_t TOTAL_SIZE = 512;
const size_t CHUNK_SIZE = 64;

sc_event next_run;

ModuleCodeMap *get_program_code() {
  static ModuleCodeMap code = {
      {{"fpga", "core0"},
       {CPUCode{.main =
                    [](Core &core) {
                      static unsigned run = 0;
                      while (run < TOTAL_RUNS) {
                        // For now, we just send dummy data
                        size_t num_bytes = TOTAL_SIZE;
                        uint8_t *data = new uint8_t[num_bytes];
                        for (size_t i = 0; i < num_bytes; ++i)
                          data[i] = static_cast<uint8_t>(0);

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
                      size_t num_bytes = TOTAL_SIZE;
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

                      if (run == TOTAL_RUNS)
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
                        if (total_buffer_size >= CHUNK_SIZE) {
                          offset += CHUNK_SIZE;
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
                            AxiDMARequest(2, CHUNK_SIZE) // chunk by chunk
                                .from_via("mem_chiplet1", "memory",
                                          0x0 + offset, "spi")
                                .to("chiplet1", "memory", 0x0);
                        core.dma(dma);
                        total_buffer_size -= CHUNK_SIZE;
                      } break;
                      case Operation::TransToDFP: {
                        SC_LOG_INFO(&core, "TransToDFP");
                        // Path 3: SRAM -> DFP with DMA Engine
                        auto dma = AxiDMARequest(3, CHUNK_SIZE)
                                       .from("chiplet1", "memory", 0x0)
                                       .to("chiplet1", "dfp", 0x0);
                        core.dma(dma);
                      } break;
                      case Operation::TransToAILC: {
                        SC_LOG_INFO(&core, "TransToAILC");
                        // Path 5: SRAM -> GeMM with DMA Engine
                        auto dma =
                            AxiDMARequest(5, CHUNK_SIZE)
                                .from("chiplet1", "memory", irq.target_address)
                                .to("chiplet1", "gemm", 0x0);
                        core.dma(dma);
                      } break;
                      default:
                        break;
                      }
                    }}}},
      {{"chiplet1", "dfp"},
       {AccelCode{.main =
                      [](HWAccel &accel, uint8_t *data, size_t size) {
                        int accel_id = 0;
                        for (size_t i = accel_id; i < size; i += 4) {
                          int x = data[i];
                          // Heavy ALU chain
                          int a = x * 3 + 1;
                          int b = x * 7 - 5;
                          int c = x ^ (x << 1);
                          int d = x ^ (x >> 2);
                          data[i] = a + b + c + d;
                        }

                        accel.wait_cycles("matalu");

                        // Path 4: DFP -> SRAM with DMA Engine
                        auto request = AxiRequest(
                            4, reinterpret_cast<unsigned char *>(data), size);
                        auto handle = accel.write(request);
                        handle->wait();
                      }}}},
      {{"chiplet1", "gemm"},
       {AccelCode{.main =
                      [](HWAccel &accel, uint8_t *data, size_t size) {
                        int accel_id = 1;
                        for (size_t i = accel_id; i < size; i += 4) {
                          int x = data[i];
                          // Heavy ALU chain
                          int a = x * 3 + 1;
                          int b = x * 7 - 5;
                          int c = x ^ (x << 1);
                          int d = x ^ (x >> 2);
                          data[i] = a + b + c + d;
                        }

                        accel.wait_cycles("matalu");

                        // Path 6: GeMM -> Chiplet 2 GeMM with DMA Engine
                        auto request =
                            AxiRequest(6,
                                       reinterpret_cast<unsigned char *>(data),
                                       size)
                                .to_via("chiplet2", "gemm", "pulp");
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

                      static unsigned total_buffer_size = TOTAL_SIZE;
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
                                       .to("chiplet2", "dfp", 0x0);
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
                        total_buffer_size -= CHUNK_SIZE;
                        offset += CHUNK_SIZE;
                      } break;
                      case Operation::FetchFromMem: {
                        SC_LOG_INFO(&core, "FetchFromMem");
                        // Path 11: mem_chiplet2 -> FPGA
                        auto dma =
                            AxiDMARequest(11, TOTAL_SIZE)
                                .from_via("mem_chiplet2", "memory", 0x0, "spi")
                                .to_via("fpga", "memory", 0x0, "pulp");
                        core.dma(dma);
                        total_buffer_size = TOTAL_SIZE;
                        offset = 0;
                      } break;
                      default:
                        break;
                      }
                    }}}},
      {{"chiplet2", "dfp"},
       {AccelCode{.main =
                      [](HWAccel &accel, uint8_t *data, size_t size) {
                        int accel_id = 3;
                        for (size_t i = accel_id; i < size; i += 4) {
                          int x = data[i];
                          // Heavy ALU chain
                          int a = x * 3 + 1;
                          int b = x * 7 - 5;
                          int c = x ^ (x << 1);
                          int d = x ^ (x >> 2);
                          data[i] = a + b + c + d;
                        }

                        accel.wait_cycles("matalu");

                        // Path 9: DFP -> SRAM with DMA Engine
                        auto request = AxiRequest(
                            9, reinterpret_cast<unsigned char *>(data), size);
                        auto handle = accel.write(request);
                        handle->wait();
                      }}}},
      {{"chiplet2", "gemm"},
       {AccelCode{.main = [](HWAccel &accel, uint8_t *data, size_t size) {
         int accel_id = 2;
         for (size_t i = accel_id; i < size; i += 4) {
           int x = data[i];
           // Heavy ALU chain
           int a = x * 3 + 1;
           int b = x * 7 - 5;
           int c = x ^ (x << 1);
           int d = x ^ (x >> 2);
           data[i] = a + b + c + d;
         }

         accel.wait_cycles("matalu");

         // Path 7: GeMM -> SRAM with DMA Engine
         auto request =
             AxiRequest(7, reinterpret_cast<unsigned char *>(data), size);
         auto handle = accel.write(request);
         handle->wait();
       }}}}};
  return &code;
}