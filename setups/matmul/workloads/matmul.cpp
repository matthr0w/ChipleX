const int NUM_CHIPLETS = 4;
const int MATRIX_SIZE  = 8;

const int ROWS_PER_CHIPLET = MATRIX_SIZE / NUM_CHIPLETS;

int main() {
	const int chunk_size = ROWS_PER_CHIPLET * MATRIX_SIZE + MATRIX_SIZE * MATRIX_SIZE;

	int *chunk = new int[chunk_size];
	int *rows  = new int[ROWS_PER_CHIPLET * MATRIX_SIZE];

	for (int i = 0; i < chunk_size; i++) {
		chunk[i] = i + 1;
	}

	const int *a = chunk;
	const int *b = chunk + ROWS_PER_CHIPLET * MATRIX_SIZE;

	//@BEGIN_CYCLE_MEASURE
	for (int row = 0; row < ROWS_PER_CHIPLET; row++) {
		for (int col = 0; col < MATRIX_SIZE; col++) {
			int sum = 0;
			for (int k = 0; k < MATRIX_SIZE; k++) {
				sum += a[row * MATRIX_SIZE + k] * b[k * MATRIX_SIZE + col];
			}
			rows[row * MATRIX_SIZE + col] = sum;
		}
	}
	//@END_CYCLE_MEASURE

	delete[] chunk;
	delete[] rows;

	return 0;
}
