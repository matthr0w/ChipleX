#include "program.h"

#include "modules/Core.h"

struct MatrixHeader {
  uint32_t rows;
  uint32_t cols;
};

void inline print_matrix(const int *matrix, int rows, int cols,
                         const std::string &label = "") {
  if (!label.empty()) {
    std::cout << label << std::endl;
  }

  for (int i = 0; i < rows; ++i) {
    std::cout << "[ ";
    for (int j = 0; j < cols; ++j) {
      std::cout << matrix[i * cols + j] << " ";
    }
    std::cout << "]" << std::endl;
  }
}

CoreCodeMap *get_program_code() {
  static CoreCodeMap code = {
      {{"fpga", 0},
       {[](Core &core) {
          const unsigned MATRIX_ROWS = 10;
          const unsigned MATRIX_COLS = 10;

          size_t header_size = sizeof(MatrixHeader);
          size_t matrix_size = MATRIX_ROWS * MATRIX_COLS * sizeof(int);
          size_t buffer_size = header_size + matrix_size;

          auto *data = new unsigned char[buffer_size];

          MatrixHeader *header = reinterpret_cast<MatrixHeader *>(data);
          header->rows = MATRIX_ROWS;
          header->cols = MATRIX_COLS;

          int *matrix = reinterpret_cast<int *>(data + header_size);
          for (int i = 0; i < MATRIX_ROWS * MATRIX_COLS; ++i)
            matrix[i] = i + 1;

          print_matrix(matrix, MATRIX_ROWS, MATRIX_COLS, "Input");

          auto reqw =
              Core::WriteRequest(0, data, buffer_size).set_dest(1).skip_cache();

          auto handle = core.write(reqw);

          handle->wait();

          delete[] data;
        },
        [](Core &core, tlm_generic_payload *irq) {
          auto addr = irq->get_address();
          auto len = irq->get_data_length();

          // Read from FPGA RAM
          auto *data = new unsigned char[len];
          auto reqr =
              Core::ReadRequest(0, addr, data, len).set_dest(0).skip_cache();
          auto handle = core.read(reqr);
          handle->wait();

          // Matrix header
          size_t header_size = sizeof(MatrixHeader);
          MatrixHeader *header = reinterpret_cast<MatrixHeader *>(data);
          uint32_t rows = header->rows;
          uint32_t cols = header->cols;

          // Matrix
          int *matrix = reinterpret_cast<int *>(data + header_size);

          print_matrix(matrix, rows, cols, "Output");

          delete[] data;

          sc_stop();
        }}},

      {{"chiplet0", 0},
       {[](Core &core) {},
        [](Core &core, tlm_generic_payload *irq) {
          auto addr = irq->get_address();
          auto len = irq->get_data_length();

          // Read from Chiplet0 RAM
          auto *read_buf = new unsigned char[len];
          auto reqr = Core::ReadRequest(0, addr, read_buf, len)
                          .set_dest(1)
                          .skip_cache();
          auto handle = core.read(reqr);
          handle->wait();

          // Matrix header
          size_t header_size = sizeof(MatrixHeader);
          MatrixHeader *header = reinterpret_cast<MatrixHeader *>(read_buf);
          uint32_t rows = header->rows;
          uint32_t cols = header->cols;

          // Matrix
          int *matrix = reinterpret_cast<int *>(read_buf + header_size);

          // Add +1
          for (int i = 0; i < rows * cols; ++i)
            matrix[i] += 1;

          core.wait_cycles("add");

          print_matrix(matrix, rows, cols, "Add");

          // Write to Chiplet2 RAM
          auto *write_buf = new unsigned char[len];
          memcpy(write_buf, read_buf, len);

          auto reqw =
              Core::WriteRequest(0, write_buf, len).set_dest(2).skip_cache();
          handle = core.write(reqw);
          handle->wait();

          delete[] read_buf;
          delete[] write_buf;
        }}},

      {{"chiplet1", 0},
       {[](Core &core) {},
        [](Core &core, tlm_generic_payload *irq) {
          auto addr = irq->get_address();
          auto len = irq->get_data_length();

          // Read from Chiplet1 RAM
          auto *read_buf = new unsigned char[len];
          auto reqr = Core::ReadRequest(0, addr, read_buf, len)
                          .set_dest(2)
                          .skip_cache();
          auto handle = core.read(reqr);
          handle->wait();

          // Matrix header
          size_t header_size = sizeof(MatrixHeader);
          MatrixHeader *header = reinterpret_cast<MatrixHeader *>(read_buf);
          uint32_t rows = header->rows;
          uint32_t cols = header->cols;

          // Matrix
          int *matrix = reinterpret_cast<int *>(read_buf + header_size);

          // Multiply by 2
          for (int i = 0; i < rows * cols; ++i)
            matrix[i] *= 2;

          core.wait_cycles("multiply");

          print_matrix(matrix, rows, cols, "Multiply");

          // write to Chiplet2 RAM
          auto *write_buf = new unsigned char[len];
          memcpy(write_buf, read_buf, len);

          auto reqw =
              Core::WriteRequest(0, write_buf, len).set_dest(3).skip_cache();
          handle = core.write(reqw);
          handle->wait();

          delete[] read_buf;
          delete[] write_buf;
        }}},

      {{"chiplet2", 0},
       {[](Core &core) {},
        [](Core &core, tlm_generic_payload *irq) {
          auto addr = irq->get_address();
          auto len = irq->get_data_length();

          // Read from Chiplet2 RAM
          auto *read_buf = new unsigned char[len];
          auto reqr = Core::ReadRequest(0, addr, read_buf, len)
                          .set_dest(3)
                          .skip_cache();
          auto handle = core.read(reqr);
          handle->wait();

          // Matrix header
          size_t header_size = sizeof(MatrixHeader);
          MatrixHeader *header = reinterpret_cast<MatrixHeader *>(read_buf);
          uint32_t rows = header->rows;
          uint32_t cols = header->cols;

          // Matrix
          int *matrix = reinterpret_cast<int *>(read_buf + header_size);

          // Transpose
          int temp[rows * cols];
          for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j)
              temp[j * rows + i] = matrix[i * cols + j];

          for (int i = 0; i < rows * cols; ++i)
            matrix[i] = temp[i];

          core.wait_cycles("transpose");

          print_matrix(matrix, rows, cols, "Transpose");

          // Write to Chiplet3 RAM
          auto *write_buf = new unsigned char[len];
          memcpy(write_buf, read_buf, len);

          auto reqw =
              Core::WriteRequest(0, write_buf, len).set_dest(4).skip_cache();
          handle = core.write(reqw);
          handle->wait();

          delete[] read_buf;
          delete[] write_buf;
        }}},

      {{"chiplet3", 0},
       {[](Core &core) {},
        [](Core &core, tlm_generic_payload *irq) {
          auto addr = irq->get_address();
          auto len = irq->get_data_length();

          // Read from Chiplet3 RAM
          auto *read_buf = new unsigned char[len];
          auto reqr = Core::ReadRequest(0, addr, read_buf, len)
                          .set_dest(4)
                          .skip_cache();
          auto handle = core.read(reqr);
          handle->wait();

          // Matrix header
          size_t header_size = sizeof(MatrixHeader);
          MatrixHeader *header = reinterpret_cast<MatrixHeader *>(read_buf);
          uint32_t rows = header->rows;
          uint32_t cols = header->cols;

          // Matrix
          int *matrix = reinterpret_cast<int *>(read_buf + header_size);

          // Subtract -5
          for (int i = 0; i < rows * cols; ++i)
            matrix[i] -= 5;

          core.wait_cycles("subtract");

          print_matrix(matrix, rows, cols, "Subtract");

          // Write to FPGA RAM
          auto *write_buf = new unsigned char[len];
          memcpy(write_buf, read_buf, len);

          auto reqw =
              Core::WriteRequest(0, write_buf, len).set_dest(0).skip_cache();
          handle = core.write(reqw);
          handle->wait();

          delete[] read_buf;
          delete[] write_buf;
        }}}};
  return &code;
}