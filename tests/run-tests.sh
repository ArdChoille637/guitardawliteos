#!/usr/bin/env bash
#
# GuitarDAWLiteOS — host-side unit tests (run on the Mac, not the Pi)
#
# The lock-free ring buffer is the one piece of M3 whose bugs would be
# nearly undebuggable on bare metal — so it gets stress-tested here on a
# real multi-core host first, twice:
#   * plain -O2 — the REAL ordering test: on an ARM64 host the weak memory
#     model can actually reorder; run this on Apple Silicon, not x86 (TSO
#     hides store reordering and weakens the run to almost nothing).
#   * ThreadSanitizer — catches accidental NON-atomic shared accesses
#     (races/UB). It does NOT verify fence strength or the ordering of
#     RELAXED atomics, so it complements the plain run; it can't replace it.
# Full closure of ordering coverage is the on-target soak during hardware
# bring-up (M3 gate: 10 min, zero corrupt).
set -euo pipefail

if [ "$(uname -m)" != "arm64" ]; then
  echo "WARNING: not an ARM64 host - the plain run loses most of its power" >&2
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${TMPDIR:-/tmp}/gdaw-tests"
mkdir -p "$OUT"

echo ">> ringbuffer test, plain -O2"
clang++ -std=c++17 -O2 -Wall -Wextra -pthread \
        -o "$OUT/ringbuffer_test" "$HERE/ringbuffer_test.cpp"
"$OUT/ringbuffer_test"

echo
echo ">> ringbuffer test, ThreadSanitizer"
clang++ -std=c++17 -O1 -g -fsanitize=thread -pthread \
        -o "$OUT/ringbuffer_test_tsan" "$HERE/ringbuffer_test.cpp"
"$OUT/ringbuffer_test_tsan"

echo
echo "ALL TEST BINARIES PASSED"
