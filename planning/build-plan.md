# GuitarDAWLiteOS — Build Plan & Task Tracker

**Phase 1 scope:** bare-metal Pi 5, I²S capture+playback (PCM1808 ADC + PCM5102A DAC), SD recording, HDMI framebuffer UI, GPIO footswitch. No USB in the audio path.
**Foundation:** [Circle](https://github.com/rsta2/circle) (GPLv3), `RASPPI=5`, `kernel_2712.img`.
**This plan supersedes the research doc's Build Sequence** where they conflict — corrections from [claim-verification.md](../docs/claim-verification.md) are folded in and flagged `[CORR-n]`.

Legend: `[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked/risk.
Effort: S ≤ half-day · M ~1–2 days · L ~3–5 days.

---

## Milestone 0 — Toolchain & first boot
**Status: build chain DONE on this Mac; physical boot pending hardware.** See [docs/build-setup.md](../docs/build-setup.md).

- [x] **0.1 Cross-compile environment** (S) — `aarch64-elf-gcc 16.1.0` + binutils 2.46.1 via Homebrew (`brew install aarch64-elf-gcc aarch64-elf-binutils`), prefix `aarch64-elf-`. (Circle's blessed `aarch64-none-elf` 15.2.Rel1 is the fallback if an addon link-fails.)
- [x] **0.2 Clone & configure Circle for Pi 5** (S) — `develop` pinned to `22722a76` at `circle/`; `./configure -r 5 -p aarch64-elf- -f` → `RASPPI=5/AARCH=64`. **Core lib + `sample/03-screentext` built & linked clean → `kernel_2712.img` (108 KB).** Reproducible via `scripts/build.sh`.
- [~] **0.3 SD boot card** (S) — **boot partition fully staged in [`sdcard/`](../sdcard/)** (config.txt, kernel_2712.img, both Pi 5 DTBs, `overlays/bcm2712d0.dtbo` — all DTBs validated). `[CORR-3]` no `pciex4_reset=0`. **Remaining (hands-on):** format microSD FAT32 + copy `sdcard/` contents. → instructions in build-setup.md.
- [~] **0.4 UART debug console** (S) — switch documented: rename `cmdline.txt.uart`→`cmdline.txt` (`logdev=ttyS11`), wire 3.3 V USB-serial to the Pi 5 dedicated UART connector (TX/RX crossed), `screen … 115200`. **Remaining (hands-on):** wire + confirm. (HDMI default needs no UART for a first-boot win.)

**Gate:** Circle sample boots (HDMI text, or UART log). ← *build side proven; awaiting a flashed card + Pi 5.*

---

## Milestone 1 — De-risking spikes (do these BEFORE wiring the full audio board)

These two spikes retire the highest-uncertainty items surfaced by verification. Cheap to do; expensive to discover late.

- [ ] **1.1 `[SPIKE A]` RP1 peripheral access smoke test** (S) — boot Circle's `sample/29-gpio` (or toggle a GPIO/blink) on Pi 5 to confirm RP1 GPIO is live on entry to `main()` with stock `config.txt`. Confirms `[CORR-3]` (Circle's PCIe RC bring-up works) on *our* board/firmware before we depend on it. ✅ Expected to pass — verification shows Circle handles this.
- [ ] **1.15 `[SPIKE C]` DSI touch display bring-up** (M) — build `sample/28-touchscreen` with `DSI_DISPLAY=0` on the official **Touch Display 2** via Circle's new `addon/rp1dsi` (video + Goodix touch + I²C backlight; **zero 40-pin GPIOs**; present at our pinned commit), then `addon/lvgl/sample` with **90° software rotation** (TD2 is portrait-native). Gates the retro-deck faceplate cutout ([retro-deck-design.md](../docs/retro-deck-design.md) §1). Fallback verified: HDMI + USB HID touch.
- [x] **1.2 `[SPIKE B]` 12.288 MHz MCLK generation** (M) `[CORR-4]` **← highest-value spike — internal route ruled out 2026-07-09, resolved via external generator, frequency confirmed 2026-07-09.** The originally-planned `clk_i2s → clk_gp0 → GPIO4` route doesn't work: `clk_i2s` is a single physical clock generator already claimed exclusively by Circle's I²S peripheral for BCLK (`64fs` = 3.072 MHz @ 48 kHz, the whole time audio runs) — MCLK needs `256fs` = 12.288 MHz, 4× that rate, from the same block, at the same time. No other RP1 clock generator has `pll_audio` as an available parent either. Full writeup: [docs/claim-verification.md](../docs/claim-verification.md).
  - **Resolution:** generate MCLK **off-chip** on an **Arduino Nano ESP32** (ESP32-S3), wired directly to PCM1808 SCKI, independent of the Pi 5's `pll_audio`. Firmware: [esp32-mclk/](../esp32-mclk/). Note: ESP32-S3 has **no** dedicated Audio PLL (APLL is original-ESP32-only, confirmed against the installed SDK's `soc/clk_tree_defs.h`) — an earlier version of this note wrongly assumed it did. It uses the general-purpose `PLL_160M` source instead, via the ESP-IDF `i2s_std` driver's `mclk_multiple` config.
  - **Verified:** no scope on hand, so measured with the chip's own PCNT (pulse counter) peripheral instead — a self-test mode in the same firmware loops the MCLK pin (D2/GPIO5) back into a counter pin (D3/GPIO6) via a jumper and prints the measured frequency once a second. Result: **12.2880 MHz**, exact match to target, repeatable across multiple readings (some readings undercounted from a loose self-test jumper, not the generator — readings were exact whenever the jumper made solid contact). This confirms frequency correctness; it does **not** characterize jitter/duty-cycle, which still needs a real scope if that level of verification ever becomes necessary — not expected to matter for a consumer-grade ADC per `[CORR-4]`/E2.

**Gate:** ✅ met — the Nano ESP32's output pin carries a correct 12.288 MHz MCLK, confirmed via the firmware's own PCNT self-test (see above). Wire it to the PCM1808 whenever Milestone 2 hardware is on the bench.

---

## Milestone 2 — Hardware bring-up: I²S loopback
**Full electrical review + wiring diagram done:** [docs/hardware-review.md](../docs/hardware-review.md), [docs/wiring-diagram.svg](../docs/wiring-diagram.svg). Key additions beyond the pin map below: PCM1808 needs a **5 V analog rail** (not 3.3 V), a **guitar front-end** (buffer → ×3 gain → bias-to-VBIAS mid-rail → AA) is mandatory, op-amp runs on 5 V, no level shifters, power off header 5 V + local LDO (not header 3V3). **Buildable schematic** ([docs/schematic.md](../docs/schematic.md), SPICE-verified + ERC-reviewed), **KiCad netlist + symbols** ([kicad/](../kicad/), bare-chip, ERC-checked), and a **breadboard build guide** ([docs/breadboard-build.md](../docs/breadboard-build.md)) are done.

**Corrected pin map (`[CORR]` C2 — verified against Circle `i2ssoundbasedevice-rp1.cpp`):**

| Source | Circle name | Signal | Direction | Connects to |
|--------:|-------------|--------|-----------|-------------|
| Pi GPIO18 | `PCMCLK` | BCLK | Pi → both | PCM1808 BCK, PCM5102A BCK |
| Pi GPIO19 | `PCMFS`  | LRCLK/FS | Pi → both | PCM1808 LRCK, PCM5102A LCK |
| Pi GPIO20 | `PCMDIN` | data **IN** (capture) | ADC → Pi | **PCM1808 DOUT** |
| Pi GPIO21 | `PCMDOUT`| data **OUT** (playback) | Pi → DAC | **PCM5102A DIN** |
| **Nano ESP32 D2/GPIO5** | — (ESP-IDF `i2s_std` MCLK-out) | MCLK 12.288 MHz | ESP32 → ADC | **PCM1808 SCKI** only — *not a Pi GPIO*, see Spike B resolution above |

Pi 5 I²S0 is **master** (clock producer) for BCLK/LRCLK/data. `[CORR-2]` SD and HDMI are on the BCM2712 and independent of this bus.

- [ ] **2.1 PCM1808 strapping** (S) `[CORR D1]` — **before power-on**: MD1=GND, MD0=GND (slave, 256/384/512fs autodetect), FMT=GND (I²S, 24-bit). (Internal 50 kΩ pulldowns mean floating=low, but tie explicitly.) SCKI←**Nano ESP32 D2**, BCK←Pi GPIO18, LRCK←Pi GPIO19, DOUT→Pi GPIO20.
- [ ] **2.2 PCM5102A (GY-PCM5102 module) strapping** (S) `[CORR-5]` — solder pads: FLT=L, DEMP=L, **XSMT=H (UNMUTE — the #1 silent-output gotcha)**, **FMT=L (I²S)**. SCK→GND (internal PLL; no MCLK needed). DIN←GPIO21, BCK←GPIO18, LCK←GPIO19. Power per module (3.3 V).
- [ ] **2.3 Playback smoke test** (S) — run Circle `sample/34-sounddevices` (I²S/PCM5102A) on Pi 5; confirm a test tone out of the PCM5102A. Validates `[CORR-5]` strapping + BCLK/LRCLK.
- [ ] **2.4 Capture smoke test** (M) — run/adapt Circle `sample/42-soundinput`; confirm the PCM1808 produces non-zero samples (depends on Spike B MCLK). Format is fixed standard-I²S 24-in-32 `[CORR C3]` — no format selection to get wrong.
- [ ] **2.5 Hardware I²S loopback** (M) — full-duplex `DeviceModeTXRX`: capture ADC → write straight to DAC. Confirms simultaneous in+out on the single I²S0 instance.

**Gate:** clean tone in (PCM1808) → out (PCM5102A) with no dropouts.

---

## Milestone 3 — Audio engine core

- [ ] **3.1 Live monitor path** (M) — ADC DMA buffer → ring buffer → DAC DMA buffer on Core 0. Target latency 32–64 samples (0.67–1.33 ms). `[CORR-1]` **Chunk size is free to choose (default 8192, no ≤128 cap)** — pick from the latency budget, not a phantom limit.
- [ ] **3.2 Lock-free SPMC ring buffer** (M) — single producer (Core 0), independent read pointers for pitch (Core 1) and storage (Core 2). Readers may lag by one buffer; that's correct by design.
- [ ] **3.3 Multi-core assignment** (S) — `CMultiCoreSupport`: Core 0 = I²S DMA + mix (never preempted); Core 1 = pitch/BPM; Core 2 = SD writer; Core 3 = UI.

**Gate:** stable real-time monitor mix, measured latency, zero underruns over 10 min.

---

## Milestone 4 — SD recording

- [ ] **4.1 FatFs integration** (S) `[CORR F1]` — use Circle `addon/fatfs` (FatFs R0.16, 1-clause BSD) on **`CEMMCDevice`** (BCM2712 SDHCI @ `0x1000FFF000` — *not* `CSDHCIDevice`, *not* RP1).
- [ ] **4.2 Background block-write queue** (M) — Core 2 drains the ring buffer in large DMA-aligned blocks (64–256 KB) → `f_write`. Never write per-interrupt. Budget: 288 KB/s/track, 1.15 MB/s ×4, ~8.7× headroom on Class-10 `[CORR F3]`.
- [ ] **4.3 Session file format** (S) — `/sessions/session_NNN/{session.json, track_NN.pcm (24-bit signed interleaved 48k), notebook.txt}`; reserve `tabs/` for Phase 2.
- [ ] **4.4 Safe shutdown** (S) — GPIO power-button IRQ / watchdog flushes open handles. Raw PCM loss on crash = acceptable; JSON corruption = not.

**Gate:** record 4 tracks simultaneously for 10 min, files intact, re-importable.

---

## Milestone 5 — Display & UI

- [ ] **5.1 Framebuffer init** (S) `[CORR G1]` — `CBcmFrameBuffer` / `CScreenDevice` via VideoCore mailbox (not `CFrameBuffer`). 1024×600 or 1280×800, 32-bit. Wired HDMI ≤1080p avoids the "limited" caveats.
- [ ] **5.2 Waveform renderer** (L) — per pixel column, draw min→max peak of the sample window. Core 3, ~30 fps.
- [ ] **5.3 Session timeline UI** (M) — tracks, transport, record/play state.

**Gate:** live waveform + transport render at 30 fps with no audio impact.

---

## Milestone 6 — Guitarist tools

- [ ] **6.1 Tuner (YIN)** (M) `[CORR-6]` — **clean-room reimplement YIN from the 2002 JASA paper** (do *not* ship `ashokfernandez/Yin-Pitch-Tracking` as MIT — it's unlicensed). **No patent barrier** (FR2825505B1 expired). 2048-sample window @ 48k on Core 1 (~0.5–2 ms). Difference fn → CMND → threshold (0.1–0.15) → parabolic interp.
- [ ] **6.2 Tuning-aware display** (S) — open-string freq tables (DADGAD, D-standard, etc.); nearest-note + cents. (Tuning Hz arrays in the research doc are correct.)
- [ ] **6.3 Metronome** (S) — sample-accurate click mixed into the DAC on Core 0.
- [ ] **6.4 BPM tap + scale/chord/interval reference** (S) — static data, no audio dependency.
- [ ] **6.5 Session notebook** (S) — text buffer → `notebook.txt`.
- [ ] **6.6 Footswitch / GPIO input handler** (M) — `CGPIOPin`; map footswitches to mode actions (record, tap, tuner, etc.).

**Gate:** tuner locks E2–E6 accurately; metronome sample-accurate; footswitches drive modes.

---

## Risk register (from verification)

| Risk | Severity | Status | Mitigation |
|------|----------|--------|------------|
| Internal GPCLK MCLK route infeasible (`clk_i2s` shared with BCLK) `[CORR-4]` | High | **Mitigated 2026-07-09** | External MCLK: Arduino Nano ESP32 (ESP32-S3 APLL) → PCM1808 SCKI, decoupled from Pi 5 `pll_audio` |
| PCM5102A XSMT shipped muted `[CORR-5]` | Med | Open | Verify XSMT=H pad at 2.2 |
| Accidentally adding `pciex4_reset=0` w/ Circle `[CORR-3]` | Low | Mitigated | Stock `config.txt`; documented |
| Shipping unlicensed YIN/board files `[CORR-6]` | Med (legal) | Mitigated | Clean-room YIN; simplecodec3 = study-only |
| Pi 5 framebuffer "limited" caveats `[CORR G1]` | Low | Mitigated | Wired HDMI ≤1080p |
| GPLv3 obligation (Circle) | Low | Accepted | Personal/open-source project |

## Dependencies pinned
- Circle `develop` @ `22722a7` (2026-06-08), `RASPPI=5` → GPLv3
- FatFs R0.16 (`addon/fatfs`) → 1-clause BSD
- YIN → clean-room from JASA 2002 (patent expired; no repo dependency)
- Hardware: PCM1808 (slave, strap-only) + PCM5102A/GY-PCM5102 (strap-only) — no driver code
- Hardware: Arduino Nano ESP32 (ESP32-S3) — external MCLK generator only, no data/BCLK/WS connection to Pi 5
