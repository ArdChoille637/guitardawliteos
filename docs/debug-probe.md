# Raspberry Pi Debug Probe → Pi 5

The Debug Probe (SC0889) does **two different jobs** on the Pi 5, and they both use the
**same 3-pin JST-SH connector on the Pi**. You pick one per boot:

| Mode | Probe port | What you get | `config.txt` |
|---|---|---|---|
| **A — UART console** | **U** | Circle's log text streamed to the Mac. Read-only observation. | `enable_jtag_gpio` stays commented out |
| **B — SWD debug** | **D** | Real breakpoints, single-step, memory/register inspection, `load` over the wire, all 4 cores. | `enable_jtag_gpio=1` |

They are mutually exclusive because the Pi 5's debug connector muxes between UART and
Cortex-A76 SWD. In mode B the JST console is gone; the fallback console is GPIO14/15 on
the 40-pin header (see [Serial while in SWD mode](#serial-while-in-swd-mode)).

**Start with mode A.** It takes five minutes and proves the cable, the connector
orientation, and the probe are all good before you add OpenOCD to the list of things
that could be wrong.

---

## Cabling (both modes)

The Pi 5's connector and the Probe's ports both follow
[RP-003139-SP, the Raspberry Pi 3-pin Debug Connector Specification](https://pip-assets.raspberrypi.com/categories/885-raspberry-pi-debug-probe/documents/RP-008189-DS-1-debug-connector-specification.pdf):

| Pin | UART signal | Serial-debug signal |
|---|---|---|
| 1 | RX | SC (serial clock) |
| 2 | GND | GND |
| 3 | TX | SD (bidirectional data) |

Signal names are **from the target's perspective**. The spec pairs them so that the
**JST-SH↔JST-SH cable in the Probe box is straight-through, pin 1 to pin 1** — the TX/RX
crossover is baked into the pin naming, so there is **nothing to swap and nothing to get
backwards**. Plug it in, both connectors are keyed.

- Probe end: the **U** port for mode A, the **D** port for mode B.
- Pi 5 end: the small 3-pin JST connector on the board edge marked **UART** — *not* the
  40-pin header.
- No VCC anywhere. 3.3 V logic on both sides, and the Pi is powered from its own supply.

Plugging a UART into a serial-debug port is explicitly safe under the spec; the reverse
can cause brief contention on pin 3 but is current-limited by the 100 Ω source-termination
resistors. So a wrong-port mistake costs you a confusing session, not hardware.

---

## Mode A — UART console

1. On the SD card, rename `cmdline.txt.uart` → `cmdline.txt`
   (it carries `logdev=ttyS11`; on Pi 5 Circle's dedicated debug UART is device number 10,
   named `ttyS11` — see `circle/doc/cmdline.txt`).
2. Probe **U** port → Pi 5 **UART** connector. Probe USB → Mac.
3. On the Mac:
   ```bash
   ls /dev/cu.usbmodem*
   ```
   The Probe is a CDC device, so it is `cu.usbmodem…` (it was `/dev/cu.usbmodem11402`
   here) — **not** `cu.usbserial…` like a generic FTDI adapter. The number changes with
   the USB port.
4. Open the console at **115200 8N1**:
   ```bash
   screen /dev/cu.usbmodem11402 115200
   ```
   Quit with `Ctrl-A` then `k`, then `y`. If `screen` leaves the terminal wedged:
   `reset`.
5. Power on the Pi. Circle's log streams out.

> The Pi 5's debug UART tops out at 921600 baud (EEPROM-configurable), but Circle logs at
> 115200 and there is no reason to push it.

---

## Mode B — SWD debugging

Host tooling is already installed (`openocd` 0.12.0, `aarch64-elf-gdb` 17.2, both from
Homebrew). `debug/rpi5.cfg` is Circle's Pi 5 target config; `interface/cmsis-dap.cfg`
ships inside OpenOCD's own script path.

### One-time SD card change

`sdcard/config.txt.swd` is the SWD-enabled variant (`enable_jtag_gpio=1` uncommented).
Copy it onto the card **as `config.txt`**:

```bash
cp sdcard/config.txt.swd /Volumes/<YOUR_SD_NAME>/config.txt
```

Keep the stock `sdcard/config.txt` to switch back.

### Optional: the wait stub

`debug/wait-stub_2712.img` is Circle's do-nothing kernel — a single branch instruction that
parks the CPU until the debugger attaches. Copy it onto the card as `kernel_2712.img` when
you want a guaranteed-quiet target to attach to. For normal work, leave your real
`kernel_2712.img` on the card; you can halt it and `load` a fresh build over SWD anyway.

### The session

Two terminals.

**Terminal 1 — OpenOCD** (leave it running):
```bash
./debug/openocd-rpi5.sh
```
Expect it to report a CMSIS-DAP adapter and four `bcm2712.cpu0..3` targets, then listen on
3333–3336. If the DAP is flaky, slow it down: `SPEED=1000 ./debug/openocd-rpi5.sh`.

**Terminal 2 — GDB:**
```bash
aarch64-elf-gdb -x debug/gdbinit src/kernel_2712.elf
```
`debug/gdbinit` already connects to `:3333` (core 0) and defines two helpers:

- `reload` — `load` the current ELF over the wire, break at `main`, continue. **This is
  the big win: no SD-card shuffling and no power cycle between builds.**
- `coreinfo` — OpenOCD's view of all four cores.

Then the usual: `break`, `step`, `next`, `bt`, `info registers`, `x/16x`, `p`.

**Multi-core:** cores 1–3 are separate GDB instances on `:3334`, `:3335`, `:3336`. Since
[`cores.cpp`](../src/cores.cpp) pins the audio engine to its own core, that is how you
inspect the audio core without stopping the boot core.

### Serial while in SWD mode

The JST console is unavailable in mode B. Circle's documented fallback
(`circle/doc/debug-swd.txt`) is a serial adapter on **GPIO14/15** plus, in `Config.mk`:

```make
DEFINE += -DSERIAL_DEVICE_DEFAULT=0
```

and `logdev=ttyS1` (not `ttyS11`) in `cmdline.txt`.

⚠️ **Pinout conflict to remember:** [retro-deck-design.md](retro-deck-design.md) reclaims
GPIO14/15 for `ENC3_SW` / `ENC4_A`. During bring-up the encoders aren't wired, so the
header UART is free — but once the encoder harness exists, **SWD and a serial console can
no longer coexist**. Plan on SWD *or* console at that point, not both.

---

## Gotchas

- **`-O2` stepping is jumpy.** `circle/Rules.mk` compiles with `-g` (good, full DWARF is in
  `kernel_2712.elf`) but optimizes at `-O2`, so single-stepping reorders and inlines. For a
  gnarly bug, rebuild that translation unit with `OPTIMIZE="-O0 -g"` — but be aware `-O0`
  changes timing, which can mask or invent problems in the I²S/DMA path.
- **`enable_jtag_gpio=1` left on** silently kills the UART console. If mode A "stops
  working", check which `config.txt` is on the card.
- **The Pi 5 debug UART is always live** for firmware/bootloader output, independent of
  Circle. Garbage or unexpected text at power-on is usually the firmware, not your kernel.
- No documentation exists from Broadcom for the BCM2712 debug architecture; `rpi5.cfg`'s
  DBGBASE/CTIBASE addresses were reverse-engineered from the ROM table by the Raspberry Pi
  forum community and carried into Circle. If OpenOCD can't find the cores, that table is
  the first suspect.

## References

- [3-pin Debug Connector Specification (RP-003139-SP)](https://pip-assets.raspberrypi.com/categories/885-raspberry-pi-debug-probe/documents/RP-008189-DS-1-debug-connector-specification.pdf)
- [Raspberry Pi Debug Probe documentation](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html)
- `circle/doc/debug-swd.txt` — Circle's own SWD walkthrough (the source for this one)
- `circle/doc/cmdline.txt` — `logdev` options and the `ttyS11` note
