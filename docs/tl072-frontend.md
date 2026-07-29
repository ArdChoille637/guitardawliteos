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

## Add a supply bypass cap (M2 review, 2026-07-22)

**`C_byp` = 100 µF electrolytic ∥ 0.1 µF ceramic at U1 pin 8 → GND.** The as-built
docs had *no* supply bypass, and the SPICE deck modeled the 9 V supply as ideal,
so rail ripple was never analyzed. **This matters more since 2026-07-28**, because
the 9 V now comes from a 7809 fed by the same pack that runs the Pi's buck, rather
than from an isolated wall PSU. It matters more than it looks anyway, because
**VBIAS ripple is not common-mode here** — at 120 Hz Cin (0.1 µF ≈ 13 kΩ) shunts the
gain stage's + input toward the low-impedance guitar, so ripple on VBIAS gets
*inverting-amplified* by Rf/Rg, not cancelled.

Cross-checked in ngspice (rail → ADC-pin transfer, ideal low-Z source = plugged-in guitar):

| Trim | rail → ADC transfer | 10 mVpp rail ripple → |
|---|---|---|
| default ×3 (Rf = 20 k) | **−31.7 dB** | ~−81 dBFS (inaudible) |
| max trim ×12 (Rf = 110 k) | **−16.9 dB** | ~−66 dBFS (audible hum) |

So the hum only bites when you crank the trim for a weak pickup — but that's a real
operating point. `C_byp` at the op-amp pin cuts the switching-residual path outright
and is standard practice for an op-amp on the end of a wall-wart cable; add it.
*(This corrects the review's own "common-mode ×1" downgrade — the ×1 assumption only
holds for an open input; a plugged-in guitar sees the Rf/Rg path.)*

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

- **Power:** 9 V pedal PSU (preferred) or battery. ⚠️ Boss-style pedal supplies are
  **center-negative** — check polarity before it checks you. ⚠️ **Keep the rail ≥ 8.3 V.**
  VBIAS = 0.545·V_rail, so below ~8.3 V a 1 Vpp hot-humbucker drives the JFET input
  under its guaranteed common-mode floor (V− + 4 V) → progressive distortion first,
  and the notorious TL07x output latch-up only at a much lower rail. A fresh 9 V is
  fine; a half-dead battery drifts into the distortion zone — prefer the PSU, or
  swap the battery early.
- **Ground:** the 9 V negative terminal joins the same star ground as
  everything else (Pi pin 6 rail). Two supply domains, one ground.
- The 9 V rail powers **only U1**. The PCM1808's VCC stays on the Pi's 5 V
  per [adc-hookup.md](adc-hookup.md) — do not put 9 V anywhere near the ADC.

## Bench setup: CopperSound DIY breadboard (medium) on the +9 V rail

The whole front-end lives on the CopperSound board — it exists for exactly
this kind of circuit:

- **+9 V rail** (7809 off the central 18 V pack, [power-tree-18v.md](power-tree-18v.md))
  **→** the CopperSound board's **DC jack** via a Boss-style center-negative
  plug, so polarity is handled by using the jack as intended. Its power rails
  become the front-end's +9 V and GND (check the board's own rail labels).
  *(Was a Gator 9 V wall PSU until 2026-07-28.)*
- **Guitar →** the board's **input jack** → Cin 0.1 µF → Rbias 1 M →
  TL072 buffer → ×3 gain stage → **Cout 1 µF → the board's output jack**.
- **Output jack → PCM1808 side** with a regular instrument cable (or a
  wire pair). Put **Rs 1 k + Ca 1 nF at the ADC end**, right at the VINL
  pin — the anti-alias RC belongs at the pin it protects, not on the
  pedal board.
- **One explicit ground wire** from the CopperSound GND rail to the star
  ground (Pi pin 6 rail). The cable's sleeve nominally carries ground
  too, but the audio reference shouldn't hang off a patch cable.

One source, one ground: the +9 V rail feeds the TL072 board and the Nano's VIN;
the Pi's 5 V/3.3 V feed the PCM1808 per [adc-hookup.md](adc-hookup.md). Keep the
9 V and 5 V branches as separate spokes from the star — see
[power-tree-18v.md](power-tree-18v.md).

This rig doubles as the pedal-development platform for the Pedal Workshop
merge — same board, pedals prototyped between the guitar and this front-end.
A pedal under test wants its own 9 V feed or a dedicated rail spoke, not a
daisy-chain through the front-end's.

## Also in the drawer: CD4053BE

Not needed for the M2 capture path — **park them for rev B**, where they're
a great fit (GPIO-controlled analog switching: the tape-insert wet/dry path,
input source select, punch-in routing — and they're the classic
Boss/Ibanez-style bypass-switching chip, relevant to the pedal-workshop
merge). One catch to remember when that day comes: at VDD = 5 V a CD4053's
select pins want VIH ≈ 3.5 V, which the Pi's 3.3 V GPIO doesn't reach — run
the 4053 at VDD = 3.3 V (fine, but signals must then stay within 0–3.3 V) or
level-shift the select lines.
