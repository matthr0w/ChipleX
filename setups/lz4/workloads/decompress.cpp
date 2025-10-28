#include <cstring>

#include "lz4.h"

struct DataHeader {
  uint32_t decompressed_data_size;
  uint32_t compressed_data_size;
};

void decompress() {
  const char *const str = "Hello, World! This is a test string for the LZ4 "
                          "compression algorithm. Hello, World! This is a test "
                          "string for the LZ4 compression algorithm.";
  const unsigned len = strlen(str) + 1;

  const auto decompressed_size = len;
  const auto compressed_size = 82;

  //@START_MEASURE
  // Allocate decompression buffer
  auto *decompressed_data = new unsigned char[decompressed_size];

  // Decompress using header offsets
  const int result =
      LZ4_decompress_safe(reinterpret_cast<const char *>(str),
                          reinterpret_cast<char *>(decompressed_data),
                          compressed_size, decompressed_size);
  //@END_MEASURE
}