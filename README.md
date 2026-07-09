# GuitarDAWLiteOS

Bare-metal **Raspberry Pi 5** loop recorder / guitarist workbench. I²S audio (PCM1808 ADC + PCM5102A DAC), SD recording, HDMI framebuffer UI, GPIO footswitches — **no OS, no USB in the audio path.** Built on the [Circle](https://github.com/rsta2/circle) bare-metal framework (GPLv3).

> **This is the project front door.** Everything links from here.

## Status — 2026-06-29
- ✅ **Research verified.** 29 load-bearing claims checked against primary sources (Circle cloned both branches, TI datasheets, RP1 datasheet, RPi white paper, forums). 6 were wrong; 3 of those change the build. → [docs/claim-verification.md](docs/claim-verification.md)
- ✅ **Build plan written**, corrections folded in, 2 new de-risking spikes added. → [planning/build-plan.md](planning/build-plan.md)
- ✅ **Milestone 0 build chain working.** `aarch64-elf-gcc 16.1.0` → Circle (`develop@22722a76`, `RASPPI=5`) → `kernel_2712.img` built & linked. One command: `scripts/build.sh`. → [docs/build-setup.md](docs/build-setup.md)
- 🔌 **Boot partition staged** in [`sdcard/`](sdcard/) (config + kernel + both Pi 5 DTBs + overlay, all validated). **Your move:** format a microSD FAT32, copy `sdcard/` contents, boot with HDMI → expect Circle log text.
- ⏭️ **Next after first boot:** **Spike B (12.288 MHz MCLK)** — the one item that gates ADC capture.

## The 5 things verification changed (read before building)
1. **No `≤128` DMA chunk cap** — it's a constructor arg, default **8192**. Ring-buffer/SD batching gets *easier*. The doc was wrong.
2. **Pi 5 SD = BCM2712 SDHCI (`CEMMCDevice`, `0x1000FFF000`), not RP1, not `CSDHCIDevice`.** Tested & supported.
3. **Do NOT add `pciex4_reset=0`** with Circle — Circle brings up the PCIe root complex itself. Stock `config.txt`.
4. **MCLK is feasible but not free.** 12.288 MHz is achievable from the Audio PLL, but GPCLK can't parent `pll_audio` directly and Circle's I²S device won't emit MCLK — it needs dedicated `clk_i2s`→GPIO4 code. **→ Spike B.**
5. **The two "MIT" libs (simplecodec3, Yin-Pitch-Tracking) are unlicensed** (all-rights-reserved). YIN patent is **expired** → clean-room reimplement from the 2002 paper.

## Hardware (Phase 1)
- Raspberry Pi 5 · microSD (Class-10/UHS-I) · HDMI monitor ≤1080p · USB-serial for the UART debug console.
- PCM1808 ADC (slave, strap-only) · PCM5102A / GY-PCM5102 DAC (strap-only).

**Verified pin map** ([details](planning/build-plan.md#milestone-2--hardware-bring-up-i²s-loopback)):

| GPIO | Signal | → |
|--:|---|---|
| 18 | BCLK | both codecs |
| 19 | LRCLK/FS | both codecs |
| 20 | data IN (capture) | ← PCM1808 DOUT |
| 21 | data OUT (playback) | → PCM5102A DIN |
| 4 | MCLK 12.288 MHz | → PCM1808 SCKI only |

⚠️ **PCM5102A breakout: set the XSMT pad HIGH (unmute)** — ships muted. ⚠️ **PCM1808: strap MD1=MD0=FMT=GND before power-on.**

## Map
- **[planning/build-plan.md](planning/build-plan.md)** — milestones 0–6, tasks with effort, risk register, pinned dependencies.
- **[docs/claim-verification.md](docs/claim-verification.md)** — full verdict table + 6 consequential corrections with sources.
- **[docs/hardware-review.md](docs/hardware-review.md)** — datasheet-verified BOM, power tree, connection/netlist table, **guitar front-end design**, grounding, risk register.
- **[docs/schematic.md](docs/schematic.md)** — buildable schematic: refdes BOM, net-by-net netlist, **SPICE-verified** front-end, build order, ERC self-check. Sheets: [front-end](docs/frontend-schematic.svg) · [codec & power](docs/codec-power-schematic.svg). Sim: [sim/](docs/sim/).
- **[docs/wiring-diagram.svg](docs/wiring-diagram.svg)** — block-level wiring overview (Pi 5 ↔ PCM1808 ↔ PCM5102A, clocks/data/power).
- **[kicad/](kicad/)** — openable **KiCad project** (`guitardawliteos.kicad_pro`, KiCad-10-verified) + generated netlist/symbols/BOM from `gen.py` (ERC-checked). See [kicad/README.md](kicad/README.md).
- **[docs/pcb-learning-path.md](docs/pcb-learning-path.md)** — **follow-along PCB-design curriculum + open-hardware roadmap** (M1 board-from-netlist → M4 Pi 5 HAT → M6 rev-B HAT+ → M7 faceplate → M8 retro deck → M9 CM5 carrier), hackability + licensing.
- **[docs/parts-order.md](docs/parts-order.md)** — **DigiKey prototyping order** (verified live 2026-07-01): breadboard + codecs + front-end + tape Level A + Touch Display 2, with MPNs, quantities, substitutions for dead parts, and the not-DigiKey list.
- **[docs/retro-deck-design.md](docs/retro-deck-design.md)** — **Phase-2 architecture (verified)**: Touch Display 2 via Circle `addon/rp1dsi` (zero GPIO), cassette tape loop as analog insert (Level A ships in rev B, VINR return + REM punch-in), three-layer retro mechanicals (HAT+ core / faceplate PCB / metal+wood chassis), rev-B GPIO budget.
- **[docs/breadboard-build.md](docs/breadboard-build.md)** + [breadboard-layout.svg](docs/breadboard-layout.svg) — solderless build with the GY modules + a DIP op-amp; placement + step-by-step + gotcha checklist.
- **[docs/build-setup.md](docs/build-setup.md)** — toolchain, build, SD flash, UART.
- Original Phase-1 research reference: project `CLAUDE.md` (the GuitarDAWLiteOS research doc).

## Build foundation (pinned)
Circle `develop` @ `22722a7` · `RASPPI=5` → `kernel_2712.img` · FatFs R0.16 (`addon/fatfs`) · YIN clean-room.
