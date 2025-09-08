#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t read_cycles(void) {
  uint64_t cycles;
  asm volatile("rdcycle %0" : "=r"(cycles));
  return cycles;
}

typedef struct {
  uint32_t rows;
  uint32_t cols;
} MatrixHeader;

const unsigned int MATRIX_ROWS = 10;
const unsigned int MATRIX_COLS = 10;

void print_matrix(const int *matrix, int rows, int cols) {
  for (int i = 0; i < rows; ++i) {
    printf("[ ");
    for (int j = 0; j < cols; ++j) {
      printf("%d ", matrix[i * cols + j]);
    }
    printf("]\n");
  }
}

int main() {
  // dummy data
  size_t header_size = sizeof(MatrixHeader);
  size_t matrix_size = MATRIX_ROWS * MATRIX_COLS * sizeof(int);
  size_t buffer_size = header_size + matrix_size;

  unsigned char *data = malloc(buffer_size);

  MatrixHeader *header = (MatrixHeader *)(data);
  header->rows = MATRIX_ROWS;
  header->cols = MATRIX_COLS;

  int *matrix = (int *)(data + header_size);
  for (int i = 0; i < MATRIX_ROWS * MATRIX_COLS; ++i) {
    matrix[i] = i + 1;
  }

  uint64_t start_cycles = read_cycles();

  // matrix header
  header_size = sizeof(MatrixHeader);
  *header = *(MatrixHeader *)(data);
  uint32_t rows = header->rows;
  uint32_t cols = header->cols;

  // matrix
  *matrix = *(int *)(data + header_size);

  // subtract -5
  for (int i = 0; i < rows * cols; ++i) {
    matrix[i] -= 5;
  }

  uint64_t end_cycles = read_cycles();

  printf("Cycles: %lu\n", end_cycles - start_cycles);

  print_matrix(matrix, rows, cols);

  return 0;
}