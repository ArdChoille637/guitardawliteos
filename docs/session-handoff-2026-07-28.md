# Session handoff — 2026-07-28

State of the hardware-documentation work at the end of this session, for
whoever (or whatever) picks it up next. Delete this file once its contents have
been absorbed into the permanent docs.

## Landed on `main`

| Commit | What |
|---|---|
| `6d259fb` (#3) | `kicad/gen.py` rev-B variant behind `GDAW_REV=B`; `kicad/test_rev.py` gate (23/23, mutation-tested) |
| `f2a9662` (#4) | Single-18 V power tree + `docs/power-tree-18v.md` |

**Rev A is still the default and regenerates byte-identically** —
`retro-deck-design.md` §5 keeps it as the learning vehicle, so rev B lives
behind a flag and writes its own `-revB` artifacts. Run
`GDAW_REV=B python3 kicad/gen.py`; verify with `python3 kicad/test_rev.py`.

Encoded in rev B: MD0/MD1 → `+3V3` (ADC is bus master), MCLK from Nano
`J4.1` → 33 Ω → `U2.6` SCKI, GPIO4 removed from J8, 3.3 V from **J8 pin 1**
(user decision), AP2112K LDO not populated.

## Open pull requests

| PR | State | Blocker |
|---|---|---|
| **guitardawliteos#5** | bench power restore, 45 W PSU, Nano-off-USB, `boot-selfcheck.md` | ready to merge; nothing pending |
| **guitardawliteos#2** | `docs/pinmap-plan-revB.md` — the pin-for-pin diagram plan | **stale**: written before #3/#4. Its §1 step 1 (the `gen.py` patch) is now **done**. Its power references still describe three supplies. Rebase + reconcile before merging. |
| **guitardawliteos#1** | analog pedal wiring docs moved in from `pedal-workshop` → `docs/pedal-wiring/` | **decision pending** (below), and stale for the same reason |
| **pedal-workshop#2** | removes `docs/wiring/gdawlitos/` from the app repo | land guitardawliteos#1 **first** so the docs are never absent from both |

## Decisions still needed

1. **guitardawliteos#1, figs 5–6.** They were drawn assuming a USB-powered Pi
   recording through a **USB audio interface** — which contradicts this project
   outright (bare metal, I²S, "no USB in the audio path"). They are currently
   *flagged* with a banner, not corrected. Either rework them to the real
   architecture or drop them and keep figs 1–4, which are architecture-neutral
   (analog stompbox harness only) and stand on their own.
2. **Circle GPIO/I²S pin mux.** Does Circle allow claiming GPIO18 as a plain
   input and then handing it to the I²S driver, or must I²S own the mux for the
   whole run? This decides whether the clock-domain probe in
   [boot-selfcheck.md](boot-selfcheck.md) can be a pre-flight step or must be a
   one-shot at startup. **Blocks implementing phase 1.**
3. **Boot self-check phase 2** (Pi → Nano RST line). J8 has no free GPIO —
   `retro-deck-design.md` §4 allocates all 28. Borrow GPIO10/11 while the
   bargraph is deferred, put it on the MCP23017, or skip. Recommendation: skip
   for now; a wedged Nano has not been observed.
4. **Power-up order in `wiring-diagram.svg`.** It previously read
   `1. Pi · 2. Nano · 3. Gator`, contradicting `adc-hookup.md`'s **Nano-first**
   rule and its review-B1 justification. Resolved toward Nano-first. Confirm
   Pi-first was not deliberate.

## Bench vs target — do not conflate

The bench runs **three supplies**: official **45 W** USB-C PD → Pi, Gator 9 V →
TL072, Nano on its own USB-C (moving to Gator 9 V → VIN). No 18 V source exists
yet. The three-supply bench keeps its **power-up ordering** and its
**load-bearing star-ground jumper** — both of those rules are relieved *only* by
having a single source. `power-tree-18v.md` carries a status banner saying so.

Codec rails are identical in both configurations, so bench measurements carry
over unchanged.

## Parked

- **SK9822 / APA102 bargraph** — needs its own buck branch off 18 V (≈3.6 A at
  full white) and a 74AHCT125 buffer (part V<sub>IH</sub> ≈ 3.5 V vs the Pi's
  3.3 V). SPI0 GPIO10/11 reservation stands.
- **SN74HC595** — no role assigned. It is **HC, not AHCT**, so it cannot
  level-shift 3.3 V → 5 V.
- **CD4053BE** — in the drawer, notes in `tl072-frontend.md`, not in any netlist.
- **2N2222A** — suited to NKK transport lamps (low-side switch), not to the
  `SOLENOID_DRV` on the TRANSPORT header.
- **4N35** — suited to the REM jack punch-in (`retro-deck-design.md` item 3),
  with the caveat that it is a phototransistor output: DC-only and
  polarity-sensitive, where a true optoMOS (AQY212/TLP222A) is bidirectional.

## Largest outstanding gap

The **physical module survey** is still unwritten. `pinmap-plan-revB.md`
identified **23 pin-level facts no document states** — chiefly the GY-PCM5102
module's header pin order, the Nano's D2/GND header positions, and the as-built
status of the M2 hardening items. A bench photo of the layout exists but was
never captured into a doc. Until that survey lands, any "pin-for-pin" wiring
diagram is partly guesswork at the module level.

Also outstanding: the `.kicad_pcb` is stale even for rev A (RV1 footprint
absent) and was not touched by #3.
