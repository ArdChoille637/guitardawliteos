# GuitarDAWLiteOS — Build Plan & Task Tracker

**Phase 1 scope:** bare-metal Pi 5, I²S capture+playback (PCM1808 ADC + PCM5102A DAC), SD recording, HDMI framebuffer UI, GPIO footswitch. No USB in the audio path.
**Foundation:** [Circle](https://github.com/rsta2/circle) (GPLv3), `RASPPI=5`, `kernel_2712.img`.
**This plan supersedes the research doc's Build Sequence** where they conflict — corrections from [claim-verification.md](../docs/claim-verification.md) are folded in and flagged `[CORR-n]`.

Legend: `[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked/risk.
Effort: S ≤ half-day · M ~1–2 days · L ~3–5 days.

---

## Milestone 0 — Toolchain & first boot
**Status: ✅ COMPLETE — first physical boot 2026-07-10** (Pi 5 2GB, revision `b04170`): the M3 kernel booted first try over HDMI, all 4 cores up, thermal loop live. See [docs/build-setup.md](../docs/build-setup.md).

- [x] **0.1 Cross-compile environment** (S) — `aarch64-elf-gcc 16.1.0` + binutils 2.46.1 via Homebrew (`brew install aarch64-elf-gcc aarch64-elf-binutils`), prefix `aarch64-elf-`. (Circle's blessed `aarch64-none-elf` 15.2.Rel1 is the fallback if an addon link-fails.)
- [x] **0.2 Clone & configure Circle for Pi 5** (S) — `develop` pinned to `22722a76` at `circle/`; `./configure -r 5 -p aarch64-elf- -f` → `RASPPI=5/AARCH=64`. **Core lib + `sample/03-screentext` built & linked clean → `kernel_2712.img` (108 KB).** Reproducible via `scripts/build.sh`.
- [x] **0.3 SD boot card** (S) — GDAWBOOT card (FAT32/MBR) flashed with [`sdcard/`](../sdcard/) contents, byte-verified. **Booted 2026-07-10.** Note: `config.txt` carries `device_tree_address=0x2000000` (keeps the firmware's DTB placement clear of the 16 MB kernel region) — build.sh preserves it.
- [x] **0.4 UART debug console** (S) — boot log shows `Serial: OK` alongside the screen (log target tty1); the documented `cmdline.txt.uart` switch stays available when a headless session needs it.

**Gate: ✅ PASSED** — boot log over HDMI, timestamped, all subsystems reporting.

---

## Milestone 1 — De-risking spikes (do these BEFORE wiring the full audio board)

These two spikes retire the highest-uncertainty items surfaced by verification. Cheap to do; expensive to discover late.

- [x] **1.1 `[SPIKE A]` RP1 peripheral access smoke test** (S) — **passed 2026-07-10, by a stronger proof than planned:** the first hardware boot ran the full M3 I²S0 engine (RP1 DMA + RP1 pin muxing) with stock `config.txt` — capture counted exactly 48,000 frames/s, confirming RP1 peripherals are fully live on entry to `main()`. Confirms `[CORR-3]` on our board/firmware.
- [ ] **1.15 `[SPIKE C]` DSI touch display bring-up** (M) — build `sample/28-touchscreen` with `DSI_DISPLAY=0` on the official **Touch Display 2** via Circle's new `addon/rp1dsi` (video + Goodix touch + I²C backlight; **zero 40-pin GPIOs**; present at our pinned commit), then `addon/lvgl/sample` with **90° software rotation** (TD2 is portrait-native). Gates the retro-deck faceplate cutout ([retro-deck-design.md](../docs/retro-deck-design.md) §1). Fallback verified: HDMI + USB HID touch.
- [x] **1.2 `[SPIKE B]` 12.288 MHz MCLK generation** (M) `[CORR-4]` **← highest-value spike — internal route ruled out 2026-07-09, resolved via external generator, frequency confirmed 2026-07-09.** The originally-planned `clk_i2s → clk_gp0 → GPIO4` route doesn't work: `clk_i2s` is a single physical clock generator already claimed exclusively by Circle's I²S peripheral for BCLK (`64fs` = 3.072 MHz @ 48 kHz, the whole time audio runs) — MCLK needs `256fs` = 12.288 MHz, 4× that rate, from the same block, at the same time. No other RP1 clock generator has `pll_audio` as an available parent either. Full writeup: [docs/claim-verification.md](../docs/claim-verification.md).
  - **Resolution:** generate MCLK **off-chip** on an **Arduino Nano ESP32** (ESP32-S3), wired directly to PCM1808 SCKI, independent of the Pi 5's `pll_audio`. Firmware: [esp32-mclk/](../esp32-mclk/). Note: ESP32-S3 has **no** dedicated Audio PLL (APLL is original-ESP32-only, confirmed against the installed SDK's `soc/clk_tree_defs.h`) — an earlier version of this note wrongly assumed it did. It uses the general-purpose `PLL_160M` source instead, via the ESP-IDF `i2s_std` driver's `mclk_multiple` config.
  - **Verified:** no scope on hand, so measured with the chip's own PCNT (pulse counter) peripheral instead — a self-test mode in the same firmware loops the MCLK pin (D2/GPIO5) back into a counter pin (D3/GPIO6) via a jumper and prints the measured frequency once a second. Result: **12.2880 MHz**, exact match to target, repeatable across multiple readings (some readings undercounted from a loose self-test jumper, not the generator — readings were exact whenever the jumper made solid contact). This confirms frequency correctness; it does **not** characterize jitter/duty-cycle, which still needs a real scope if that level of verification ever becomes necessary — not expected to matter for a consumer-grade ADC per `[CORR-4]`/E2.

**Gate:** ✅ met — the Nano ESP32's output pin carries a correct 12.288 MHz MCLK, confirmed via the firmware's own PCNT self-test (see above). Wire it to the PCM1808 whenever Milestone 2 hardware is on the bench.

---

## Milestone 2 — Hardware bring-up: I²S loopback
**Full electrical review + wiring diagram done:** [docs/hardware-review.md](../docs/hardware-review.md), [docs/wiring-diagram.svg](../docs/wiring-diagram.svg). Key additions beyond the pin map below: PCM1808 needs a **5 V analog rail** (not 3.3 V), a **guitar front-end** (buffer → ×3 gain → bias-to-VBIAS mid-rail → AA) is mandatory, op-amp runs on 5 V, no level shifters, power off header 5 V + local LDO (not header 3V3). **Buildable schematic** ([docs/schematic.md](../docs/schematic.md), SPICE-verified + ERC-reviewed), **KiCad netlist + symbols** ([kicad/](../kicad/), bare-chip, ERC-checked), and a **breadboard build guide** ([docs/breadboard-build.md](../docs/breadboard-build.md)) are done.

**Pin map (updated 2026-07-10 — ADC is the bus master; see claim-verification.md and [docs/adc-hookup.md](../docs/adc-hookup.md)):**

| Source | Signal | Direction | Connects to |
|--------:|--------|-----------|------|
| **PCM1808 BCK** | BCLK 3.072 MHz | **ADC → Pi + DAC** | Pi GPIO18, PCM5102A BCK |
| **PCM1808 LRCK** | LRCLK/FS 48 kHz | **ADC → Pi + DAC** | Pi GPIO19, PCM5102A LCK |
| PCM1808 DOUT | data **IN** (capture) | ADC → Pi | Pi GPIO20 |
| Pi GPIO21 | data **OUT** (playback) | Pi → DAC | **PCM5102A DIN** |
| **Nano ESP32 D2/GPIO5** | MCLK/SCKI 12.288 MHz | ESP32 → ADC | **PCM1808 SCKI** only |

The **PCM1808 is I²S master** (divides SCKI to BCK/LRCK); the Pi runs slave (`GDAW_I2S_SLAVE`, Circle RP1 **I2S1** instance, same GPIO18-21 pads via AltFn4). Rationale: two free-running crystals can't stay fs-locked; the chip's §7.4.2 resync would mute audio continuously in the Pi-master arrangement. `[CORR-2]` SD and HDMI are on the BCM2712 and independent of this bus.

- [ ] **2.1 PCM1808 strapping** (S) — **before power-on**: **MD1=HIGH, MD0=HIGH (MASTER, 256fs → SCKI/256 = 48 kHz)**, FMT=GND (I²S, 24-bit). SCKI←**Nano ESP32 D2**, BCK→Pi GPIO18, LRCK→Pi GPIO19, DOUT→Pi GPIO20. Board-specific pin table (purple CJMCU-1808 breakout: no onboard regulator — both 5 V and 3.3 V rails required; analog in = RIN/–/LIN edge pads): [docs/adc-hookup.md](../docs/adc-hookup.md).
- [ ] **2.2 PCM5102A (GY-PCM5102 module) strapping** (S) `[CORR-5]` — solder pads: FLT=L, DEMP=L, **XSMT=H (UNMUTE — the #1 silent-output gotcha)**, **FMT=L (I²S)**. SCK→GND (internal PLL; no MCLK needed). DIN←GPIO21, BCK←GPIO18, LCK←GPIO19. Power per module (3.3 V).
- [ ] **2.3 Playback smoke test** (S) — run Circle `sample/34-sounddevices` (I²S/PCM5102A) on Pi 5; confirm a test tone out of the PCM5102A. Validates `[CORR-5]` strapping + BCLK/LRCLK.
- [ ] **2.4 Capture smoke test** (M) — run/adapt Circle `sample/42-soundinput`; confirm the PCM1808 produces non-zero samples (depends on Spike B MCLK). Format is fixed standard-I²S 24-in-32 `[CORR C3]` — no format selection to get wrong.
- [ ] **2.5 Hardware I²S loopback** (M) — full-duplex `DeviceModeTXRX`: capture ADC → write straight to DAC. Confirms simultaneous in+out on the single **I²S1 (slave)** instance (post-pivot; `GDAW_I2S_SLAVE`).

**Gate:** clean tone in (PCM1808) → out (PCM5102A) with no dropouts.

---

## Milestone 3 — Audio engine core
**Status 2026-07-10: engine verified live on hardware** (no codecs yet). First boot ran the full engine: `I2S full duplex (TXRX): RUNNING`, capture counting **exactly 48,000 frames/s**, `starve` frozen at the one-time 16-frame startup priming, `drop 0`, `laps 0/0` (no ring overruns), storage drain at exactly 384 KB/s in 256 KB blocks, 48+ s observed clean. Remaining for the gate: real audio through real codecs (M2 wiring) + the 10-min soak. Source: [src/](../src/), host tests: [tests/](../tests/).

- [~] **3.1 Live monitor path** (M) — **implemented** in [src/audioengine.cpp](../src/audioengine.cpp): full-duplex `DeviceModeTXRX` on one `CI2SSoundBaseDevice` (the `test/sound-controller/soundshell.cpp` pattern — the only TXRX usage in the Circle tree), overriding `GetChunk`/`PutChunk` directly (lowest-latency path; both run on core 0 in DMA-completion IRQ context under the driver's spinlock). Chunk = 32 words = 16 frames = 333 µs → worst-case in→out ≈ 3 chunks = **1.0 ms**, inside the 0.67–1.33 ms target. Key driver contract honored: `GetChunk` must always return the full word count (returning short permanently stops the stream); starve/drop counters are the only underrun visibility (the RP1 cyclic DMA has none in hardware).
- [~] **3.2 Lock-free SPMC ring buffer** (M) — **implemented + host-verified** in [src/ringbuffer.h](../src/ringbuffer.h): single producer broadcasting to independent per-reader cursors, wait-free writer (never inspects readers, overwrites oldest), seqlock-style **reserve/commit index pair** for torn-read detection. The first version validated against the commit index only — the host stress test caught real torn reads (writer scribbles payload before publishing); the reserve index fixed it. [tests/ringbuffer_test.cpp](../tests/ringbuffer_test.cpp) passes clean under plain `-O2` and ThreadSanitizer (1 writer + 3 readers, 20 M words, forced overruns, zero corruption). Freestanding-safe: no std headers, GCC `__atomic` builtins only (no `<atomic>` in Circle's `-nostdinc++` build; no libc `<string.h>` either — that one silently broke the build via the `-MG` dep-file trap).
- [~] **3.3 Multi-core assignment** (S) — **implemented** in [src/cores.cpp](../src/cores.cpp): Core 0 = I²S IRQ callbacks + thermal/heartbeat loop (M0 behavior retained); Core 1 = analysis (level meter now, YIN in M6); Core 2 = storage drain in 256 KB blocks (FatFs lands in M4 — FatFs is spinlock-guarded but must never be called from IRQ); Core 3 = 2 s stats reporter (UI in M5). Workers hold on an atomic start flag until core 0 releases them. **Build-critical:** `ARM_ALLOW_MULTI_CORE` changes `CSpinLock`'s class layout and atomic strength, so it must cover the whole Circle tree — [scripts/build.sh](../scripts/build.sh) appends it to `Config.mk` and forces a clean lib rebuild the first time.

**Adversarial review (2026-07-10), applied:** a multi-agent review of the M3 code found one **critical boot-blocker** — the 4 MiB BSS ring pushed the kernel past Circle's default `KERNEL_MAX_SIZE` (2 MB), which would have put all four cores' stacks *inside* the ring buffer: a silent, pre-output boot crash on first hardware boot, invisible in the 146 KB `.img` (BSS is NOBITS). Fixed tree-wide with `-DKERNEL_MAX_SIZE=0x1000000` in `Config.mk` + a whole-tree clean rebuild, and [scripts/build.sh](../scripts/build.sh) now has a **post-link guard** (fails if `_end` ≥ `MEM_KERNEL_END`). Also applied: `src/*.o` now depend on `Config.mk` (stale-object trap), DEFINE/toolchain changes force a whole-tree clean (poisoned-archive trap that would otherwise bite at M4 when FatFs libs get linked), SD staging verifies the boot-critical DTBs/cmdline exist, and the ring tests gained a deterministic overrun/resync test, an exact value==absolute-index integrity invariant, anti-vacuous-pass assertions, and bounded-drain hang detection. `SignExtend24` was also corrected to the low-24-bit wire format (Circle `ConvertReadSoundFormat` + `GetRangeMin/Max` = ±(2²³−1) are the evidence).

**Gate (unchanged, needs hardware):** stable real-time monitor mix, measured latency, zero underruns over 10 min.

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
| Internal GPCLK MCLK route infeasible (`clk_i2s` shared with BCLK) `[CORR-4]` | High | **Mitigated 2026-07-09** | External MCLK: Arduino Nano ESP32 (ESP32-S3, **PLL_160M via `i2s_std`** — the S3 has *no* APLL) → PCM1808 SCKI, decoupled from Pi 5 `pll_audio` |
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
