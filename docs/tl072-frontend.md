# TL072 front-end variant — single 9 V (drawer-parts build)

The canonical front-end ([schematic.md](schematic.md)) runs an MCP6002/OPA1662
on the Pi's 5 V rail. A **TL072 cannot do that** — but on a **9 V pedal supply
or battery** (its natural habitat) it works beautifully, and the topology is
otherwise identical. SPICE-verified: [sim/frontend-tl072-9v.cir](sim/frontend-tl072-9v.cir)
→ [sim/frontend-tl072-9v-results.txt](sim/frontend-tl072-9v-results.txt).

## Why the TL072 can't run the 5 V design

Three independent dealbreakers on a 5 V single rail (classic TL072CP specs):
1. **Minimum supply ~7 V** — 5 V is simply out of spec.
2. **JFET input common-mode must stay ≥ V− + 4 V** — the 2.5 V mid-rail bias
   sits 1.5 V below that; risk of the notorious TL07x phase-reversal.
3. **Output swings only to ~1.5 V from each rail** — the design needs 4.0 V
   peaks on a 5 V rail; the TL072 tops out around 3.5 V.

## The 9 V variant — two changes from schematic.md

| What | 5 V canonical | 9 V TL072 |
|---|---|---|
| U1 supply (pin 8 / pin 4) | +5 V / GND | **+9 V** / GND |
| VBIAS divider R1/R2 | 10 k / 10 k → 2.5 V | **10 k / 12 k → 4.9 V** (keeps the JFET inputs ≥ 4 V from ground with margin) |

Everything else is unchanged: Cin 0.1 µF → Rbias 1 M → U1A buffer → U1B
non-inverting ×3 (Rg 10 k to VBIAS, Rf 10 k + trim), Cout 1 µF → Rs 1 k +
Ca 1 nF → VINL. **Cout isolates the 9 V domain from the ADC** — the PCM1808
self-biases its input after the cap, so the ADC side doesn't know or care
what rail the op-amp runs on. TL072 is pin-compatible with the MCP6002
(standard dual op-amp DIP-8), so the breadboard layout is unchanged too.

## SPICE results (ngspice-46, 2026-07-10)

| Check | Result | Pass bar |
|---|---|---|
| DC bias | 4.909 V, all nodes centered | ~4.9 V |
| Gain | 9.54 dB (×3.00), flat 100 Hz–20 kHz | ~9.5 dB |
| Low corner | 2.7 Hz | ≤ 20 Hz |
| Hot humbucker 1 Vpp → ADC pin | **1.02–3.97 V** | inside 1.0–4.0 V full-scale |
| JFET common-mode minimum | 4.41 V | ≥ 4.0 V (guaranteed limit) |
| Op-amp headroom | no clip even at 1.6 Vpp overdrive | ADC full-scale clips first → set the trim so hard strums peak just under it |

## Bench notes

- **Power:** 9 V battery or pedal PSU. ⚠️ Boss-style pedal supplies are
  **center-negative** — check polarity before it checks you.
- **Ground:** the 9 V negative terminal joins the same star ground as
  everything else (Pi pin 6 rail). Two supply domains, one ground.
- The 9 V rail powers **only U1**. The PCM1808's VCC stays on the Pi's 5 V
  per [adc-hookup.md](adc-hookup.md) — do not put 9 V anywhere near the ADC.

## Also in the drawer: CD4053BE

Not needed for the M2 capture path — **park them for rev B**, where they're
a great fit (GPIO-controlled analog switching: the tape-insert wet/dry path,
input source select, punch-in routing — and they're the classic
Boss/Ibanez-style bypass-switching chip, relevant to the pedal-workshop
merge). One catch to remember when that day comes: at VDD = 5 V a CD4053's
select pins want VIH ≈ 3.5 V, which the Pi's 3.3 V GPIO doesn't reach — run
the 4053 at VDD = 3.3 V (fine, but signals must then stay within 0–3.3 V) or
level-shift the select lines.
