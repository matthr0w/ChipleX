void matmultiply() {
  int A[4] = {1, 2, 3, 4};
  int B[4] = {1, 2, 3, 4};
  int result[4];

  //@START_MEASURE
  result[0] = A[0] * B[0] + A[1] * B[2];
  result[1] = A[0] * B[1] + A[1] * B[3];
  result[2] = A[2] * B[0] + A[3] * B[2];
  result[3] = A[2] * B[1] + A[3] * B[3];
  //@END_MEASURE
}