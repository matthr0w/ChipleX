#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lz4.h"

uint64_t read_cycles(void) {
  uint64_t cycles;
  asm volatile("rdcycle %0" : "=r"(cycles));
  return cycles;
}

int main() {
  const char *const str = "Hello, World! This is a test string for the LZ4 "
                          "compression algorithm. Hello, World! This is a test "
                          "string for the LZ4 compression algorithm.";

  uint64_t start_cycles = read_cycles();

  // compress data
  // calculate data sizes
  const unsigned int str_size = (unsigned int)(strlen(str) + 1);
  const int max_compressed_size = LZ4_compressBound(str_size);
  const int max_buffer_size = sizeof(unsigned int) + max_compressed_size;

  // allocate buffer
  char *compressed_data = malloc(max_buffer_size);

  // prepend original data size
  memcpy(compressed_data, &str_size, sizeof(unsigned int));

  // compress data
  const int compressed_data_size = LZ4_compress_default(
      str, compressed_data + sizeof(unsigned int), str_size, max_buffer_size);

  // resize compressed data buffer
  const int resized_data_size = sizeof(unsigned int) + compressed_data_size;
  compressed_data = (char *)realloc(compressed_data, resized_data_size);

  uint64_t end_cycles = read_cycles();

  printf("Cycles: %lu\n", end_cycles - start_cycles);

  return 0;
}