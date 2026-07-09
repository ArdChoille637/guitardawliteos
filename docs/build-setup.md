# GuitarDAWLiteOS — Build Setup, Flash & UART Guide

Records the working build environment (Milestone 0) and the hands-on steps to boot it on a Pi 5.

## Environment (verified working 2026-06-29, this Mac)
- **Host:** macOS 27 (arm64), GNU Make 3.81, Homebrew 6.0.5.
- **Toolchain:** `aarch64-elf-gcc` **16.1.0** + `aarch64-elf-binutils` (`ld` 2.46.1), via `brew install aarch64-elf-gcc aarch64-elf-binutils`. Prefix `aarch64-elf-`.
  - Circle's *blessed* toolchain is ARM's `aarch64-none-elf` 15.2.Rel1; the Homebrew `aarch64-elf` one is the "distro toolchain" Circle says "may work — test yourself." It **does** work here (full core lib + sample built & linked clean). If a future addon (e.g. `circle-stdlib`/newlib) link-fails, fall back to ARM's [aarch64-none-elf 15.2.Rel1](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) and re-`configure -p aarch64-none-elf-`.
- **Circle:** `rsta2/circle`, branch `develop`, pinned to commit **`22722a76`** (the commit verification was done against), at `circle/`.
- **Config:** `circle/Config.mk` → `RASPPI = 5`, `AARCH = 64`, `PREFIX64 = aarch64-elf-`.

## Build (reproducible)
```bash
scripts/build.sh                        # builds sample/03-screentext -> kernel_2712.img, refreshes sdcard/
scripts/build.sh sample/02-screenpixel  # any other sample
scripts/build.sh --libs                 # core lib only
```
Under the hood: `./configure -r 5 -p aarch64-elf- -f` → `cd lib && make -j` → `cd <sample> && make` → copies `kernel_2712.img` + `config.txt` into `sdcard/`.
**Output of the toolchain validation build:** `kernel_2712.img`, 108 KB, from `03-screentext` (renders text to the HDMI framebuffer; inits serial @115200 too).

## SD boot partition — what goes on it
Pi 5 boots its firmware from the on-board EEPROM, so the FAT partition needs only a few files. The complete, ready set is staged in **`sdcard/`**:

| File | Purpose |
|------|---------|
| `config.txt` | from Circle `boot/config64.txt`; has `arm_64bit=1` + `[pi5] kernel=kernel_2712.img` |
| `kernel_2712.img` | the Circle application (currently the `03-screentext` demo) |
| `bcm2712-rpi-5-b.dtb` | device tree, original Pi 5 |
| `bcm2712d0-rpi-5-b.dtb` | device tree, D0-stepping Pi 5 (covers newer boards) |
| `overlays/bcm2712d0.dtbo` | required D0 overlay |
| `cmdline.txt.uart` | *not active by default* — see UART section |

DTBs/overlay were pulled from the firmware revision Circle pins (`0641c5bf…`); all three validated with the `d00dfeed` device-tree magic.

## Flash it (hands-on — your part)
1. Insert the microSD in the Mac. In **Disk Utility**, erase it as **MS-DOS (FAT)**, scheme **Master Boot Record**. (≥ 2 GB card; the whole card as one FAT partition is fine.)
2. Copy the **contents** of `sdcard/` (not the folder itself) to the root of the card, preserving the `overlays/` subfolder. e.g.:
   ```bash
   cp -R guitardawliteos/sdcard/. /Volumes/<YOUR_SD_NAME>/
   ```
   (Don't copy `cmdline.txt.uart` as-is unless you want the UART console — see below.)
3. Eject, insert into the Pi 5, attach an **HDMI monitor** (≤1080p), power on.
4. **Expected first boot:** within a couple seconds the screen shows Circle log text (compile time, system notices). That confirms Milestone 0 end-to-end: firmware → device tree → `kernel_2712.img` → framebuffer.

## UART debug console (Milestone 0.4 — optional, for headless/log capture)
The HDMI default needs no extra hardware. To route the log to a serial console instead:
1. Rename the staged file on the card: `cmdline.txt.uart` → **`cmdline.txt`** (contents: `logdev=ttyS11`). On Pi 5 the serial log device is **`ttyS11`**.
2. Connect a **3.3 V USB-to-serial adapter** to the Pi 5's **dedicated 3-pin UART/debug connector** (the small JST connector, *not* the 40-pin header): adapter **RX ← Pi TX**, adapter **TX → Pi RX**, **GND ↔ GND**. Do **not** connect the adapter's VCC.
3. On the Mac, open the console at **115200 8N1**:
   ```bash
   ls /dev/cu.usb*        # find the adapter, e.g. /dev/cu.usbserial-XXXX
   screen /dev/cu.usbserial-XXXX 115200      # (quit: Ctrl-A then k)
   ```
4. Power on; the Circle log streams over serial. (Ref: Circle `doc/bootloader.txt`, `doc/cmdline.txt`.) If the dedicated connector gives nothing, the fallback is GPIO14=TXD/GPIO15=RXD on the 40-pin header.

## Notes / gotchas carried from verification
- **Do NOT add `pciex4_reset=0`** to `config.txt` — Circle brings up the PCIe RC itself (see [claim-verification.md](claim-verification.md) #3).
- For 4K HDMI you'd need `hdmi_enable_4kp60=1`; at ≤1080p the framebuffer "limited" caveats don't bite.
- The toolchain prefix lives in `Config.mk`; `scripts/build.sh` exports `/opt/homebrew/bin` on PATH so the brew toolchain is found in non-login shells.
