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
  uint32_t channels;
} ImageHeader;

#define MAX_WIDTH 24
#define MAX_HEIGHT 32
#define NUM_CHANNELS 3
#define CROP_MARGIN 12
#define CROP_WIDTH MAX_WIDTH
#define CROP_HEIGHT (MAX_HEIGHT - CROP_MARGIN)

unsigned char
    read_buffer[sizeof(ImageHeader) + MAX_WIDTH * MAX_HEIGHT * NUM_CHANNELS];
unsigned char
    write_buffer[sizeof(ImageHeader) + CROP_WIDTH * CROP_HEIGHT * NUM_CHANNELS];

int main() {
  // dummy data
  size_t header_size = sizeof(ImageHeader);
  ImageHeader *header = (ImageHeader *)read_buffer;
  header->width = MAX_WIDTH;
  header->height = MAX_HEIGHT;
  header->channels = NUM_CHANNELS;

  unsigned char *src_pixels = read_buffer + header_size;
  for (int i = 0; i < MAX_WIDTH * MAX_HEIGHT * NUM_CHANNELS; i++) {
    src_pixels[i] = i % 256;
  }

  uint64_t start_cycles = read_cycles();

  // image header
  uint32_t width = header->width;
  uint32_t height = header->height;
  uint32_t channels = header->channels;

  // update image header
  uint32_t new_width = width;
  uint32_t new_height = height - 2 * CROP_MARGIN;

  ImageHeader *new_header = (ImageHeader *)write_buffer;
  new_header->width = new_width;
  new_header->height = new_height;
  new_header->channels = channels;

  // crop image
  unsigned char *dst_pixels = write_buffer + header_size;

  for (uint32_t y = 0; y < new_height; ++y) {
    unsigned char *src_row = src_pixels + ((y + CROP_MARGIN) * width) * 3;
    unsigned char *dst_row = dst_pixels + (y * new_width) * 3;
    memcpy(dst_row, src_row, new_width * 3);
  }

  uint64_t end_cycles = read_cycles();

  printf("Cycles: %lu\n", end_cycles - start_cycles);

  return 0;
}