#include <cstddef>
#include <cstdint>

typedef struct {
  uint32_t rows;
  uint32_t cols;
} MatrixHeader;

const unsigned MATRIX_ROWS = 10;
const unsigned MATRIX_COLS = 10;

void transpose() {
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

  //@START_MEASURE
  // Matrix header
  header_size = sizeof(MatrixHeader);
  header = reinterpret_cast<MatrixHeader *>(data);
  uint32_t rows = header->rows;
  uint32_t cols = header->cols;

  // Matrix
  matrix = reinterpret_cast<int *>(data + header_size);

  // Transpose
  int temp[rows * cols];
  for (int i = 0; i < rows; ++i)
    for (int j = 0; j < cols; ++j)
      temp[j * rows + i] = matrix[i * cols + j];

  for (int i = 0; i < rows * cols; ++i)
    matrix[i] = temp[i];
  //@END_MEASURE

  delete[] data;
}