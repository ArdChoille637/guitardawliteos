# esp32-mclk

Standalone MCLK generator for the PCM1808 ADC, running on an **Arduino Nano
ESP32** (ESP32-S3). This is the resolution to Spike B — the Pi 5 cannot
generate a `pll_audio`-locked 12.288 MHz clock on any GPIO while its I²S
peripheral is running (see [../docs/claim-verification.md](../docs/claim-verification.md),
"Update 2026-07-09"). This board sidesteps the problem entirely by generating
MCLK off-chip.

**✅ Bench-tested 2026-07-09**, without a scope — see "How it was verified"
below. Measured **12.2880 MHz**, exact match to target.

## What it does

`esp32-mclk.ino` configures the ESP32-S3's `i2s_std` peripheral as an
MCLK-only clock source:
- Sample rate 48 kHz, `mclk_multiple = I2S_MCLK_MULTIPLE_256` → 12.288 MHz,
  using the driver's default clock source (`I2S_CLK_SRC_DEFAULT` = the
  general-purpose `PLL_160M`). **Correction:** an earlier version of this
  file said ESP32-S3 has a dedicated Audio PLL (APLL) and that the code
  explicitly selected it. Neither is true — APLL is original-ESP32-only
  (confirmed against the installed SDK's `soc/clk_tree_defs.h`); the S3's
  `i2s_std` clock source options are just `PLL_160M`/`PLL_240M`/`XTAL`. The
  code now just uses the driver's sane default rather than requesting a
  clock source that doesn't exist on this chip.
- BCLK/WS/DATA GPIOs are all `I2S_GPIO_UNUSED` — only the MCLK pin is driven.
  Since the 2026-07-10 clock pivot the **PCM1808 divides this MCLK into
  BCLK/LRCLK** (it straps as bus master) and the Pi 5 runs I²S *slave* on
  GPIO18/19/20/21 — see [docs/adc-hookup.md](../docs/adc-hookup.md); this
  board never touches those signals either way.
- A built-in **self-test**: the ESP32-S3's PCNT (pulse counter) peripheral
  counts edges on D3 (GPIO6) and prints the measured frequency once a
  second — see below.

## How it was verified (no oscilloscope on hand)

Jumper **D2 → D3** (GPIO5 → GPIO6) so the MCLK output loops back into the
chip's own PCNT counter input, then watch the Serial Monitor at 115200 baud:
a fresh reading prints every second, e.g. `Measured MCLK: 12.2880 MHz
(target 12.288000 MHz)`. This confirms the output frequency is correct; it
does **not** characterize jitter or duty cycle the way a real scope would —
not expected to matter for a consumer-grade ADC like the PCM1808, which per
`[CORR-4]`/E2 only needs fs-sync, not phase-lock. **Remove the D2–D3 jumper**
before wiring the board to the real PCM1808 — the self-test loopback isn't
part of normal operation.

## Build & flash

1. Arduino IDE (or `arduino-cli`) with the **esp32:esp32 core v3.x**
   (Espressif's own board index, not Arduino's `arduino:esp32` package —
   that one tops out at ESP-IDF 4.4 and lacks the `driver/i2s_std.h` /
   `driver/pulse_cnt.h` APIs used here). Board index URL:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Board: **`esp32:esp32:nano_nora`** ("Arduino Nano ESP32" in the IDE's
   board list under the Espressif index — a different FQBN from the
   Arduino-index entry of the same display name).
3. Open `esp32-mclk.ino`, select the board's port, upload.
   - **This board's native USB has no auto-reset circuit** — uploads don't
     "just work" the way they do on boards with an FTDI/CH340 chip.
     Uploading via `arduino-cli upload` / the IDE's dfu-util path may fail
     with "No DFU capable USB device available." If so, flash directly with
     `esptool` instead (see below) while the board is in **Native Boot
     mode**: short pin **B1 to GND**, press **RESET** once, confirm the
     onboard RGB LED goes solid **purple**, then flash. Afterward, remove
     the B1–GND jumper and press RESET once (single tap) to boot normally.
4. Open the Serial Monitor at 115200 baud — expect a startup banner
   followed by a `Measured MCLK: ...` line every second.

### Manual esptool flash (if dfu-util upload fails)
```sh
arduino-cli compile --fqbn esp32:esp32:nano_nora --build-path /tmp/esp32-mclk-build .

PLATFORM=<arduino15>/packages/esp32/hardware/esp32/3.3.10   # find via: arduino-cli core list
BUILD=/tmp/esp32-mclk-build
PORT=<port shown while the board is in Native Boot mode / solid purple LED>

esptool --chip esp32s3 --port "$PORT" --before default-reset --after hard-reset write-flash -z \
  --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x0      "$BUILD/esp32-mclk.ino.bootloader.bin" \
  0x8000   "$BUILD/esp32-mclk.ino.partitions.bin" \
  0xe000   "$PLATFORM/tools/partitions/boot_app0.bin" \
  0xf70000 "$PLATFORM/variants/arduino_nano_nora/extra/nora_recovery/nora_recovery.ino.bin" \
  0x10000  "$BUILD/esp32-mclk.ino.bin"
```

## Wiring (to the PCM1808, after the self-test jumper is removed)

| Nano ESP32 pin | Signal | → |
|---|---|---|
| D2 (GPIO5) | MCLK 12.288 MHz | PCM1808 **SCKI (pin 6)** only |
| GND | common ground | tie to the Pi 5 / breadboard ground |

Power the Nano ESP32 from **its own USB-C** — do **not** feed the 5 V
breadboard rail into VIN (the Nano ESP32's VIN regulator wants 6–21 V;
5 V there is out of spec). It's electrically independent of the Pi 5
audio board except for the MCLK line and a shared ground.

## Why not the Pi 5 / a Pico / a Si5351

- **Pi 5 GPCLK0 (internal):** ruled out — `clk_i2s` (the only RP1 clock
  generator with `pll_audio` as a parent) is already claimed by the I²S
  peripheral for BCLK the whole time audio runs; no other RP1 clock
  generator can source `pll_audio` at all. See claim-verification.md.
- **Si5351A:** viable alternative (I²C-configured clock-gen chip), but a new
  part to source; not chosen here since a Nano ESP32 covers the same job
  through a documented high-level driver call (`i2s_std`'s `mclk_multiple`)
  instead of hand-computing register values from a datasheet.
- **RP2040/Pico:** also viable — its GPOUT clock divider is functionally
  in the same category as what ESP32-S3 actually has (a plain divider, not
  a dedicated audio PLL — that APLL advantage was a mistaken claim, see
  above). The real reason to prefer the Nano ESP32 here is the same one as
  vs. the Si5351: less hand-rolled clock-register code, not superior clock
  hardware.
