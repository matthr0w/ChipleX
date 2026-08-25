#pragma once

// Must match the chiplet count in system.yaml.
constexpr int NUM_CHIPLETS = 4;
// Must match axi.width in system.yaml.
constexpr int AXI_WIDTH_BITS = 32;

// Problem size, any multiple of NUM_CHIPLETS
constexpr int MATRIX_SIZE  = 8;
constexpr int ELEMENT_SIZE = static_cast<int>(sizeof(int));

// Rows of C one chiplet owns
constexpr int ROWS_PER_CHIPLET = MATRIX_SIZE / NUM_CHIPLETS;

constexpr int ROW_BYTES    = MATRIX_SIZE * ELEMENT_SIZE;
constexpr int MATRIX_BYTES = MATRIX_SIZE * ROW_BYTES;

// One chiplet's rows: of A inbound, of C outbound
constexpr int BLOCK_BYTES = ROWS_PER_CHIPLET * ROW_BYTES;

// What the manager sends a worker: its row block of A followed by all of B
constexpr int CHUNK_BYTES = BLOCK_BYTES + MATRIX_BYTES;

// One AXI transfer carries at most 256 beats of the bus width, capped at 4 KB,
// and each block moves in a single transfer.
constexpr int MAX_TRANSFER_BYTES = 256 * AXI_WIDTH_BITS / 8 < 4096 ? 256 * AXI_WIDTH_BITS / 8 : 4096;

static_assert(MATRIX_SIZE % NUM_CHIPLETS == 0, "MATRIX_SIZE must be a multiple of NUM_CHIPLETS");
static_assert(AXI_WIDTH_BITS >= 8 && AXI_WIDTH_BITS % 8 == 0, "AXI_WIDTH_BITS must be a whole number of bytes");
static_assert(CHUNK_BYTES <= MAX_TRANSFER_BYTES,
              "operand chunk exceeds one AXI transfer: lower MATRIX_SIZE, or widen axi.width in system.yaml "
              "and AXI_WIDTH_BITS above with it");
static_assert(BLOCK_BYTES <= MAX_TRANSFER_BYTES, "result block exceeds one AXI transfer");

inline int element_a(int row, int col) {
	return row * MATRIX_SIZE + col + 1;
}

inline int element_b(int row, int col) {
	return MATRIX_SIZE * MATRIX_SIZE + row * MATRIX_SIZE + col + 1;
}

// What one worker receives: its row block of A, then all of B. B crosses every
// link, since every worker needs all of it.
inline void fill_chunk(int *chunk, int chiplet) {
	for (int row = 0; row < ROWS_PER_CHIPLET; row++) {
		for (int col = 0; col < MATRIX_SIZE; col++) {
			chunk[row * MATRIX_SIZE + col] = element_a(chiplet * ROWS_PER_CHIPLET + row, col);
		}
	}

	int *b = chunk + ROWS_PER_CHIPLET * MATRIX_SIZE;
	for (int row = 0; row < MATRIX_SIZE; row++) {
		for (int col = 0; col < MATRIX_SIZE; col++) {
			b[row * MATRIX_SIZE + col] = element_b(row, col);
		}
	}
}

// One worker's row block of C from the chunk it received.
inline void multiply_block(const int *chunk, int *rows) {
	const int *a = chunk;
	const int *b = chunk + ROWS_PER_CHIPLET * MATRIX_SIZE;
	for (int row = 0; row < ROWS_PER_CHIPLET; row++) {
		for (int col = 0; col < MATRIX_SIZE; col++) {
			int sum = 0;
			for (int k = 0; k < MATRIX_SIZE; k++) {
				sum += a[row * MATRIX_SIZE + k] * b[k * MATRIX_SIZE + col];
			}
			rows[row * MATRIX_SIZE + col] = sum;
		}
	}
}
