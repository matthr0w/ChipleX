#include "blur.h"

int main() {
	auto *in  = new unsigned char[BAND_IN_BYTES];
	auto *out = new unsigned char[BAND_OUT_BYTES];

	for (unsigned i = 0; i < BAND_IN_BYTES; ++i) {
		in[i] = static_cast<unsigned char>(i * 7 + 13);
	}

	//@BEGIN_CYCLE_MEASURE
	blur_band(in, BAND_ROWS, out);
	//@END_CYCLE_MEASURE

	// Consume the result so the kernel cannot be optimized away.
	unsigned checksum = 0;
	for (unsigned i = 0; i < BAND_OUT_BYTES; ++i) {
		checksum += out[i];
	}

	delete[] in;
	delete[] out;

	return static_cast<int>(checksum & 0x1);
}
