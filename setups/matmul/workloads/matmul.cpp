#include <cstring>

const int MATRIX_SIZE  = 4;
const int ELEMENT_SIZE = sizeof(int);
const int ROW_SIZE     = MATRIX_SIZE * ELEMENT_SIZE;

const int matrixA[MATRIX_SIZE][MATRIX_SIZE] = {
    {1,  2,  3,  4 },
    {5,  6,  7,  8 },
    {9,  10, 11, 12},
    {13, 14, 15, 16}
};

const int matrixB[MATRIX_SIZE][MATRIX_SIZE] = {
    {17, 18, 19, 20},
    {21, 22, 23, 24},
    {25, 26, 27, 28},
    {29, 30, 31, 32}
};

int main() {
	const int data_size_per_chiplet = ROW_SIZE + MATRIX_SIZE * MATRIX_SIZE * ELEMENT_SIZE;
	auto     *data                  = new unsigned char[data_size_per_chiplet];

	// Fill the buffer for the chiplet
	std::memcpy(data, &matrixA[0][0], ROW_SIZE);
	std::memcpy(data + ROW_SIZE, &matrixB[0][0], MATRIX_SIZE * MATRIX_SIZE * ELEMENT_SIZE);

	//@BEGIN_CYCLE_MEASURE
	int *A                  = reinterpret_cast<int *>(data);
	int *B                  = reinterpret_cast<int *>(data + ROW_SIZE);
	int  C_row[MATRIX_SIZE] = {0};

	for (int j = 0; j < MATRIX_SIZE; j++) {
		for (int k = 0; k < MATRIX_SIZE; k++) {
			C_row[j] += A[k] * B[k * MATRIX_SIZE + j];
		}
	}

	auto *result = new unsigned char[ROW_SIZE];
	std::memcpy(result, C_row, ROW_SIZE);
	//@END_CYCLE_MEASURE

	delete[] data;
	delete[] result;

	return 0;
}