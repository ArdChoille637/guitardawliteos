# GuitarDAWLiteOS — Breadboard / Perfboard Build Guide

The fast, solderless way to bring up the Phase-1 audio hardware before committing to the [KiCad PCB](../kicad/). Uses **breakout modules** (GY-PCM5102 DAC, GY-PCM1808 ADC or a bare PCM1808 on a TSSOP→DIP adapter) and a **DIP op-amp**, so everything plugs into a breadboard.

See the placement diagram (rendered in chat / [breadboard-layout.svg](breadboard-layout.svg)).

> ⚠️ **Clock pivot (2026-07-10): the PCM1808 is now the I²S bus master** — strap **MD0=MD1=HIGH (3.3 V)**, FMT=GND; the ADC divides the Nano's 12.288 MHz MCLK into BCK/LRCK and the **Pi 5 runs I²S slave** (`GDAW_I2S_SLAVE` in [src/config.h](../src/config.h)). The old MD-to-GND / Pi-as-master instructions are gone from this doc. For the as-built bench wire-by-wire card use **[adc-hookup.md](adc-hookup.md)** + [wiring-diagram.svg](wiring-diagram.svg) + [codec-power-schematic.svg](codec-power-schematic.svg); for the 9 V TL072 drawer-parts front-end use [tl072-frontend.md](tl072-frontend.md) + [tl072-frontend-schematic.svg](tl072-frontend-schematic.svg).

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
3. **PCM1808 (ADC)** — middle. Straps **MD0/MD1→3.3 V** (master, 256 fs), FMT→GND. Decoupling 0.1 µF+10 µF at VCC, VDD, VREF.
4. **GY-PCM5102 (DAC)** — right. Pads **FLT=L, DEMP=L, XSMT=H, FMT=L**; SCK→GND. Out via its 3.5 mm jack.

## Build order (with test checkpoints)
Post-pivot the ADC drives every bus clock — **nothing plays or captures until the Nano + ADC are up**, so they come first.
1. **Rails + cobbler.** Wire +5 V / +3.3 V / GND. Double-check polarity before powering.
2. **MCLK (Spike B) — done:** the **Arduino Nano ESP32** ([esp32-mclk/esp32-mclk.ino](../esp32-mclk/)) generates 12.288 MHz on its own **D2 (GPIO5)** pin, independent of the Pi 5. Flashed and bench-verified via the chip's own PCNT self-test (no scope needed) at 12.2880 MHz, exact match. (The Pi-internal `GPCLK0` route was ruled out — `clk_i2s` is already claimed by BCLK the whole time I²S runs; see [claim-verification.md](claim-verification.md).)
3. **ADC (bus master):** strap **MD0/MD1→3.3 V, FMT→GND**; VCC←5 V (+0.1 µF&10 µF), VDD←3.3 V (+0.1 µF&10 µF), VREF→0.1 µF&10 µF to GND; SCKI←**Nano ESP32 D2**; the ADC then **drives** BCK→GPIO18, LRCK→GPIO19, DOUT→GPIO20. Boot the M3 kernel: the HDMI stats line's capture counter should tick at 48,000 frames/s. Temporarily feed VINL a line-level signal to see non-zero samples. (Wire-by-wire card: [adc-hookup.md](adc-hookup.md).)
4. **DAC playback:** GY-PCM5102 VIN←5 V, GND, **BCK/LCK ← the same ADC-driven bus** (rows shared with Pi GPIO18/19), DIN←GPIO21, **SCK→GND**, pads **L/L/H/L**. With the monitor path live you should hear the VINL signal at the 3.5 mm out. *No sound? Check **XSMT=H** first.*
5. **Front-end:** build VBIAS (R1/R2/C1 → 2.5 V), then the MCP6002 buffer + gain (Rg=10 k, Rf=10 k + RV1), Cin/Rbias, Cout→Rs→Ca→VINL, Cr on VINR, C8 bypass on the op-amp. Jack → Cin. Set RV1 to **×3**; check a hard strum peaks ≈ −1…−3 dBFS on a level meter.

## Common-mistake checklist
- [ ] **XSMT pad = HIGH** on the GY-PCM5102 (ships LOW = muted → silence).
- [ ] PCM1808 **MD0/MD1 HIGH (3.3 V), FMT to GND** *before power-on* (selects I²S **master**, 256 fs — the 2026-07-10 pivot; MD-to-GND slave mode is the old, wrong strap).
- [ ] PCM1808 **VCC = 5 V**, **VDD = 3.3 V** (not both 3.3 V — analog needs 5 V).
- [ ] **GPIO20 = capture-in** (ADC DOUT), **GPIO21 = playback-out** (DAC DIN) — not swapped.
- [ ] **MCLK from the Nano ESP32** present (12.288 MHz on its D2 pin) before expecting ADC data — this no longer comes from a Pi GPIO.
- [ ] Guitar goes through the **front-end**, never straight to VINL.
- [ ] Op-amp on **5 V** (so it can swing to 4.0 V); MCP6002 is RRIO so it's fine.
- [ ] Single **star ground**; AGND≡DGND on the PCM1808.

Once this works on the breadboard, the [KiCad netlist](../kicad/) turns it into a fabbable board.
