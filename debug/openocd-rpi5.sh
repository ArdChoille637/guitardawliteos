#!/usr/bin/env bash
# Start OpenOCD against the Raspberry Pi 5 over SWD via the Raspberry Pi Debug Probe.
#
# Prereqs (see docs/debug-probe.md):
#   - Probe "D" port -> Pi 5 UART connector (JST-JST cable, straight through)
#   - SD card booted with config.txt containing enable_jtag_gpio=1
#   - brew install open-ocd aarch64-elf-gdb
#
# GDB then attaches to :3333 (core 0), :3334/:3335/:3336 (cores 1-3).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Lower this if the DAP is flaky (rpi5.cfg defaults to 4000 kHz).
SPEED="${SPEED:-4000}"

exec openocd \
  -f interface/cmsis-dap.cfg \
  -c "adapter speed ${SPEED}" \
  -f "${HERE}/rpi5.cfg" \
  "$@"
