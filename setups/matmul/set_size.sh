#!/usr/bin/env bash
#
# Set MATRIX_SIZE across the setup: include/matmul.h for the program and
# workloads/matmul.cpp for the cycle estimate.
#
# The size has to be a literal in the workload source. The estimator decides
# whether a cached cycle count is still valid from the hash of that one file, so
# a size taken from a shared header would leave a stale count in place.
#
# Usage: ./set_size.sh <MATRIX_SIZE>   # any multiple of 4, the chiplet count
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <MATRIX_SIZE, multiple of 4>" >&2
  exit 2
fi

size=$1
case "$size" in
  ''|*[!0-9]*) echo "error: '$size' is not a number" >&2; exit 2 ;;
esac
if [ "$size" -eq 0 ] || [ $((size % 4)) -ne 0 ]; then
  echo "error: MATRIX_SIZE must be a positive multiple of 4" >&2
  exit 2
fi

cd "$(dirname "$0")"
for f in include/matmul.h workloads/matmul.cpp; do
  sed -i -E "s/(MATRIX_SIZE[[:space:]]*=[[:space:]]*)[0-9]+;/\1${size};/" "$f"
done

grep -H -E "MATRIX_SIZE[[:space:]]*=[[:space:]]*[0-9]+;" include/matmul.h workloads/matmul.cpp

# The manager sends each worker its row block of A plus all of B in one AXI
# transfer, which carries at most 256 beats of the bus width, capped at 4 KB.
width=$(sed -nE 's/^constexpr int AXI_WIDTH_BITS = ([0-9]+);.*/\1/p' include/matmul.h)
limit=$((256 * width / 8))
if [ "$limit" -gt 4096 ]; then
  limit=4096
fi

chunk=$((5 * size * size))
if [ "$chunk" -gt "$limit" ]; then
  echo
  echo "note: the operand chunk is now ${chunk} bytes, above the ${limit} bytes one AXI" >&2
  echo "      transfer carries at AXI_WIDTH_BITS=${width}. Raise axi.width in system.yaml and" >&2
  echo "      AXI_WIDTH_BITS in include/matmul.h together, or the build stops at the" >&2
  echo "      static_assert there." >&2
fi
