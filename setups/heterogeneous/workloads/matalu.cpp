#include <cstddef>
#include <cstdint>

const size_t CHUNK_SIZE = 64;

int main() {
	size_t   size = CHUNK_SIZE;
	uint8_t *data = new uint8_t[size];
	for (size_t i = 0; i < size; ++i) {
		data[i] = static_cast<uint8_t>(0);
	}

	int accel_id = 0;

	//@BEGIN_CYCLE_MEASURE
	//@BEGIN_SPEEDUP_MEASURE
	for (size_t i = accel_id; i < size; i += 4) {
		int x = data[i];
		// Heavy ALU chain
		int a   = x * 3 + 1;
		int b   = x * 7 - 5;
		int c   = x ^ (x << 1);
		int d   = x ^ (x >> 2);
		data[i] = a + b + c + d;
	}
	//@END_SPEEDUP_MEASURE
	//@END_CYCLE_MEASURE

	delete[] data;

	return 0;
}