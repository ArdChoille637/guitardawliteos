#!/usr/bin/env bash
#
# GuitarDAWLiteOS — reproducible Circle build + SD staging (macOS, Pi 5 / AArch64)
#
# Usage:
#   scripts/build.sh                       # build default sample (03-screentext)
#   scripts/build.sh sample/02-screenpixel # build a specific sample
#   scripts/build.sh --libs                # (re)build core lib only
#
# Produces sample/<x>/kernel_2712.img and refreshes ./sdcard/ for copying to the
# FAT32 boot partition of a microSD.
set -euo pipefail

PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CIRCLE="$PROJ/circle"
SAMPLE="${1:-$PROJ/src}"   # default: our bring-up kernel (thermal-managed)
export PATH="/opt/homebrew/bin:$PATH"          # Homebrew aarch64-elf toolchain
PREFIX=aarch64-elf-

command -v ${PREFIX}gcc >/dev/null || { echo "ERROR: ${PREFIX}gcc not found. Run: brew install aarch64-elf-gcc aarch64-elf-binutils"; exit 1; }
[ -d "$CIRCLE/.git" ] || { echo "ERROR: $CIRCLE missing. Clone rsta2/circle (develop @ 22722a7) there."; exit 1; }

cd "$CIRCLE"
# 1. Ensure Pi 5 / AArch64 config
if [ ! -f Config.mk ] || ! grep -q '^RASPPI = 5' Config.mk; then
  echo ">> configuring Circle for Pi 5"
  ./configure -r 5 -p "$PREFIX" -f
fi

# 2. Core library (sample 03 only links libcircle.a)
echo ">> building core library"
( cd lib && make -j"$(sysctl -n hw.ncpu)" )

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

# 4. Refresh SD staging (device trees are kept; only kernel + config refreshed)
echo ">> staging $PROJ/sdcard/"
mkdir -p "$PROJ/sdcard/overlays"
cp "$KIMG" "$PROJ/sdcard/kernel_2712.img"
cp "$CIRCLE/boot/config64.txt" "$PROJ/sdcard/config.txt"
echo "OK: $(ls -la "$PROJ/sdcard/kernel_2712.img" | awk '{print $5" bytes"}'), built from $SAMPLE"
echo "Copy the CONTENTS of $PROJ/sdcard/ to the FAT32 boot partition of the microSD."
