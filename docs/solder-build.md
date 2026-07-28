# M2 Audio Board — FINALIZED SOLDER SPEC

**Status: frozen 2026-07-28.** This is the single authoritative document for the permanent
soldered M2 audio board. Every value here is decided — nothing is gated on a later measurement,
nothing says "optional", nothing says "or". Where an earlier doc defers a choice, **this file
overrides it**.

**Build style (confirmed with Michael 2026-07-28):** breadboards are layout aids only; this is a
solder-first build. Modules are **soldered directly to perfboard**, not socketed. Front-end rail
stays **9 V** (no 12 V output available on the Gator).

Supersedes, where they conflict: `adc-hookup.md` (the "measure first" gate), `breadboard-build.md`
(single shared 5 V rail), `hardware-review.md` (ferrite filter, LDO option), `schematic.md`
(rev-A netlist — already bannered), `build-plan.md:39` (the "local LDO" line).

Prior review basis: [2026-07-21](reviews/2026-07-21-m2-bench-adversarial-review.md) ·
[2026-07-28 schematic review](reviews/2026-07-28-schematic-wiring-review.md) · plus a 19-agent
pre-solder freeze audit and a fresh ngspice re-verification of the front-end.

---

## ⛔ STOP — three things to do before the iron is hot

These are the irreversible ones. Because the modules are soldered down, each of these becomes a
desoldering job if you skip it.

### S1. Prove the I²S **slave** path boots — it has never run on this hardware

This is the single biggest risk in the build and it is not a wiring question.

The one recorded successful audio boot (`de2fb53`, 2026-07-10) ran the **master-mode** kernel —
it predates the clock pivot (`f524c7d`) that introduced `GDAW_I2S_SLAVE`. The log it recorded,
`I2S full duplex (TXRX): RUNNING`, does not even have the slave suffix the current kernel prints.
So **RP1 I2S1, AltFn4, slave clocking has never once executed on this Pi.** Every wire you are
about to make permanent assumes it works.

Do a temporary clip-lead hookup of the ADC first — Nano MCLK, the four I²S lines, both rails,
ground — and boot the current image. Pass criteria, both required:

```
I2S full duplex (TXRX, slave - ADC masters the bus): RUNNING
```
and the `gdawcores` stats line showing `cap` climbing by ~48,000/s between two 2-second reports.

If `cap` stays frozen at 0, **do not solder** — see D2 below, then the debug ladder.

### S2. Set the GY-PCM5102's config pads **before** the module goes down

The four config pads (FLT / DEMP / XSMT / FMT) are on the module's **underside** and ship bridged
**all-low**. XSMT low = **permanently muted DAC**. Once the module is soldered flat to perfboard
you cannot reach them.

They are 3-way pads (H island — centre — L island). Setting a pad high is **two operations**:

1. Wick away the factory **centre→L** bridge.
2. Bridge **centre→H** only. *Never bridge H to L — that shorts the module's 3.3 V to ground.*

| Pad | Set to | Meaning |
|-----|--------|---------|
| FLT | **L** (leave factory bridge) | normal filter roll-off |
| DEMP | **L** (leave factory bridge) | de-emphasis off |
| **XSMT** | **H** — wick centre→L, bridge centre→H | **UNMUTE. The #1 silent-output gotcha.** |
| FMT | **L** (leave factory bridge) | I²S format |

Verify with a meter **before mounting**: XSMT centre reads ~0 Ω to the module's 3.3 V node and
open to GND; FLT / DEMP / FMT centres each read ~0 Ω to GND. Also confirm `SCK` (a header pin, not
a pad) will be tied to GND.

> Set by **chip pin**, not by pad number, if there is any doubt: XSMT = PCM5102A pin 17,
> FMT = 16, FLT = 11, DEMP = 10. The H1L…H4L position map in `hardware-review.md:88-95` comes from
> third-party wikis, not the datasheet.

### S3. Meter the CJMCU-1808's straps and analog pads before wiring them

`adc-hookup.md:126` warns that some batches ship with wrong solder jumpers — but it buries this in
the *debug* ladder, i.e. after the board is built. Do it first, module unpowered and in your hand:

- **MD0, MD1, FMT to GND** must each read **~50 kΩ** (the chip's internal pulldowns). A reading
  near **0 Ω** means that pin is factory-bridged to ground and cannot be strapped high — that
  module will never master the bus. Reject it.
- **Analog pads (RIN / – / LIN):** the documented order is single-source and reverse-engineered.
  Ohm each of the three edge holes to a module GND header pin — **the one reading ~0 Ω is "–"**.
  The outer two are the inputs. Confirm which outer pad is LIN before soldering the front-end to it.

---

## 1. Nets — the complete solder list

Seven wires to the Pi, and that is all that touches J8.

| Net | From | To | Notes |
|-----|------|----|----|
| `MCLK` | Nano **D2** → **33 Ω** | ADC **SCK** | 12.288 MHz. Twist with its own ground return. |
| `BCLK` | ADC **BCK** → **100 Ω** | Pi **J8 p12** (GPIO18) **and** DAC **BCK** | 3.072 MHz, **ADC drives**. Two wires out of the ADC pin — star at the source, do not daisy-chain. |
| `LRCLK` | ADC **LRC** → **100 Ω** | Pi **J8 p35** (GPIO19) **and** DAC **LCK** | 48 kHz, **ADC drives**. Same star-at-source rule. |
| `CAPDAT` | ADC **OUT** | Pi **J8 p38** (GPIO20) | capture data |
| `PLAYDAT` | Pi **J8 p40** (GPIO21) | DAC **DIN** | playback data |
| `+5V_RAW` | Pi **J8 p2** | DAC **VIN** | **direct — never through the 10 Ω** |
| `+5V_ADC` | Pi **J8 p2** → **10 Ω** | ADC **+5V** | ADC analog only. See §2. |
| `+3V3` | Pi **J8 p1** | ADC **3.3** pin, ADC MD0, ADC MD1 | direct wire, no regulator, no series R |
| `GND` | star island | Pi **J8 p6** | one wire. See §3. |

**Straps** (set before power-on; tie directly, no series resistors — the internal 50 kΩ pulldowns
mean any series R above ~20 kΩ reads LOW = slave = dead bus; each strapped pin draws ~66 µA):

- ADC **MD0 → +3V3**, **MD1 → +3V3** (master, 256 fs), **FMT → GND** (I²S).
- DAC **SCK pin → GND** (hard tie, shortest link — internal PLL locks off BCK).
- DAC pads per **S2**.

### Why the 100 Ω on BCK and LRCK

`circle/lib/sound/i2ssoundbasedevice-rp1.cpp:164-165` selects the GPIO mode from a single boolean:
`m_bSlave ? GPIOModeAlternateFunction4 : GPIOModeAlternateFunction2`. In **non**-slave mode
GPIO18/19 become **push-pull outputs** — driving straight into the PCM1808's own push-pull clock
outputs. One wrong SD card, one stale kernel image, one undefined `GDAW_I2S_SLAVE`, and you have
two CMOS drivers fighting on a permanent board.

100 Ω at the **ADC pin end** caps that contention at ~16 mA per line. Cost: ~2.5 ns RC into ~25 pF
of pad and wire, against a 325 ns bit period. It buys damage protection for nothing.

---

## 2. Power — the 5 V rail splits into two branches

This is the change most likely to be got wrong, because every older doc draws **one** shared 5 V rail.

```
Pi J8 p2 (5 V) ──┬──────────────────────────────────► DAC VIN        (branch A, ~25 mA, RAW)
                 │
                 └── 10 Ω ──┬── node A ─────────────► ADC "+5V"      (branch B, ~9 mA, FILTERED)
                            │
                            ├── 470 µF / 16 V ──► star   (watch polarity)
                            └── 0.1 µF X7R ─────► star   (both physically AT the ADC's +5V pin)

Pi J8 p1 (3.3 V) ───────────┬──────────────────────► ADC "3.3" (VDD)
                            ├──────────────────────► ADC MD0, MD1 straps
                            └── 0.1 µF X7R ────────► star (at the ADC's 3.3 pin, leads < 10 mm)
```

**Never run the DAC through the 10 Ω.** Both loads together are 34 mA → 340 mV drop → ADC VCC
**4.41 V** on a −5 % sagged header, **below the PCM1808's 4.5 V minimum**. Worse, the DAC's
charge-pump current pulses would land on the rail the ADC uses as its *ratiometric* reference
(FS = 0.6·VCC), which is the exact mechanism the filter exists to stop.

Branch B alone: 9 mA × 10 Ω = **90 mV**, VCC ≈ 4.91 V. Comfortable.

**The filter is unconditional.** `adc-hookup.md:74` says "before adding parts, measure" — that
gate is unrunnable here, because the silence-FFT it calls for needs a working capture path, which
needs the soldered board. Build it in; run the FFT afterwards as verification, not as a gate.

**No ferrite, no LDO.** The Würth 74275022 beads are optional RF hygiene only — a ferrite alone is
≈0 dB in-band against this mechanism. The "optional local 3.3 V LDO" in `hardware-review.md` and
`build-plan.md:39` is **closed**: ADC VDD runs from J8 p1 direct. The "not header 3V3" rule in
those docs applies to the **analog** rail only and lost its qualifier.

---

## 3. Ground — build a real star island

Every doc names "Pi J8 pin 6" as the star. That is an *electrical* node, not a *physical* one, and
up to eight conductors are ordained to land on it. You cannot solder eight wires to one 0.1″
header pin.

**Build the star as a copper island on the board:** a length of bare 18–20 AWG solid wire (or
copper tape) bridging one contiguous perfboard row, ~25–40 mm, sited next to the ADC. Everything
returns there. **Exactly one** wire leaves the island for Pi J8 p6, kept short.

Tie order along the island, quietest first, closest to the ADC:

1. ADC `–` input-reference pad, and the ADC's **left** GND header pin
2. ADC decoupling returns (the 470 µF, both 0.1 µF)
3. ADC **right** GND header pin → this one is the origin of the return conductor bundled with the
   BCK/LRC/OUT wires *(use **both** ADC GND pins — the old "either one, one is enough" note
   predates the paired-return mandate and contradicts it)*
4. MCLK paired return (twisted with the D2 wire), and the Nano's second, separate ground
5. DAC GND
6. CopperSound 9 V-domain ground
7. → the single spoke to Pi J8 p6

Paired/twisted HF returns beside MCLK and the clock bundle are **required** and are *not*
forbidden "ground islands" — that wording is retired. The one-point rule is a **DC** rule.

---

## 4. Front-end — 9 V, values unchanged, one part added

**Keep every existing value.** `R1 = 10 k`, `R2 = 12 k` (VBIAS 4.91 V), `Rbias = 1 M`,
`Cin = 0.1 µF`, `Rg = 10 k`, `Rf = 10 k + trim`, `Cout = 1 µF`, **`Rs = 1 k`**, `Ca = 1 nF`.

**Add `C_byp` = 100 µF electrolytic ∥ 0.1 µF ceramic at U1 pin 8 → GND.** Mandated since the
2026-07-22 review but missing from the schematic sheet until today. Without it, VBIAS ripple is
*inverting-amplified* by Rf/Rg (−16.9 dB at max trim ≈ −66 dBFS of hum).

> **`Rs` stays 1 kΩ.** The "1 k → 1.5 k" upgrade was floated as optional and was **rejected on
> review**: 1 k is stated identically in nine places across every wire-from doc, both schematic
> sheets, and both SPICE decks. Changing only some of them desynchronizes the set for a margin
> improvement nobody needs.

### The headroom finding — set your trim accordingly

Re-verified in ngspice today (tanh macromodel validated against the repo's own deck to 5 significant
figures). **On 9 V there is no bias point that satisfies both constraints:**

| VBIAS | Output swing (guaranteed-min part) | JFET input CM floor |
|-------|-----------------------------------|---------------------|
| ≥ 4.5 V required | ≤ 4.5 V required | — |

The window is exactly zero wide. The as-built 4.91 V deliberately favours the **input common-mode**,
and that is the right call — CM violation on a TL07x means **phase inversion**, a far nastier
failure than clipping. Do **not** "fix" this by moving to 10 k/10 k.

The cost, measured:

| Part | Peak before op-amp clips | Usable level at the ADC |
|------|--------------------------|--------------------------|
| typical (swings 1.5 V from rail) | 6.41 V — no clip | full scale reachable |
| **guaranteed-min** (3.0 V from rail) | **6.00 V — clips** | **−2.9 dBFS**, positive peaks only |

**So: set the gain trim so hard strums peak at −6 dBFS.** You have comfortable margin there on any
part, and you should be tracking at −6 dBFS anyway. The docs' old unqualified "no clip even at
1.6 Vpp overdrive" was a *typical*-part result mislabeled as worst-case.

*(If a 12 V supply ever becomes available: change R2 12 k → 10 k for a 6.0 V mid-rail and every
constraint passes with 1.5 V margin, at zero cost in level. One resistor.)*

---

## 5. Nano ESP32 — reflash before mounting

Two firmware changes are already applied in [`esp32-mclk/esp32-mclk.ino`](../esp32-mclk/esp32-mclk.ino).
**Flash this version before the board goes in.**

1. `StartMclk()` moved to the **first** line of `setup()`. It previously ran after
   `Serial.begin()` + `delay(2000)`, leaving a **2-second dead-MCLK window on every reset,
   brownout and USB re-enumeration** — and interrupting SCKI auto-powers-down the PCM1808.
2. The two `ESP_ERROR_CHECK`s in `loop()` are now soft errors. `ESP_ERROR_CHECK` **panics and
   reboots**, which would drop the clock for the whole rig because a *diagnostic* counter hiccuped.

**Keep the D2→D3 link permanently** (tap on the Nano side of the 33 Ω, so the counter reads the
source while the resistor still damps the run to the ADC). `adc-hookup.md:99` says remove it —
that is right for a bench jumper and wrong here. On a soldered board with no scope, the sketch's
1 Hz `Measured MCLK: 12.2880 MHz` print is your **only** continuous proof the clock root is alive.

The Nano keeps its own USB-C supply. Mount it with the USB-C port overhanging the board edge and
put a cable-tie anchor behind it — that connector is now a permanent part of the system.

---

## 6. Parts you do not have yet

Verified against `parts-order.md` — **none of these are in any BOM in the repo.** A solder session
is one sitting; a missing 12 k stops it.

| Qty | Part | For |
|----:|------|-----|
| 10 | **12 kΩ** ¼ W metal film | **R2, the as-built VBIAS divider.** `parts-order.md:55` runs 1 M / 10 k / 5.6 k / 3.9 k / 1 k / 470 Ω — 12 k was never ordered |
| 10 | **100 Ω** ¼ W metal film | BCK + LRCK contention protection (§1) |
| 10 | **33 Ω** ¼ W metal film | Nano D2 series (§1) |
| 10 | **10 Ω** ¼ W metal film | analog-5 V RC (§2) |
| 4 | **470 µF / 16 V** low-ESR radial | analog-5 V RC bulk (§2) |
| 4 | **100 µF / 16 V** radial | `C_byp` at TL072 pin 8 (§4) |
| 20 | **0.1 µF X7R** ceramic | ADC VCC + VDD decoupling, `C_byp` HF half |
| 1 | 2×20 female header **or** IDC socket + ribbon | the only Pi J8 interface |
| 1 | 3.5 mm or ¼″ PCB-mount jack | front-end cable lands here |
| — | FR4 perfboard | prefer a **Perma-Proto-style board with etched power rails** — the rails give you the §3 star island for free and transfer your breadboard layout 1:1 |

---

## 7. Design in test points now

In slave mode `RunI2S()` has **no failure path** — its only `return FALSE` sits inside
`if (!m_bSlave)`. `Start()` returns TRUE with the ADC unpowered, unclocked, or miswired. At least
six distinct faults all produce the identical symptom: `cap` frozen at 0.

Solder an **8-way 0.1″ test header** before you mount the modules, tapping **SCKI, BCK, LRCK,
DOUT, DIN, +3V3, +5V, GND**. Put the SCKI/BCK/LRCK taps on the **ADC side** of the series
resistors so you measure what the chip actually sees. This is the cheapest thing in the build and
the difference between a five-minute diagnosis and pulling a soldered module.

---

## 8. Build order

1. **S1, S2, S3** above. Do not skip.
2. Perfboard: 2×20 header, then the §3 ground island, then the two 5 V branches and the 3.3 V wire.
3. Test header (§7).
4. ADC down: straps first (MD0/MD1 → 3V3, FMT → GND), then decoupling at its pins, then the four
   I²S lines with their series resistors and paired returns.
5. DAC down (pads already set per S2): VIN from raw 5 V, SCK → GND, BCK/LCK/DIN.
6. Front-end: `C_byp` at U1 pin 8, then Rs + Ca **at the ADC LIN pad**, not on the pedal board.
7. Nano: reflashed per §5, 33 Ω in the D2 lead, two ground wires, D2→D3 link kept.

**Power-up order: Nano → Pi → Gator 9 V. Power down in reverse.** Kill all three before touching
any wire. Nano-first is verified safe (PCM1808 SCK/MD/FMT inputs are rated −0.3…+6.5 V independent
of VDD).

**First boot expectation:** `I2S full duplex (TXRX, slave - ADC masters the bus): RUNNING`, `cap`
climbing ~48,000/s, `starve`/`drop`/`laps` at 0, `peak %` responding when you play.

---

## 9. If capture is dead — diagnosis step 0

Before suspecting a solder joint, check this. `write32(m_ulBase + CCR, 0x10)` — the register that
sets the I²S word-select size to 32 bits — sits **inside `if (!m_bSlave)`** at
`i2ssoundbasedevice-rp1.cpp:373-381`. In the as-built slave configuration **it is never written**,
so CCR keeps its reset value.

If the bus clocks are confirmed good at the test points (SCKI 12.288 MHz, BCK 3.072 MHz, LRCK
48 kHz) but `cap` is frozen or the data is misaligned, move that `write32` outside the `if` block,
rebuild and retry. It is harmless in master mode — the same value the master path already writes.

Then the normal ladder: SCKI wire → Nano power → MD0/MD1 straps (both must be HIGH or the ADC
never masters the bus) → LIN wiring → front-end power → `–` pad grounded.

---

*Frozen by Claude Code, 2026-07-28. Front-end numbers re-verified in ngspice-46 against a
macromodel validated to the repo's own published results. Firmware claims verified against the
Circle and `esp32-mclk` sources and the git record.*
