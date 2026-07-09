# GuitarDAWLiteOS — Retro Deck Design (Phase 2 architecture)

**Date:** 2026-06-30. The three Phase-2 directions — **touch display**, **cassette analog tracking**, **retro-gear form factor** — researched against primary sources (Circle source at our pinned commit, TI/Tascam datasheets, the official HAT+ spec, maker-community practice) with adversarial verification on the two architecture-deciding claims. Both **confirmed**.

**The product in one sentence:** a cassette-deck-styled open-hardware guitar workstation — Pi 5 + audio HAT+ inside a folded-metal/wood-cheek chassis, a PCB faceplate with real VU meters and illuminated transport keys, a 7″ touch display as the "cassette window," and an *optional real cassette tape loop* as a lo-fi analog insert.

---

## 1. Touch display — SOLVED, zero GPIO cost ✅ (verified in Circle source)

**Default: official Raspberry Pi Touch Display 2 (7″; 5″ for a compact build) on the DSI port, driven by Circle's `addon/rp1dsi`.**

The decisive finding: Circle gained `addon/rp1dsi` on **2026-05-26** — a bare-metal port of Raspberry Pi's own Linux DSI driver for Pi 5. It is present at our pinned commit (`22722a76`) and provides:
- **Video**: `CRPiTouchScreen` (a full `CDisplay`) → Synopsys DSI host + **RP1 DPI-DMA autonomous scanout** (CPU cost = drawing + cache-clean only; no per-frame CPU streaming).
- **Touch**: ported Goodix GT911 driver (Touch Display 2, 5-contact) and FT5x06 (v1 panel), exposed via the same generic `touch1` device as USB touch.
- **Backlight** over I²C. Auto-detects v1 vs TD2 from the display's regulator ID.
- `sample/28-touchscreen` and `addon/lvgl/sample` already build for it (`DSI_DISPLAY = 0`).

Why this beats our previous HDMI plan, not just matches it:
- **Zero 40-pin GPIOs.** DSI video + touch I²C ride the dedicated FPC connector (RP1 I²C bus 6/4 on the DISP FFC — *not* header pins). The display takes only 5 V/GND from header pins 2/6. The whole header stays free for audio + controls.
- **Bypasses the Pi 5 firmware HDMI path entirely** — the thing Circle itself marks "limited" (missing modes, some displays dead, the WLAN-vs-HDMI interaction). `rp1dsi` talks to RP1 directly.
- **Audio-safe**: DSI scanout and I²S use separate RP1 DMA engines; UI runs on its own core (Core 3 in our plan).

**Caveats (both manageable):**
1. **TD2 is portrait-native 720×1280**; hardware rotation exists only for the discontinued v1 panel. → Build the UI in **LVGL with 90° software rotation from day one**; mount the panel sideways in the faceplate. Trivial load on a Cortex-A76.
2. The addon is **~5 weeks old, develop-only, unreleased** → treat as beta; our pin already includes it; add a bring-up spike (below).

**Fallbacks (both verified working on Pi 5):**
- **HDMI + USB HID touch** (Circle's digitizer-class driver, multi-touch, validated on Waveshare 5″/7″ panels) — zero HAT changes; the dev-bench option.
- **SPI TFT** (ST7789/ILI9341 + XPT2046 resistive) — only as an optional *mini* panel (e.g. a 2.8″ "tape counter window"); must use **SPI0 (GPIO 7–11)** — SPI1/SPI3 collide with our I²S pins.

**New spike — Spike C (display):** build `sample/28-touchscreen` with `DSI_DISPLAY=0` on the Touch Display 2, then `addon/lvgl/sample` with 90° rotation. Run alongside Spike B (MCLK). Gate for committing the faceplate cutout dimensions.

---

## 2. Cassette analog tracking — ship Level A, provision Level B, defer Level C ✅ (skeptic-confirmed w/ corrections)

**Architecture: the tape path is an optional analog *insert*, not the recording medium.** The dry 24-bit loop always stays in DSP; `TAPE OUT` sends the mix to tape, the deck's output returns into the PCM1808's **currently-unused VINR channel**, and the UI blends wet/dry. Cassette S/N (~59 dB) and wow/flutter (0.25 %) are the *product* — hiss, HF saturation, chorus-like warble — exactly the effect the market already validates (Onde Magnetique OM-1, Chase Bliss Generation Loss).

### Level A — external deck interface (**ships in rev B**, ~$5–8 BOM)
| # | Provision | Detail (skeptic-corrected values) |
|---|-----------|------------------------------------|
| 1 | **TAPE OUT** RCA pair | PCM5102A 2.1 Vrms → **~17.5 dB pad** to consumer −10 dBV. A 5.6k/1k L-pad alone gives only 16.4 dB → **include the trim pot** (not optional) to hit deck nominal (Tascam 202MKVII: −9 dBu = 0.28 Vrms in, 33 kΩ). |
| 2 | **TAPE IN** jack | → mono-sum → AC-couple → **VINR**, selected by a **jumper** (default = AC-ground, so rev-A behavior is untouched). Deck return ~0.46 Vrms from 1 kΩ drives the 60 kΩ / 3 Vpp input directly with ~7.3 dB headroom. |
| 3 | **REM jack** (2.5 mm TS) | optoMOS relay on **1 GPIO** = contact closure in series with the deck motor supply → **hardware punch-in from the footswitch**, exactly like a Portastudio. Works on portable/Portastudio-class decks (component hi-fi decks don't have REM — hence #4). |
| 4 | **IR-LED footprint** (unpopulated) | second GPIO; for hi-fi decks that only take IR remotes (e.g. 202MKVII's RC-1331). |
| 5 | **TRANSPORT header** (8-pin, unpopulated) | `SOLENOID_DRV, MOTOR_EN, TACH_IN, SW1(tape-present), SW2(rec-inhibit), SW3(stop-pos), V_TRANSPORT(6V/12V jumper), GND` — pin budget proven by the AVRTapeControl project against exactly our candidate mechanisms. |

### Level B — embedded mechanism (**provisioned, populate later**)
New-manufacture Tanashin TN-21ZLG-clone transports are purchasable (~$10–20, AliExpress; the same clone family ships in current TEAC/Tascam dual decks), plus TA7668-based record/play head boards (~$10). Boombox-grade fidelity — which *suits* the lo-fi bus. Level B = mount a mech in the chassis, drive it from the TRANSPORT header, feed it from TAPE OUT/IN. No HAT respin needed.

### Level C — our own record path (**deferred; future daughter-board**)
Playback preamp is well-trodden (Rod Elliott P247-style NAB 3180 µs/120 µs EQ, ~40–46 dB gain, modern op-amps); the **bias/erase oscillator (60–100 kHz) + record amp + calibration** is the genuinely hard analog-tape part. Not on the HAT. Keep the VINR path clean so a future head-preamp board can drop in.

**Firmware:** tape insert = wet/dry blend UI + REM punch-in mapping; Level B adds transport state-machine (solenoid pulse timing, tach-based end-of-tape stop — logic portable from AVRTapeControl).

---

## 3. Retro form factor — three-layer mechanical architecture ✅

The HAT+ spec (2023-12, current 2024-12) **deprecates the rigid 65×56 rule**: a HAT+ needs only the 40-pin connection, ≥1 aligned mounting hole, and the ID EEPROM for branding. Oversized boards are explicitly legal — the **Blokas Pisound (100×56 mm)** is the decade-proven precedent for exactly our use case (¼″ jacks exiting the rear wall).

```
┌──────────────────────────────────────────────────────┐
│  ENCLOSURE  — folded-metal U-chassis + wood cheeks   │  ← open DXF, classic hi-fi build
│  ┌────────────────────────────────────────────────┐  │
│  │ FACEPLATE PCB — the entire front cosmetics     │  │  ← 2.0 mm FR4, silkscreen art,
│  │ VU meters · transport keys · TD2 window ·      │  │    parts' nuts clamp it (no visible
│  │ knobs · (optional cassette door)               │  │    fasteners); rear component PCB
│  └────────────────────────────────────────────────┘  │
│   Pi 5 + Active Cooler + [AUDIO HAT+] + cassette mech │  ← 16 mm standoffs/stacking header
└──────────────────────────────────────────────────────┘
```

1. **Audio core = "GuitarDAW Audio HAT+"** — the rev-B board. Either strict 65×56.5 or a **Pisound-style stretch (~100×56)** so guitar/line/tape jacks exit the rear wall directly. Requirements adopted:
   - **CAT24C32 ID EEPROM** @ I²C 0x50 (Standard class) on ID_SD/ID_SC, 3.9 kΩ pull-ups, nothing else on those pins. Costs <$1; buys the HAT+ wordmark **and** a published Linux device-tree overlay (`simple-audio-card` for PCM1808/PCM5102A) — the same open board then works plug-and-play on Linux too. Bare-metal Circle ignores it at runtime.
   - **16 mm standoffs + 16 mm stacking header** → the official **Active Cooler fits underneath** (this is also the thermal answer, see below).
   - **Left-edge cutout** aligned to the Pi 5 PCIe FFC (keeps NVMe/Hailo expansion open); PoE keep-out per spec; all four holes on the 58×49 grid.
   - Exports to the panel: **buffered L/R line taps** (for meters) + an **SPI + GPIO control bus** connector.
2. **Faceplate PCB** — 2.0 mm FR4 (or copper-poured 1.6 mm against flex), the standard eurorack/pedal-DIY faceplate technique: solder-mask + silkscreen retro graphics, exposed-gold "brushed metal" accents, cutouts for the meters and the TD2 window; pots/jacks/switch nuts clamp faceplate to a rear component PCB carrying switches/LEDs — fastener-free face.
3. **Enclosure** — folded sheet-metal chassis + **wood side cheeks** (both as open DXF for any sheet-metal/CNC service). Hammond 1456CWW (real walnut sides) is a fine *v1 prototype* shell but the walnut line is being discontinued — don't make it the open-hardware dependency.

**Panel hardware (all current-production, open-BOM-safe):**
- **VU meters: real 200 µA moving-coil movements** (sub-$5, current production; the strongest retro cue). Drive: TL072 precision full-wave rectifier off the HAT's buffered line taps, **on the panel PCB**, with damping caps (cheap movements are underdamped — budget up to ~440 µF). 
- **LED bargraph (alternative/addition): NOT LM3915** — discontinued, wrong for an open BOM. Use **APA102/SK9822 SPI-clocked addressable LEDs** driven by firmware (we own the sample stream → true dBFS ballistics), behind silkscreen segment windows = convincing '80s bargraph.
- **Transport keys:** real interlocking piano-key banks are NOS-only → design around **NKK UB-series illuminated square pushbuttons** (current production, momentary; "latching" + lamp state in firmware, which a loop recorder wants anyway) + standard 3PDT footswitches.
- **Thermal:** Pi 5 soft-throttles at 80 °C — a closed box with passive cooling *will* throttle mid-take. **Active Cooler under the HAT** + period-correct **vent grilles** (cassette decks always had them — free styling) + firmware fan curve; the guitar signal path is analog+DMA, so fan PWM noise stays out of the audio.

---

## 4. Rev-B GPIO budget (draft — finalize at rev-B schematic)
| GPIO | Assignment | Fixed? |
|------|-----------|--------|
| 0/1 | ID EEPROM (ID_SD/ID_SC) — nothing else | HAT+ rule |
| 2/3 | I²C1 → broken out (hackability + panel expander option) | free bus |
| 4 | MCLK (GPCLK0 → PCM1808 SCKI) | fixed |
| 18–21 | I²S0 (BCLK/LRCLK/DIN/DOUT) | fixed |
| 7–11 | SPI0 → panel bus: APA102 bargraph, optional SPI TFT | reserved |
| 5, 6, 16, 17 | TRANSPORT: REM optoMOS, IR_TX, SOLENOID_DRV, MOTOR_EN | rev B |
| 22–27 | Footswitches + transport sensors (TACH, SW1–3) | aux block |
| 12/13 | spare (PWM-capable) — panel lamp dimming etc. | spare |

**Standing rule:** never reassign 4/18–21; keep 7–11 for SPI0 (SPI1/SPI3 are dead to us — they collide with I²S pins).

## 5. What changes where
- **Rev A (current KiCad project)**: unchanged — it's the learning vehicle. Everything above is **rev B**.
- **`kicad/gen.py`**: rev-B netlist additions (tape pads/jumper, REM optoMOS, TRANSPORT header, EEPROM, line-tap buffers, panel connector) get added behind a clearly-marked REV-B section when we cut that board.
- **Build plan**: Spike C (DSI bring-up) joins Spike A/B in Milestone 1; faceplate + enclosure become new milestones in the learning path (M7/M8).
- **CM5 carrier (phase 3)**: route the same 22-pin DSI FPC + 5 V display power; **rp1dsi is unconfirmed on CM5** (Circle lists CM5 as "reported to work") → its own validation spike.

## 6. Risk register (Phase 2)
| Risk | Severity | Mitigation |
|------|----------|------------|
| `rp1dsi` is ~5 weeks old, unreleased | Med | Spike C before faceplate commit; HDMI+USB-touch fallback verified |
| TD2 portrait-only scanout | Low | LVGL 90° rotation from day one |
| REM punch-in only fits portable/Portastudio decks | Low | IR footprint + TRANSPORT header cover the rest |
| Piano-key/walnut parts are NOS/discontinued | Med | NKK UB keys + open-DXF wood cheeks in the open BOM; NOS only for personal builds |
| Pi 5 throttling in closed box | High | Active Cooler under 16 mm-standoff HAT + vent grilles + fan curve |
| LM3915 in old references | — | banned from BOM; APA102/SK9822 instead |

**Sources:** Circle `addon/rp1dsi/*`, `lib/usb/usbtouchscreen.cpp`, `lib/input/xpt2046touchscreen.cpp`, README/CHANGELOG @ `22722a76`; GitHub commits API (addon age); raspberrypi.com Touch Display 2 docs + HAT+ spec (2024-12 build) + Pi 5 mechanical drawing; TI SLAS859C / SLES177B; tascam.com 202MKVII specs; Dallas Makerspace REM-jack thread; Fagear AVRTapeControl; Rod Elliott P247; Blokas Pisound docs; Michael Fidler VU-meter measurements; DigiKey TechForum (latching switches); RPi thermal testing + Jeff Geerling case roundup.
