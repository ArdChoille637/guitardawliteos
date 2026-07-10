#!/usr/bin/env bash
#
# GuitarDAWLiteOS — reproducible Circle build + SD staging (macOS, Pi 5 / AArch64)
#
# Usage:
#   scripts/build.sh                       # build src/ (the M3 audio kernel)
#   scripts/build.sh sample/02-screenpixel # build a Circle sample (only ones
#                                          #   linking libcircle/libsound —
#                                          #   others need ./makeall first)
#   scripts/build.sh --libs                # (re)build Circle libs only
#
# Produces kernel_2712.img and refreshes ./sdcard/ for copying to the
# FAT32 boot partition of a microSD.
set -euo pipefail

PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CIRCLE="$PROJ/circle"
SAMPLE="${1:-$PROJ/src}"   # default: our M3 audio-engine kernel
export PATH="/opt/homebrew/bin:$PATH"          # Homebrew aarch64-elf toolchain
PREFIX=aarch64-elf-

command -v ${PREFIX}gcc >/dev/null || { echo "ERROR: ${PREFIX}gcc not found. Run: brew install aarch64-elf-gcc aarch64-elf-binutils"; exit 1; }
[ -d "$CIRCLE/.git" ] || { echo "ERROR: $CIRCLE missing. Clone rsta2/circle (develop @ 22722a7) there."; exit 1; }

# Whole-tree clean: Circle libs (all of them, built or not — ./makeall clean
# is cheap on clean dirs), addon objects, and our app. Required whenever a
# Config.mk DEFINE or the toolchain changes: DEFINEs like ARM_ALLOW_MULTI_CORE
# and KERNEL_MAX_SIZE change class layouts / memory-map constants baked into
# EVERY object, and Rules.mk objects do not depend on Config.mk. A partial
# clean (just lib + lib/sound) leaves poisoned archives for whatever gets
# linked later (fs/sched at M4, any sample) — reviewer-confirmed trap.
clean_tree () {
  ( cd "$CIRCLE" && ./makeall clean >/dev/null 2>&1 || true )
  ( cd "$CIRCLE" && find lib addon -name '*.o' -o -name '*.d' -o -name '*.a' 2>/dev/null | xargs rm -f 2>/dev/null || true )
  ( cd "$PROJ/src" && make clean >/dev/null 2>&1 || true )
  rm -f "$PROJ/src"/*.d
}

cd "$CIRCLE"
# 1. Ensure Pi 5 / AArch64 config
if [ ! -f Config.mk ] || ! grep -q '^RASPPI = 5' Config.mk; then
  echo ">> configuring Circle for Pi 5"
  ./configure -r 5 -p "$PREFIX" -f
  # configure -f regenerates Config.mk from scratch: every DEFINE below is
  # gone and anything previously compiled is suspect. Clean it all.
  clean_tree
fi

# 1b. Ensure the required tree-wide DEFINEs. Both change compiled layouts:
#   ARM_ALLOW_MULTI_CORE — CSpinLock class layout + atomic strength
#   KERNEL_MAX_SIZE      — memory map: stacks sit at MEM_KERNEL_END, and our
#                          4 MiB BSS ring blows the 2 MB default (the stacks
#                          would land INSIDE the ring buffer: guaranteed
#                          silent boot crash — post-link guard below checks)
NEED_CLEAN=0
ensure_define () {
  local key="$1" line="$2"
  if ! grep -q "$key" Config.mk; then
    echo ">> adding $line to Config.mk"
    printf '%s\n' "$line" >> Config.mk
    NEED_CLEAN=1
  fi
}
ensure_define 'ARM_ALLOW_MULTI_CORE' 'DEFINE += -DARM_ALLOW_MULTI_CORE'
ensure_define 'KERNEL_MAX_SIZE'      'DEFINE += -DKERNEL_MAX_SIZE=0x1000000'

# 1c. Toolchain stamp: a brew upgrade replaces the versioned Cellar path that
# Rules.mk's -M dep files embed; stale .d files then break make with a
# misleading "No rule to make target .../stdint.h". Detect and clean.
STAMP="$CIRCLE/.toolchain-stamp"
TOOLVER="$(${PREFIX}gcc -dumpfullversion)"
if [ ! -f "$STAMP" ] || [ "$(cat "$STAMP")" != "$TOOLVER" ]; then
  [ -f "$STAMP" ] && { echo ">> toolchain changed ($(cat "$STAMP") -> $TOOLVER): cleaning tree"; NEED_CLEAN=1; }
  echo "$TOOLVER" > "$STAMP"
fi

if [ "$NEED_CLEAN" = 1 ]; then
  echo ">> DEFINE/toolchain change: cleaning Circle libs + addons + src"
  clean_tree
fi

# 2. Circle libraries: core + sound (the M3 kernel links libsound.a)
echo ">> building core library"
( cd lib && make -j"$(sysctl -n hw.ncpu)" )
echo ">> building sound library"
( cd lib/sound && make -j"$(sysctl -n hw.ncpu)" )

[ "$SAMPLE" = "--libs" ] && { echo "libs built."; exit 0; }

# 3. Target -> kernel_2712.img (absolute path, or a path relative to circle/)
case "$SAMPLE" in
  /*) TDIR="$SAMPLE" ;;
  *)  TDIR="$CIRCLE/$SAMPLE" ;;
esac
echo ">> building $TDIR"
( cd "$TDIR" && make )
KIMG="$TDIR/kernel_2712.img"
[ -f "$KIMG" ] || { echo "ERROR: $KIMG not produced"; exit 1; }

# 3b. Post-link guard: the kernel (including BSS, which is invisible in the
# .img file size) must fit under KERNEL_MAX_SIZE, or Circle places the core
# stacks inside our data and the boot dies before any output. Check the
# linker's _end against MEM_KERNEL_START + KERNEL_MAX_SIZE.
KELF="$TDIR/kernel_2712.elf"
if [ -f "$KELF" ]; then
  KMAX_HEX="$(grep -o 'KERNEL_MAX_SIZE=0x[0-9A-Fa-f]*' Config.mk | cut -d= -f2 || true)"
  KMAX=$(( ${KMAX_HEX:-0x200000} ))          # Circle default: 2 MB
  KEND=$(( 0x80000 + KMAX ))                 # MEM_KERNEL_START + KERNEL_MAX_SIZE
  BSS_END="0x$(${PREFIX}nm "$KELF" | awk '$3 == "_end" {print $1}')"
  if [ "$BSS_END" = "0x" ] || [ $(( BSS_END )) -ge "$KEND" ]; then
    echo "ERROR: kernel _end ($BSS_END) exceeds MEM_KERNEL_END ($(printf 0x%x "$KEND"))."
    echo "       Core stacks would sit inside kernel data -> silent boot crash."
    echo "       Raise KERNEL_MAX_SIZE in Config.mk (multiple of 16 KB) and rebuild."
    exit 1
  fi
  echo ">> size check: _end $BSS_END < MEM_KERNEL_END $(printf 0x%x "$KEND") OK"
fi

# 4. Refresh SD staging (device trees are kept; only kernel + config refreshed)
echo ">> staging $PROJ/sdcard/"
mkdir -p "$PROJ/sdcard/overlays"
cp "$KIMG" "$PROJ/sdcard/kernel_2712.img"
cp "$CIRCLE/boot/config64.txt" "$PROJ/sdcard/config.txt"

# 4b. Verify the boot-critical files a Pi 5 needs are actually staged — the
# kernel alone does not boot (firmware wants the DTBs + overlay; cmdline.txt
# carries the fan/log options our kernel expects).
MISSING=0
for f in bcm2712-rpi-5-b.dtb bcm2712d0-rpi-5-b.dtb overlays/bcm2712d0.dtbo cmdline.txt; do
  if [ ! -f "$PROJ/sdcard/$f" ]; then
    echo "WARNING: sdcard/$f is MISSING — the Pi 5 will not boot without it."
    MISSING=1
  fi
done
[ "$MISSING" = 1 ] && echo "         (DTBs come from 'make' in $CIRCLE/boot, cmdline.txt from docs/build-setup.md)"

echo "OK: $(ls -la "$PROJ/sdcard/kernel_2712.img" | awk '{print $5" bytes"}'), built from $SAMPLE"
echo "Copy the CONTENTS of $PROJ/sdcard/ to the FAT32 boot partition of the microSD."
