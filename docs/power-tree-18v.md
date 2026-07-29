# Power tree — single 18 V source

**Decision (2026-07-28):** the rig runs from **one** central 18 V tool battery.
The Gator 9 V pedal PSU and the Pi's 27 W USB-C PD brick are **removed**, and
the Nano ESP32 no longer runs off its own USB-C. Every rail is derived on-board.

This supersedes the three-independent-supplies arrangement described in
[adc-hookup.md](adc-hookup.md), [tl072-frontend.md](tl072-frontend.md) and
[wiring-diagram.svg](wiring-diagram.svg) before this date.

## The tree

```
Ryobi 18 V pack  (15.0–20.5 V over discharge)
  │
  ├─ FUSE 3–5 A blade, within 10 cm of the pack
  ├─ low-voltage cutoff module, trips at 15.0 V
  ├─ MASTER SWITCH (SPST, ≥3 A)
  │
  ├──► 7809 linear regulator ──► +9 V rail
  │        (100 nF in, 10 µF out, clip-on heatsink)
  │        ├──► TL072 front-end V+ (pin 8)        [was: Gator 9 V]
  │        └──► Nano ESP32 VIN                    [was: its own USB-C]
  │
  └──► 5 V buck converter, ≥5 A, input rated ≥24 V
           └──► Pi 5                              [was: 27 W USB-C PD]
                  └─ Pi J8 p2 (5 V)  → PCM1808 VCC, PCM5102A VIN
                     Pi J8 p1 (3.3 V) → PCM1808 VDD, MD0/MD1 straps, XSMT
                     Pi J8 p6         → star ground
```

The codec rails are unchanged: they still come off the Pi's header exactly as
[adc-hookup.md](adc-hookup.md) specifies. Only the *upstream* source changed.

## Why each choice

**Nano ESP32 from the +9 V rail, not from 18 V and not from 5 V.** Its VIN
regulator wants **6–21 V**. 5 V is below the minimum — the long-standing "never
feed the 5 V rail into VIN" rule still holds. Raw 18 V is nominally inside the
window, but a freshly charged pack sits near 20.5 V, which is uncomfortably
close to the 21 V ceiling. The 9 V rail sits mid-window at every state of
charge. Cost: the Nano's onboard regulator drops 9 V → 3.3 V at ~50–100 mA,
so ~0.6 W in a small package — expect it warm, not hot.

**Pi 5 via header pins 2/4 — with eyes open.** Two ways to get the pack's
energy into the Pi:

| | Header injection (p2/p4) | 18 V → USB-C **PD source** board |
|---|---|---|
| Parts | one buck | buck + PD-source module |
| PD negotiation | none | real 5 V/5 A handshake |
| Pi input protection / polyfuse | **bypassed** | retained |
| USB peripheral budget | firmware caps total USB at 600 mA unless `usb_max_current_enable=1` | full |

Header injection is documented here as the default **because this build hangs
nothing off USB** — the audio path is I²S, and the design rule is explicitly "no
USB in the audio path." The 600 mA cap therefore costs nothing today. If USB
peripherals are ever added, switch to the PD-source board rather than raising
the cap.

What header injection *does* cost is the Pi's input protection. Compensate with
the pack-side fuse, the low-voltage cutoff, and wiring that cannot be reversed
by construction (keyed connector or polarised header).

**Buck sizing.** Spec **≥5 A** even though this workload will draw far less.
The Pi 5 pulls large current transients, and a buck sized to the average will
brown it out on a spike. Add bulk capacitance at the Pi end of the 5 V run.

## What got better

Collapsing three supplies into one **removes a hazard** the M2 bench review
flagged. Previously the star-ground jumper was load-bearing for the Nano→ADC
clock line: if it dropped, MCLK return current plus inter-supply Y-capacitor
leakage had to find its way home through the PCM1808's SCKI input clamp. With a
single supply there is no second supply to leak against, and no inter-supply
potential to develop.

The paired/twisted HF returns (MCLK, and the BCK/LRC/OUT bundle) and the ~33 Ω
series resistor at the Nano's D2 are still wanted — those are EMI hygiene, not
supply-isolation fixes, and they are unaffected by this change.

## What got worse

**Everything now shares one ground and one source**, so the analog front-end no
longer enjoys any isolation from the Pi's current transients or the buck's
switching residue. Two consequences:

1. Keep the 9 V branch and the 5 V branch as **separate spokes from the star**,
   not daisy-chained through each other.
2. The analog-rail R on the PCM1808's VCC is **no longer conditional** — the
   frozen M2 wiring already splits J8 p2 into *branch A → DAC VIN direct* and
   *branch B → 10 Ω → ADC +5 V*. Keep that split. Consolidating supplies makes
   it more valuable, not less, and the companion bulk (470 µF ∥ 0.1 µF) is now
   worth fitting rather than deferring. **Never run the DAC through the 10 Ω:**
   34 mA × 10 Ω = 340 mV, which drops ADC VCC to 4.41 V on a sagged rail —
   under its 4.5 V minimum.

## Runtime

| Load | Draw |
|---|---|
| Pi 5, bare-metal audio workload | ≈ 6–8 W |
| Nano ESP32 + analog front-end | ≈ 1 W |
| **Total, current build** | **≈ 9 W** → ~1.9 h on a 2 Ah pack (36 Wh), ~3.8 h on 4 Ah |

### Deferred — not in the current build (2026-07-28)

- **SK9822 / APA102 LED bargraph** — parked. When it returns it needs **its own
  buck branch off the 18 V rail**, never the Pi's header: 60 LEDs at full white
  is ≈ 3.6 A / 18 W, which would dominate this entire budget and roughly halve
  runtime. It also needs a 3.3 → 5 V buffer (74AHCT125) on data and clock, since
  the part's V<sub>IH</sub> ≈ 0.7 × V<sub>DD</sub> = 3.5 V and the Pi drives
  3.3 V. Pin reservation (SPI0, GPIO10/11) stays as-is in
  [retro-deck-design.md](retro-deck-design.md) — nothing here reassigns it.
- **SN74HC595 shift register** — parked; no role assigned. Note for whenever it
  is picked up: it is **HC**, not AHCT, so its own V<sub>IH</sub> at 5 V is
  ≈ 3.5 V. It cannot serve as a 3.3 V → 5 V level shifter, for the LED strip or
  anything else.

Stop discharging at **15.0 V**. The cutoff module is not optional on a lithium
tool pack.

## Order of operations

Unchanged in principle — the Nano must clock before the ADC is expected to
master the bus — but there is now **one switch** instead of three supplies to
sequence:

1. Wire everything **unpowered**. Straps first: FMT→GND, MD0/MD1→3.3 V.
2. **Leave the D2→D3 link in place** — the frozen M2 wiring keeps it as a
   permanent MCLK health readout, tapped *before* the 33 Ω.
3. **Meter the rails before landing the codec leads.** With the module's two
   power leads *not yet connected*, close the master switch and confirm +9 V,
   +5 V, and the Pi's J8 p1/p2 at 3.3 V/5.0 V. J8 pins 1 and 2 are physically
   adjacent and VDD is 4 V abs-max — a one-row slip kills the ADC.
4. Open the master switch, land the codec leads, close it again.
5. Before touching any wire later: **open the master switch first.** One switch
   now kills every rail, which is the main ergonomic win of this change.
