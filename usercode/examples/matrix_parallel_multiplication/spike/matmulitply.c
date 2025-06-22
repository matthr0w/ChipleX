#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t read_cycles(void) {
  uint64_t cycles;
  asm volatile("rdcycle %0" : "=r"(cycles));
  return cycles;
}

#define MATRIX_SIZE 4
#define ELEMENT_SIZE sizeof(int)
#define ROW_SIZE (MATRIX_SIZE * ELEMENT_SIZE)

const int matrixA[MATRIX_SIZE][MATRIX_SIZE] = {
    {1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}, {13, 14, 15, 16}};

const int matrixB[MATRIX_SIZE][MATRIX_SIZE] = {
    {17, 18, 19, 20}, {21, 22, 23, 24}, {25, 26, 27, 28}, {29, 30, 31, 32}};

int main(void) {
  int data_size = ROW_SIZE + MATRIX_SIZE * MATRIX_SIZE * ELEMENT_SIZE;
  unsigned char *data = (unsigned char *)malloc(data_size);

  memcpy(data, &matrixA[0][0], ROW_SIZE);
  memcpy(data + ROW_SIZE, &matrixB[0][0],
         MATRIX_SIZE * MATRIX_SIZE * ELEMENT_SIZE);

  uint64_t start_cycles = read_cycles();

  int *A = (int *)data;
  int *B = (int *)(data + ROW_SIZE);
  int C_row[MATRIX_SIZE] = {0};

  for (int j = 0; j < MATRIX_SIZE; j++) {
    for (int k = 0; k < MATRIX_SIZE; k++) {
      C_row[j] += A[k] * B[k * MATRIX_SIZE + j];
    }
  }

  unsigned char *result = (unsigned char *)malloc(ROW_SIZE);
  memcpy(result, C_row, ROW_SIZE);

  uint64_t end_cycles = read_cycles();

  printf("Cycles: %lu\n", end_cycles - start_cycles);

  return 0;
}