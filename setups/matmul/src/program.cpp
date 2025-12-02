#include "program.h"

#include "modules/Core.h"

const int MATRIX_SIZE = 4;
const int ELEMENT_SIZE = sizeof(int);
const int ROW_SIZE = MATRIX_SIZE * ELEMENT_SIZE;

const int matrixA[MATRIX_SIZE][MATRIX_SIZE] = {
    {1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}, {13, 14, 15, 16}};

const int matrixB[MATRIX_SIZE][MATRIX_SIZE] = {
    {17, 18, 19, 20}, {21, 22, 23, 24}, {25, 26, 27, 28}, {29, 30, 31, 32}};

ModuleCodeMap *get_program_code() {
  static ModuleCodeMap code = {
      {{"fpga", "core0"},
       {CPUCode{.main =
                    [](Core &core) {
                      const int data_size_per_chiplet =
                          ROW_SIZE + MATRIX_SIZE * MATRIX_SIZE * ELEMENT_SIZE;
                      auto *data = new unsigned char[data_size_per_chiplet * 4];

                      std::shared_ptr<RequestHandle> handle = nullptr;
                      for (int chiplet = 0; chiplet < 4; ++chiplet) {
                        unsigned char *chunk =
                            data + chiplet * data_size_per_chiplet;

                        // Fill the buffer for this chiplet
                        std::memcpy(chunk, &matrixA[chiplet][0], ROW_SIZE);
                        std::memcpy(chunk + ROW_SIZE, &matrixB[0][0],
                                    MATRIX_SIZE * MATRIX_SIZE * ELEMENT_SIZE);

                        auto reqw =
                            AxiRequest(chiplet, chunk, data_size_per_chiplet)
                                .to_via("chiplet" + std::to_string(chiplet),
                                        "memory", "interconnect")
                                .skip_cache();

                        handle = core.write(reqw);
                      }

                      handle->wait();

                      delete[] data;
                    },
                .irq =
                    [](Core &core, const IRQ &irq) {
                      static unsigned interrupt_count = 0;

                      // Save result in static variable
                      static int resultC[MATRIX_SIZE][MATRIX_SIZE];

                      uint32_t address = irq.target_address;
                      unsigned int data_size = irq.data_length;

                      int chiplet_id = (address - 0x40000) / 0x1000;

                      auto *data = new unsigned char[data_size];

                      auto reqr = AxiRequest(chiplet_id, data, data_size)
                                      .set_addr(address)
                                      .skip_cache();
                      auto handle = core.read(reqr);
                      handle->wait();

                      int *row_result = reinterpret_cast<int *>(data);

                      memcpy(&resultC[chiplet_id][0], row_result, ROW_SIZE);

                      delete[] data;

                      std::cout << "Temporary Matrix Result" << std::endl;
                      for (int i = 0; i < MATRIX_SIZE; i++) {
                        std::stringstream ss;
                        for (int j = 0; j < MATRIX_SIZE; j++)
                          ss << resultC[i][j] << " ";
                        std::cout << ss.str() << std::endl;
                      }
                      std::cout << std::endl;

                      interrupt_count++;

                      if (interrupt_count == 4)
                        sc_stop();
                    }}}},

      {{"chiplet0", "core0"},
       {CPUCode{.main = [](Core &core) {},
                .irq =
                    [](Core &core, const IRQ &irq) {
                      uint32_t address = irq.target_address;
                      int data_size = irq.data_length;

                      auto *data = new unsigned char[data_size];
                      auto reqr = AxiRequest(0, data, data_size)
                                      .set_addr(address)
                                      .skip_cache();
                      auto handle = core.read(reqr);
                      handle->wait();

                      int *A = reinterpret_cast<int *>(data);
                      int *B = reinterpret_cast<int *>(data + ROW_SIZE);
                      int C_row[MATRIX_SIZE] = {0};

                      for (int j = 0; j < MATRIX_SIZE; j++)
                        for (int k = 0; k < MATRIX_SIZE; k++)
                          C_row[j] += A[k] * B[k * MATRIX_SIZE + j];

                      auto *result = new unsigned char[ROW_SIZE];
                      memcpy(result, C_row, ROW_SIZE);

                      core.wait_cycles("matmul");

                      auto reqw = AxiRequest(0, result, ROW_SIZE)
                                      .to_via("fpga", "memory", "interconnect")
                                      .set_addr(0x0)
                                      .skip_cache();
                      handle = core.write(reqw);
                      handle->wait();

                      delete[] data;
                      delete[] result;
                    }}}},

      {{"chiplet1", "core0"},
       {CPUCode{.main = [](Core &core) {},
                .irq =
                    [](Core &core, const IRQ &irq) {
                      uint32_t address = irq.target_address;
                      int data_size = irq.data_length;

                      auto *data = new unsigned char[data_size];
                      auto reqr = AxiRequest(0, data, data_size)
                                      .set_addr(address)
                                      .skip_cache();
                      auto handle = core.read(reqr);
                      handle->wait();

                      int *A = reinterpret_cast<int *>(data);
                      int *B = reinterpret_cast<int *>(data + ROW_SIZE);
                      int C_row[MATRIX_SIZE] = {0};

                      for (int j = 0; j < MATRIX_SIZE; j++)
                        for (int k = 0; k < MATRIX_SIZE; k++)
                          C_row[j] += A[k] * B[k * MATRIX_SIZE + j];

                      auto *result = new unsigned char[ROW_SIZE];
                      memcpy(result, C_row, ROW_SIZE);

                      core.wait_cycles("matmul");

                      auto reqw = AxiRequest(0, result, ROW_SIZE)
                                      .to_via("fpga", "memory", "interconnect")
                                      .set_addr(0x1000)
                                      .skip_cache();
                      handle = core.write(reqw);
                      handle->wait();

                      delete[] data;
                      delete[] result;
                    }}}},

      {{"chiplet2", "core0"},
       {CPUCode{.main = [](Core &core) {},
                .irq =
                    [](Core &core, const IRQ &irq) {
                      uint32_t address = irq.target_address;
                      int data_size = irq.data_length;

                      auto *data = new unsigned char[data_size];
                      auto reqr = AxiRequest(0, data, data_size)
                                      .set_addr(address)
                                      .skip_cache();
                      auto handle = core.read(reqr);
                      handle->wait();

                      int *A = reinterpret_cast<int *>(data);
                      int *B = reinterpret_cast<int *>(data + ROW_SIZE);
                      int C_row[MATRIX_SIZE] = {0};

                      for (int j = 0; j < MATRIX_SIZE; j++)
                        for (int k = 0; k < MATRIX_SIZE; k++)
                          C_row[j] += A[k] * B[k * MATRIX_SIZE + j];

                      auto *result = new unsigned char[ROW_SIZE];
                      memcpy(result, C_row, ROW_SIZE);

                      core.wait_cycles("matmul");

                      auto reqw = AxiRequest(0, result, ROW_SIZE)
                                      .to_via("fpga", "memory", "interconnect")
                                      .set_addr(0x2000)
                                      .skip_cache();
                      handle = core.write(reqw);
                      handle->wait();

                      delete[] data;
                      delete[] result;
                    }}}},

      {{"chiplet3", "core0"},
       {CPUCode{.main = [](Core &core) {},
                .irq =
                    [](Core &core, const IRQ &irq) {
                      uint32_t address = irq.target_address;
                      int data_size = irq.data_length;

                      auto *data = new unsigned char[data_size];
                      auto reqr = AxiRequest(0, data, data_size)
                                      .set_addr(address)
                                      .skip_cache();
                      auto handle = core.read(reqr);
                      handle->wait();

                      int *A = reinterpret_cast<int *>(data);
                      int *B = reinterpret_cast<int *>(data + ROW_SIZE);
                      int C_row[MATRIX_SIZE] = {0};

                      for (int j = 0; j < MATRIX_SIZE; j++)
                        for (int k = 0; k < MATRIX_SIZE; k++)
                          C_row[j] += A[k] * B[k * MATRIX_SIZE + j];

                      auto *result = new unsigned char[ROW_SIZE];
                      memcpy(result, C_row, ROW_SIZE);

                      core.wait_cycles("matmul");

                      auto reqw = AxiRequest(0, result, ROW_SIZE)
                                      .to_via("fpga", "memory", "interconnect")
                                      .set_addr(0x3000)
                                      .skip_cache();
                      handle = core.write(reqw);
                      handle->wait();

                      delete[] data;
                      delete[] result;
                    }}}}};
  return &code;
}