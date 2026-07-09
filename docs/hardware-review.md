# GuitarDAWLiteOS — Hardware Review & Wiring

**Date:** 2026-06-29. Every electrical value here is datasheet-verified (TI PCM1808 SLES177B, PCM5102A SLAS859C, RP1 Peripherals datasheet, RPi "Using the I²S peripherals" white paper RP-009699-WP-1, RPi 3-pin Debug Connector spec) and the two highest-risk decisions (power rails, front-end gain/bias) were independently re-checked. See also the wiring diagram (rendered in chat / `docs/wiring-diagram.svg`).

## Headline findings
1. **The research doc had no analog front-end. You cannot wire a guitar straight to the ADC.** A passive pickup is ~hundreds of kΩ at its resonant peak and only ~0.1–0.7 Vpp; the PCM1808 input is 60 kΩ and wants ~3 Vpp on a 2.5 V bias. Direct connection = ~14 dB of tone-sucking loading **and** far too quiet. A buffer + gain stage is mandatory (designed below).
2. **PCM1808 analog VCC must be 5 V** (ROC min 4.5 V) — it *cannot* run all-3.3 V. Digital VDD = 3.3 V. Two rails.
3. **No level shifters needed** between PCM1808 (VDD=3.3 V) and Pi 5 3.3 V GPIO, either direction.
4. **Front-end op-amp runs on the 5 V rail**, not 3.3 V — its output must swing up to 4.0 V.
5. **Corrected front-end nominal gain = ×3 (+9.5 dB)**, not ×4 — ×4 clips a hot-humbucker transient.
6. **Don't power the analog side from header 3V3** (pins 1/17 are unspecified/shared/noisy). Take 5 V from header pin 2 and regulate locally.

---

## Bill of materials (Phase 1 audio board)
| Qty | Part | Role | Notes |
|----:|------|------|-------|
| 1 | Raspberry Pi 5 | host | I²S0 master, 3.3 V GPIO |
| 1 | PCM1808 (14-pin SSOP) **or** GY-PCM1808 breakout | stereo ADC (capture) | strap-only, slave mode |
| 1 | GY-PCM5102 (PCM5102A) breakout | stereo DAC (playback) | strap-only, internal PLL |
| 1 | Dual op-amp, RRIO-ish, low-noise, 5 V | guitar front-end | **OPA1662** (best audio) or **OPA2353/OPA2350** (guaranteed single-5 V RRIO) |
| 1 | ¼" mono jack | guitar input | switched/unswitched |
| 1 | 3.5 mm jack or L/R/G pads | line output | on GY-PCM5102 already |
| 0–1 | *(optional)* 3.3 V LDO (MCP1700/LP5907) | clean-VDD upgrade | default = VDD from header 3V3 |
| 1 | Ferrite bead + RC | clean analog 5 V | for PCM1808 VCC + op-amp |
| — | Decoupling: 0.1 µF + 10 µF sets; 1 µF film coupling caps; 1 MΩ/10 kΩ/trimpot; 1 kΩ + 1 nF AA | passives | values below |

Currents are tiny: PCM1808 ≈ 9 mA (analog) + 6 mA (digital); op-amp ≈ 10 mA; PCM5102A ≈ 25 mA. The Pi's 5 V/5 A supply has ample headroom.

---

## Power tree (verified)
```
Header 5V (J8 pin 2) ──┬─► [ferrite + 10µF+0.1µF] ─► ANALOG 5V rail ─┬─► PCM1808 VCC (pin3, analog)
                       │                                            └─► Op-amp V+ (front-end)
                       ├─► PCM5102A module VIN (its onboard LDO → 3.3V)
Header 3V3 (J8 pin 1) ───► PCM1808 VDD (pin4) + U3 XSMT strap   [default; optional local LDO off 5V = extra-clean digital rail]
Header GND (J8 pin 6/9/…) ─► single-point star ground (AGND≡DGND under each chip)
```
- **Why a 5 V analog rail:** PCM1808 ROC = VCC 4.5/5/5.5 V. Full-scale (3 Vpp) and VREF (2.5 V) are *ratiometric to VCC*, so use a clean, stable 5 V (ferrite + bulk + 0.1 µF) — VCC ripple becomes gain error/noise.
- **Why not header 3V3 for analog:** Pi 5 docs publish **no** 3V3 header current spec; it's leftover from the Pi's own regulator and electrically noisy — bad for audio SNR, so the **analog 5 V rail never comes from it.** Digital VDD is different: ~6 mA, and its noise doesn't reach the audio path (VCC is the SNR-critical rail), so by default **VDD runs from header 3V3 pin 1 directly**; a local 3.3 V LDO off 5 V is an optional clean-rail upgrade.
- **Op-amp on 5 V:** the front-end output must reach 1.0–4.0 V (full-scale window). A 3.3 V rail tops out below 4.0 V → it would clip. Run the op-amp from the analog 5 V rail.

---

## Connection / netlist table
Pi physical pins are J8 (40-pin header, identical layout to Pi 4). PCM1808 pins are the 14-SSOP numbers.

### Clocks & data (I²S0, RP1 ALT function **a2**; GPCLK0 = **a0**)
| Signal | Pi GPIO | Pi pin | → to |
|--------|---------|-------:|------|
| **MCLK** 12.288 MHz (GPCLK0) | GPIO4 | **7** | PCM1808 **SCKI (pin 6)** *only* (PCM5102A self-clocks) |
| **BCLK** (I²S0 SCLK) | GPIO18 | **12** | PCM1808 **BCK (pin 8)** + PCM5102A **BCK** |
| **LRCLK/WS** (I²S0 WS) | GPIO19 | **35** | PCM1808 **LRCK (pin 7)** + PCM5102A **LCK** |
| **Capture data** (I²S0 SDI) | GPIO20 | **38** | ← PCM1808 **DOUT (pin 9)** |
| **Playback data** (I²S0 SDO) | GPIO21 | **40** | → PCM5102A **DIN** |

### Power & ground
| Rail | Pi pin | → to |
|------|-------:|------|
| 5 V | **2** | analog-5V (PCM1808 VCC pin 3 via ferrite, op-amp V+), PCM5102A VIN |
| 3.3 V (header pin 1; LDO optional) | 1 | PCM1808 VDD (pin 4) |
| GND | **6**, 9, 14, 20, 25, 30, 34, 39 | common star ground |

### PCM1808 strapping (set before power-on; internal 50 kΩ pulldowns)
| Pin | Strap | Selects |
|-----|-------|---------|
| MD0 (10) | → GND | slave mode (with MD1) |
| MD1 (11) | → GND | slave mode (256/384/512 fs auto) |
| FMT (12) | → GND | **I²S, 24-bit** |

### PCM5102A (GY-PCM5102) config
| Item | Set | Why |
|------|-----|-----|
| SCK | → **GND** | enable internal PLL off BCK (no MCLK); needs BCK = 32fs/64fs — Circle gives 64fs (3.072 MHz @ 48k) ✓ |
| FLT pad (H1L) | **L** | normal-latency filter |
| DEMP pad (H2L) | **L** | de-emphasis off |
| **XSMT pad (H3L)** | **H** | **UNMUTE** — ships LOW = silent; #1 gotcha |
| FMT pad (H4L) | **L** | I²S |

### Decoupling (place at the pins)
- PCM1808: **VCC, VDD, VREF each → 0.1 µF + 10 µF**. VREF (pin 1) → AGND. Do **not** drive VREF.
- PCM5102A (module already populates): AVDD/CPVDD/3.3V → 0.1 µF + 10 µF; charge-pump flying caps 2.2 µF; output RC **470 Ω + 2.2 nF** per channel.
- AGND ≡ DGND tied directly under each chip; single return to a Pi GND pin physically near the I²S pins.

---

## Guitar analog front-end (the missing piece)
Mono guitar → **VINL** only. Single 5 V supply, everything biased to an independent **VBIAS = 2.5 V** mid-rail (its own 10k/10k divider + cap — **not** the ADC's VREF pin, which is decouple-only). VBIAS and the ADC's internal 2.5 V are equal, so the AC-coupled handoff to VINL is seamless.

```
 ¼" tip ─┤(Cin 0.1µF)├─┬─────────────[+]\
                       │              op-amp A ── buffer out ──┤(Rg 10k)├──[-]\
                    [R 1MΩ to VBIAS]  (×1, Zin≈1MΩ)                           op-amp B ──┬─┤(Cout 1µF)├─[Rs 1k]─┬─► VINL (pin13)
                       │                                       VREF─[-]ref   (×3 nom)   │                     │
                      VBIAS(2.5V)                              Rf=10k+100k trimpot (×2..×12)              [Ca 1nF to AGND]
 VINR (pin14) ─┤(1µF)├─ AGND   (AC-ground unused R channel; keep its internal 2.5V bias)
```

**Stage roles & values (verified):**
1. **High-Z buffer** — AC-couple the jack (`Cin` 0.1 µF) into a **1 MΩ bias resistor to VBIAS**, then a unity-gain op-amp. The 1 MΩ (not the ADC's 60 kΩ) is what the pickup sees → mimics a guitar amp, preserves the resonant-peak treble. HPF = 1/(2π·1 M·0.1 µF) ≈ **1.6 Hz** (well below low-E 82 Hz).
2. **Trimmable gain** — non-inverting, referenced to VBIAS. `Av = 1 + Rf/Rg`, `Rg = 10 kΩ` fixed, `Rf = 10 kΩ + 100 kΩ trimpot (RV1)` → **×2…×12 (+6…+21.6 dB)**. **Default ×3 (+9.5 dB).**
3. **AC-couple to ADC** — `Cout` 1 µF into VINL (which self-biases to 2.5 V). HPF ≈ 2.7 Hz into 60 kΩ.
4. **Gentle anti-alias** — `Rs` 1 kΩ + `Ca` 1 nF at VINL → fc ≈ 159 kHz (the ΔΣ + on-chip 1.3 MHz AA does the real work; this is just RF cleanup). Keep Rs ≪ 60 kΩ.

**Headroom (×3, output centered at 2.5 V, FS window 1.0–4.0 V):**
| Source | level | at ×3 | % FS |
|--------|------:|------:|-----:|
| Single-coil normal | ~0.2 Vpp | 0.6 Vpp | 20% |
| Single-coil hard | ~0.4 Vpp | 1.2 Vpp | 40% |
| Humbucker hard | ~0.7 Vpp | 2.1 Vpp | 70% |
| Hot-HB transient | ~1.0 Vpp | 3.0 Vpp | **100% (exactly FS — no clip)** |

→ Default ×3 is clean even for hot humbuckers; trim **up** for quiet single-coils, **down** if a clip LED triggers. (×4 would put the hot-HB transient at 4 Vpp = 33% over FS → clipping. That's the corrected item.)

**Op-amp:** dual, **5 V single supply**, output must reach within ~1 V of each rail at the load. **OPA1662** (low-noise audio) preferred; **OPA2353/OPA2350** if you want guaranteed input+output rail-to-rail on a single 5 V. Avoid NE5532/TL07x here (won't swing to 1 V on a single 5 V rail).

---

## Level & compatibility summary
- Pi → PCM1808 inputs: Pi drives 3.3 V ≫ 2 V VIH min. ✓ (SCKI/MD/FMT are 5 V-tolerant; BCK/LRCK in slave mode are *not* 5 V-tolerant, but a 3.3 V Pi never violates that.)
- PCM1808 DOUT (VOH ≥ 2.8 V) → Pi GPIO (reads >2.0 V high). ✓
- RP1 GPIO is **3.3 V CMOS, not 5 V tolerant** — never feed a 5 V logic signal back into GPIO20. Default drive 4 mA is fine for short traces; bump to 8–12 mA for longer runs / fan-out.

## Risk / gotcha register
| Item | Severity | Mitigation |
|------|----------|------------|
| Wiring guitar straight to VINL | **High** | front-end buffer+gain (above) — never direct |
| PCM5102A XSMT pad = mute | High | bridge **H3L = HIGH** |
| PCM1808 analog on 3.3 V | High | analog VCC = **5 V**, clean |
| Front-end op-amp on 3.3 V | High | op-amp on **5 V** rail (needs 4.0 V swing) |
| Fixed/high front-end gain clips | Med | **×3 default**, trim + clip LED |
| Header 3V3 for analog | Med | 5 V + local LDO; star ground |
| 5 V on BCK/LRCK/DOUT | Med | never — all from 3.3 V Pi |
| MD/FMT/SCK set after power-on | Low | strap before power-on |

## Sources
TI PCM1808 SLES177B (Pin Functions, §6.1/6.3/6.5, §8.2 Fig 26, §9, §10 Fig 27); TI PCM5102A SLAS859C (Pin Functions, §9.3.5 BCK-PLL, Table 11, output 2.1 Vrms, Fig 33); RP1 Peripherals datasheet §3.1/§3.1.1 Table 4 (I²S0=a2, GPCLK0=a0), §3.1.3 pads; RPi white paper RP-009699-WP-1 Table 3/5; RPi 3-pin Debug Connector spec RP-003139-SP; GY-PCM5102 pad map (mt32-pi wiki, arduino-audio-tools #773, makerguides). Plus this project's [claim-verification.md](claim-verification.md) for the digital-side facts (MCLK, strapping, pin map).
