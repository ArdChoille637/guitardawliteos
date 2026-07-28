# Schematics & Wiring — Adversarial Review (2026-07-28)

Review of the drawing set a builder actually wires from: `wiring-diagram.svg`,
`codec-power-schematic.svg`, `tl072-frontend-schematic.svg`, `frontend-schematic.svg`,
`schematic.md`, `adc-hookup.md`, `breadboard-build.md`, `hardware-review.md`, the ngspice decks,
and the `kicad/` netlist. Method: 6 parallel finder lenses (wiring · power/ground · analog front-end ·
KiCad netlist · buildable schematic · clocking & logic levels) → merge/triage → one adversarial
refuting verifier per surviving finding.

**This run also closes the gap left by [2026-07-21](2026-07-21-m2-bench-adversarial-review.md)**, which
died on a token limit with the `clocking`, `analog`, and `firmware-contract` lenses never run and most
verifiers never executed. All three of those lenses ran to completion here, and every finding below
carries a real verifier verdict.

**Result: 8 findings triaged from 30 raw · 6 CONFIRMED · 2 REFUTED.** Nothing here blocks first light.
No finding is a wiring error: the post-pivot topology (Nano MCLK → ADC master → Pi slave + DAC), the
pin map, and the strap tables are consistent across every as-built sheet. The defects are **stale
guidance in the surrounding docs and two fixes that landed in prose but never on the drawings.**

---

## CONFIRMED

### 1. MAJOR — `C_byp` is missing from the TL072 sheet (regression from the 2026-07-22 fix)

[`tl072-frontend.md:32-52`](../tl072-frontend.md) mandates **`C_byp` = 100 µF ∥ 0.1 µF at U1 pin 8 → GND**,
added because the ngspice rail→ADC transfer is **−16.9 dB at max trim** (≈ −66 dBFS hum). That fix landed
in the prose only: `grep -i byp docs/tl072-frontend-schematic.svg` returns **zero hits**, and the sheet's
`+9V` node (line 71) runs straight into R1 10 k with nothing to ground. The sheet is indexed by
`schematic.md:9` as the as-built bench front-end — it is what gets wired.

Sharpening the finding: the 9 V sheet is not cap-less (it draws C1 10 µF on the VBIAS divider, lines 74-76),
which makes the absent rail cap read as deliberate rather than as an omission. The 5 V canonical sheet
*does* carry its bypass note (`frontend-schematic.svg:76`).

**Fix:** draw `C_byp` from `+9V` to GND on the SVG, or at minimum add it to the notes block beside the pin-8 line.

### 2. MAJOR — `build-plan.md:39` sends the builder to an LDO that does not exist in this build

The Milestone-2 preamble says power off **"header 5 V + local LDO (not header 3V3)"**. Milestone 2 *is* the
CJMCU-1808 breadboard bench — and that module has **no onboard regulator**. Every as-built doc requires
header **pin 1**: `adc-hookup.md:63` ("both required"), `breadboard-build.md:31`, `wiring-diagram.svg:71`,
and `hardware-review.md:46`/`:50` ("by default VDD runs from header 3V3 pin 1 directly; a local 3.3 V LDO
off 5 V is an optional clean-rail upgrade").

A builder trusting the M2 summary skips the pin-1 wire — **ADC digital VDD unpowered = dead capture** — and
hunts a part the bench BOM doesn't contain. The original rule was **analog-rail-only**; the summary dropped
the qualifier. `hardware-review.md` compounds it: headline 6 (line 20) says "regulate locally" and the risk
register (line 149) says "5 V + local LDO", while its own power tree shows no regulator on the analog path.

**Fix:** rewrite line 39 to "analog off header 5 V (never header 3V3 for *analog*); digital VDD from header
pin 1 direct; no onboard regulator on the CJMCU breakout." Verifier's correction to carry: the only LDO in
the project is `U4 AP2112K-3.3` on the **KiCad bare-chip board**, and there it is a **digital**-rail
regulator, not an analog one.

### 3. MAJOR — the TL072 SPICE deck labels a *typical* swing as "worst-case", and that inverts its own conclusion

[`sim/frontend-tl072-9v.cir:7`](../sim/frontend-tl072-9v.cir) reads `VMARGIN = 1.5 (TL072 worst-case output
swing limit from each rail)`. For the classic TL07xC (±15 V, RL ≥ 10 k) **VOM is typ ±13.5 V / min ±12 V** —
so 1.5 V from the rail is the **typical** swing and 3 V from the rail is the guaranteed minimum. The repo
demonstrably knows the difference: the 5 V sibling deck re-runs at `VMARGIN=0.6` explicitly "to model
OPA1662 worst-case" (`frontend.cir:7`, documented at `schematic.md:85`). The TL072 deck claims that rigor
in a comment but never performs the run.

The consequence is load-bearing. At a 3 V guaranteed-min margin the op-amp clips at feo = 6.00 V while the
ADC's 4.0 V full scale sits at feo = 6.434 V — so **the op-amp clips first, on positive peaks only**,
inverting the deck's own assertion at lines 75-77 ("the ADC's 1.0–4.0 V full-scale clips first"), which
`tl072-frontend.md:63` repeats as a pass. A builder would expect symmetric digital clipping and hear
one-sided analog clipping.

Verifier's narrowing, all worth keeping: **only the positive peak is at risk** (vmin = 3.406 V clears even a
3 V margin) — this is an asymmetry created by VBIAS = 0.545·V_rail, not a general headroom shortfall. The
≥ 8.3 V battery gate was derived from the input common-mode floor alone with no output-headroom term
(at 8.3 V, headroom is 2.28 V). And `cmmax` = 5.408 V is **measured but has no pass bar anywhere** — the
docs guard only the ≥ 4.0 V CM floor. Not a hardware hazard: Rf is "10 k + trim", so gain can be trimmed down.

**Open (needs the datasheet, not the repo):** the classic TL072 datasheet publishes no guaranteed swing at
9 V single-supply, and the real load here is light (~20 k), which helps. Whether a worst-case part actually
soft-clips at 9 V is genuinely unresolved.

**Fix:** re-run the transient at `VMARGIN=3`, relabel line 7 as "typical (VOM typ, RL ≥ 10 k)", document the
resulting margin honestly, and add an upper-CM pass bar beside the existing ≥ 4.0 V floor.

### 4. MINOR — both SVG sheets lack the adjudicated A3/B2 mitigations and still print the retired "no islands" wording

The 2026-07-21 review adjudicated A3/B2 with the fix "paired HF return + 33 Ω at D2 … two Nano grounds …
docs stop calling paired returns islands". `adc-hookup.md:79-89` complies. **Neither drawing does:**

| | `wiring-diagram.svg` | `codec-power-schematic.svg` |
|---|---|---|
| Nano grounds | one (`:23`) | one (`:14`) |
| 33 Ω at D2 | absent (grep for `33` hits only coordinates) | absent |
| paired/twisted HF return | absent | absent |
| retired wording | `:71` "one rail, no islands" | `:39` "one point, no ground islands" |

Both sheets are named as wire-from sources by other docs, so a builder working from either alone omits all
three mitigations — and would actively refuse the second Nano ground wire `adc-hookup.md` mandates.
Severity stays minor: Science graded these EMI hygiene and robustness, not a measured-noise or damage risk.

**Fix:** on both sheets, "D2 —33R→ MCLK; **two** GND wires (one twisted with MCLK) → star", a paired-return
note on the BCK/LRC/OUT bundle, and reword the star note to "one **DC** point (paired HF clock returns OK —
not islands)".

### 5. MINOR — `hardware-review.md:100` names a star-ground location that doesn't resolve to pin 6

"…single return to a Pi GND pin **physically near the I²S pins**" — but the I²S pins are J8 12/35/38/40,
whose nearest header grounds are **14/34/39**, while pin 6 sits beside the power pins. Every other as-built
doc names pin 6 explicitly (`adc-hookup.md:66`, `breadboard-build.md:32`, `schematic.md:46`,
`codec-power-schematic.svg:38`, `wiring-diagram.svg:72`).

**Downgraded from major** — the verifier refuted the larger half of the original claim, correctly:
line 76's "6, 9, 14, 20, 25, 30, 34, 39" is simply the complete set of GND pins on a 40-pin header
(an enumeration in a *Pi pin* column, pin 6 bolded and listed first), and both line 47 and line 100 say
"single-point"/"single return" — the doc never instructs landing grounds on multiple pins, so there is no
ground-loop hazard. The banner was also not overstated: line 8 scopes "still current" to the
analog/power/front-end *rationale*, and lines 9-10 hand wiring authority to `adc-hookup.md`.

**Fix:** rewrite line 100 to name pin 6; optionally caption line 76 as "header GND pins available; star at pin 6".

### 6. MINOR — the superseded ferrite-only analog filter is still prescribed by two docs and carried into `gen.py`

The adjudicated A1 verdict is that **a ferrite alone is ≈0 dB in-band** against the PCM1808's ratiometric
rail coupling, and the fix is a measurement-gated **RC (≈10 Ω + 470 µF ∥ 0.1 µF)**. That landed in
`adc-hookup.md:69-77` only. Still prescribing the old network as current: `hardware-review.md:43` (power
tree), `:49` ("clean, stable 5 V (ferrite + bulk + 0.1 µF)"), `:74` ("VCC pin 3 via ferrite"), and
`schematic.md:29`/`:44`/`:90` (FB1). `kicad/gen.py:51`/`:80` carries FB1 into any rev-B regeneration.
`codec-power-schematic.svg:36` nets analog VCC straight to +5 V with no filter and no pointer to the
pending measurement gate.

**Fix:** update those rows to the A1 verdict or annotate them "superseded — see `adc-hookup.md`"; add one
note line to the codec sheet pointing at the silence-FFT gate; put the FB1-vs-RC decision on the rev-B worklist.

---

## REFUTED

### R1. `schematic.md`'s build order is *not* a live rev-A hazard

The finding claimed step 3 had been refreshed post-pivot while step 4 still said "strap U2
(MD0/MD1/FMT→GND)", so the line-5 banner no longer reads as covering the build list. **`git blame -L 88,95`
kills it:** step 3 is commit `c237c24` (2026-07-09 18:25) — the Spike-B resolution, which *predates* the
pivot commit `f524c7d` (2026-07-10). `git diff c237c24 40a6248 -- docs/schematic.md` shows the post-pivot
commit touched only the banner and the Sheets bullets (5 insertions, 2 deletions). The entire numbered
list is uniformly pre-pivot rev-A behind a banner whose wording ("straps … **in this doc** are stale …
Wire from adc-hookup.md … instead") covers step 4 exactly. The "one maintained step" premise doesn't exist.

The secondary claim — that "the PCM1808 only needs fs-sync, not phase-lock" is a refuted rationale stated as
current fact — also fails *as a `schematic.md` defect*: it is verbatim `[CORR-4]`/E2 wording that still lives
un-bannered in `claim-verification.md:40`/`:79` and `build-plan.md:32`/`:114`, and its operative half is
*more* true post-pivot. If that wording is stale, it's stale over there, at info grade.

### R2. The DAC 5 V VIN "contradiction" — my error, not the repo's

A finding claimed every doc powers the GY-PCM5102 from 5 V while the as-built record says 3.3 V. **The
as-built record it was checking against was my own briefing text, and I wrote it wrong** — I compressed
`build-plan.md:54` ("Power per module (3.3 V)") into "3.3 V module power" and handed that to the reviewers
as ground truth. The repo is consistent: 5 V → VIN → the module's onboard LDO → 3.3 V internally, stated
in one line at `hardware-review.md:45` ("PCM5102A module VIN (its onboard LDO → 3.3V)") and confirmed as a
hard rule at `schematic.md:103` ("U3 must be the GY-PCM5102 **module** (onboard LDO) on the 5 V VIN").

Residual info-grade nit only: the two SVGs say bare "VIN ← 5 V" without that parenthetical. Adding four
words would stop the next reader making the same mistake I did.

---

## Carried forward (below the reporting cut, real, for the rev-B worklist)

- **`gen.py` rev-A items beyond the two the `kicad/README.md` banner names:** stale `I2S0 (RP1 alt a2)`
  comment; the GPIO4 declaration coupled into ERC; no Nano MCLK-in connector; docstring still claims it
  "matches docs/schematic.md". A complete regeneration list, not new defects.
- **Prior review's M2 (cable-sleeve ground loop) was never folded into `adc-hookup.md`** as promised —
  `wiring-diagram.svg:63` still reads "explicit GND wire to star (not just cable sleeve)", which keeps both
  return paths. The 2026-07-21 review listed it open; it stays open.
- `kicad/README.md` footprint table says the 2.2 µF caps are `C_1206`; the netlist and BOM say `C_0805`.
- `frontend-schematic.svg` (5 V canonical) carries no on-sheet marker distinguishing it from the 9 V bench
  sheet — both are current, for different builds, and only `schematic.md`'s index says which is which.

## Verified clean (no findings)

- **Topology and pin map.** `wiring-diagram.svg`, `codec-power-schematic.svg`, `adc-hookup.md`,
  `breadboard-build.md`, and `build-plan.md`'s M2 table agree wire-for-wire: MCLK Nano D2 → SCKI;
  BCK → p12/GPIO18; LRC → p35/GPIO19; OUT → p38/GPIO20; DIN ← p40/GPIO21; MD0/MD1 → 3.3 V, FMT → GND;
  DAC SCK → GND, XSMT = H.
- **Clock arithmetic.** 12.288 MHz ÷ 256 = 48 kHz LRCK, ÷ 4 = 3.072 MHz BCK — stated consistently everywhere.
- **Logic levels.** No 5 V signal reaches a Pi GPIO; the ADC's digital domain is on 3.3 V VDD and its
  outputs drive the Pi at 3.3 V.
- **KiCad netlist integrity.** `python3 validate.py` → 41 components, 28 nets, 129 nodes, all refs resolve;
  both custom symbols' pin counts check out. (Structurally sound — it validates the rev-A design, which the
  README correctly bans from fab.)

*Findings by 6 Fable-5 finder lenses, merged by a triage judge, each verified by an independent refuting
verifier that read the cited files. Two findings were killed on the evidence, one of them by catching an
error I introduced in the briefing.*
