#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SIZE 1000

void initialize_array(int arr[], int size) {
  for (int i = 0; i < size; i++)
    arr[i] = rand() % 100;
}

int main() {
  srand(time(NULL));

  int A[SIZE];
  int B[SIZE];
  int C[SIZE];

  initialize_array(A, SIZE);
  initialize_array(B, SIZE);

  asm("# LLVM-MCA-BEGIN TEST");
  for (int i = 0; i < SIZE; i++)
    C[i] = A[i] + B[i];
  asm("# LLVM-MCA-END");

  return 0;
}