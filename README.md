# GuitarDAWLiteOS

Bare-metal **Raspberry Pi 5** loop recorder / guitarist workbench. I²S audio (PCM1808 ADC + PCM5102A DAC), SD recording, HDMI framebuffer UI, GPIO footswitches — **no OS, no USB in the audio path.** Built on the [Circle](https://github.com/rsta2/circle) bare-metal framework (GPLv3).

> **This is the project front door.** Everything links from here.

## Status — 2026-07-10
- 🎉 **FIRST HARDWARE BOOT — success, first try.** Pi 5 (2GB) booted the M3 audio-engine kernel over HDMI: all 4 cores up, `I2S full duplex (TXRX): RUNNING`, capture counting **exactly 48,000 frames/s**, zero drops, zero ring overruns, storage drain at exactly the audio rate, thermal loop live. Milestone 0 ✅, Spike A ✅ (RP1 proven by the running I²S DMA itself), M3 engine verified live (codecs still to wire).
- ✅ **Research verified.** 29 load-bearing claims checked against primary sources. 6 were wrong; 3 of those change the build. → [docs/claim-verification.md](docs/claim-verification.md)
- ✅ **Spike B done.** Internal MCLK route impossible (`clk_i2s` is claimed by BCLK); an Arduino Nano ESP32 generates 12.2880 MHz instead, bench-verified via its own PCNT peripheral. → [esp32-mclk/](esp32-mclk/)
- ✅ **Milestone 3 code live** — full-duplex I²S monitor path (~1.0 ms worst-case in→out), lock-free SPMC broadcast ring (seqlock reserve/commit; host tests caught a real torn-read bug pre-hardware; passes plain + ThreadSanitizer), 4-core split. Adversarially reviewed (5 lenses, 2 rounds); the review caught a critical `KERNEL_MAX_SIZE` boot-blocker before first boot. → [src/](src/), [tests/](tests/), [planning/build-plan.md](planning/build-plan.md)
- ⏭️ **Next: Milestone 2 wiring** — breadboard the codecs + front-end per [docs/breadboard-build.md](docs/breadboard-build.md) (DAC playback first for the fastest win), Nano ESP32 → PCM1808 SCKI, then the M3 gate: clean tone in→out, 10 min, zero underruns.

## The 6 things verification changed (read before building)
1. **No `≤128` DMA chunk cap** — it's a constructor arg, default **8192**. Ring-buffer/SD batching gets *easier*. The doc was wrong.
2. **Pi 5 SD = BCM2712 SDHCI (`CEMMCDevice`, `0x1000FFF000`), not RP1, not `CSDHCIDevice`.** Tested & supported.
3. **Do NOT add `pciex4_reset=0`** with Circle — Circle brings up the PCIe root complex itself. Stock `config.txt`.
4. **MCLK cannot come from the Pi 5's own GPIO while I²S is running.** `clk_i2s` — the only RP1 clock generator with `pll_audio` as a parent — is already claimed for BCLK. **→ external MCLK generator (Nano ESP32), see Spike B above.**
5. **The two "MIT" libs (simplecodec3, Yin-Pitch-Tracking) are unlicensed** (all-rights-reserved). YIN patent is **expired** → clean-room reimplement from the 2002 paper.
6. **(2026-07-09)** Even the *revised* internal-MCLK mechanism from correction #4 (`clk_i2s→clk_gp0→GPIO4`) turned out to be a resource conflict, not just missing Circle code — see [claim-verification.md](docs/claim-verification.md).

## Hardware (Phase 1)
- Raspberry Pi 5 · microSD (Class-10/UHS-I) · HDMI monitor ≤1080p · USB-serial for the UART debug console.
- PCM1808 ADC (**bus master**, strap-only) · PCM5102A / GY-PCM5102 DAC (strap-only).
- **Arduino Nano ESP32** — external MCLK generator (see [esp32-mclk/](esp32-mclk/)), electrically independent of the Pi 5 except for the MCLK line + shared ground.

**Verified pin map** ([details](planning/build-plan.md#milestone-2--hardware-bring-up-i²s-loopback)) — **the PCM1808 masters the I²S bus** (2026-07-10 pivot: two free-running crystals can't stay fs-locked, so the ADC divides the Nano's MCLK into BCK/LRCK and the Pi runs I²S slave — see [docs/claim-verification.md](docs/claim-verification.md)):

| Source | Signal | → |
|--:|---|---|
| **PCM1808 BCK** | BCLK 3.072 MHz (ADC-driven) | Pi GPIO18 + DAC BCK |
| **PCM1808 LRCK** | LRCLK 48 kHz (ADC-driven) | Pi GPIO19 + DAC LCK |
| PCM1808 DOUT | data IN (capture) | → Pi GPIO20 |
| Pi GPIO21 | data OUT (playback) | → PCM5102A DIN |
| **Nano ESP32 D2/GPIO5** | MCLK 12.288 MHz | → PCM1808 SCKI only |

⚠️ **PCM5102A breakout: set the XSMT pad HIGH (unmute)** — ships muted. ⚠️ **PCM1808: strap MD1=MD0=HIGH (master 256fs), FMT=GND, before power-on** — board-specific pin table in [docs/adc-hookup.md](docs/adc-hookup.md).

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
- **[docs/adc-hookup.md](docs/adc-hookup.md)** — **bench card: every wire for Pi 5 + PCM1808 + Nano ESP32 capture hookup** (power, ground, straps, clocks, data, order of operations).
- **[docs/tl072-frontend.md](docs/tl072-frontend.md)** — **drawer-parts front-end variant: TL072 on 9 V** (SPICE-verified; two changes from the canonical schematic) + CD4053BE rev-B notes.
- **[docs/build-setup.md](docs/build-setup.md)** — toolchain, build, SD flash, UART.
- Original Phase-1 research reference: project `CLAUDE.md` (the GuitarDAWLiteOS research doc).

## Build foundation (pinned)
Circle `develop` @ `22722a7` · `RASPPI=5` → `kernel_2712.img` · FatFs R0.16 (`addon/fatfs`) · YIN clean-room.
