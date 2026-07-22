#include <cstring>
#include <stdint.h>

struct ImageHeader {
	uint32_t width;
	uint32_t height;
};

#define WIDTH    (256 - 2 * 28)
#define HEIGHT   (256 - 2 * 28)
#define CHANNELS 3

int main() {
	// Dummy image data
	const unsigned total_len = sizeof(ImageHeader) + WIDTH * HEIGHT * CHANNELS;
	auto          *read_buf  = new unsigned char[total_len];
	auto          *header    = reinterpret_cast<ImageHeader *>(read_buf);
	header->width            = WIDTH;
	header->height           = HEIGHT;

	//@BEGIN_CYCLE_MEASURE
	const size_t header_size = sizeof(ImageHeader);
	header                   = reinterpret_cast<ImageHeader *>(read_buf);

	auto *write_buf = new unsigned char[total_len];
	std::memcpy(write_buf, read_buf, header_size);

	for (size_t i = header_size; i < total_len; ++i) {
		write_buf[i] = 255 - read_buf[i];
	}
	//@END_CYCLE_MEASURE

	delete[] read_buf;
	delete[] write_buf;

	return 0;
}