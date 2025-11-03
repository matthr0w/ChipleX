#include <cstring>
#include <stdint.h>

struct ImageHeader {
  uint32_t width;
  uint32_t height;
};

#define WIDTH 256
#define HEIGHT 256
#define CHANNELS 3

void crop() {
  // Dummy image data
  auto *read_buf =
      new unsigned char[sizeof(ImageHeader) + WIDTH * HEIGHT * CHANNELS];
  auto *header = reinterpret_cast<ImageHeader *>(read_buf);
  header->width = WIDTH;
  header->height = HEIGHT;

  //@START_MEASURE
  const size_t header_size = sizeof(ImageHeader);
  header = reinterpret_cast<ImageHeader *>(read_buf);

  const int crop_margin = 28;
  uint32_t new_width = header->width - 2 * crop_margin;
  uint32_t new_height = header->height - 2 * crop_margin;

  size_t new_img_size = new_width * new_height * 3;
  size_t new_buf_size = header_size + new_img_size;

  auto *write_buf = new unsigned char[new_buf_size];
  auto *new_header = reinterpret_cast<ImageHeader *>(write_buf);
  new_header->width = new_width;
  new_header->height = new_height;

  auto *src = read_buf + header_size;
  auto *dst = write_buf + header_size;

  for (uint32_t y = 0; y < new_height; ++y) {
    unsigned char *src_row =
        src + ((y + crop_margin) * header->width + crop_margin) * 3;
    unsigned char *dst_row = dst + (y * new_width) * 3;
    std::memcpy(dst_row, src_row, new_width * 3);
  }
  //@END_MEASURE

  delete[] read_buf;
  delete[] write_buf;
}