#include <stdint.h>
#include <stdio.h>

static inline uint64_t rdcycle(void) {
  uint64_t c;
  asm volatile("rdcycle %0" : "=r"(c));
  return c;
}

volatile uint64_t DATA = 123;

int main() {
  volatile uint64_t sum = 0;
  const uint64_t N = 10000000;

  uint64_t t0 = rdcycle();
  for (uint64_t i = 0; i < N; i++)
    sum += i; // pure ALU work, no memory loads
  uint64_t t1 = rdcycle();

  printf("ALU Results:\n");
  printf("Sum = %lu\n", sum);
  printf("Cycles = %lu\n", t1 - t0);
  printf("Cycles per iteration = %.4f\n\n", (double)(t1 - t0) / (double)N);

  sum = 0;

  uint64_t t2 = rdcycle();
  for (uint64_t i = 0; i < N; i++)
    sum += DATA; // memory load each iteration
  uint64_t t3 = rdcycle();

  printf("Load Results:\n");
  printf("Sum = %lu\n", sum);
  printf("Cycles = %lu\n", t3 - t2);
  printf("Cycles per iteration = %.4f\n", (double)(t3 - t2) / (double)N);

  return 0;
}