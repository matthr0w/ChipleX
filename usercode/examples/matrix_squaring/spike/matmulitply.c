#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

uint64_t read_cycles(void) {
  uint64_t cycles;
  asm volatile("rdcycle %0" : "=r"(cycles));
  return cycles;
}

int main() {
  // dummy data
  int A[4] = {1, 2, 3, 4};
  int B[4] = {1, 2, 3, 4};

  uint64_t start_cycles = read_cycles();

  int result[4];

  result[0] = A[0] * B[0] + A[1] * B[2];
  result[1] = A[0] * B[1] + A[1] * B[3];
  result[2] = A[2] * B[0] + A[3] * B[2];
  result[3] = A[2] * B[1] + A[3] * B[3];

  uint64_t end_cycles = read_cycles();

  printf("Cycles: %lu\n", end_cycles - start_cycles);

  return 0;
}