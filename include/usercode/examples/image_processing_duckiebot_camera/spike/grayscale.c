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
unsigned char write_buffer[sizeof(ImageHeader) + CROP_WIDTH * CROP_HEIGHT * 1];

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

  // update header
  size_t new_img_size = width * height * 1;

  memcpy(write_buffer, read_buffer, header_size);

  ImageHeader *new_header = (ImageHeader *)write_buffer;
  new_header->channels = 1;

  // convert to grayscale
  unsigned char *dst_pixels = write_buffer + header_size;

  for (size_t i = 0; i < new_img_size; ++i) {
    unsigned char r = src_pixels[i * 3 + 0];
    unsigned char g = src_pixels[i * 3 + 1];
    unsigned char b = src_pixels[i * 3 + 2];

    // standard grayscale conversion
    unsigned char gray = (unsigned char)(0.299 * r + 0.587 * g + 0.114 * b);

    dst_pixels[i] = gray;
  }

  uint64_t end_cycles = read_cycles();

  printf("Cycles: %lu\n", end_cycles - start_cycles);

  return 0;
}