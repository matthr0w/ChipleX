#include <cstring>
#include <stdint.h>

struct ImageHeader {
	uint32_t width;
	uint32_t height;
	uint32_t channels;
};

#define WIDTH    32
#define HEIGHT   (24 - 12)
#define CHANNELS 3

int main() {
	// Dummy image data
	auto *read_buf   = new unsigned char[sizeof(ImageHeader) + WIDTH * HEIGHT * CHANNELS];
	auto *header     = reinterpret_cast<ImageHeader *>(read_buf);
	header->width    = WIDTH;
	header->height   = HEIGHT;
	header->channels = CHANNELS;

	//@BEGIN_CYCLE_MEASURE
	const size_t header_size = sizeof(ImageHeader);
	header                   = reinterpret_cast<ImageHeader *>(read_buf);
	uint32_t width           = header->width;
	uint32_t height          = header->height;

	size_t new_img_size = width * height;
	size_t new_buf_size = header_size + new_img_size;

	auto *write_buf = new unsigned char[new_buf_size];
	std::memcpy(write_buf, read_buf, header_size);

	auto *new_header     = reinterpret_cast<ImageHeader *>(write_buf);
	new_header->channels = 1;

	auto *src = read_buf + header_size;
	auto *dst = write_buf + header_size;

	for (size_t i = 0; i < new_img_size; ++i) {
		unsigned char r = src[i * 3 + 0];
		unsigned char g = src[i * 3 + 1];
		unsigned char b = src[i * 3 + 2];

		dst[i] = static_cast<unsigned char>(0.299 * r + 0.587 * g + 0.114 * b);
	}
	//@END_CYCLE_MEASURE

	delete[] read_buf;
	delete[] write_buf;

	return 0;
}