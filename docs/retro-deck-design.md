# GuitarDAWLiteOS — Retro Deck Design (Phase 2 architecture)

**Date:** 2026-06-30. The three Phase-2 directions — **display**, **cassette analog tracking**, **retro-gear form factor** — researched against primary sources (Circle source at our pinned commit, TI/Tascam datasheets, the official HAT+ spec, maker-community practice) with adversarial verification on the two architecture-deciding claims. Both **confirmed**.

> **UI pivot (2026-07-23, Michael's decision): NO touch.** The interface is **5 rotary encoder knobs
> (with push) + a sustain-pedal footswitch + a plain display**. Touch is dropped as an input method —
> which fits the retro-deck concept better anyway (real knobs on a tape deck). §1 and §1b below are
> rewritten to the new facts (all verified against the pinned Circle tree, 2026-07-23); the GPIO budget
> in §4 is recomputed and now closes only with a zero-GPIO display (DSI display-only or HDMI).

**The product in one sentence:** a cassette-deck-styled open-hardware guitar workstation — Pi 5 + audio HAT+ inside a folded-metal/wood-cheek chassis, a PCB faceplate with real VU meters, illuminated transport keys, **five rotary knobs and a footswitch jack as the interface**, a display as the "cassette window," and an *optional real cassette tape loop* as a lo-fi analog insert.

---

## 1. Display (no touch) — two zero-GPIO options; SPI TFT ruled out ✅ (re-verified 2026-07-23)

**The display is output-only.** Verified against the pinned Circle tree after the UI pivot:

- **Option A (product default): Touch Display 2 as a *display-only* panel.** Circle's `addon/rp1dsi`
  supports this first-class: `CRPiTouchScreen(pInterrupt, nDepth, nDisplay, bEnableTouch=FALSE)`
  (`rpitouchscreen.h:40-42`) — with `FALSE` the GT911 is **never even powered** (CTP_RESET stays
  deasserted, no touch I²C traffic, `rpitouchscreen2.cpp:100-136`), while panel init, DSI DPI-DMA
  autonomous scanout, I²C backlight control, and the vsync callback all still work. LVGL runs fine
  without touch (`CLVGL(CDisplay*)`; the `touch1` lookup is optional/non-fatal, `lvgl.cpp:151`).
  **Zero J8 GPIOs** (video + control I²C ride the dedicated DISP FFC on RP1 bus 6/4; only 5 V/GND from
  pins 2/6) — the property the whole GPIO budget in §4 now depends on. Still portrait-native 720×1280 →
  LVGL 90° software rotation. Yes, we pay for a touch layer we don't use — there is **no non-touch
  official DSI panel**, and `rp1dsi` supports **only** the official panels (hard-coded regulator
  auto-detect at I²C 0x45, `rpitouchscreen.cpp:62-100`; third-party DSI would need a new panel driver).
- **Option B (dev bench, works today): HDMI.** The M3 kernel already renders on HDMI via
  `CScreenDevice`, headless-safe. Unlimited panel choice, zero GPIO, but: firmware framebuffer path is
  the one Circle marks "limited", mode fixed at boot (no hotplug), bulkier inside the chassis, and the
  documented WLAN-vs-HDMI interaction (irrelevant — we use no WLAN). Fine until the faceplate forces
  the decision.
- **SPI TFT: RULED OUT (2026-07-23).** Circle does have ST7789/ILI9341 drivers (`addon/display/`), but
  the pin budget cannot close: 5 encoders + pedal need 16 GPIOs, and a TFT needs SPI0 CE/DC/RST on pins
  the encoders now own — plus the APA102 bargraph shares SPI0 and has **no chip select** (it clocks in
  all bus traffic), so TFT-on-SPI0 corrupts it electrically. Dead option, not a fallback.

**Spike C (re-scoped): display-only + controls bring-up.** Build `sample/28-touchscreen` with
`DSI_DISPLAY=0` and `bEnableTouch=FALSE` on the TD2, then `addon/lvgl/sample` with 90° rotation and
**encoder-driven navigation** (LVGL encoder input group). Gate for committing the faceplate cutout
dimensions. HDMI path needs no spike — already proven live.

---

## 1b. Control surface — 5 rotary encoders + sustain-pedal footswitch (NEW 2026-07-23, verified)

**Hardware on hand:** five digital rotary encoder knobs with integrated push switches (EC11-class or
KY-040-class modules) + (planned) a Yamaha sustain pedal as the footswitch.

**Encoder electrical facts (verified in the pinned Circle tree):**
- RP1 internal pull-ups exist and work: `SetMode(GPIOModeInputPullUp)` (`gpiopin2712.cpp:257-299`).
  Also enable the RP1 **Schmitt trigger** per input (`SetSchmittTrigger`, `gpiopin2712.cpp:301-319`) —
  `SetMode` does *not* do it automatically.
- **KY-040 module gotcha:** the boards carry 10 k pull-ups on A/B to their "+" pin — power "+" from
  **3.3 V only** (5 V would drive 5 V into RP1 pads through those pull-ups). The SW pin usually has
  **no** onboard pull-up → still enable the internal one. Bare EC11: common → GND, internal pull-ups on
  A/B/SW, nothing else needed.
- All pins must be configured on **core 0 during `CKernel::Initialize`** (RP1 southbridge assert,
  `gpiopin2712.cpp:185`), before the secondary cores start.

**Read strategy (decided): poll from core 3, no GPIO interrupts.** Circle *has* full RP1 GPIO IRQ
machinery, but all peripheral IRQs land on **core 0** — the audio DMA/mix core — and 5 fast-turned
encoders emit O(10³) edges/s plus bounce bursts; each would be a PCIe-latency ISR stealing audio
headroom for zero benefit. Instead:
- One `CGPIOPin::ReadAll()` returns the **entire GPIO bank 0 in a single register read**
  (`gpiopin2712.cpp:594-597`) — all 16 inputs in one ~1 µs PCIe round-trip, never 16 separate reads.
- Core 3 (currently the 2 s stats loop) restructures into a **2 kHz tick** (worst-case flicked EC11 ≈
  400 edges/s → ≥1 kHz needed; 2 kHz safe). Cost: **≈0.2–0.5 % of one core** — negligible.
- Quadrature decode via the 16-entry transition table (bounce between adjacent Gray states cancels —
  no time-debounce needed); gate to one count per detent. Push switches + pedal: ~10 ms
  consecutive-sample debounce on the same tick. Events → small SPSC queue to the UI (same pattern as
  the audio ring).

**Sustain pedal (Yamaha FC5/FC4A) — verified facts, incl. the polarity gotcha:**
- ¼″ **TS** plug, passive momentary leaf switch, nothing else inside (no debounce — do it in software).
  (FC3A is the TRS half-damper — different animal, don't buy that one.)
- **Yamaha is NORMALLY CLOSED at rest — pressing OPENS the contact** (teardown + FC4A spec sheets +
  measurement consensus; much web folklore says the opposite and is wrong; Roland DP-series is the
  *same* NC convention, Korg/Casio are the NO camp; Boss FS-5U has a polarity slide).
- **Firmware handles polarity by auto-calibration, like every commercial keyboard:** sample the GPIO at
  boot (and on jack insertion) and latch that level as RELEASED — works for Yamaha NC, Korg-style NO,
  and either FS-5U slide position with zero config. (Yamaha's own FAQ documents this exact power-on
  sampling; a manual invert setting is the fallback, per Roland's "Damper Polarity" menu precedent.)
- Wiring: jack sleeve → GND, tip → GPIO25 with internal pull-up + Schmitt. Caveat: an **unplugged jack
  reads identical to "pressed"** for an NC pedal — use a switched-contact jack on the faceplate if
  plug-detection matters.

**Suggested knob map (v0, Michael's to override):** K1 input trim · K2 monitor/output level · K3 loop
select (push = arm rec/overdub) · K4 feedback/decay · K5 menu-navigate (push = confirm). Pedal =
classic looper semantics: tap = record/overdub toggle, double-tap = stop, hold = clear.

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

## 4. Rev-B GPIO budget (recomputed 2026-07-23 for the knob-UI pivot — finalize at rev-B schematic)

**The math:** 28 J8 GPIOs − 2 (EEPROM) − 4 (I²S) − 2 (I²C1) − 2 (APA102 data/clk) = 18 native pins;
the new controls need **16** (5 × {A, B, SW} + pedal). It closes **only** because the display is
zero-GPIO (§1) — and only by reclaiming the SPI-TFT fallback pins (7/8/9), the header-UART breakout
(14/15 — the JST-SH debug connector is unaffected), and moving the slow tape-transport signals to an
**MCP23017 expander on I²C1** (the "panel expander option" from the old table, now exercised).

| GPIO | Assignment | Was |
|------|-----------|-----|
| 0/1 | ID EEPROM (ID_SD/ID_SC) — nothing else (HAT+ rule) | unchanged |
| 2/3 | I²C1 → **MCP23017 panel expander** (transport slow signals + NKK keys + lamps) | "expander option" |
| 4 | **ENC1_A** | free (reclaimed from MCLK) |
| 5 / 6 | ENC1_B / ENC1_SW | REM → 26 · IR_TX → 27 |
| 7 / 8 / 9 | ENC2_A / ENC2_B / ENC2_SW | SPI0 CE1/CE0/MISO (SPI-TFT fallback — **dropped**) |
| 10 / 11 | SPI0 MOSI / SCLK → **APA102 bargraph** (data + clock) | unchanged |
| 12 / 13 | ENC3_A / ENC3_B | spare PWM (lamp dimming → expander or APA chain) |
| 14 / 15 | ENC3_SW / ENC4_A | header UART0 breakout (reclaimed; JST debug console unaffected) |
| 16 / 17 | ENC4_B / ENC4_SW | SOLENOID_DRV / MOTOR_EN (→ expander) |
| 18–21 | I²S (ADC-driven BCK/LRC + data; Pi slave) | unchanged, fixed |
| 22 / 23 / 24 | ENC5_A / ENC5_B / ENC5_SW | aux block |
| 25 | **SUSTAIN_PEDAL** (¼″ TS jack, pull-up + Schmitt, boot auto-polarity — §1b) | aux block |
| 26 | REM optoMOS (punch-in relay) | was 5 |
| 27 | IR_TX footprint (38 kHz carrier — needs a native pin, can't come from the expander) | was 6 |

**On the MCP23017:** SOLENOID_DRV, MOTOR_EN, TACH_IN, SW1–3 (tape-present / rec-inhibit / stop-pos),
NKK UB transport keys, lamp on/off. All slow; acceptable because Level-B tape is "provision, populate
later."

**Honest consequences of closing the budget:**
- **J8 is 100 % consumed — zero spare native GPIOs.** "Broken-out spares" hackability now lives on the
  I²C1 expander bus instead. (Optional recovery: REM is slow — moving it to the expander frees GPIO26
  as the single spare.)
- Solenoid pulse timing and TACH edge-timing from an I²C expander are degraded (poll-only; no free
  native INT pin) — flagged for the rev-B schematic if Level B ever populates.
- PWM lamp dimming is gone; NKK lamps go on/off via expander or ride the APA102 chain.

**Standing rule (amended 2026-07-23):** never reassign 18–21; keep **10/11** for the APA102 (SPI0
data/clock). The old "keep all of 7–11 for SPI0" rule died with the SPI-TFT option; GPIO4's
reservation died with the MCLK reclaim.

## 5. What changes where
- **Rev A (current KiCad project)**: unchanged — it's the learning vehicle. Everything above is **rev B**.
- **`kicad/gen.py`**: rev-B netlist additions (tape pads/jumper, REM optoMOS, TRANSPORT header, EEPROM, line-tap buffers, panel connector) get added behind a clearly-marked REV-B section when we cut that board.
- **Build plan**: Spike C (DSI bring-up) joins Spike A/B in Milestone 1; faceplate + enclosure become new milestones in the learning path (M7/M8).
- **CM5 carrier (phase 3)**: route the same 22-pin DSI FPC + 5 V display power; **rp1dsi is unconfirmed on CM5** (Circle lists CM5 as "reported to work") → its own validation spike.

## 6. Risk register (Phase 2)
| Risk | Severity | Mitigation |
|------|----------|------------|
| `rp1dsi` is young, unreleased | Med | Spike C (display-only, `bEnableTouch=FALSE`) before faceplate commit; plain-HDMI fallback already proven live |
| TD2 portrait-only scanout | Low | LVGL 90° rotation from day one (unchanged by the touch drop) |
| J8 100 % consumed by the knob UI | Med | Expander bus is the hackability escape hatch; REM→expander frees one spare if needed |
| REM punch-in only fits portable/Portastudio decks | Low | IR footprint + TRANSPORT header cover the rest |
| Piano-key/walnut parts are NOS/discontinued | Med | NKK UB keys + open-DXF wood cheeks in the open BOM; NOS only for personal builds |
| Pi 5 throttling in closed box | High | Active Cooler under 16 mm-standoff HAT + vent grilles + fan curve |
| LM3915 in old references | — | banned from BOM; APA102/SK9822 instead |

**Sources:** Circle `addon/rp1dsi/*`, `lib/usb/usbtouchscreen.cpp`, `lib/input/xpt2046touchscreen.cpp`, README/CHANGELOG @ `22722a76`; GitHub commits API (addon age); raspberrypi.com Touch Display 2 docs + HAT+ spec (2024-12 build) + Pi 5 mechanical drawing; TI SLAS859C / SLES177B; tascam.com 202MKVII specs; Dallas Makerspace REM-jack thread; Fagear AVRTapeControl; Rod Elliott P247; Blokas Pisound docs; Michael Fidler VU-meter measurements; DigiKey TechForum (latching switches); RPi thermal testing + Jeff Geerling case roundup.
