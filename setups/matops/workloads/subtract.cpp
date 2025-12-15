#include <cstddef>
#include <cstdint>

typedef struct {
  uint32_t rows;
  uint32_t cols;
} MatrixHeader;

const unsigned MATRIX_ROWS = 10;
const unsigned MATRIX_COLS = 10;

int main() {
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

  //@BEGIN_CYCLE_MEASURE
  // Matrix header
  header_size = sizeof(MatrixHeader);
  header = reinterpret_cast<MatrixHeader *>(data);
  uint32_t rows = header->rows;
  uint32_t cols = header->cols;

  // Matrix
  matrix = reinterpret_cast<int *>(data + header_size);

  // Subtract -5
  for (int i = 0; i < rows * cols; ++i)
    matrix[i] -= 5;
  //@END_CYCLE_MEASURE

  delete[] data;

  return 0;
}