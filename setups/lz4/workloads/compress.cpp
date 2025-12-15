#include <cstring>

#include "lz4.h"

struct DataHeader {
  uint32_t decompressed_data_size;
  uint32_t compressed_data_size;
};

int main() {
  const char *const str = "Hello, World! This is a test string for the LZ4 "
                          "compression algorithm. Hello, World! This is a test "
                          "string for the LZ4 compression algorithm.";
  const unsigned len = strlen(str) + 1;

  //@BEGIN_CYCLE_MEASURE
  // Calculate compression sizes
  const int max_compressed_size = LZ4_compressBound(len);
  auto *compressed_data = new unsigned char[max_compressed_size];

  // Compress
  const int compressed_data_size = LZ4_compress_default(
      str, reinterpret_cast<char *>(compressed_data), len, max_compressed_size);

  // Build header + payload
  DataHeader header;
  header.decompressed_data_size = len;
  header.compressed_data_size = compressed_data_size;

  const int total_size = sizeof(DataHeader) + compressed_data_size;
  auto *packet = new unsigned char[total_size];

  std::memcpy(packet, &header, sizeof(DataHeader));
  std::memcpy(packet + sizeof(DataHeader), compressed_data,
              compressed_data_size);
  //@END_CYCLE_MEASURE

  delete[] compressed_data;
  delete[] packet;

  return 0;
}