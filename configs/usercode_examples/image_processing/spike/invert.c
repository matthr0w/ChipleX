#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint64_t read_cycles(void) {
  uint64_t cycles;
  asm volatile("rdcycle %0" : "=r"(cycles));
  return cycles;
}

typedef struct {
  uint32_t width;
  uint32_t height;
} ImageHeader;

#define MAX_WIDTH 256
#define MAX_HEIGHT 256
#define CROP_MARGIN 28
#define CROP_WIDTH (MAX_WIDTH - 2 * CROP_MARGIN)
#define CROP_HEIGHT (MAX_HEIGHT - 2 * CROP_MARGIN)

unsigned char read_buffer[sizeof(ImageHeader) + MAX_WIDTH * MAX_HEIGHT * 3];
unsigned char write_buffer[sizeof(ImageHeader) + CROP_WIDTH * CROP_HEIGHT * 3];

int main() {
  // dummy data
  size_t header_size = sizeof(ImageHeader);
  ImageHeader *header = (ImageHeader *)read_buffer;
  header->width = MAX_WIDTH;
  header->height = MAX_HEIGHT;

  unsigned char *src_pixels = read_buffer + header_size;
  for (int i = 0; i < MAX_WIDTH * MAX_HEIGHT * 3; i++) {
    src_pixels[i] = i % 256;
  }

  uint64_t start_cycles = read_cycles();

  // copy header
  memcpy(write_buffer, read_buffer, header_size);

  // invert image
  for (int i = header_size; i < CROP_WIDTH * CROP_HEIGHT * 3; ++i) {
    write_buffer[i] = 255 - read_buffer[i];
  }

  uint64_t end_cycles = read_cycles();

  printf("Cycles: %lu\n", end_cycles - start_cycles);

  return 0;
}