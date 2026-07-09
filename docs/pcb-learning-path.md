# GuitarDAWLiteOS — PCB design learning path & open-hardware roadmap

A follow-along curriculum that teaches PCB design **by turning GuitarDAWLiteOS into a hackable open-hardware board**. Each milestone is a real, finishable step with a deliverable.

---

## First: how KiCad files actually work (this is why the earlier files wouldn't open)
KiCad's **File → Open** wants a **project**, not a netlist or a symbol library:

| File | What it is | How you use it |
|------|-----------|----------------|
| `*.kicad_pro` | **the project** — open *this* | double-click / File → Open |
| `*.kicad_sch` | the schematic | opens inside the project (Eeschema) |
| `*.kicad_pcb` | the board | opens inside the project (Pcbnew) |
| `*.kicad_sym` | a **symbol library** (parts you place) | registered via a sym-lib-table, *not* opened |
| `*.net` | a **netlist** (connections export) | *imported* into a board, not opened |

`kicad/` now contains a real, openable project (verified with `kicad-cli`): **open [`kicad/guitardawliteos.kicad_pro`](../kicad/guitardawliteos.kicad_pro)**. It comes up to a titled blank schematic with the `GuitarDAWLiteOS` symbol library (PCM1808, PCM5102A) already registered, plus the generated `guitardawliteos.net` / `bom.csv` as inputs.

---

## Two ways in
- **See it instantly (5 min):** open the project → **PCB Editor** → *File → Import → Netlist…* → `guitardawliteos.net` → all 41 footprints drop in with a ratsnest. Great for seeing what a board *is*.
- **Learn it (the path below):** draw the schematic yourself from the reference, then lay out the board. This is where the skills come from.

Your reference / "answer key" while you draw: [schematic.md](schematic.md) (net-by-net), [frontend-schematic.svg](frontend-schematic.svg), [codec-power-schematic.svg](codec-power-schematic.svg), [bom.csv](../kicad/bom.csv).

---

## Curriculum (each milestone = one sitting)

### M1 — KiCad orientation & a board from the netlist  ·  *learn the tool end-to-end*
Open the project, tour Eeschema/Pcbnew, import `guitardawliteos.net` to a board, draw a rectangular **Edge.Cuts** outline, run **DRC**, export **Gerbers** (`File → Fabrication Outputs`). You won't route yet — the goal is the full loop: project → board → fab files.
**Deliverable:** a zip of Gerbers for a (blank-routed) board. You'll have touched every KiCad area once.

### M2 — Draw the schematic  ·  *the core skill*
Start with the **front-end** (the SPICE-verified part): place `Device:R`/`Device:C`, the op-amp (`Amplifier_Operational:OPA1662` or `Device:Opamp_Dual`), wire it per [frontend-schematic.svg](frontend-schematic.svg). Then add `PCM1808`/`PCM5102A` (from the `GuitarDAWLiteOS` lib), the `AP2112K-3.3`, and the Pi header (`Connector_Generic:Conn_02x20_Odd_Even`). Use **global labels** for `+5V`, `+3V3`, `GND`, `MCLK`, `BCLK`, `LRCLK`, `CAPDAT`, `PLAYDAT` instead of long wires. Annotate, **assign footprints** (see bom.csv), run **ERC** to zero.
**Deliverable:** your own `.kicad_sch` that ERCs clean — compare its netlist to `guitardawliteos.net`.

### M3 — Layout & routing  ·  *the craft*
Update PCB from schematic. Place: keep the **analog front-end and its star ground away from the BCLK/LRCLK/MCLK** runs; `FB1`+`C2`/`C3` hard against U2 VCC. Route power first (use the **Power** net class = wider tracks), then signals; add a **ground pour**. Run **DRC** to zero.
**Deliverable:** a routed 2-layer board + 3D view (`Alt+3`).

### M4 — Make it a Raspberry Pi HAT  ·  *the form factor*
Set the board outline to the **HAT mechanical spec** (65 × 56 mm, the 4 mounting holes at the standard coordinates), align the 2×20 header to the Pi, and add the optional **ID EEPROM** (24Cxx on GPIO0/GPIO1 = ID_SD/ID_SC) so the HAT self-identifies. Add the hackability features below.
**Deliverable:** a mountable HAT that physically fits a Pi 5 and **ships with GuitarDAWLiteOS** (HAT + Pi 5 + the SD card you already built).

### M5 — Open-hardware release  ·  *ship it*
Add a `LICENSE` for the hardware (recommend **CERN-OHL-S v2** — strongly reciprocal, the hardware analogue of the GPLv3 your Circle firmware already uses). Export the release set: schematic PDF, Gerbers + drill, BOM, pick-and-place, a 3D render, and the **editable KiCad sources**. Tag a version.
**Deliverable:** a `hardware/releases/v0.1/` folder anyone can fabricate from — true open hardware.

### M6 — Rev B: the "GuitarDAW Audio HAT+"  ·  *your second board*
Re-spin the audio HAT with the Phase-2 features from [retro-deck-design.md](retro-deck-design.md): the **tape loop** (TAPE OUT pad + trim, TAPE IN→VINR jumper, REM optoMOS jack, unpopulated TRANSPORT header + IR footprint), the **ID EEPROM** (CAT24C32 @0x50 — makes it an official HAT+ *and* Linux-dual-use), buffered line taps + panel connector, 16 mm stacking for the Active Cooler, and optionally the **Pisound-style 100×56 stretch** so jacks exit the rear wall. This is where `gen.py` earns its keep: add the rev-B section, regenerate, re-import.
**Deliverable:** rev-B board — the one that ships in the deck.

### M7 — Faceplate PCB + panel electronics  ·  *the retro face*
A 2.0 mm FR4 faceplate (silkscreen retro graphics, exposed-gold accents) with cutouts for **real VU meters** (200 µA movements + TL072 rectifier drivers), **NKK UB illuminated transport keys**, knobs, and the **Touch Display 2 window** (portrait panel mounted sideways); rear component PCB carries switches/LEDs/APA102 bargraph, clamped by the panel hardware — no visible fasteners. PCB-as-front-panel is standard synth-DIY practice; this milestone is where the deck gets its face.
**Deliverable:** faceplate + panel PCB pair.

### M8 — Enclosure + integration  ·  *the deck*
Folded sheet-metal U-chassis + CNC wood side cheeks (both published as open DXF), Active Cooler venting through period-correct grilles, Pi 5 + HAT+ + panel + (optionally) a Tanashin-clone cassette mechanism inside. Hammond 1456CWW is a fine prototype shell (walnut line is being discontinued — not an open-BOM dependency).
**Deliverable:** the assembled retro deck, shipping GuitarDAWLiteOS.

### M9 — (stretch) Embed the Pi: CM5 carrier  ·  *advanced*
Once HAT skills are solid, design a carrier for the **Compute Module 5** (the real "embedded Pi"). Genuinely advanced — high-speed differential pairs, power sequencing, the two 100-pin connectors — a *phase 3*, not a first board. Route the same 22-pin DSI FPC + display power; note `rp1dsi` is **unconfirmed on CM5** (validation spike required). Same audio subcircuit, now on your own mainboard.

---

## Designing for hackability (build these into M3–M4)
- **Break out the spares:** spare GPIO, the I²C bus, and UART to a labeled header — invite hacking.
- **Test points** on `VBIAS`, `MCLK`, `BCLK`, `VINL`, `+5V`, `+3V3` — so the board is probeable (and you can scope Spike B).
- **Option jumpers:** input-source select, front-end bypass, gain-range — so behavior is reconfigurable without a respin.
- **Sockets:** an SOIC-8 socket (or DIP + adapter) for the op-amp lets people roll their own front-end.
- **Silkscreen everything:** pin-1 dots, net names at headers, the XSMT/MD-strap reminders — the board should be self-documenting.
- **Publish editable sources**, not just Gerbers. Open hardware = others can *modify*, which needs the `.kicad_*` files + this repo.

## Workflow you now have (and how to refine it)
- **Circuit-as-code:** [`kicad/gen.py`](../kicad/gen.py) defines components + nets in Python and emits the netlist/symbols/BOM with a built-in ERC. Edit the circuit there → `python3 gen.py` → re-import. Pairs code review with schematic capture.
- **Headless validation:** `kicad-cli sch erc` / `pcb drc` / `sch export svg` let you check designs from the terminal or CI — no GUI needed. (`kicad/validate.py` does the same for the generated files.)
- **Version control:** commit the `.kicad_*` sources; KiCad files are text and diff reasonably. Keep `gen.py` as the source of truth for the netlist.
- **Next refinement:** add a `make` target that runs gen.py + kicad-cli ERC/DRC + Gerber export, so a board release is one command.

## Recommended direction
**Start at M1→M4 as a Pi 5 HAT.** It's the right first PCB (beginner-feasible, immediately ships with the OS, genuinely open hardware), and the audio subcircuit is identical to what a future CM5 carrier (M6) would carry — so nothing is wasted. "Embedded Pi" (CM5) is a great *phase-2 goal* once the HAT skills land. If you'd rather aim straight at CM5, say so and I'll restructure M4 around the CM5 connectors instead — but I'd still prototype the audio as a HAT first.

> Licensing stack for the open-hardware release: **hardware** → CERN-OHL-S v2 · **firmware** → GPLv3 (Circle) · **docs** → CC-BY-SA 4.0.
