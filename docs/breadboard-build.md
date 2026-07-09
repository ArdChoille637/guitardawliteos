# GuitarDAWLiteOS — Breadboard / Perfboard Build Guide

The fast, solderless way to bring up the Phase-1 audio hardware before committing to the [KiCad PCB](../kicad/). Uses **breakout modules** (GY-PCM5102 DAC, GY-PCM1808 ADC or a bare PCM1808 on a TSSOP→DIP adapter) and a **DIP op-amp**, so everything plugs into a breadboard.

See the placement diagram (rendered in chat / [breadboard-layout.svg](breadboard-layout.svg)).

## Why this differs from the PCB
The [KiCad board](../kicad/) uses **bare** PCM1808 + PCM5102A and includes the DAC's charge-pump caps, LDO, and output filter. On the breadboard, the **GY-PCM5102 module hides all of that** (onboard 3.3 V LDO, charge pump, and a 3.5 mm output jack), so the breadboard parts count is much lower. Same I²S/clock/front-end topology.

## Parts (breadboard-friendly)
| Qty | Part | Note |
|----:|------|------|
| 1 | Raspberry Pi 5 | + a 40-pin **GPIO breakout / T-cobbler** + ribbon (much more reliable than 8 loose jumpers) |
| 1 | **GY-PCM5102** module | DAC; has onboard LDO + 3.5 mm out |
| 1 | **GY-PCM1808** module (or bare PCM1808 on a TSSOP-14→DIP adapter) | ADC |
| 1 | **MCP6002** (DIP-8) | op-amp; RRIO, single-5 V, breadboard-friendly. *(OPA1662/OPA2353 for the PCB.)* |
| 1 | ¼″ mono jack + short leads | guitar in |
| 1 | **100 kΩ** trimpot (RV1, e.g. Bourns 3296W) | gain trim — in series with Rf 10 k → ×2…×12 |
| — | R: 1 MΩ, 2×10 kΩ, 10 kΩ, 1 kΩ | Rbias, R1/R2, Rg, Rf, Rs |
| — | C: 0.1 µF×(Cin, C8, +PCM1808 VCC/VDD/VREF 0.1 µF), 1 µF×(Cout, Cr), 1 nF (Ca), 10 µF×(C1 + PCM1808 bulk) | film/ceramic + a couple electrolytics |
| 1 | breadboard + jumper kit | |

> A passive guitar pickup needs the front-end — **don't plug the guitar into the ADC directly** (see [hardware-review.md](hardware-review.md)).

## Power rails on the breadboard
| Rail | From | Feeds |
|------|------|-------|
| **+5 V** (red) | Pi header **pin 2** | GY-PCM5102 VIN, PCM1808 VCC, op-amp V+ |
| **+3.3 V** (a spare row) | Pi header **pin 1** | PCM1808 VDD only (digital) |
| **GND** (blue) | Pi header **pin 6** | everything; single star tie |

(On the breadboard the GY-PCM5102's own LDO handles its 3.3 V, so header 3V3 only carries PCM1808's ~6 mA — well within budget.)

## Placement (left → right; keep analog away from the clocks)
1. **Pi cobbler / power-in** — left edge. Land +5 V→red rail, +3V3→a row, GND→blue rail.
2. **Guitar front-end** (analog zone) — ¼″ jack → `Cin` → `Rbias`(to VBIAS) → MCP6002 buffer → gain (`Rg`,`Rf`+`RV1`) → `Cout`→`Rs`→`Ca` → ADC VINL. `VBIAS` divider (`R1`/`R2`/`C1`) and `C8` bypass nearby. Keep this cluster **left and away** from the BCLK/LRCLK/MCLK jumpers.
3. **PCM1808 (ADC)** — middle. Straps MD0/MD1/FMT→GND. Decoupling 0.1 µF+10 µF at VCC, VDD, VREF.
4. **GY-PCM5102 (DAC)** — right. Pads **FLT=L, DEMP=L, XSMT=H, FMT=L**; SCK→GND. Out via its 3.5 mm jack.

## Build order (with test checkpoints)
1. **Rails + cobbler.** Wire +5 V / +3.3 V / GND. Double-check polarity before powering.
2. **DAC playback** (fastest win): GY-PCM5102 VIN←5 V, GND, BCK←GPIO18, LCK←GPIO19, DIN←GPIO21, **SCK→GND**, pads **L/L/H/L**. → run Circle `sample/34-sounddevices`; you should hear a tone. *No sound? Check **XSMT=H** first.*
3. **MCLK (Spike B):** generate GPCLK0 = 12.288 MHz on **GPIO4** (both MCLK 256 fs and BCLK 64 fs must come off `pll_audio`). **Scope-verify** before wiring the ADC.
4. **ADC capture:** strap MD0/MD1/FMT→GND; VCC←5 V (+0.1 µF&10 µF), VDD←3.3 V (+0.1 µF&10 µF), VREF→0.1 µF&10 µF to GND; SCKI←GPIO4, BCK←GPIO18, LRCK←GPIO19, DOUT→GPIO20. Temporarily feed VINL a line-level signal → confirm capture with Circle `sample/42-soundinput`.
5. **Front-end:** build VBIAS (R1/R2/C1 → 2.5 V), then the MCP6002 buffer + gain (Rg=10 k, Rf=10 k + RV1), Cin/Rbias, Cout→Rs→Ca→VINL, Cr on VINR, C8 bypass on the op-amp. Jack → Cin. Set RV1 to **×3**; check a hard strum peaks ≈ −1…−3 dBFS on a level meter.

## Common-mistake checklist
- [ ] **XSMT pad = HIGH** on the GY-PCM5102 (ships LOW = muted → silence).
- [ ] PCM1808 **MD0/MD1/FMT all to GND** *before power-on* (selects I²S slave).
- [ ] PCM1808 **VCC = 5 V**, **VDD = 3.3 V** (not both 3.3 V — analog needs 5 V).
- [ ] **GPIO20 = capture-in** (ADC DOUT), **GPIO21 = playback-out** (DAC DIN) — not swapped.
- [ ] **MCLK on GPIO4** present (12.288 MHz) before expecting ADC data.
- [ ] Guitar goes through the **front-end**, never straight to VINL.
- [ ] Op-amp on **5 V** (so it can swing to 4.0 V); MCP6002 is RRIO so it's fine.
- [ ] Single **star ground**; AGND≡DGND on the PCM1808.

Once this works on the breadboard, the [KiCad netlist](../kicad/) turns it into a fabbable board.
