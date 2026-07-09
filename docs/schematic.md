# GuitarDAWLiteOS — Schematic, Netlist & BOM (Phase 1 audio board)

**Date:** 2026-06-29. Buildable schematic for the Pi 5 + PCM1808 (capture, with guitar front-end) + PCM5102A (playback) board. All pins/values are datasheet-verified; the analog front-end is **SPICE-verified** (ngspice, deck in [sim/frontend.cir](sim/frontend.cir), results in [sim/frontend-results.txt](sim/frontend-results.txt)).

**Sheets**
- **Front-end** — [frontend-schematic.svg](frontend-schematic.svg): guitar ¼″ → buffer → ×3 gain → AA → VINL, with the VBIAS mid-rail.
- **Codec & power** — [codec-power-schematic.svg](codec-power-schematic.svg): Pi 5 J8 ↔ PCM1808 ↔ PCM5102A, decoupling, strapping (nets joined by name).
- Supersedes nothing; complements [hardware-review.md](hardware-review.md) (rationale) and [wiring-diagram.svg](wiring-diagram.svg) (overview).

---

## Bill of materials (reference designators)
| Ref | Value / Part | Notes |
|-----|--------------|-------|
| U1 | **OPA1662** (audio) or **OPA2353** (RRIO) dual op-amp, SOIC-8 | front-end; single +5 V |
| U2 | **PCM1808** ADC, SSOP-14 (or GY-PCM1808 breakout) | capture, slave/strap-only |
| U3 | **GY-PCM5102** (PCM5102A) breakout | playback, internal PLL |
| J1 | ¼″ mono jack | guitar in |
| J2 | 3.5 mm jack / 3-pin terminal | line out (on U3 module) |
| RV1 | 100 kΩ trimpot | front-end gain trim |
| R1, R2 | 10 kΩ | VBIAS divider |
| Rbias | 1 MΩ | input bias / Z-in |
| Rg | 10 kΩ | gain set (to VBIAS) |
| Rf | 10 kΩ fixed (+ RV1) | feedback; ≈20 kΩ at ×3 nominal |
| Rs | 1 kΩ | anti-alias series |
| FB1 | ferrite bead (e.g. 600 Ω @100 MHz) | +5 V → U2 VCC |
| Cin | 0.1 µF film | input AC-couple |
| Cout, Cr | 1 µF film | output AC-couple / VINR AC-gnd |
| Ca | 1 nF C0G | anti-alias shunt |
| C1 | 10 µF | VBIAS reservoir |
| C2,C4,C6 | 0.1 µF ceramic | U2 VCC/VDD/VREF bypass |
| C3,C5,C7 | 10 µF | U2 VCC/VDD/VREF bulk |
| C8 | 0.1 µF ceramic | U1 op-amp V+ local bypass (to GND) |

U3 (GY-PCM5102) is self-decoupled and carries its own output RC (470 Ω + 2.2 nF/ch) and LDO.

---

## Netlist (by net)
**Power**
- `+5V` — Pi J8 **pin 2**; U1 V+ (pin 8, + local **C8** 0.1 µF→GND); U3 VIN; **FB1**→U2 VCC (pin 3).
- `+3V3` — Pi J8 **pin 1**; U2 VDD (pin 4); U3 **XSMT pad → H**.
- `GND` — Pi J8 **pin 6** (star); U1 V− (pin 4); U2 AGND(2), DGND(5), MD0(10), MD1(11), FMT(12); U3 GND, SCK, FLT/DEMP/FMT pads; J1 sleeve; all decoupling/bias returns. **AGND ≡ DGND**, single star to one Pi GND.

**I²S0 (RP1 ALT a2; clocks from Pi master)**
- `MCLK` — Pi **p7/GPIO4** (GPCLK0) → U2 **SCKI (6)**. *ADC only.*
- `BCLK` — Pi **p12/GPIO18** → U2 **BCK (8)** + U3 **BCK**.
- `LRCLK` — Pi **p35/GPIO19** → U2 **LRCK (7)** + U3 **LCK**.
- `CAPDAT` — U2 **DOUT (9)** → Pi **p38/GPIO20**.
- `PLAYDAT` — Pi **p40/GPIO21** → U3 **DIN**.

**Strapping (no nets — tie at the pin)**
- U2: MD0=MD1=FMT → GND (slave, I²S 24-bit). U2 SCKI driven by MCLK.
- U3 pads: FLT=L, DEMP=L, FMT=L, **XSMT=H (+3V3)**; SCK→GND (internal PLL off 64fs BCLK).

**Analog front-end (sheet 1)**
- `VBIAS` (2.5 V) — R1(+5V)–node–R2(GND); C1 node→GND; Rbias top; Rg bottom. *Independent of U2 VREF.*
- J1 tip → Cin → node nbi (Rbias→VBIAS) → U1A `+`; U1A out → U1A `−` (buffer) and → U1B `+`.
- U1B `−` = ninv: Rg→VBIAS, Rf→U1B out (`feo`).
- feo → Cout → Rs → node `VINL`; Ca: VINL→GND. `VINL` → U2 **VINL (13)**.
- U2 **VINR (14)** → Cr → GND (AC-ground unused channel).
- U2 **VREF (1)** → C6‖C7 → AGND (decouple only — do **not** drive/load).

**Output**
- U3 OUTL/OUTR/GND → J2 (line out, 2.1 Vrms, ground-centered).

---

## Front-end SPICE verification (ngspice)
Deck: [sim/frontend.cir](sim/frontend.cir). Reproduce: `ngspice -b docs/sim/frontend.cir`.

| Measurement | Result | Expected | ✓ |
|-------------|--------|----------|---|
| DC bias (vbias, buffer, gain, VINL) | 2.500 V | 2.5 V mid-rail | ✓ |
| Gain @ 1 kHz | **9.54 dB (×3.00)** | ×3 | ✓ |
| Flatness (100 Hz / 20 kHz) | 9.535 / 9.542 dB | flat | ✓ |
| Low −3 dB corner | **2.6 Hz** | ≪ 82 Hz (low-E) | ✓ |
| Hot-HB 1 Vpp transient | feo **0.997–3.997 V** | 1.0–4.0 V, no clip | ✓ |
| Overdrive 1.6 Vpp | clips 0.09 / 4.89 V | rails (validates trim) | ✓ |
| Real op-amp (0.6 V rail margin = OPA1662) | feo 0.996–3.997 V | still no clip | ✓ |

Re-running with `ngspice -b --define VMARGIN=0.6 docs/sim/frontend.cir` models OPA1662's worst-case 0.6–4.4 V swing — the transient is unchanged (0.996–3.997 V), so a real **non-RRIO** audio op-amp suffices (RRIO/OPA2353 is optional). High-frequency rolloff is the passive AA at fc = 1/(2π·1 kΩ·1 nF) ≈ **159 kHz** (the ideal-clamp sim op-amp is flat past 2 MHz, as expected).

---

## Build / assembly order (breadboard or perfboard)
1. **Power & ground first.** Bring in +5 V (Pi p2) and +3V3 (Pi p1); set the single star ground. Add FB1 + C2/C3 to U2 VCC.
2. **DAC playback path** (fastest win): wire U3 (VIN, GND, BCK, LCK, DIN, SCK→GND), set pads **L,L,H,L** (XSMT=H!). Test with Circle `sample/34-sounddevices`.
3. **MCLK (Spike B)** before the ADC: GPCLK0 on GPIO4 = 12.288 MHz → U2 SCKI. **MCLK (256 fs) and BCLK (64 fs) must both derive from `pll_audio`** (256 fs ÷ 4 = 64 fs) so SCKI and LRCK stay frequency-coherent — verify on a scope.
4. **ADC capture path**: strap U2 (MD0/MD1/FMT→GND), wire SCKI/BCK/LRCK/DOUT, VCC/VDD/VREF + decoupling. Temporarily feed VINL a line signal to confirm capture (Circle `sample/42-soundinput`).
5. **Front-end last**: build VBIAS (R1/R2/C1), then U1A buffer, U1B gain (Rg, Rf+RV1), Cout/Rs/Ca → VINL; Cr on VINR. Set RV1 to ×3 and check on a level meter (hard strum ≈ −1…−3 dBFS).

## ERC self-check
- Every IC power pin has a net and local decoupling. ✓
- No output drives another output; U2 DOUT is the only driver of `CAPDAT`. ✓
- U2 BCK/LRCK are 3.3 V-domain (VDD) and only ever driven by the 3.3 V Pi (never 5 V). ✓
- VREF is decouple-only (not driven); front-end uses its own VBIAS. ✓
- All strap pins tied (no floating MD0/MD1/FMT or XSMT). ✓

*Adversarial ERC review (3 independent reviewers, 2026-06-29): **no blockers or majors.** I²S connectivity clean; power/ground and front-end minor-only. Applied: VDD from header 3V3 direct (LDO now optional), U1 bypass **C8** added, gain trim ×2…×12, MCLK/BCLK coherence noted. Hard build rule confirmed: U3 must be the GY-PCM5102 **module** (onboard LDO) on the 5 V VIN — never a bare PCM5102A, and never strap XSMT to 5 V.*

## Open option
Front-end is **mono guitar → VINL**; VINR is AC-grounded. To add a **line/aux or second input** (mic, stereo pedal → VINR), duplicate the front-end (or a simpler unity buffer) into VINR and read both I²S channels — ask and I'll extend both sheets.
