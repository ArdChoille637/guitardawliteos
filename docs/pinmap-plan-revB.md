# PLAN — Definitive rev-B pin-for-pin schematic + wiring diagram for GuitarDAWLiteOS

**Scope of this document:** the plan for producing the artwork, not the artwork. All statements below are rev B (2026-07-10 pivot: PCM1808 = I2S bus master, Pi = slave, Nano ESP32 = MCLK source) unless explicitly flagged rev A.

---

## 1. Source of truth — firm recommendation

**Two-track, one authority per track:**

- **Schematic (electrical): GENERATED from `kicad/gen.py`, after a rev-B patch.** gen.py is the only complete (41 components, 129 pin nodes, every chip pin numbered), reproducible (byte-identical regeneration, diff-verified), self-verifying (built-in `erc()`: every pin on exactly one net) artifact in the repo. No hand-drawn SVG can be machine-checked; every hand-drawn sheet in the repo has already drifted from at least one prose doc. **Do not generate today** — gen.py is entirely rev A (MCLK from J3.7/GPIO4, MD0/MD1 on GND, no Nano ESP32 component) and would produce an authoritative-looking wrong diagram.
- **Wiring diagram (physical): HAND-AUTHORED SVG, checked against the generated netlist by script.** gen.py cannot know physical facts it has no model of: the GY-PCM5102 module header order, the CJMCU-1808 edge pads, Nano header positions, the J8 physical grid, breadboard/bench geometry. Those come from the bench survey (step 1 of execution order), and the SVG's connectivity is diffed against gen.py's nets mechanically (§6), so hand-authoring costs no trust.

**Exact gen.py patch required (blocks everything downstream):**

1. Move `U2.10 (MD0)` and `U2.11 (MD1)` from the `GND` net to `+3V3`.
2. Delete the `MCLK` net's `J3.7 (GPIO4)` node; add a new component `J4` "Nano ESP32 MCLK header" (2 signal pins: D2/GPIO5, GND ×2) and net `MCLK: J4.D2 → R_mclk(33Ω) → U2.6 (SCKI)`. Add the 33 Ω series resistor as a real component (M2 review B2/A3).
3. Decide and encode the 3V3 source (open question Q1, §8): either add `J3.1` and delete U4/Clo/Cli (matches the as-built bench), or keep the AP2112K LDO and add a prominent `# VARIANT: HAT-only, bench uses J3.1` comment. gen.py currently declares "Pi 3V3 pin intentionally unused," which contradicts every doc and the bench.
4. Add variant flags for VINR (`Cr 1 µF` = bare-chip build vs. open pad = CJMCU as-built) and the +5 V VCC filter (FB1/C2/C3 vs. direct wire + conditional RC per M2-A1) — encode the bench default, comment the alternative.
5. Bump the embedded rev string from `2026-06-29` to `rev B / <patch date>`; re-run gen.py + `erc()`; regenerate `.net`/`.bom`/`.sym`; re-import to the `.kicad_pcb` or explicitly banner the PCB as stale (it already drifted: RV1 footprint absent, verified grep count 0).
6. (Cheap, high-value) **Retarget:** `gen.py`'s `erc()` already asserts pin existence (`gen.py:130`, "net {net}: {ref} has no pin {pin}") — that step is a no-op. The real gap is `kicad/validate.py`, which resolves net-node refs to components (line 39) but never validates pin *numbers*. Extend `validate.py` to cross-check `.net` node pins against gen.py's `COMP` pin dicts (or the `.kicad_sym`), and decide whether its <2-node warning becomes an error.

**Until the patch lands, the interim pin authority is `docs/adc-hookup.md`** (backed by `codec-power-schematic.svg` for direction, `hardware-review.md` for rationale) — exactly as every pivot banner in the repo designates. No pin fact may be sourced from `docs/schematic.md`'s netlist body, `breadboard-layout.svg`, the drifted `.kicad_pcb`, or `docs/pedal-wiring/fig6-pinout-reference.svg` (different rig).

---

## 2. Deliverable set

| # | File | Type | Shows |
|---|------|------|-------|
| D1 | `kicad/gen.py` (patched) + regenerated `guitardawliteos.net` / `bom.csv` / `.kicad_sym` | netlist SSOT | Rev-B electrical truth, machine-checkable |
| D2 | `docs/pinmap-revB.csv` | data | The §3 table as machine-readable CSV (net, from-refdes.pin, to-refdes.pin, J8 phys, GPIO, direction, domain, status). Consumed by the check script (§6) and by both drawings. Generated from gen.py's NETS dict plus a physical-pin overlay file. |
| D3 | `docs/system-schematic-revB.svg` | **schematic** — refdes + pin numbers, no physical placement | The one sheet that does not exist today: the whole system electrically on one page — TL072 9 V front-end domain, PCM1808 with all 14 pins, GY-PCM5102 (pad names + chip pins where verified), Pi J8 as a connector symbol, Nano ESP32 as a connector symbol, all straps, all decoupling, **drive-direction arrows on every I2S net**, power-domain boxes (9 V / 5 V / 3V3 / 3.3 V-logic). Generated/traced from D1+D2, not freehand. |
| D4 | `docs/wiring-diagram.svg` (updated in place, not duplicated) | **wiring diagram** — physical, connector-by-connector | Already rev-B; the update adds what the M2 review and gap analysis found missing: 33 Ω at Nano D2, TWO Nano ground wires, twisted/paired HF ground returns (MCLK pair, BCK/LRC/OUT bundle), the surveyed GY-PCM5102 header order, J2 jack contact mapping, and all §5 hazard callouts. This sheet is what the builder's hands follow. |
| D5 | `docs/tl072-frontend-schematic.svg` (updated in place) | schematic fragment | Add the 2026-07-22 `C_byp 100 µF ‖ 0.1 µF` at TL072 pin 8 (currently text-only in `tl072-frontend.md`), number all eight TL072 pins, and place RV1's three terminals per gen.py's rheostat topology (§3, FEO/MIDF). |
| D6 | `docs/continuity-card-revB.md` | bench check card | Printable meter checklist (§6, V3). |
| D7 | `docs/module-survey-revB.md` + photos | measurement record | Closes the physical gaps: GY-PCM5102 header pin order + pad↔TSSOP pin mapping, Nano D2/GND header positions, CJMCU edge-pad order confirmation, as-built status of the M2 hardening items and the VCC rail filter. Input to D2–D4, referenced by them. |

**Explicitly NOT redrawn:** `codec-power-schematic.svg` (current, rev-B, stays as the direction/power quick-reference), `frontend-schematic.svg` (canonical 5 V HAT variant — gets only a "future HAT variant, bench uses TL072/9V sheet" banner), `breadboard-layout.svg` and `fig6-pinout-reference.svg` (stay bannered STALE / different-rig; never edited into currency). `docs/schematic.md`'s netlist body gets one added line pointing at D1/D3 as its replacement.

**Sheet scope decision:** D3/D4 cover the **codec path + front-end + clocks + power only**. The phase-2 control surface (encoders, MCP23017, APA102, sustain pedal) is provision-only in the repo (no I2C address, no expander pin map, no hardware) — it gets a *reserved-pins table* on D3 (BCM + J8 physical, from `retro-deck-design.md`) and nothing more. Drawing pin-level phase-2 art now would exceed the documented facts.

---

## 3. The pin table (rev-B netlist the diagrams encode)

Refdes: **U1** = TL072 (as-built front-end; canonical MCP6002/OPA1662 variant noted), **U2** = PCM1808 on CJMCU-1808 module, **U3** = GY-PCM5102 module, **J8** = Pi 5 40-pin header, **NANO** = Arduino Nano ESP32, **J1** = guitar jack (front-end input), **J2** = line-out jack.

### Power & ground

| Net | Endpoints (device.refdes pin N) | J8 phys / GPIO | Status |
|---|---|---|---|
| +5V | J8 pin 2 → U2 module '+5V' pad = U2 pin 3 (VCC); J8 pin 2 → U3 pad VIN | p2 | As-built: direct wire. **OPEN-A:** conditional RC (10 Ω + 470 µF ‖ 0.1 µF) pending silence-FFT (M2-A1) — draw direct, annotate the gated fix |
| +3V3 | J8 pin 1 → U2 module '3.3' pad = U2 pin 4 (VDD); J8 pin 1 → U3 pad XSMT (strap H) | p1 | Bench truth. **OPEN-B:** gen.py must choose pin 1 vs. AP2112K LDO (Q1). **OPEN-C:** physical XSMT strap implementation on the module (bridge vs. wire) — survey D7 |
| GND (star) | J8 pin 6 → U2 module GND; → U2 '-' edge pad; → NANO GND ×2 (positions **OPEN-D**, survey); → CopperSound/TL072 GND rail; → U3 pad GND; → J2 sleeve | p6 | HF exception: paired returns with MCLK and BCK/LRC/OUT bundle, all landing at pin 6 |
| NANO power | Own USB-C only. **Never** 5 V rail → NANO VIN (VIN spec 6–21 V) | — | Hard rule |
| 9 V domain | Gator 9 V → CopperSound DC jack → TL072 U1 pin 8 (V+), U1 pin 4 (V−) → 9 V return; C_byp 100 µF ‖ 0.1 µF at U1 pin 8 | — | Only crossings out of the domain: signal to LIN, ground to star |

### Clocks & data (direction arrows mandatory)

| Net | Driver → receivers | J8 phys / GPIO | Status |
|---|---|---|---|
| MCLK 12.288 MHz | NANO D2 (ESP32-S3 GPIO5, header position **OPEN-D**) → R 33 Ω → U2 pin 6 (SCKI). ADC **only** — U3 gets no MCLK | — | 33 Ω install status **OPEN-E** (survey). Remove D2→D3 self-test jumper first |
| BCLK 3.072 MHz | **U2 pin 8 (BCK) drives** → J8 pin 12 (GPIO18, Pi slave) + U3 pad BCK | p12 / GPIO18 | Endpoints identical to rev A; direction reversed |
| LRCLK 48 kHz | **U2 pin 7 (LRCK) drives** → J8 pin 35 (GPIO19) + U3 pad LCK | p35 / GPIO19 | Same |
| CAPDAT | U2 pin 9 (DOUT) → J8 pin 38 (GPIO20) | p38 / GPIO20 | Sole driver = U2.9 |
| PLAYDAT | J8 pin 40 (GPIO21) → U3 pad DIN | p40 / GPIO21 | GPIO21↔pin 40 is a fixed BCM/physical mapping. **Already stated in rev-B artwork** (`wiring-diagram.svg`: "J8 p40 GPIO21 → DAC DIN"; `codec-power-schematic.svg`: "p40 GPIO21 -> PLAYDAT") — verify unchanged, nothing to add |
| Pi I2S mux | GPIO18–21, RP1 I2S1 slave, ALT **a4** | — | **OPEN-F:** a4 is asserted (hardware-review.md L58) but its citation covers only I2S0=a2; re-verify against the RP1 datasheet table before printing on D3 |

### Straps (set BEFORE power-on — TI requirement)

| Strap | Level / tie | Status |
|---|---|---|
| U2 pin 10 (MD0) | → +3V3 (HIGH) | rev B master. GND = rev-A stale |
| U2 pin 11 (MD1) | → +3V3 (HIGH) | 256 fs |
| U2 pin 12 (FMT) | → GND (LOW) | I2S 24-bit, same both revs |
| U3 pad FLT (H1L) | L | Pad↔TSSOP pin mapping (11/10/17/16 for FLT/DEMP/XSMT/FMT) is third-party-sourced — **OPEN-G**, survey D7; draw pad names, add chip pins only if verified |
| U3 pad DEMP (H2L) | L | |
| U3 pad XSMT (H3L) | **H (+3V3, never 5 V)** | Ships LOW = muted |
| U3 pad FMT (H4L) | L | |
| U3 pad SCK | → GND (internal PLL from BCK, 64 fs) | |

### Analog path (as-built TL072/9 V variant)

| Net | Endpoints | Status |
|---|---|---|
| Guitar in | J1 tip → Cin 0.1 µF → NBI; Rbias 1 M: NBI → VBIAS; NBI → U1 pin 3 (+A) | Standard dual-op-amp pinout: 1=OUTA, 2=−A, 3=+A, 4=V−, 5=+B, 6=−B, 7=OUTB, 8=V+ (identical TL072/MCP6002) |
| Buffer | U1 pin 1 (OUTA) → U1 pin 2 (−A) unity; → U1 pin 5 (+B) | |
| VBIAS 4.9 V | 9 V → R1 10k → VBIAS → R2 12k → GND; C1 to GND | 5 V/2.5 V (10k/10k) is the canonical HAT variant only |
| Gain | NINV = U1 pin 6, Rg 10k: NINV→VBIAS, Rf 10k: NINV→MIDF; **MIDF = Rf.2 → RV1.1; FEO = U1 pin 7 + RV1.2 (wiper) + RV1.3 + Cout.1** | RV1 rheostat topology adopted from gen.py (only complete statement, rev-agnostic). Normalize label: "Rf 10k + RV1 100k rheostat (~20k at ×3 nominal)" |
| Into ADC | Cout 1 µF → Rs 1 kΩ → U2 module LIN edge pad = U2 pin 13 (VINL); Ca 1 nF pad→GND **at the pad** | Optional Rs 1.5 k upgrade (B3) noted |
| Input ref | U2 '-' edge pad → star GND (J8 pin 6) | |
| VINR | U2 pin 14: module edge pad RIN **left open** (as-built; on-board AC termination; rev-B tape return lands here later) | Bare-chip variant: Cr 1 µF → GND. Edge-pad order RIN / − / LIN is single-source reverse-engineered — **OPEN-H**, print with the verify-against-your-board flag |
| VREF | U2 pin 1 → decoupling to AGND only (module onboard; bare chip 0.1 µF + 10 µF) | Never drive/load |
| Line out | U3 pad OUTL → J2 tip; OUTR → J2 ring; GND → J2 sleeve | **OPEN-I:** tip=L/ring=R/sleeve=GND is the recommended standard TRS mapping but is stated nowhere — one-line user decision (Q2) |

### Reserved (phase-2, table-only on D3)

Verbatim from `retro-deck-design.md` §4 (2026-07-23 recomputed budget) — **15 encoder pins, not 10**: GPIO0/1 ID-EEPROM · GPIO2/3 I2C1 · **GPIO4/5/6 ENC1** · **GPIO7/8/9 ENC2** (reclaimed SPI-TFT pins) · GPIO10/11 APA102 · **GPIO12/13/14 ENC3** · **GPIO15/16/17 ENC4** (14/15 reclaimed from UART0) · GPIO18–21 I2S (in use, rev B) · **GPIO22/23/24 ENC5** · GPIO25 SUSTAIN · GPIO26 REM · GPIO27 IR_TX — **with J8 physical numbers added from the standard BCM↔J8 map** (a mechanical fill; currently a repo gap). MCP23017 address/pin map: no facts exist; table row says "TBD — do not draw."

---

## 4. Conflicts to resolve first (all blocking)

| # | Conflict | Resolution | Decided by |
|---|---|---|---|
| C1 | MD0/MD1 straps: GND (schematic.md netlist, gen.py, breadboard-layout.svg) vs. +3V3 (adc-hookup.md et al.) | **+3V3 (rev B).** GND = dual-slave dead bus. Encode in gen.py patch step 1 | Already decided by the 2026-07-10 pivot record; adc-hookup.md is the designated authority. No new decision — just propagate |
| C2 | MCLK source: Pi GPIO4/J3.7 (gen.py, schematic.md body) vs. Nano D2/GPIO5 (all rev-B sources) | **Nano D2 → SCKI only.** GPIO4 route is physically impossible (clk_i2s claimed by BCLK, claim-verification.md + Circle source) and GPIO4 is re-budgeted as ENC1_A. gen.py patch step 2 | Same — pivot record; physics verified |
| C3 | BCLK/LRCLK drive direction on GPIO18/19 | **PCM1808 drives; Pi slave (GDAW_I2S_SLAVE).** Diagrams must carry direction arrows from rev-B knowledge — netlist connectivity alone is direction-agnostic. Add the "only slave-mode kernel images may boot on rev-B wiring" note (M2 uncovered-lens item) | Pivot record. **Correction:** an earlier draft cited a "firmware team confirms no master-mode image remains bootable" — no such confirmation exists in the repo. `GDAW_I2S_SLAVE` in `src/config.h` is a compile-time flag and does not prevent a pre-pivot `kernel_2712.img` from booting off an old SD card. Escalated to **Q9 (§8)** |
| C4 | 3V3 source: J8 pin 1 (all docs, bench) vs. AP2112K LDO (gen.py HAT) | Bench diagrams (D3/D4) show **pin 1**. gen.py must pick one and say so | **User — Q1 (§8)** |

C1–C3 need no human input — they are settled; the work is mechanical propagation into gen.py before anything is generated. C4 is the only blocking conflict requiring a one-line human answer.

---

## 5. Hazard callouts the artwork must carry

On **D4 (wiring diagram)** — the sheet hands follow — as boxed callouts at the wire they concern:

1. **J8 pin 1 / pin 2 adjacency kill hazard:** pins 1 and 2 drawn as distinct labeled adjacent positions; "VDD abs max 4 V — NEVER 5 V on the '3.3' pad. Meter both rail rows with module power leads NOT landed, power down, then connect."
2. **Strap-before-power:** at MD0/MD1/FMT: "Set before power-on (TI). Rev-B = MD0/MD1 HIGH. Never re-strap live."
3. **3.3 V-only I2S domain:** whole bus tinted/labeled "3.3 V CMOS only — no shifters needed AND no 5 V signal ever. PCM1808 5 V-tolerant pins: SCKI/MD/FMT only; BCK/LRCK are VDD-domain, NOT tolerant. RP1 GPIO NOT 5 V tolerant."
4. **XSMT:** "→ +3V3, never 5 V. Module ships bridged LOW = hard mute (#1 silent-output gotcha). Tied high = no soft-mute: connect amp/phones last, volume down, disconnect before power-cycles or clock-wire work (M4)."
5. **GY module vs. bare chip:** part labeled "GY-PCM5102 **module** (onboard LDO) — VIN=5 V is module-only; never substitute a bare PCM5102A."
6. **Nano VIN:** "Nano powered by own USB-C only. Do NOT wire 5 V rail to VIN (spec 6–21 V). Crossings: D2 via 33 Ω + two GND wires, nothing else."
7. **MCLK wire:** "3.3 V logic; SCKI rated to +6.5 V VDD-independent — Nano-first power-up safe (B1, *Indicative*). Remove D2→D3 self-test jumper before connecting. 12.2880 MHz PCNT-verified; jitter/duty uncharacterized — do not over-claim."
8. **9 V domain box:** "9 V feeds ONLY the TL072 — never near any ADC/Pi pin. Two crossings only: signal to LIN (Rs 1 k + Ca 1 nF at the pad), ground to star."
9. **Grounds:** two Nano ground wires drawn; paired/twisted HF returns drawn; "each lands at J8 pin 6 — no ground islands"; avoid the deliberate sleeve+wire loop on the CopperSound cable.
10. **CJMCU edge pads:** "RIN / − / LIN order is single-source reverse-engineered; some batches shipped wrong solder jumpers — verify against your board before soldering."
11. **Power order:** Nano → Pi → Gator 9 V; reverse to power down; kill all three before touching any wire.
12. **Title block on every sheet:** "**rev B — 2026-07-10 pivot (PCM1808 bus master, Pi I2S slave).** GPIO18/19 have opposite drive directions vs. rev A on identical pads. Supersedes schematic.md netlist body / breadboard-layout.svg."

On **D3 (schematic)**: items 2, 3, 5, 12 plus the two *Indicative*-flag notes (SCKI abs-max, MCLK SNR) and the VCC-ratiometric note ("FS = 0.6·VCC — rail ripple multiplies onto samples; gated RC fix per M2-A1").

If any phase-2 sketching ever lands on a sheet: "KY-040 '+' = 3.3 V ONLY" — otherwise the sheets state they are codec-path-scoped.

---

## 6. Verification strategy (mechanical, no "review carefully")

**V1 — Netlist self-check (automated, CI-able):**
- Patched gen.py must pass its own `erc()` (every pin on exactly one net, nets ≥ 2 nodes) **plus** the new pin-number-exists assertion (§1 step 6).
- Regeneration diff: `python3 kicad/gen.py && git diff --exit-code kicad/guitardawliteos.net kicad/bom.csv kicad/guitardawliteos.kicad_sym` — any drift fails.
- New check in `validate.py`: parse the `.kicad_pcb`, assert footprint count == component count and every net's pad count matches the `.net` (catches the RV1-class drift that is invisible today).
- Grep gate: rev-A markers (`J3.7`, `GPIO4.*SCKI`, `MD0.*GND`) must not appear in generated outputs; the embedded rev string must not be `2026-06-29`.

**V2 — Diagram-vs-netlist diff (the load-bearing check):**
- Both SVGs embed their connectivity as structured data: a `<metadata>` JSON block listing every wire as `{net, from: "REF.PIN", to: "REF.PIN"}` — written as the SVG is authored, not reverse-engineered from geometry.
- New script `kicad/check_diagrams.py`: loads gen.py's NETS dict (import, not regex), loads each SVG's metadata block and `docs/pinmap-revB.csv`, and asserts set-equality of edges per net **within a declared comparison scope**, plus: direction attributes on BCLK/LRCLK/CAPDAT/PLAYDAT/MCLK match a hardcoded rev-B direction table; every J8 physical-pin label matches the BCM↔physical map; every OPEN-flagged item still carries its flag text. **Scope caveat (blocking, must be designed before V2 can pass):** gen.py models U2/U3 as *bare chips* with HAT-level decoupling and PCM5102A charge-pump nets (C9–C16, LDOO/CPP/CPM/VNEG/LOUT/ROUT) that physically live *on* the CJMCU/GY modules and can never appear in a bench wiring diagram. Naive set-equality therefore fails by construction. Add a §1 patch item to either (a) re-model U2/U3 as module-level components — module pads as pins, chip-pin mapping as an attribute — or (b) tag an explicit `ON_MODULE` component set that `check_diagrams.py` excludes from the diff. Write whichever policy is chosen into the V2 spec before implementing it. Exit nonzero on any mismatch. Run in the same CI step as V1.
- The physical facts gen.py cannot know (module header order, jack contacts) are checked against `docs/module-survey-revB.md` via the same CSV — the survey doc is the ground truth for those rows, and the script asserts the SVG matches the CSV, which cites the survey.

**V3 — Bench continuity card (`docs/continuity-card-revB.md`, D6):**
All powers off, meter in continuity/diode mode, generated row-by-row from the pin table: (1) J8 p1 ↔ U2 module '3.3' pad: beep; J8 p2 ↔ '3.3' pad: **no beep** (kill-hazard check); (2) MD0 and MD1 pads ↔ 3V3 rail: beep; ↔ GND: no beep; FMT ↔ GND: beep; (3) each I2S line end-to-end (Nano D2 ↔ SCKI through 33 Ω ≈ 33 Ω reading, not 0 — doubles as the OPEN-E install check); (4) XSMT ↔ 3V3 beep, ↔ 5 V no beep; (5) Nano GND ↔ star: beep on **two** independent wires (lift one, other still beeps); (6) 9 V rail ↔ any U2/J8 pin: no beep except star ground. Then the powered check: rails metered with module leads not landed; boot log must read `I2S full duplex (TXRX, slave - ADC masters the bus): RUNNING`.

**V4 — Freshness tripwire:** a `docs/REV` file stating `B`; check_diagrams.py asserts every deliverable's title block cites it. Any future rev-C pivot flips one file and CI flags every stale sheet at once.

---

## 7. Execution order

| Step | Work | Effort | Parallel with |
|---|---|---|---|
| 1 | **Bench survey** (D7): photograph + record GY-PCM5102 header order and pad↔chip pins, Nano D2/GND positions, CJMCU pad markings, presence/absence of 33 Ω, second ground, twisted returns, VCC filter; run the silence-FFT if feasible | 1–2 h bench | Step 2 |
| 2 | **gen.py rev-B patch** (§1 items 1–6) + regenerate + V1 green. Requires Q1 answered | 2–3 h | Step 1 |
| 3 | **`docs/pinmap-revB.csv`** (D2) emitted from patched gen.py + physical overlay from survey; resolve OPEN-D/E/G/H from step 1, OPEN-F from the RP1 datasheet lookup (30 min) | 1 h | — (needs 1+2) |
| 4 | **`docs/system-schematic-revB.svg`** (D3) from D1/D2, with embedded metadata block | 3–4 h | Step 5 |
| 5 | **Update `docs/wiring-diagram.svg`** (D4) + **`tl072-frontend-schematic.svg`** (D5), metadata blocks included | 2–3 h | Step 4 |
| 6 | **`kicad/check_diagrams.py`** (V2) + validate.py PCB check; wire into CI/pre-commit | 2 h | Steps 4–5 |
| 7 | Run V1+V2 to green; fix mismatches (expect a few — that's the point) | 1 h | — |
| 8 | **Continuity card** (D6) generated from the CSV; execute V3 on the bench; record results in D7 | 1 h desk + 1 h bench | — |
| 9 | Housekeeping: banner `frontend-schematic.svg`, pointer line in `schematic.md`, re-banner or re-import `.kicad_pcb`, `docs/REV` file | 30 min | Step 8 |

Critical path ≈ steps 2→3→4→7→8: roughly two focused days including bench time. Steps 1 and 2 start immediately and independently; only Q1 blocks step 2's item 3.

---

## 8. Open questions for the user (one line each)

1. **(Q1, blocks gen.py patch)** Should rev-B gen.py model the bench (3V3 from J8 pin 1, delete the AP2112K LDO) or the future HAT (keep LDO, banner it) — which build is gen.py the SSOT *for*?
2. **(Q2)** Line-out J2: confirm tip=OUTL / ring=OUTR / sleeve=GND (standard TRS), or is the bench actually a 3-pin terminal block — which, and in what order?
3. **(Q3)** Are the M2 hardening items (33 Ω at D2, second Nano ground, twisted HF returns) physically installed on the bench today, or should the diagrams mark them "required, not yet fitted"?
4. **(Q4)** Was the silence-FFT ever run on the ADC +5 V rail, and if so was the RC filter installed — draw direct wire or RC?
5. **(Q5)** Which VINR treatment is primary artwork: CJMCU as-built (RIN open) with the bare-chip Cr variant as an inset, or both at equal weight?
6. **(Q6)** Can you photograph the GY-PCM5102 header/pads, the Nano wiring, and the CJMCU edge pads for the survey (D7), or should step 1 be scheduled as a guided bench session?
7. **(Q7)** Confirm codec-path-only scope for D3/D4 (phase-2 controls as a reserved-pin table, no drawn wiring) — yes/no?
8. **(Q8)** After the patch, does netlist authority formally transfer to gen.py (banners in adc-hookup.md updated to say "generated from kicad/gen.py rev B"), or does adc-hookup.md remain co-authoritative for bench procedure?
---

## 9. Review record

This plan was produced by parallel readers over every hardware document in the
repo, reconciled into one rev-B pin map, then attacked by two independent
adversarial reviewers (technical + executability). **13 issues were raised
against the plan itself; 3 were blocking and are corrected above:**

| # | Blocking issue | Correction |
|---|---|---|
| B1 | §3 reserved-pin table misquoted `retro-deck-design.md` §4 — listed 10 encoder pins, dropping ENC2 (GPIO7/8/9) and GPIO14/15 | Replaced with the verbatim 15-pin budget |
| B2 | §4 C3 cited a "firmware team confirms no master-mode image remains bootable" that **exists nowhere in the repo** | Clause deleted; escalated to Q9 as a real open question |
| B3 | The load-bearing V2 check (SVG↔gen.py net set-equality) **cannot pass as specified** — gen.py models bare chips + on-module components that can never appear in a bench diagram | Scope caveat added; a module-modelling or `ON_MODULE` exclusion policy must be designed before V2 is implemented |

Notable non-blocking corrections also applied: the p40/GPIO21 label was claimed
missing from `wiring-diagram.svg` but is already present (dropped from the work
list); the "extend `erc()`" step was a no-op and now targets `validate.py`; and
the V1 grep gate was structurally vacuous and is replaced with assertions
against gen.py's imported `NETS` dict.

**Still open from the critique, for the author to fold in:** the plan's in-place
rev-B patch contradicts `retro-deck-design.md` §5 ("Rev A KiCad project:
unchanged — it's the learning vehicle"; rev-B changes go behind a marked REV-B
section) — this needs an explicit supersede or a flagged implementation; J2's
line-out mapping is largely answerable from the repo (the GY module's onboard
3.5 mm jack) and should not default to hand-wired pads; the gen.py patch spec is
internally inconsistent about which items block on Q1 and models `J4` as 2 pins
while describing 3; debug-probe wiring (`docs/debug-probe.md`) should be named in
the scope exclusions; and nothing in V1–V3 checks gen.py's chip pin numbers
against the TI datasheets — a one-time hand-check against SLES177B / SLAS859C
should be recorded in D7.

### Analysis findings feeding this plan

- **10 doc-vs-doc conflicts**, 3 blocking: MD0/MD1 strap level (GND vs +3V3),
  MCLK source (Pi GPIO4 vs Nano D2), and BCLK/LRCLK drive direction — in each
  case `gen.py` + `schematic.md`'s netlist body + `breadboard-layout.svg` carry
  rev A while `adc-hookup.md` + `hardware-review.md` + `claim-verification.md`
  carry rev B.
- **15 hazards**, incl. VDD 4 V abs-max next to J8's adjacent 3.3 V/5 V pins,
  the non-5 V-tolerant RP1 GPIO and PCM1808 clock pins, the GY-PCM5102 shipping
  hard-muted (XSMT), the "never feed 5 V to Nano VIN" rule, and the 9 V TL072
  domain coexisting with 3.3 V-only pins.
- **23 pin-level gaps** no document currently states — most consequentially the
  GY-PCM5102 module header order, the Nano's D2/GND header positions, and the
  as-built status of the M2 hardening items. These are what the D7 bench survey
  exists to close.
