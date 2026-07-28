# GuitarDAWLiteOS — DigiKey Prototyping Order

**Verified against live DigiKey listings 2026-07-01** (stock counts are snapshots — reverify at checkout). Search DigiKey by the **MPN** column. Covers: breadboard prototype (Milestone 2), spikes A/B/C, guitar front-end, tape-loop Level A, and a rev-B PCB stock-up. Skip any section you don't need yet.

## 1. Pi platform & debug *(skip what you already own)*
| Qty | MPN | Item | ~$ | Note |
|--:|------|------|---:|------|
| 1 | **SC0889** | Raspberry Pi Debug Probe | 12 | **Get this** — the UART console via the Pi 5 3-pin JST-SH debug connector; cables included. Replaces a generic USB-serial + guesswork. |
| 1 | **SC1148** | Pi 5 Active Cooler | 5 | Needed for the closed retro box later; nice on the bench now. |
| 1 | ~~SC1158~~ | ~~27 W USB-C PD supply~~ — **dropped 2026-07-28** | — | Superseded by the single 18 V source: 5 V buck (≥5 A) off the pack feeds J8 p2/p4. See `docs/power-tree-18v.md`. |
| (1) | SC1432 | Raspberry Pi 5, 8 GB | **175** | ⚠️ DRAM-shortage pricing (was $80 MSRP). **If you already have a Pi 5, skip**; if buying spares for the project, decide whether to wait out the memory price surge. |

## 1b. MCLK generator (Spike B, added 2026-07-09)
| Qty | MPN | Item | ~$ | Note |
|--:|------|------|---:|------|
| 1 | **ABX00083** | Arduino Nano ESP32 (ESP32-S3, with headers) | ~19 | **Skip if you already own one.** Generates the 12.288 MHz MCLK for the PCM1808 off-board — the Pi 5's internal `GPCLK0` route was ruled out (`clk_i2s` conflicts with BCLK). See [claim-verification.md](claim-verification.md), [../esp32-mclk/](../esp32-mclk/). Price is a search-derived estimate, not checkout-verified like the rest of this doc — reverify at DigiKey before ordering. |

## 2. Breadboard infrastructure
| Qty | MPN | Item | ~$ | Note |
|--:|------|------|---:|------|
| 1 | **BB830** | 830-pt solderless breadboard | 9 | ⚠️ Order DigiKey PN **4526-BB830-ND** (DigiKey-direct) — there's a marketplace duplicate listing of the same MPN. |
| 1 | **2028** (Adafruit) | Pi T-Cobbler Plus + ribbon, 40-pin | 8 | Only 68 in stock — order early. |
| 1 | TW-E012-000 | 140-pc jumper wire kit | 7 | |
| 2 | A 08-LC-TT | DIP-8 socket | 0.4 | For the MCP6002 (MPN really has the space). |
| 3 | PRPC040SAAN-RC | 1×40 breakaway male header | 1.7 | Snap to length for the TSSOP adapters. |

## 3. Audio codecs + adapters (breadboard path = bare chips on adapters)
| Qty | MPN | Item | ~$ | Note |
|--:|------|------|---:|------|
| 3 | **PCM1808PWR** | ADC, TSSOP-14 (cut tape) | 4.8 | ×3 = soldering practice spares. 4,990 in stock. |
| 3 | **PCM5102APWR** | DAC, TSSOP-20 (cut tape) | 11.4 | PWR cut-tape is ~half the tube price. |
| 1 | **1210** (Adafruit) | TSSOP-14→DIP adapter, 6-pack | 5 | For the PCM1808. |
| 1 | **1206** (Adafruit) | TSSOP-20→DIP adapter, 3-pack | 4.5 | For the PCM5102A. ⚠️ 1207 is the 16-pin version — wrong. Only 61 in stock. |
| 1 | 111X (Switchcraft) | ¼″ mono guitar jack, panel | 4.6 | The classic amp jack. |

> Alternative if you'd rather not solder TSSOP yet: **Adafruit 6250** (PCM5102 breakout) is in DigiKey's channel; the GY-PCM5102/GY-PCM1808 modules themselves are **AliExpress/Amazon only** (see §8).

## 4. Guitar front-end
| Qty | MPN | Item | ~$ | Note |
|--:|------|------|---:|------|
| 3 | **MCP6002-I/P** | dual RRIO op-amp, DIP-8 (breadboard) | 1.2 | 11k+ in stock. |
| 3 | **OPA1662AID** | dual audio op-amp, SOIC-8 (rev-B PCB) | 8.2 | In stock (tube). ⚠️ The fallback "OPA1678AID" doesn't exist and OPA1678ID is backorder-only — OPA1662 is the part. |
| 1 | **3296W-1-104LF** | 100 k 25-turn trimmer (RV1 gain) | 2.4 | |
| 1 | 3296W-1-102LF | 1 k trimmer (tape-out level, §6) | 2.4 | |

## 5. Passives (audio path + decoupling) — all verified in stock
| Qty | MPN | Item | ~$ | Note |
|--:|------|------|---:|------|
| 5 | **MKS2D031001A00MSSD** | 0.1 µF/100 V WIMA box film (Cin) | 2.8 | ⚠️ The ±10 % sibling (…KSSD) is discontinued — order this ±20 % one. |
| 5 | **B32529C0105K000** | 1 µF/63 V TDK box film (Cout, Cr) | 3.3 | ⚠️ **All WIMA MKS2 1 µF variants are dead at DigiKey**; this EPCOS/TDK is the stocked 5 mm part. |
| 3 | FG28C0G1H102JNT06 | 1 nF C0G disc (Ca anti-alias) | 0.8 | |
| 3 | C320C222J1G5TA | 2.2 nF C0G (DAC output RC) | 1.6 | |
| 10 | ECA-1EM100 | 10 µF/25 V electrolytic (bulk decoupling, VBIAS) | 1.9 | |
| 2 | **74275022** | Würth THT ferrite bead (FB1) | 0.8 | Closest stocked TH ~600 Ω-class bead (512 Ω@100 MHz, 3 A). Only 282 in stock. |
| 10 ea | (generic) | 1 % ¼ W metal-film resistors: **1 M, 10 k, 5.6 k, 3.9 k, 1 k, 470 Ω** | ~6 | Commodity — e.g. Yageo MFR-25 series; or a resistor kit if you don't have one. 0.1 µF ceramic decoupling (0.1 µF X7R radial ×10) too if not on hand. |

## 6. Tape loop — Level A (rev-B provisions, breadboard-testable now)
| Qty | MPN | Item | ~$ | Note |
|--:|------|------|---:|------|
| 2 | **AQY212GH** | PhotoMOS relay (REM punch-in), 60 V/1.1 A | 5.6 | ⚠️ **TLP222A is obsolete** (whole family discontinued) — AQY212GH is the pick; TLP240A(F) is the cheaper stocked alt ($2.16). |
| 1 | SJ1-2503A | 2.5 mm jack (REM) | 0.7 | It's TRS (no true TS part stocked): wire **tip+sleeve**, leave ring floating. |
| 4 | **PJRAN1X1U01X** | Switchcraft RCA jack, PCB right-angle | 5.7 | Tape in/out pairs. ⚠️ Same Sky RCJ-014/041 are 0-stock/15-wk backorder — don't spec them. |

## 7. Rev-B PCB stock-up (optional now — cheap, and you'll need them)
| Qty | MPN | Item | ~$ |
|--:|------|------|---:|
| 3 | AP2112K-3.3TRG1 | 3.3 V LDO, SOT-25 | 0.7 |
| 2 | **CAT24C32WI-GT3** | HAT+ ID EEPROM, SOIC-8 | 0.9 |

## 8. Display
| Qty | MPN | Item | ~$ | Note |
|--:|------|------|---:|------|
| 1 | **SC1635** | **Touch Display 2, 7″** (720×1280 DSI) | 60 | The Spike-C / retro-deck default — **used display-only** (`bEnableTouch=FALSE`; UI pivot 2026-07-23: knobs, not touch). No non-touch official DSI panel exists and `rp1dsi` only drives the official panels, so the touch layer is paid for but unused. Plain HDMI = the $0 bench alternative. ⚠️ Earlier docs guessed SC1148 — that's the Active Cooler; **SC1635** is the 7″ TD2. |
| (1) | SC1975 | Touch Display 2, 5″ | 40 | Compact-build variant — optional (also display-only). |

### 8b. Control surface (UI pivot 2026-07-23 — knobs Michael already owns + pedal additions)
| Qty | Part | What | ~$ | Note |
|----:|------|------|---:|------|
| 5 | EC11-class encoder w/ push (e.g. Bourns **PEC11R-4215F-S0024**) | rotary knob + push | ~1.50 ea | **Michael already has 5 knob-button encoder modules** — buy nothing unless they prove to be 5 V-only boards. KY-040 modules: power "+" from **3.3 V only**; SW pin needs the internal pull-up. |
| 1 | ¼″ TS panel jack (Switchcraft 111X pattern) | sustain-pedal input | ~3 | Second ¼″ jack beyond the guitar input. Switched-contact tip if plug-detection wanted. |
| 1 | Yamaha **FC5** (or FC4A) | sustain-pedal footswitch | ~20 | TS momentary, **normally-closed at rest** — firmware auto-polarity at boot handles it (and any NO pedal). Avoid FC3A (TRS half-damper, not a switch). |
| 1 | **MCP23017-E/SP** | I²C1 panel expander (rev-B) | ~2 | Absorbs the slow tape-transport signals + NKK keys; required by the recomputed GPIO budget (J8 is fully consumed by the knob UI). |

## NOT from DigiKey (order elsewhere)
- **GY-PCM5102 / GY-PCM1808 modules** — AliExpress/Amazon (~$3–8). Fastest breadboard path; §3's bare-chips-on-adapters is the DigiKey-only equivalent.
- **Cassette mechanism** (Tanashin TN-21ZLG clone, ~$10–20) + **TA7668 head-preamp board** (~$10) — AliExpress. Level B, not needed yet.
- **200 µA VU meter movements** (~$5–15) — AliExpress/Amazon. M7 faceplate, not needed yet.
- **NKK UB illuminated switches** — DigiKey *does* carry them (~$10+ each) but they're M7 panel parts; don't buy until the faceplate design.
- **Bench tools**: Spike B (12.288 MHz MCLK) is **done** — frequency-verified 2026-07-09 via the Nano's own PCNT self-test, no scope needed. A scope/LA (≥25 MHz, e.g. Digilent or used) is now **optional**, only for the open *jitter-characterization* question (see the M2 review) — not required to build or bring up the bench.

## Rough totals
- **Core prototype** (§2–§6, no Pi/display): **~$95–110**
- **+ Debug Probe & Active Cooler** (§1, no Pi): +$29
- **+ Nano ESP32 MCLK generator** (§1b, skip if you already own one): +$19
- **+ 7″ Touch Display 2**: +$60
- Everything incl. a new Pi 5: ~$380 (of which $175 is shortage-priced Pi)

### Checkout gotchas recap
1. BB830: pick the **DigiKey-direct** listing (4526-…), not marketplace.
2. Low-stock items to grab first: Adafruit **1206** (61), T-Cobbler **2028** (68), Würth bead **74275022** (282).
3. TLP222A, WIMA 1 µF MKS2, Same Sky RCA jacks, ±10 % MKS2 0.1 µF: all dead/backordered — substitutes above are the verified picks.
