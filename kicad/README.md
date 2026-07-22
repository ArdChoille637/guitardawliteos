# GuitarDAWLiteOS — KiCad project files

> ⚠️ **This netlist is rev-A (Pi-as-master) and predates the 2026-07-10 clock pivot.** `gen.py` still routes
> **MCLK from Pi header pin 7 (GPIO4)** into the PCM1808 SCKI and **straps MD0/MD1 to GND (slave)**. The
> as-built bench is the opposite: **MCLK from the Nano ESP32 D2**, **MD0/MD1 to +3.3 V (ADC = bus master)**,
> Pi = I²S slave. Use this project as a **PCB-design learning vehicle only** — do **not** fab a board from it
> until `gen.py` is re-generated for the pivot (rev-B). The as-built truth is [../docs/adc-hookup.md](../docs/adc-hookup.md)
> + the wiring/codec SVG sheets. "Same I²S/clock topology either way" below refers to the *breadboard vs
> bare-chip* choice, **not** to master/slave — that pivot post-dates this board.

An openable KiCad project for the Phase-1 audio board, plus the generated inputs that feed it.

> **To open in KiCad: open `guitardawliteos.kicad_pro`.** A netlist (`.net`) and a symbol library (`.kicad_sym`) are *not* openable projects — that's why the earlier files wouldn't load. New to this? Read [../docs/pcb-learning-path.md](../docs/pcb-learning-path.md).

| File | What it is |
|------|-----------|
| **`guitardawliteos.kicad_pro`** | **the KiCad project — open THIS** |
| `guitardawliteos.kicad_sch` | schematic (titled blank canvas; draw it per the learning path) |
| `guitardawliteos.kicad_pcb` | board (blank; import the netlist, or lay out from your schematic) |
| `sym-lib-table` | registers the `GuitarDAWLiteOS` symbol lib into the project |
| `gen.py` | generator: defines components + nets, runs ERC-lite, emits the files below. Re-run: `python3 gen.py` |
| `guitardawliteos.net` | **KiCad netlist** (Eeschema export format) — import into the PCB editor |
| `guitardawliteos.kicad_sym` | **custom symbol library** — PCM1808 + PCM5102A (not in stock KiCad libs) |
| `bom.csv` | bill of materials (ref, value, footprint) |
| `validate.py` | parses + schema-checks the `.net` and `.kicad_sym` (stdlib; stands in for `kicad-cli`) |

## What this design is
A **bare-chip carrier board**: discrete **OPA1662** guitar front-end + bare **PCM1808** ADC + bare **PCM5102A** DAC + **AP2112-3.3** LDO + a **2×20 header** to the Pi 5. 41 components, 28 nets. Pinouts are datasheet-verified; the front-end matches [../docs/schematic.md](../docs/schematic.md) and is SPICE-verified.

> **Differs from the breadboard build** ([../docs/breadboard-build.md](../docs/breadboard-build.md)): that one uses the **GY-PCM5102 module** (onboard LDO + output filter). This PCB uses the **bare PCM5102A**, so it includes what the module hides — the AP2112 LDO (5 V→3.3 V), the charge-pump caps (C10/C11 on CAPP/CAPM/VNEG), the LDOO cap (C14), and the output RC (Rol/Col, Ror/Cor). Same I²S/clock/front-end topology either way.

## Use it — two paths

**A. PCB-first (fastest):** the netlist already carries footprints (all stock KiCad libs), so you can skip drawing a schematic.
1. Open `guitardawliteos.kicad_pro` → open the **PCB Editor**.
2. **File → Import → Netlist…** → select `guitardawliteos.net` → *Update PCB*. All 41 parts drop in with footprints + ratsnest.
3. Arrange and route. Keep the analog front-end (U1, J1, the front-end R/C) away from the I²S clocks; single-point star ground; FB1 + C2/C3 right at U2 VCC.

**B. Schematic-first:** to draw the full schematic.
1. **Preferences → Manage Symbol Libraries → add** `guitardawliteos.kicad_sym` (it provides `PCM1808` and `PCM5102A`).
2. Draw using those two symbols + stock `Device:R/C`, `Amplifier_Operational:OPA1662` (or `Device:Opamp_Dual`), `Connector_Generic:Conn_02x20_Odd_Even` (Pi), `Regulator_Linear:AP2112K-3.3`.
3. Annotate → assign footprints (see `bom.csv`) → **Tools → ERC** → generate netlist → PCB.

## Footprints (defaults — reassign freely)
| Part | Footprint |
|------|-----------|
| U1 OPA1662 | `Package_SO:SOIC-8_3.9x4.9mm_P1.27mm` |
| U2 PCM1808 | `Package_SO:TSSOP-14_4.4x5mm_P0.65mm` |
| U3 PCM5102A | `Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm` |
| U4 AP2112-3.3 | `Package_TO_SOT_SMD:SOT-23-5` |
| R / C | `R_0805` / `C_0805` (10 µF/2.2 µF → `C_1206`) |
| RV1 | `Potentiometer_THT:Trimmer_Bourns_3296W_Vertical` |
| J1/J2 | `PinHeader_1x03` (swap for an audio-jack footprint) |
| J3 Pi | `PinHeader_2x20_P2.54mm` |

Prefer hand-soldering? Reassign R/C to `Resistor_THT` / `Capacitor_THT` in the footprint assignment step.

## Notes & caveats
- **gen.py ERC-lite** checks: every component pin on exactly one net, every net ≥2 nodes, unique refs, footprints present, s-expr balanced. It is clean (it caught two real omissions during authoring — PCM5102A FMT and an unused Pi 3V3 pin). `python3 validate.py` re-parses + schema-checks the outputs (41 components / 28 nets / 129 nodes / 2 symbols — all resolve). Still run **KiCad's own ERC** after drawing the schematic.
- **Validation status: ✅ KiCad-verified.** `kicad-cli` (KiCad **10.0.4**) loaded the symbol library via `sym upgrade` (parsed + re-saved, no warnings) and `sym export svg` rendered both `PCM1808` and `PCM5102A` with all pin names — so the symbols open cleanly in KiCad. The netlist is schema-checked by `validate.py` (41 comp / 28 nets / 129 nodes, all refs resolve); KiCad imports netlists only through the Pcbnew GUI (no CLI path), so confirm that import in the app. The committed `.kicad_sym` uses the portable `20211014` format (reads in KiCad 6–10).
- The Pi header's **3V3 pin is intentionally unused** — this board makes its own 3.3 V via U4 (so don't also wire Pi 3V3 in).
- U4 **EN (pin 3) is tied to VIN** (always enabled).
- Confirm your exact op-amp's pinout matches the standard dual layout (1=OUTA…8=V+) before committing copper.
- This is a **netlist + symbols**, not a finished PCB layout — placement/routing is yours (see the breadboard guide for the placement strategy, which applies to the PCB too).
