#include <cstring>
#include <stdint.h>

struct ImageHeader {
  uint32_t width;
  uint32_t height;
  uint32_t channels;
};

#define WIDTH 32
#define HEIGHT 24
#define CHANNELS 3

int main() {
  // Dummy image data
  auto *read_buf =
      new unsigned char[sizeof(ImageHeader) + WIDTH * HEIGHT * CHANNELS];
  auto *header = reinterpret_cast<ImageHeader *>(read_buf);
  header->width = WIDTH;
  header->height = HEIGHT;
  header->channels = CHANNELS;

  //@BEGIN_CYCLE_MEASURE
  const size_t header_size = sizeof(ImageHeader);
  header = reinterpret_cast<ImageHeader *>(read_buf);

  const int crop_margin = 12;
  uint32_t new_width = header->width;
  uint32_t new_height = header->height - crop_margin;
  uint32_t channels = header->channels;

  size_t new_img_size = new_width * new_height * 3;
  size_t new_buf_size = header_size + new_img_size;

  auto *write_buf = new unsigned char[new_buf_size];
  auto *new_header = reinterpret_cast<ImageHeader *>(write_buf);
  new_header->width = new_width;
  new_header->height = new_height;
  new_header->channels = channels;

  auto *src = read_buf + header_size;
  auto *dst = write_buf + header_size;

  for (uint32_t y = 0; y < new_height; ++y)
    std::memcpy(dst + y * new_width * 3,
                src + (y + crop_margin) * new_width * 3, new_width * 3);
  //@END_CYCLE_MEASURE

  delete[] read_buf;
  delete[] write_buf;

  return 0;
}