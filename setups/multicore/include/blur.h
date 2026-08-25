#pragma once

#include <stdint.h>

constexpr unsigned IMAGE_WIDTH  = 256;
constexpr unsigned IMAGE_HEIGHT = 256;
constexpr unsigned CHANNELS     = 3;

constexpr unsigned ROW_BYTES = IMAGE_WIDTH * CHANNELS;

// Rows per band, the unit of work a core claims.
constexpr unsigned BAND_ROWS = 8;

constexpr unsigned BANDS = (IMAGE_HEIGHT + BAND_ROWS - 1) / BAND_ROWS;

// A band is filtered with one row above and below it, so the vertical taps of the
// 3x3 kernel are always in the buffer.
constexpr unsigned HALO_ROWS = 2;

constexpr unsigned BAND_IN_BYTES  = (BAND_ROWS + HALO_ROWS) * ROW_BYTES;
constexpr unsigned BAND_OUT_BYTES = BAND_ROWS * ROW_BYTES;

// 3x3 box blur of `rows` image rows. `in` holds those rows plus their halo rows,
// `out` receives the filtered ones. Horizontal taps clamp at the image edge.
inline void blur_band(const unsigned char *in, unsigned rows, unsigned char *out) {
	for (unsigned y = 0; y < rows; ++y) {
		for (unsigned x = 0; x < IMAGE_WIDTH; ++x) {
			const unsigned left  = (x == 0) ? 0 : x - 1;
			const unsigned right = (x + 1 == IMAGE_WIDTH) ? x : x + 1;

			for (unsigned c = 0; c < CHANNELS; ++c) {
				unsigned sum = 0;
				for (unsigned tap = 0; tap < 3; ++tap) {
					const unsigned char *row  = in + (y + tap) * ROW_BYTES;
					sum                      += row[left * CHANNELS + c];
					sum                      += row[x * CHANNELS + c];
					sum                      += row[right * CHANNELS + c];
				}
				out[y * ROW_BYTES + x * CHANNELS + c] = static_cast<unsigned char>(sum / 9);
			}
		}
	}
}
