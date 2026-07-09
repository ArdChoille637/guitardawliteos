# GuitarDAWLiteOS — Research-Claim Verification Report

**Date:** 2026-06-29
**Method:** 6 source/web finders + 13 independent adversarial skeptics (19 agents total).
Circle was **cloned on both `master` (release-51, 2026-05-05) and `develop` (HEAD `22722a7`, 2026-06-08)** and read directly; TI datasheets (PCM1808 SLES177B, PCM5102A SLAS859C), the RP1 Peripherals datasheet (RP-008370), the Raspberry Pi "Using the I²S peripherals" white paper (RP-009699-WP-1), RPi bare-metal forum threads, and the GitHub REST API were used for the rest.

**Bottom line:** The platform thesis holds — Circle has real, hardware-tested Pi 5 RP1 I²S (capture + playback), SD, and framebuffer support, and an exact 12.288 MHz MCLK for the PCM1808 is achievable. **But 6 claims are wrong or materially misleading, and 3 of those change the build sequence.** Read "Consequential corrections" before building.

---

## Status legend
- ✅ **Confirmed** — verified against primary source, survived refutation.
- 🟡 **Corrected/nuanced** — core idea right, but a detail is wrong or needs qualification. **Act on the corrected statement, not the doc.**
- ❌ **Refuted** — the claim as written is false.

---

## Summary table (29 claims)

| ID | Claim (abbrev.) | Verdict | One-line correction / confirmation |
|----|-----------------|---------|------------------------------------|
| B1 | Circle has working Pi 5 RP1 I²S on master+develop | ✅ | `CI2SSoundBaseDevice` in `i2ssoundbasedevice-rp1.cpp`, byte-identical on both branches, built when `RASPPI=5`, tested on PCM5102A/PCM5122/WM8960. |
| B2 | Uses Synopsys DesignWare I²S + `pll_audio` clock | ✅ | Ported from Linux `dwc-i2s.c`; clocked from `GPIOClockSourcePLLAudio`. ("rev 1.11a" is asserted by the RP1 datasheet, not by Circle source.) |
| B3 | DMA chunk size capped at ≤128 (pll_audio "fix") | ❌ | **FALSE.** Chunk size is a constructor arg, **default 8192**, no ≤128 cap anywhere. See correction #1. |
| B4 | DMA mode + IRQ fallback both exist | ✅ | DMA is default; `#define USE_I2S_SOUND_IRQ` switches to programmed-I/O (2-ch only). |
| C1 | RP1 has 3 DW I²S; I²S0=master, I²S1=slave, I²S2 unconnected | ✅ | Confirmed via RPi white paper (3 instances, only 2 pinned, "Instance 2 not accessible"). Circle exposes I²S0/I²S1 only. |
| C2 | GPIO18-21 pinout; which line is capture vs playback | ✅ | **GPIO20 = capture-in (ADC DOUT→here), GPIO21 = playback-out (→DAC DIN).** Doc's two statements are both right once chip-side vs Pi-side naming is normalized. |
| C3 | RP1 I²S = standard I²S only (no LJ/RJ/TDM) | ✅ | Confirmed in driver **and** silicon (rev 1.11a, fixed 50:50 frame sync). Strap both codecs for I²S. |
| C4 | Memory map (I²S0/I²S1/GPIO0 bases) | ✅ | All five addresses match `bcm2712.h` exactly on both branches. |
| I5 | Circle is GPLv3 | ✅ | GPLv3-or-later (LICENSE + per-file headers). |
| D4a | Circle "ships a PCM5102A driver" | ✅ | No dedicated class — PCM5102A is a strapped I²S DAC needing no driver (works when no I²C address given). |
| D4b | Circle ships a WM8960 driver | ✅ | `CWM8960SoundController` (I²C), used by both legacy and RP1 I²S paths. |
| F1 | Pi 5 SD via `CSDHCIDevice`/EMMC | 🟡 | Class is **`CEMMCDevice`**, and Pi 5 SD is the **BCM2712 SDHCI at `0x1000FFF000` — NOT RP1**. Supported + tested. See correction #2. |
| F2 | FatFs (ChaN) under `addon/fatfs`, 1-clause BSD | ✅ | FatFs R0.16; `LICENSE.txt` literally says "equivalent to the 1-clause BSD license." |
| G1 | Pi 5 framebuffer via VideoCore mailbox | 🟡 | Class is **`CBcmFrameBuffer`** (not `CFrameBuffer`); mechanism confirmed; Pi 5 marked **"limited"** (firmware caveats, not a missing path). |
| A1 | `pciex4_reset=0` needed for RP1 access | 🟡 | Param is real/correct **for raw bare-metal**, but **Circle does NOT need or use it** — Circle brings up the PCIe RC itself. See correction #3. |
| A2 | Firmware resets PCIe RC; RP1 access before = data abort | ✅ | Confirmed (observed `dscr=0x03047e93` abort on early RP1 read). |
| A3 | Address translation `0x1F00000000 + (rp1−0x40000000)` | ✅ | Exact; UART0 `0x40030000`→`0x1F00030000` matches datasheet + Circle. |
| E1 | Pi 5 can emit ~12.288 MHz MCLK from `pll_audio` | 🟡 | **Feasible — ADC not broken** — but the doc's *mechanism* is wrong. See correction #4 (the most important one). |
| E2 | MCLK↔BCLK phase not guaranteed but OK for ADC/DAC | ✅ | Confirmed; PCM1808 needs only fs-synchronization, not fixed phase. |
| D2 | PCM1808 slave needs external MCLK 256fs=12.288 MHz | ✅ | Confirmed; no internal PLL. 384fs/512fs also valid. |
| D1 | PCM1808 strap-only; MD0/MD1/FMT; slave=all low | ✅ | Confirmed exactly: MD1=MD0=Low (slave), FMT=Low (I²S 24-bit); set before power-on; internal 50 kΩ pulldowns. |
| D3 | PCM5102A FMT→GND=I²S; modules may default LJ | ✅ | Confirmed + **added gotcha: the XSMT pad ships bridged LOW = MUTE; must be moved HIGH.** See correction #5. |
| I1 | `rsta2/minisynth` (Circle synth, PCM5102A, MIDI, HDMI GUI) | ✅ | Confirmed (GPL-3.0). PCM5102A is one of several supported DACs. |
| I2 | `probonopd/MiniDexed` w/ experimental Pi 5 | ✅ | Confirmed; Pi 5 experimental (no HDMI sound / USB-gadget yet). |
| I3 | `kmwebnet/simplecodec3` PCM5102A+PCM1808 board, **MIT** | ❌ | Board exists & pairs both chips, but **NOT MIT — no license at all** (all-rights-reserved). See correction #6. |
| I4 | diyelectromusic Pi 5 quad-PCM5102A on Circle | ✅ | Confirmed (May 27 2024 write-up, builds on Circle quad-lane I²S). |
| H1 | `ashokfernandez/Yin-Pitch-Tracking`, C, **MIT** | ❌ | Code exists (pure-C, embedded-oriented) but **NOT MIT — no license** (all-rights-reserved). See correction #6. |
| H2 | A YIN patent exists and "must be reckoned with" | 🟡 | A real patent existed (**FR2825505B1**, France Télécom/de Cheveigné) but is **EXPIRED (2018 lapse / 2021 term end) → public domain.** No live barrier. See correction #6. |
| F3 | Throughput math (288 KB/s/track, 1.15 MB/s ×4) | ✅ | Arithmetic confirmed; ~8.7× headroom on Class-10/UHS-I. |

---

## Consequential corrections (these change the build)

### 1. ❌ The "DMA chunk size ≤ 128" gotcha is FALSE — you have lots of headroom
The doc says to design the ring buffer around a ≤128-sample DMA chunk (interrupt every 2.67 ms). **Not true.** In `i2ssoundbasedevice-rp1.cpp` the chunk size is a constructor parameter `m_nChunkSize` that **defaults to 8192** `u32`-samples and directly sizes the DMA buffers; there is **no `128`/`MAX_CHUNK`/clamp anywhere** in the file, and nothing in the source or CHANGELOG ties `pll_audio` to a "chunk-size audio-quality bug fix." (The only per-transfer FIFO-half-depth limit is in the *optional* IRQ path, not the default DMA path.)
**Effect on plan:** Drop the ≤128 constraint. You can run larger DMA chunks, which *simplifies* SD-write batching and the Core-0 mix loop. Pick chunk size from your latency target, not a phantom cap. `pll_audio` is confirmed as the clock source (claim B2 stands) — just not for the reason the doc gave.

### 2. 🟡 Pi 5 SD card is on the BCM2712 (not RP1), via `CEMMCDevice` (not `CSDHCIDevice`)
On Pi 5, Circle drives the microSD through the **BCM2712's own SDHCI controller at physical base `0x1000FFF000`** (Broadcom labels it `sdio1`; Linux DT comment: *"SDIO1 is used to drive the SD card"*). It is **not** behind RP1/PCIe. The driver class is **`CEMMCDevice`** (an SDHCI-spec host) — there is no class literally named `CSDHCIDevice`. Pi 5 SD is implemented and **tested** (README marks it `x`, lists Pi 5 as "Tested"), not WIP.
**Effect on plan:** (a) Fix the class name in code/docs. (b) Architectural bonus: SD storage and the HDMI framebuffer (also BCM2712/mailbox) do **not** depend on the RP1 PCIe link — only I²S/GPIO do. With Circle this is moot (Circle brings up PCIe anyway), but it's useful if you ever bisect a bring-up failure.

### 3. 🟡 With Circle, do **NOT** add `pciex4_reset=0`
`pciex4_reset=0` is a real, current `config.txt` parameter (default 1; Pi 5 only) and **is** the correct workaround **for raw bare-metal projects that don't write their own PCIe root-complex driver.** **Circle is the opposite case:** it ships a full PCIe RC driver (`lib/bcmpciehostbridge.cpp`, ported from Linux `pcie-brcmstb.c`) driven by the RP1 southbridge driver, so **RP1 peripherals are live on entry to `main()` with a stock `config.txt`.** Circle's own `boot/config*.txt` contain no PCIe line, and its Pi 5 docs say "no specific action is necessary."
**Effect on plan:** Remove `pciex4_reset=0` from the Step-1 instructions **for the Circle-based build.** Keep it documented only as the fallback for a hypothetical no-Circle bring-up. (Claims A2/A3 — the *why* — remain correct.)

### 4. 🟡 MCLK for the PCM1808 is feasible, but Circle won't hand it to you — it's a real task
This is the single most important correction. The exact **12.288 MHz** SCKI is achievable on Pi 5 bare-metal because it comes from the **retunable Audio PLL** (1.536 GHz VCO ÷ 125 = 12.288 MHz, all integer), **not** the 200 MHz `pll_sys` (the "200/16=12.5, no clean divide" problem only applies to `pll_sys`-sourced GPCLKs). **ADC capture is not blocked.** But the doc's one-liner ("derive from a GPCLK output connected to `pll_audio`") is wrong in two concrete ways, both confirmed in Circle source *and* Linux `clk-rp1.c`:
- **GPCLK0/1/2 (GPIO4/5/6) cannot directly select `pll_audio` as parent.** `pll_audio` is an *exclusive parent of `clk_i2s`*. The valid route is **`pll_audio` → `clk_i2s` → `clk_gp0` → GPIO4 (Alt0)** (`clk_i2s` *is* a legal `clk_gp0` parent).
- **Circle's I²S sound device does not emit an MCLK pin** — it only drives BCLK (`64·fs` = 3.072 MHz @ 48 k) and LRCLK on GPIO18/19. To get 12.288 MHz on a header pin you must instantiate a **separate `clk_i2s` clock at `StartRate(12288000)`** and route it out via `clk_gp0`→GPIO4.
**Effect on plan:** MCLK generation becomes an explicit **early de-risking spike (Spike B)**, not a one-line assumption. Both SCKI (12.288 MHz, 256fs) and BCLK (3.072 MHz, 64fs) trace to the same `pll_audio` lock, so they stay frequency-coherent — verify the divider chain on the bench. (PCM5102A does **not** need MCLK — it has an internal PLL off BCLK, SCK tied low — so GPIO4 feeds the PCM1808 only.)

#### ⛔ Update 2026-07-09 — the `clk_i2s → clk_gp0 → GPIO4` route above does not actually work
Went to implement Spike B and found the mechanism this correction describes is blocked, in two layers:
1. **Circle doesn't implement the route at all.** `gpioclock-rp1.h`'s `TGPIOClockSource` enum has no value representing "clk_i2s"; `s_ParentAux[]` for `GPIOClock0/1/2` only lists `GPIOClockSourceXOscillator` and `GPIOClockSourcePLLSys` — never `clk_i2s`. (Confirmed against Circle `develop@22722a76`, `lib/gpioclock-rp1.cpp`.) The route *is* real in the RP1 silicon — upstream Linux `drivers/clk/clk-rp1.c` (`rpi-6.6.y`) lists `clk_i2s` as aux-index 10 of `clk_gp0`'s parent array — so this half is a genuine Circle API gap, not a hardware wall. Patchable in principle.
2. **The blocking problem: `clk_i2s` is a single physical clock generator already claimed exclusively by Circle's I²S peripheral for BCLK.** `CI2SSoundBaseDevice`'s constructor owns `GPIOClockI2S` and calls `m_Clock.StartRate(CHANS * CHANLEN * m_nSampleRate)` (`i2ssoundbasedevice-rp1.cpp:383`) — `64fs` = 3.072 MHz @ 48 kHz — for the entire time the sound device runs. MCLK needs `256fs` = 12.288 MHz, **4× that rate, from the same hardware block, at the same time.** One clock generator cannot output two frequencies simultaneously — patching in the `clk_gp0` route from item 1 would still fail the moment audio is actually running. Checked every other RP1 clock generator Circle/Linux expose (`clk_adc`, `clk_pwm0`, `clk_pwm1`, `clk_audio_in`, `clk_audio_out`) — `pll_audio` is disabled/unavailable as a parent for all of them in the real driver too. **There is no on-chip path to a `pll_audio`-locked signal on any GPIO while the I²S peripheral is active.**

**Effect on plan:** the internal-GPCLK approach for Spike B is dropped. **Resolution: generate MCLK off-chip**, on an **Arduino Nano ESP32** (ESP32-S3), wired to the PCM1808's SCKI pin, fully decoupled from the Pi 5's `pll_audio`/`clk_i2s`. ESP-IDF's `i2s_std` driver has first-class MCLK-out support (`mclk_io_num` / `mclk_multiple = I2S_MCLK_MULTIPLE_256`), so the driver computes the divider coefficients for us; BCLK/WS/DATA pins are left `I2S_GPIO_UNUSED` since the Nano ESP32 only needs to emit the MCLK tone. This keeps `[CORR-4]`/E2 intact — the PCM1808 only needs fs-sync, not phase-lock to the Pi 5's BCLK, which is exactly what an independently-clocked external generator gives it.

**Correction (still 2026-07-09, caught while getting the firmware to compile):** an earlier draft of this note claimed ESP32-S3 has a dedicated Audio PLL (APLL) that made it a better choice than a Pico for jitter. **That's wrong.** Checked the installed SDK's `soc/clk_tree_defs.h` directly: APLL only exists on the *original* ESP32 — `soc_periph_i2s_clk_src_t` on the S3 only offers `PLL_160M`/`PLL_240M` (the general-purpose SoC PLL, shared with other peripherals, not audio-dedicated) or `XTAL`. The firmware's attempt to request `I2S_CLK_SRC_APLL` failed to compile (`'I2S_CLK_SRC_APLL' was not declared in this scope`), which is what caught this. No bit-exact 12.288000 MHz is possible from any of these chips' crystals regardless (Pi 5 `pll_sys`/xosc, RP2040, and ESP32-S3's crystal all hit the same "needs a factor the crystal doesn't have" wall) — the ESP32-S3's genuine advantage here is just the high-level `i2s_std` driver API doing the divider math for us, not superior clock hardware.

**Verified 2026-07-09, no oscilloscope available:** the firmware includes a self-test using the ESP32-S3's own PCNT (pulse counter) peripheral — loop the MCLK pin back into a counter input with a jumper (D2→D3) and it prints the measured frequency once a second. Measured **12.2880 MHz**, exact match to target, repeatable whenever the loopback jumper made solid contact. This confirms frequency correctness; it does not characterize jitter/duty-cycle the way a real scope would, which isn't expected to matter for a consumer-grade ADC per `[CORR-4]`/E2 above.

Firmware: [../esp32-mclk/](../esp32-mclk/). Sources: Circle `lib/gpioclock-rp1.cpp`/`include/circle/gpioclock-rp1.h` (`develop@22722a76`); Linux `drivers/clk/clk-rp1.c` (`rpi-6.6.y`, upstream Raspberry Pi fork); Espressif ESP-IDF I²S docs (`esp32s3/api-reference/peripherals/i2s.html`); installed `esp32:esp32` core 3.3.10 headers (`soc/clk_tree_defs.h`, `driver/i2s_std.h`, `driver/pulse_cnt.h`).

### 5. 🟡 PCM5102A breakout: the real gotcha is XSMT (mute), not just FMT
FMT→GND = I²S is confirmed. But on GY-PCM5102/HiLetgo modules the back-side solder pads ship bridged **all-low**, which is correct for FLT/DEMP/FMT but **wrong for XSMT (pad H3L): low = MUTE.** You must move **XSMT to HIGH (unmute)** or you'll get silence and chase a phantom wiring bug. Verify all four pads: FLT=L, DEMP=L, **XSMT=H**, FMT=L.

### 6. ❌/🟡 License reality: two "MIT" libraries are unlicensed; the YIN patent is expired
- **`kmwebnet/simplecodec3`** and **`ashokfernandez/Yin-Pitch-Tracking`** are **both unlicensed** (no LICENSE file, GitHub `spdx_id` null). They default to **all-rights-reserved**, which is *stricter* than MIT, not looser. → Use simplecodec3 as a **schematic study reference only** (don't redistribute the KiCad files); for the tuner, **reimplement YIN clean-room from the 2002 JASA paper** (or use a permissively-licensed lib) rather than shipping Ashok's code as MIT. Optionally, open an issue asking each author to add an OSI license.
- **YIN patent:** A real, numbered patent existed — **FR2825505B1 / WO2002097793A1**, inventor Alain de Cheveigné, assignee **France Télécom** (not "de Cheveigné/Kawahara," not CNRS/Ircam). It **lapsed in Feb 2018 and reached full term 2021-06-01; no US/EP grant ever issued.** → **No live patent barrier in 2026.** The only residual encumbrance is the **CNRS reference *code*'s research-only license**, which binds that specific source — avoided entirely by a clean-room reimplementation from the paper.

---

## Minor accuracy fixes (don't change the plan, fix in docs/code)
- Class names: `CSDHCIDevice` → **`CEMMCDevice`**; `CFrameBuffer` → **`CBcmFrameBuffer`** (`CScreenDevice` is correct).
- `ARM_I2S1_END` in `bcm2712.h` is defined as `ARM_I2S0_BASE+0xFFF` (harmless copy-paste; references I2S0).
- Pi 5 framebuffer "limited" caveats: 4K needs `hdmi_enable_4kp60=1`; headless init needs `SCREEN_HEADLESS`; WLAN is unreliable with HDMI active. None affect a wired HDMI monitor at ≤1080p.
- Circle Pi 5 I²S build dispatch: Makefile gates on `RASPPI==5`; the header dispatch is `#if RASPPI >= 5`. Build with `RASPPI=5`.
- "DesignWare rev 1.11a" is correct but sourced from the RP1 datasheet/white paper, not Circle's code.

---

## Primary sources (representative)
- Circle source, both branches: `lib/sound/i2ssoundbasedevice-rp1.cpp`, `include/circle/sound/i2ssoundbasedevice-rp1.h`, `lib/gpioclock-rp1.cpp`, `include/circle/bcm2712.h`, `include/circle/bcm2835.h`, `addon/SDCard/emmc.{cpp,h}`, `lib/bcmpciehostbridge.cpp`, `lib/southbridge.cpp`, `lib/bcmframebuffer.cpp`, `addon/fatfs/`, `README.md`, `CHANGELOG.md`.
- TI PCM1808 datasheet (SLES177B); TI PCM5102A datasheet (SLAS859C).
- RP1 Peripherals datasheet (RP-008370) §2.5.4 audio clocks, §3.7 I²S.
- Raspberry Pi white paper "Using the I²S peripherals on Raspberry Pi SBCs" (RP-009699-WP-1).
- RPi forum threads t=368402 (RP1 bare-metal access), t=359494/t=359512 (RP1 I²S/audio clocks), t=383613 (GPCLK divisors).
- Linux `drivers/clk/clk-rp1.c`, `drivers/pci/controller/pcie-brcmstb.c`, `bcm2712-rpi-5-b.dts`.
- Google Patents FR2825505B1 / WO2002097793A1; de Cheveigné & Kawahara, JASA 111(4):1917 (2002).
- GitHub REST API for repo/license metadata.
