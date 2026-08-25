#include "program.h"

#include "modules/Core.h"

struct MatrixHeader {
	uint32_t rows;
	uint32_t cols;
};

void inline print_matrix(const int *matrix, int rows, int cols, const std::string &label = "") {
	if (!label.empty()) {
		std::cout << label << std::endl;
	}

	for (int i = 0; i < rows; ++i) {
		for (int j = 0; j < cols; ++j) {
			std::cout << matrix[i * cols + j] << " ";
		}
		std::cout << std::endl;
	}
}

ModuleCodeMap *get_program_code() {
	static ModuleCodeMap code = {
	    {{"io", "core0"},
	     {CPUCode{.main =
	                  [](Core &core) {
		                  const unsigned MATRIX_ROWS = 10;
		                  const unsigned MATRIX_COLS = 10;

		                  size_t header_size = sizeof(MatrixHeader);
		                  size_t matrix_size = MATRIX_ROWS * MATRIX_COLS * sizeof(int);
		                  size_t buffer_size = header_size + matrix_size;

		                  auto *data = new unsigned char[buffer_size];

		                  MatrixHeader *header = reinterpret_cast<MatrixHeader *>(data);
		                  header->rows         = MATRIX_ROWS;
		                  header->cols         = MATRIX_COLS;

		                  int *matrix = reinterpret_cast<int *>(data + header_size);
		                  for (int i = 0; i < MATRIX_ROWS * MATRIX_COLS; ++i) {
			                  matrix[i] = i + 1;
		                  }

		                  print_matrix(matrix, MATRIX_ROWS, MATRIX_COLS, "Input");

		                  auto reqw = AxiRequest(0, data, buffer_size).to_via("chiplet0", "memory", "interconnect");

		                  auto handle = core.write(reqw);

		                  handle->wait();

		                  delete[] data;
	                  },
	              .irq =
	                  [](Core &core, const IRQ &irq) {
		                  auto addr = irq.target_address;
		                  auto len  = irq.data_length;

		                  // Read from IO RAM
		                  auto *data   = new unsigned char[len];
		                  auto  reqr   = AxiRequest(0, data, len).set_addr(addr);
		                  auto  handle = core.read(reqr);
		                  handle->wait();

		                  // Matrix header
		                  size_t        header_size = sizeof(MatrixHeader);
		                  MatrixHeader *header      = reinterpret_cast<MatrixHeader *>(data);
		                  uint32_t      rows        = header->rows;
		                  uint32_t      cols        = header->cols;

		                  // Matrix
		                  int *matrix = reinterpret_cast<int *>(data + header_size);

		                  print_matrix(matrix, rows, cols, "Output");

		                  delete[] data;

		                  sc_stop();
	                  }}}},

	    {{"chiplet0", "core0"},
	     {CPUCode{.main = [](Core &core) {},
	              .irq =
	                  [](Core &core, const IRQ &irq) {
		                  auto addr = irq.target_address;
		                  auto len  = irq.data_length;

		                  // Read from Chiplet0 RAM
		                  auto *read_buf = new unsigned char[len];
		                  auto  reqr     = AxiRequest(0, read_buf, len).set_addr(addr);
		                  auto  handle   = core.read(reqr);
		                  handle->wait();

		                  // Matrix header
		                  size_t        header_size = sizeof(MatrixHeader);
		                  MatrixHeader *header      = reinterpret_cast<MatrixHeader *>(read_buf);
		                  uint32_t      rows        = header->rows;
		                  uint32_t      cols        = header->cols;

		                  // Matrix
		                  int *matrix = reinterpret_cast<int *>(read_buf + header_size);

		                  // Add +1
		                  for (int i = 0; i < rows * cols; ++i) {
			                  matrix[i] += 1;
		                  }

		                  core.wait_cycles("add");

		                  print_matrix(matrix, rows, cols, "Add");

		                  // Write to Chiplet2 RAM
		                  auto *write_buf = new unsigned char[len];
		                  memcpy(write_buf, read_buf, len);

		                  auto reqw = AxiRequest(0, write_buf, len).to_via("chiplet1", "memory", "interconnect");
		                  handle    = core.write(reqw);
		                  handle->wait();

		                  delete[] read_buf;
		                  delete[] write_buf;
	                  }}}},

	    {{"chiplet1", "core0"},
	     {CPUCode{.main = [](Core &core) {},
	              .irq =
	                  [](Core &core, const IRQ &irq) {
		                  auto addr = irq.target_address;
		                  auto len  = irq.data_length;

		                  // Read from Chiplet1 RAM
		                  auto *read_buf = new unsigned char[len];
		                  auto  reqr     = AxiRequest(0, read_buf, len).set_addr(addr);
		                  auto  handle   = core.read(reqr);
		                  handle->wait();

		                  // Matrix header
		                  size_t        header_size = sizeof(MatrixHeader);
		                  MatrixHeader *header      = reinterpret_cast<MatrixHeader *>(read_buf);
		                  uint32_t      rows        = header->rows;
		                  uint32_t      cols        = header->cols;

		                  // Matrix
		                  int *matrix = reinterpret_cast<int *>(read_buf + header_size);

		                  // Multiply by 2
		                  for (int i = 0; i < rows * cols; ++i) {
			                  matrix[i] *= 2;
		                  }

		                  core.wait_cycles("multiply");

		                  print_matrix(matrix, rows, cols, "Multiply");

		                  // write to Chiplet2 RAM
		                  auto *write_buf = new unsigned char[len];
		                  memcpy(write_buf, read_buf, len);

		                  auto reqw = AxiRequest(0, write_buf, len).to_via("chiplet2", "memory", "interconnect");
		                  handle    = core.write(reqw);
		                  handle->wait();

		                  delete[] read_buf;
		                  delete[] write_buf;
	                  }}}},

	    {{"chiplet2", "core0"},
	     {CPUCode{.main = [](Core &core) {},
	              .irq =
	                  [](Core &core, const IRQ &irq) {
		                  auto addr = irq.target_address;
		                  auto len  = irq.data_length;

		                  // Read from Chiplet2 RAM
		                  auto *read_buf = new unsigned char[len];
		                  auto  reqr     = AxiRequest(0, read_buf, len).set_addr(addr);
		                  auto  handle   = core.read(reqr);
		                  handle->wait();

		                  // Matrix header
		                  size_t        header_size = sizeof(MatrixHeader);
		                  MatrixHeader *header      = reinterpret_cast<MatrixHeader *>(read_buf);
		                  uint32_t      rows        = header->rows;
		                  uint32_t      cols        = header->cols;

		                  // Matrix
		                  int *matrix = reinterpret_cast<int *>(read_buf + header_size);

		                  // Transpose
		                  int temp[rows * cols];
		                  for (int i = 0; i < rows; ++i) {
			                  for (int j = 0; j < cols; ++j) {
				                  temp[j * rows + i] = matrix[i * cols + j];
			                  }
		                  }

		                  for (int i = 0; i < rows * cols; ++i) {
			                  matrix[i] = temp[i];
		                  }

		                  core.wait_cycles("transpose");

		                  print_matrix(matrix, rows, cols, "Transpose");

		                  // Write to Chiplet3 RAM
		                  auto *write_buf = new unsigned char[len];
		                  memcpy(write_buf, read_buf, len);

		                  auto reqw = AxiRequest(0, write_buf, len).to_via("chiplet3", "memory", "interconnect");
		                  handle    = core.write(reqw);
		                  handle->wait();

		                  delete[] read_buf;
		                  delete[] write_buf;
	                  }}}},

	    {{"chiplet3", "core0"},
	     {CPUCode{.main = [](Core &core) {},
	              .irq =
	                  [](Core &core, const IRQ &irq) {
		                  auto addr = irq.target_address;
		                  auto len  = irq.data_length;

		                  // Read from Chiplet3 RAM
		                  auto *read_buf = new unsigned char[len];
		                  auto  reqr     = AxiRequest(0, read_buf, len).set_addr(addr);
		                  auto  handle   = core.read(reqr);
		                  handle->wait();

		                  // Matrix header
		                  size_t        header_size = sizeof(MatrixHeader);
		                  MatrixHeader *header      = reinterpret_cast<MatrixHeader *>(read_buf);
		                  uint32_t      rows        = header->rows;
		                  uint32_t      cols        = header->cols;

		                  // Matrix
		                  int *matrix = reinterpret_cast<int *>(read_buf + header_size);

		                  // Subtract -5
		                  for (int i = 0; i < rows * cols; ++i) {
			                  matrix[i] -= 5;
		                  }

		                  core.wait_cycles("subtract");

		                  print_matrix(matrix, rows, cols, "Subtract");

		                  // Write to IO RAM
		                  auto *write_buf = new unsigned char[len];
		                  memcpy(write_buf, read_buf, len);

		                  auto reqw = AxiRequest(0, write_buf, len).to_via("io", "memory", "interconnect");
		                  handle    = core.write(reqw);
		                  handle->wait();

		                  delete[] read_buf;
		                  delete[] write_buf;
	                  }}}}
    };
	return &code;
}