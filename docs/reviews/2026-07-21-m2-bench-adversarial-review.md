# M2 Bench — Adversarial Review (2026-07-21)

Adversarial review of the **as-built M2 bench package** (post clock-pivot `f524c7d`, sheets redrawn
`40a6248`): the wiring diagram, the two schematic sheets, `adc-hookup.md`, and the supporting docs the
builder wires from. Method: a 6-lens multi-agent finder fan-out (clocking · analog · power · firmware
contract · doc-consistency · bench-procedure) → per-finding refuting verifiers → completeness critic.

## ⚠️ Read this first — the run was incomplete

The session token limit was hit mid-run (resets nightly). **Only 3 of 6 finder lenses completed** —
`power`, `consistency`, `procedure`. The **`clocking`, `analog`, and `firmware-contract` lenses died before
returning**, as did the completeness critic and **most of the refuting verifiers**. Consequences:

- **Two findings are fully adversarially verified** (3/3 refuters voted "stands", high confidence):
  the `hardware-review.md` and `breadboard-layout.svg` stale-strap hazards. Treat these as **Confirmed**.
- **Every other finding "survived" by default** because its refuters never ran. They are **Plausible /
  unverified** — a competent finder raised them, but nothing adversarial has yet tried to kill them. Do not
  treat them as confirmed; they are the *input* to verification, not its output.
- **Three whole lenses are missing.** The dedicated clocking, analog-front-end, and firmware-contract
  sweeps produced nothing. The `power` lens happened to pick up the headline clock-jitter and rail-noise
  issues anyway, but the firmware/hardware-contract questions (what the M3 engine does when BCK/LRC stop,
  slave-mode MSB alignment, GPIO contention if an old master-mode image boots) are **uncovered** — a
  re-run when the limit resets should prioritize them.

A clean re-run is cached and one command away: resume `wf_18ba93c0-249` after the limit resets.

## What was already fixed in this commit

The doc-consistency lens proved today's "stale-doc purge" (`40a6248`) was **incomplete** — it fixed
`breadboard-build.md`/`schematic.md`/`esp32-mclk` but missed several files that still tell a hand-wirer to
build the **old Pi-as-master topology**. Those are corrected here:

| File | Was | Now |
|------|-----|-----|
| `docs/hardware-review.md` | strap table MD0/MD1→GND (slave), clock table sourced from Pi GPIO18/19, BOM "Pi I²S0 master" — **no banner**, yet README bills it as the authoritative netlist | pivot banner + corrected strap/direction/BOM tables (ADC master, Pi I²S1 slave) |
| `docs/breadboard-layout.svg` | placement graphic labels MD0/MD1/FMT→GND and MCLK←GPIO4 | visible REV-A / PRE-PIVOT banner overlay |
| `kicad/README.md` | `gen.py` still nets MCLK from GPIO4 + grounds MD0/MD1; README asserted "same topology either way" | rev-A banner: learning vehicle only, do not fab until `gen.py` re-generated |
| `planning/build-plan.md` | risk register credits "ESP32-S3 APLL" (the repo disproved this); task 2.5 "single I²S0 instance" | "PLL_160M via i2s_std, no APLL"; "I²S1 (slave)" |
| `docs/parts-order.md`, `docs/retro-deck-design.md` | "you'll want a scope for Spike B" / "run alongside Spike B" — Spike B closed 2026-07-09 | scope marked optional (jitter char only); Spike B noted closed |

**Net effect:** every doc that could have led the builder to wire the ADC as a slave (dead bus) or run MCLK
off a dead Pi pin now either carries the correct straps or a visible stale banner.

## Findings that need bench work or Claude Science (not yet fixed)

These are **Plausible / unverified**. The physics ones (`science_candidate`) are handed to Claude Science
via [`SCIENCE-PROMPT_guitardawliteos-audio-bench.md`](../../../shared-with-claude-team/SCIENCE-PROMPT_guitardawliteos-audio-bench.md);
none block first-light (a clean tone in→out), but several bound the noise floor and should be settled before
committing rev-B silicon.

### MAJOR — physics / noise floor (→ Science)
- **P1 · Analog-5 V has no filtering post-pivot.** The as-built card feeds the PCM1808 analog VCC straight
  from Pi J8 pin 2 — the ferrite + 10 µF + 0.1 µF network `hardware-review.md` mandated (and `parts-order.md`
  bought, Würth 74275022) never made it onto the as-built sheets. Because the PCM1808 full-scale is
  *ratiometric* (FS = 0.6·VCC, VREF = 0.5·VCC), rail ripple is **multiplicative gain modulation** of every
  sample, and TI publishes no VCC PSRR. → restore the filter; Science to bound how much couples in-band.
- **P2 · MCLK is a 13 + 1/48 fractional-divided clock.** The ESP32-S3 has no APLL; 160 MHz / 12.288 MHz is
  non-integer, so the I²S divider dithers — a deterministic ~6.25 ns p-p phase sawtooth at exactly
  fs_mod/24 = 256 kHz. The 1 s PCNT test measures *average* frequency and is blind to it **by construction**,
  and the "jitter doesn't matter" waiver in `claim-verification.md` predates the pivot that made this MCLK
  the modulator clock of the whole audio domain. → Science: do the PM sidebands + coherent decimation images
  stay below −100 dBFS in-band? If not, rev-B needs a real oscillator (BOM change).
- **P3 · Star-only grounding routes 12/3 MHz return current through the shared star spokes**, in series with
  the ADC's own "–" analog input reference, and the docs' "no ground islands" rule forbids the standard fix
  (a paired HF return per clock line). → Science: is a captured-silence FFT a valid no-scope proxy, and what
  fix (paired returns + ~33 Ω at D2) is warranted while keeping the DC star intact?

### MAJOR — bench procedure / safety (mostly doc edits, some → Science)
- **Q1 · The debug ladder sends hands to a live board** with no "kill all three supplies before touching
  wires" rule; the pin-1/pin-2 adjacency means a one-row slip puts 5 V on the 4 V-abs-max VDD pin.
- **Q2 · Power order injects 12.288 MHz into the *unpowered* PCM1808's SCKI** every boot (Nano-first, Pi
  supplies the ADC rails). Safe **iff** SCKI abs-max is VDD-independent — a datasheet question nobody checked;
  the reverse order (Pi first) loses nothing functionally. → Science: SLES177B abs-max table.
- **Q3 · The single star-ground jumper is load-bearing for the Nano→ADC clock line** — if it falls out or
  the Nano is replugged signal-first, MCLK's return + inter-supply Y-cap leakage flow through SCKI's clamp.
  → give the Nano two ground wires; Science to bound the worst-case injection current.

### MINOR — mostly cheap doc/procedure additions
- **M1 · TL072 9 V front-end has no supply bypass** in the as-built docs; the VBIAS divider passes 120 Hz
  ripple at ~−37.5 dB which the ×11-trim then re-amplifies, and a part-discharged 9 V battery drops VBIAS
  into TL07x phase-reversal territory. → add 100 µF + 0.1 µF at U1 pin 8; drop or gate the battery option.
  (→ Science for the hum-vs-trim number.)
- **M2 · Cable sleeve + explicit ground wire = a deliberate ground loop.** Carry ground on one; note the
  sleeve-lift option; order the star so "–" and CopperSound return tie closest to pin 6.
- **M3 · Order-of-operations omits the Gator 9 V domain entirely** and has no teardown order. → "…then the
  Pi, then the Gator 9 V — power down in reverse."
- **M4 · XSMT hard-tied high defeats soft-mute** → the DAC pops on every rail ramp / clock glitch, and the
  debug ladder makes clock glitches a routine bench event. → bench note: amp/phones last, volume down,
  disconnect before any power-cycle or clock-wire work.
- **M5 · The 5 V-on-3.3-pin warning is visual-only.** → add a "meter the two rail rows before landing the
  module's power leads" step; name the pin-1/pin-2 adjacency explicitly.

## Suggested order of operations

1. **(done)** stale-doc hazards — fixed in this commit.
2. Fold the cheap procedure/doc additions (Q1, M2–M5) into `adc-hookup.md` — no analysis needed.
3. Restore the analog-5 V filter (P1) on the sheets — it was always in the plan.
4. Hand P1–P3, Q2–Q3, M1 to **Claude Science** for first-principles bounds (prompt filed).
5. **Re-run the review** (`clocking`/`analog`/`firmware` lenses + verifiers) when the token limit resets,
   to confirm the Plausible findings and cover the missing firmware-contract lens.

*Findings raised by Fable-5 finder agents; two verified 3/3 by independent refuters; the rest unverified due
to the token-limit cutoff. Nothing here blocks first-light.*

---

## Update 2026-07-22 — Claude Science verdicts + Claude Code cross-check

The physics questions (A1–A4, B1–B3) went to Claude Science; the reply is logged in
`shared-with-claude-team/SHARED_MEMORY.md`. Claude Code independently reproduced the load-bearing ones
(house rule: Science's numbers are Indicative until reproduced). Net result — **first light is unaffected;
one confirmed noise fix, one refuted rev-B cost, and one correction *to* Science.**

| # | Science verdict | Code cross-check | Action taken |
|---|---|---|---|
| **A1** ratiometric rail coupling | **Confirmed real** (−52 dBc/50 mVpp; ferrite alone ≈0 dB in-band; RC needed) — but measure first | Arithmetic reproduced (FS RMS 1.06 V → −99 dBFS = 11.9 µVrms ✓) | `adc-hookup.md`: restore filter as an **RC (10 Ω + 470 µF ∥ 0.1 µF)**, gated on a silence-FFT A/B |
| **A2** fractional-N MCLK jitter | **Refuted as SNR threat** — deterministic 6.25 ns sawtooth, sidebands at ±256 kHz fold to −134 dBFS in-band, 35 dB under the floor | Reasoning sound (periodic ⇒ discrete out-of-band lines, not a broadband floor; 256 kHz vs 48 kHz gcd = 16 kHz matches the fold claim). Accepted `Indicative` pending the borrowed-crystal falsifier | **No rev-B oscillator for SNR.** PLL_160M stands. `claim-verification.md` jitter note upheld |
| **A3** HF ground bounce | Bounce real but folds out of band; in-band residue negligible | Plausible; consistent with 12.288 MHz = 256·fs | `adc-hookup.md`: paired HF return + 33 Ω at D2 as **EMI hygiene**, docs stop calling paired returns "islands" |
| **A4** TL072 rail rejection | **Downgraded** to "common-mode ×1, ~−87 dBFS" | ❌ **CORRECTED.** ngspice PSRR of the actual deck: rail→ADC = **−31.7 dB (×3) / −16.9 dB (max trim)** — VBIAS ripple is *inverting-amplified* by Rf/Rg, because Cin shunts the + input to the low-Z guitar at 120 Hz. Science's ×1 holds only for an **open** input. The **finder's −66 dBFS was right.** | `tl072-frontend.md`: add **100 µF ∥ 0.1 µF at U1 pin 8** (more warranted than the downgrade implied); gate battery **≥ 8.3 V** |
| **B1** SCKI abs-max | Refuted — SLES177B §6.1 = −0.3…+6.5 V, VDD-independent; Nano-first safe | Datasheet not on disk → **relayed, `Indicative` to Code**; conclusion is "no change," low risk | `adc-hookup.md`: note Nano-first is verified safe |
| **B2** ground-open injection | Benign (Y-cap 0.5 mA ≪ ±10 mA clamp) | Relayed; the two-ground-wire mitigation is free | folded into the A3 note (two Nano grounds) |
| **B3** TL072 into unpowered ADC | Within spec (7.5 mA vs ±10 mA, 25 % margin) | Relayed | recommend **Rs 1 k → 1.5 k** (2× margin, corner 159→106 kHz, no audio penalty) — optional |

**The one that matters:** the cross-check caught Science's A4 downgrade — the ×1 common-mode assumption is
wrong for a plugged-in guitar (ngspice: the hum is 21 dB worse at max trim than Science estimated, matching
the original finder). This is the Code⇄Science pairing working as intended in both directions. Handed back to
Science as a note; the fix (bypass cap) was already the right call and is now better justified.

**Still open (→ Michael):** A1's real magnitude needs a **silence-FFT of the Pi 5 V rail** (the one thing
no-scope analysis can't close); if a **12.288 MHz canned oscillator** can be borrowed, an A/B against it is
the clean falsifier for both A2 and A3.
