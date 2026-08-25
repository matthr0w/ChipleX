#!/usr/bin/env bash
#
# Set the core count across the setup so it can run as the single-core
# sequential baseline or as a multi-core parallel blur:
#   - cores.num       in system.yaml
#   - NUM_CORES       in src/program.cpp
#   - executors:      in workloads/blur.yaml
#                     workloads.yaml
#
# The cycle count is identical on every core (same kernel), so the estimate is
# replicated from core0's current entry rather than re-run per core.
#
# Usage: ./set_cores.sh <NUM_CORES>   # a positive integer
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <NUM_CORES, positive integer>" >&2
  exit 2
fi

cores=$1
case "$cores" in
  ''|*[!0-9]*) echo "error: '$cores' is not a number" >&2; exit 2 ;;
esac
if [ "$cores" -eq 0 ]; then
  echo "error: NUM_CORES must be a positive integer" >&2
  exit 2
fi

cd "$(dirname "$0")"

sed -i -E "s/(NUM_CORES[[:space:]]*=[[:space:]]*)[0-9]+;/\1${cores};/" src/program.cpp
sed -i -E "s/(num:[[:space:]]*)[0-9]+/\1${cores}/" system.yaml

# Regenerate the executor list; the sequential run just has core0.
{
  echo "executors:"
  for i in $(seq 0 $((cores - 1))); do
    echo "  - compute.core${i}"
  done
} > workloads/blur.yaml

# Mirror the estimate onto every core, reusing core0's current values so a
# checked-in count survives; a parameter change still re-triggers estimation
# through the input hash.
cycles=$(sed -nE 's/^[[:space:]]*cycles_count:[[:space:]]*([0-9]+).*/\1/p' workloads.yaml | head -n1)
hash=$(sed -nE 's/^[[:space:]]*input_hash:[[:space:]]*([0-9a-f]+).*/\1/p' workloads.yaml | head -n1)
{
  echo "workloads:"
  echo "  blur:"
  for i in $(seq 0 $((cores - 1))); do
    echo "    compute.core${i}:"
    echo "      cycles_count: ${cycles}"
    echo "      input_hash: ${hash}"
  done
} > workloads.yaml

echo "Set core count to ${cores}:"
grep -H -E "NUM_CORES[[:space:]]*=[[:space:]]*[0-9]+;" src/program.cpp
grep -H -E "num:[[:space:]]*[0-9]+" system.yaml
