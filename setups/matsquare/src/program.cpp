#include "program.h"

#include "modules/Core.h"

static void matrix_multiply(Core &core, uint32_t src_addr1, uint32_t src_addr2,
                            uint32_t dest_addr) {
  auto *buf1 = new unsigned char[4 * sizeof(int)];
  auto *buf2 = new unsigned char[4 * sizeof(int)];

  auto reqr =
      Core::ReadRequest(1, src_addr1, reinterpret_cast<unsigned char *>(buf1),
                        4 * sizeof(int))
          .set_dest(1)
          .skip_cache();
  auto handle = core.read(reqr);
  handle->wait();
  reqr =
      Core::ReadRequest(2, src_addr2, reinterpret_cast<unsigned char *>(buf2),
                        4 * sizeof(int))
          .set_dest(1);
  handle = core.read(reqr);
  handle->wait();

  int *A = reinterpret_cast<int *>(buf1);
  int *B = reinterpret_cast<int *>(buf2);
  int result[4];

  result[0] = A[0] * B[0] + A[1] * B[2];
  result[1] = A[0] * B[1] + A[1] * B[3];
  result[2] = A[2] * B[0] + A[3] * B[2];
  result[3] = A[2] * B[1] + A[3] * B[3];

  auto *data = new unsigned char[4 * sizeof(int)];
  std::memcpy(data, result, 4 * sizeof(int));

  auto reqw = Core::WriteRequest(3, reinterpret_cast<unsigned char *>(data),
                                 4 * sizeof(int))
                  .set_addr(dest_addr)
                  .set_dest(1)
                  .skip_cache();
  handle = core.write(reqw);
  handle->wait();

  delete[] buf1;
  delete[] buf2;
}

CoreCodeMap *get_program_code() {
  static CoreCodeMap code = {
      {{"fpga", 0},
       {[](Core &core) {
          int matrix[4] = {1, 2, 3, 4};
          auto *data = new unsigned char[sizeof(matrix)];
          memcpy(data, matrix, sizeof(matrix));

          auto reqw =
              Core::WriteRequest(0, reinterpret_cast<unsigned char *>(data),
                                 sizeof(matrix))
                  .set_dest(1)
                  .skip_cache();
          auto handle = core.write(reqw);
          handle->wait();

          delete[] data;
        },
        [](Core &core, tlm_generic_payload *irq) {
          auto *data = new unsigned char[4 * sizeof(int)];

          auto reqr = Core::ReadRequest(0, irq->get_address(),
                                        reinterpret_cast<unsigned char *>(data),
                                        4 * sizeof(int))
                          .set_dest(0)
                          .skip_cache();
          auto handle = core.read(reqr);
          handle->wait();

          int *result = reinterpret_cast<int *>(data);

          LOG_INFO("A^6 = [" << result[0] << " " << result[1] << " ; "
                             << result[2] << " " << result[3] << "]");

          delete[] data;

          sc_stop();
        }}},

      {{"chiplet0", 0},
       {[](Core &core) {},
        [](Core &core, tlm_generic_payload *irq) {
          uint32_t matrix_addr = irq->get_address();

          matrix_multiply(core, matrix_addr, matrix_addr,
                          0x1000); // A^2 = A * A
          core.wait_cycles("matmul");
          matrix_multiply(core, 0x1000, matrix_addr, 0x2000); // A^3 = A^2 * A
          core.wait_cycles("matmul");
          matrix_multiply(core, 0x2000, matrix_addr, 0x3000); // A^4 = A^3 * A
          core.wait_cycles("matmul");
          matrix_multiply(core, 0x3000, matrix_addr, 0x4000); // A^5 = A^4 * A
          core.wait_cycles("matmul");
          matrix_multiply(core, 0x4000, matrix_addr, 0x5000); // A^6 = A^5 * A
          core.wait_cycles("matmul");

          auto *data = new unsigned char[4 * sizeof(int)];
          auto reqr = Core::ReadRequest(4, 0x5000,
                                        reinterpret_cast<unsigned char *>(data),
                                        4 * sizeof(int))
                          .set_dest(1)
                          .skip_cache();
          auto handle = core.read(reqr);
          handle->wait();

          auto reqw =
              Core::WriteRequest(5, reinterpret_cast<unsigned char *>(data),
                                 4 * sizeof(int))
                  .set_dest(0)
                  .skip_cache();
          handle = core.write(reqw);
          handle->wait();

          delete[] data;
        }}}};
  return &code;
}