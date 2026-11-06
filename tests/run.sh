#!/usr/bin/env bash
# Host-side tests: no board, no sensor, no CI needed. Needs a C++11 compiler (g++ or clang++).
#   tests/run.sh
set -euo pipefail
cd "$(dirname "$0")/.."
CXX=${CXX:-g++}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
FLAGS="-std=c++11 -Wall -Wextra -Werror"

echo "== shared header copies are identical"
if ! cmp -s arduino/M3200/src/m3200_decode.h esphome/components/m3200/m3200_decode.h; then
  echo "  FAIL: arduino/M3200/src/m3200_decode.h and esphome/components/m3200/m3200_decode.h differ"
  diff -u arduino/M3200/src/m3200_decode.h esphome/components/m3200/m3200_decode.h || true
  exit 1
fi
echo "  ok"

echo "== m3200_decode.h"
$CXX $FLAGS tests/test_decode.cpp -o "$OUT/test_decode"
"$OUT/test_decode"

echo "== Arduino library (fake Wire)"
$CXX $FLAGS -Itests/fakes -Iarduino/M3200/src tests/test_arduino.cpp arduino/M3200/src/M3200.cpp -o "$OUT/test_arduino"
"$OUT/test_arduino"

echo "All host tests passed."
