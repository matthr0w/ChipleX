#!/usr/bin/env bash
#
# Regression harness: run every setup and compare its stats.json against the
# committed golden output in tests/golden/. This is the framework's safety net
# against unintended behavior changes (stats.json is deterministic: the
# bit-error RNG is seeded and the JSON keys are sorted).
#
# Prerequisites: the simulator must be built (see README / `make`) and
# SYSTEMC_PATH + LD_LIBRARY_PATH set exactly as for `make run`.
#
# Usage:
#   scripts/regression.sh              # verify all setups against golden
#   scripts/regression.sh --update     # (re)generate the golden files
#   scripts/regression.sh <setup>...   # verify only the named setups
#
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
GOLDEN="$ROOT/tests/golden"

UPDATE=0
if [ "${1:-}" = "--update" ]; then
  UPDATE=1
  shift
fi

if [ ! -x "$ROOT/sim" ]; then
  echo "Error: ./sim is not built. Run 'make' with SYSTEMC_PATH set." >&2
  exit 2
fi

mkdir -p "$GOLDEN"
if [ "$#" -gt 0 ]; then
  setups="$*"
else
  setups=$(ls -d setups/*/ | xargs -n1 basename)
fi

fail=0
for s in $setups; do
  rm -f stats.json
  SYSTEMC_DISABLE_COPYRIGHT_MESSAGE=1 timeout 600 \
    ./sim --setup="$s" --logging=SILENT >/dev/null 2>&1
  ec=$?
  if [ ! -f stats.json ]; then
    echo "FAIL $s: produced no stats.json (exit $ec)"
    fail=1
    continue
  fi
  # The wall-clock line varies run to run; exclude it from the comparison.
  cur="$(mktemp)"
  grep -v '"execution_time_ms"' stats.json >"$cur"

  if [ "$UPDATE" = "1" ]; then
    cp "$cur" "$GOLDEN/$s.stats.json"
    echo "updated $s"
  elif [ ! -f "$GOLDEN/$s.stats.json" ]; then
    echo "WARN  $s: no golden file (run: scripts/regression.sh --update)"
    fail=1
  elif diff -q "$GOLDEN/$s.stats.json" "$cur" >/dev/null; then
    echo "PASS  $s"
  else
    echo "FAIL  $s: stats.json differs from golden"
    fail=1
  fi
  rm -f "$cur"
done

exit $fail
