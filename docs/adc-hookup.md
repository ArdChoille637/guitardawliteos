# ADC capture hookup — Pi 5 + PCM1808 + Nano ESP32 (bench card)

Every wire for the Milestone-2 **capture** side: power, ground, clocks, data,
straps. Sources: [hardware-review.md](hardware-review.md) (rails/decoupling),
[kicad/gen.py](../kicad/gen.py) netlist (pin numbers), [esp32-mclk/](../esp32-mclk/)
(MCLK generator). The DAC/playback side is in [breadboard-build.md](breadboard-build.md) step 2.

PCM1808 pins below are the **bare TSSOP-14 chip** (on a DIP adapter).
Using a **GY-PCM1808 module** instead: it takes 5 V + GND only (own onboard
regulator — skip the VDD/VREF/strap rows) and the signal pins carry the same
roles on the silkscreen, typically `SCK`=SCKI, `BCK`=BCK, `LRC`=LRCK,
`OUT`=DOUT — **verify against your module's silkscreen before wiring.**

## Power

| Device | Powered by | Notes |
|---|---|---|
| **Pi 5** | Official 27 W USB-C PD supply | Must negotiate the 5 V/5 A mode — under-voltage events get logged by the kernel. |
| **Nano ESP32** | Its **own USB-C** (wall adapter or computer) | Do **not** feed 5 V into its VIN pin — VIN expects 6–21 V through the onboard buck. USB-C is the clean option. Electrically independent of the Pi except MCLK + shared ground. |
| **PCM1808** | From the Pi header (rows below) | Two rails: **analog VCC = 5 V** (full-scale and VREF are ratiometric to it — do not run it at 3.3 V), digital VDD = 3.3 V. |

## Ground (star point = Pi J8 pin 6)

| From | To |
|---|---|
| Pi **J8 pin 6** (GND) | breadboard ground rail — the single star point |
| Nano ESP32 **GND** pin | ground rail |
| PCM1808 **AGND (pin 2)** + **DGND (pin 5)** | ground rail (AGND ≡ DGND, one star — no separate ground islands) |

## PCM1808 power + straps (bare chip; straps BEFORE power-on)

| PCM1808 pin | Connect to | Why |
|---|---|---|
| **3 VCC** | Pi **J8 pin 2** (5 V) — ferrite bead in series if you have one; **0.1 µF + 10 µF** at the pin | Analog rail: sets full-scale 3 Vpp and VREF 2.5 V |
| **4 VDD** | Pi **J8 pin 1** (3.3 V) + **0.1 µF + 10 µF** | Digital rail (~6 mA, header 3V3 is fine for it) |
| **1 VREF** | **0.1 µF + 10 µF to GND** — nothing else | Decouple only; never drive it |
| **10 MD0** | GND | Slave mode, 256/384/512fs autodetect |
| **11 MD1** | GND | (with MD0=GND) |
| **12 FMT** | GND | I²S, 24-bit |

## Clocks + data

| Signal | From | To | Rate |
|---|---|---|---|
| **MCLK** | Nano ESP32 **D2** (GPIO5) | PCM1808 **SCKI (pin 6)** | 12.288 MHz (256 fs) |
| **BCLK** | Pi **J8 pin 12** (GPIO18) | PCM1808 **BCK (pin 8)** | 3.072 MHz (64 fs) |
| **LRCLK** | Pi **J8 pin 35** (GPIO19) | PCM1808 **LRCK (pin 7)** | 48 kHz |
| **Capture data** | PCM1808 **DOUT (pin 9)** | Pi **J8 pin 38** (GPIO20) | 24-in-32, stereo |

No level shifters anywhere: PCM1808 DOUT's 2.8 V high is fine into the Pi,
and the Pi's / Nano ESP32's 3.3 V clocks are fine into the PCM1808.

## Analog input

| PCM1808 pin | Connect to |
|---|---|
| **13 VINL** | The guitar **front-end** output ([schematic.md](schematic.md)). For a first smoke test without the front-end: a **line-level** source through a **1 µF** cap. Never the guitar directly. |
| **14 VINR** | **1 µF to GND** (AC-ground the unused channel) |

## Order of operations

1. Wire everything **unpowered**; double-check the straps (MD0/MD1/FMT all GND) and rail polarity.
2. **Remove the D2→D3 self-test jumper** on the Nano ESP32 (it was only for the PCNT frequency check).
3. Power the Nano ESP32 first — its serial monitor should print the MCLK banner; D2 now carries 12.288 MHz.
4. Power the Pi. Boot log: `I2S full duplex (TXRX): RUNNING`.
5. Feed VINL a signal → the stats line's `peak` % should come alive (that's core 1's level meter reading real ADC samples).
6. `starve`/`drop`/`laps` should all stay frozen/zero, same as the codec-less first boot.

⚠️ The #1 capture gotcha: **no MCLK = DOUT stays silent** (all zeros, `peak 0%`).
If `peak` never moves, scope/check the SCKI line first, then the straps.
