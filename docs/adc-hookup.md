# ADC capture hookup — Pi 5 + PCM1808 breakout + Nano ESP32 (bench card)

Every wire for the Milestone-2 **capture** side, written for the exact board
on hand: the **purple "CJMCU-1808" PCM1808 breakout** (Rakstore/HiLetgo,
~25×32 mm, four 10 µF electrolytics, two 6-pin headers, no input jack).
Board facts verified against the TI datasheet (SLES177B), the WLED
MoonModules line-in guide, and multiple project writeups (2026-07-10) —
no public schematic of this module exists, so anything single-source is
flagged. The DAC/playback side is in [breadboard-build.md](breadboard-build.md).

## ⚡ Clock topology (changed 2026-07-10 — read first)

**The PCM1808 is the I²S bus master, not the Pi.** It divides the Nano
ESP32's 12.288 MHz into BCK + LRCK itself and *drives* them; the Pi's I²S
runs as slave (already flipped in the kernel — `GDAW_I2S_SLAVE` in
[src/config.h](../src/config.h)). Why: in slave mode the PCM1808 demands
SCKI frequency-locked to LRCK — two free-running crystals (Nano vs Pi)
drift tens of ppm apart, and the chip's resync logic (datasheet §7.4.2)
would mute-and-fade the audio every fraction of a second. An RPi engineer
on the forums called the async arrangement a non-starter; ADC-as-master is
the community-standard fix. One clock domain, rooted in the Nano's crystal.
(The PCM5102A doesn't care — its PLL locks off whatever drives BCK.)

## The module's pins, mapped

| Silkscreen | Chip pin | Role in this build |
|---|---|---|
| **FMT** | 12 | → **GND** (I²S format) |
| **MD1** | 11 | → **3.3 V rail** (master mode…) |
| **MD0** | 10 | → **3.3 V rail** (…at 256 fs: 12.288 MHz ÷ 256 = 48 kHz) |
| **GND** (left) | AGND/DGND | → star ground |
| **3.3** | 4 (VDD) | ← **3.3 V INPUT** — there is **no onboard regulator**; both rails are required |
| **+5V** | 3 (VCC) | ← **5 V INPUT** (analog rail — genuinely needs 5 V; 3.3 V here causes random glitches) |
| **BCK** | 8 | → Pi **J8 pin 12** (GPIO18) — **ADC drives it** (3.072 MHz) |
| **OUT** | 9 (DOUT) | → Pi **J8 pin 38** (GPIO20) — capture data |
| **LRC** | 7 (LRCK) | → Pi **J8 pin 35** (GPIO19) — **ADC drives it** (48 kHz) |
| **SCK** | 6 (SCKI) | ← **Nano ESP32 D2** (12.288 MHz MCLK) |
| **GND** (right) | AGND/DGND | duplicate — connect either one, not both needed |
| **3.3V** (right) | 4 (VDD) | duplicate of "3.3" |

⚠️ **Never feed 5 V into the 3.3/3.3V pins** — chip VDD absolute max is 4 V;
5 V risks permanent damage.

**Analog input:** the three plated holes on the board edge (between the
header rows), order **RIN / – / LIN** (right in, audio ground, left in) —
from a reverse-engineered footprint, single source, so **sanity-check
against your board's markings**. Inputs are AC-coupled on-board (two of the
electrolytics) and the chip self-biases them — feed line-level directly to
the pads:

| Pad | Connect to |
|---|---|
| **LIN** | the TL072 front-end output ([tl072-frontend.md](tl072-frontend.md)) via the CopperSound board's output jack. Keep **Rs 1 k + Ca 1 nF** at this pad. For a smoke test: any line-level source. |
| **–** | star ground (this is the input's reference) |
| **RIN** | leave unconnected for now (on-board cap AC-terminates it; rev-B tape return lands here) |

## Power & ground

| Device | Powered by |
|---|---|
| Pi 5 | official 27 W USB-C PD |
| Nano ESP32 | its own USB-C (never 5 V into VIN — it wants 6–21 V) |
| PCM1808 module | **+5V** ← Pi J8 **pin 2** and **3.3** ← Pi J8 **pin 1** — both required |
| TL072 front-end | Gator 9 V → CopperSound DC jack (separate domain, shared ground) |

Star ground = Pi J8 **pin 6**: module GND, Nano GND, CopperSound GND rail,
input **–** pad — one rail, no islands.

## Order of operations

1. Wire everything **unpowered**. Straps: FMT→GND, **MD0 and MD1→3.3 V rail**
   (TI requires mode pins set before power-on). Double-check no 5 V touches
   a 3.3 pin.
2. **Remove the D2→D3 self-test jumper** on the Nano ESP32.
3. Power the Nano first (D2 carries 12.288 MHz), then the Pi.
4. Boot log: `I2S full duplex (TXRX, slave - ADC masters the bus): RUNNING`.
   **`cap`/`play` only start counting once the ADC is wired, powered, and
   clocked** — a bare-board boot correctly shows 0s now (unlike the old
   master-mode kernel).
5. Feed LIN a signal → `peak %` comes alive. `starve`/`drop`/`laps` stay 0.

## Debug ladder (capture dead?)

1. `cap` frozen at 0 → no bus clocks: check SCKI wire (Nano D2), Nano power,
   then MD0/MD1 straps (both must be HIGH or the ADC never masters the bus).
2. `cap` counting but `peak 0%` → clocks fine, no signal: check LIN wiring,
   front-end power, input **–** pad grounded.
3. Interrupting SCKI even briefly auto-powers-down the chip; on resume DOUT
   stays zero for ~1024 SCKI + 8960/fs and then **fades in** over ~1 ms —
   a beat of silence after clock glitches is the chip, not your wiring.
4. Quality caveat from the field: some batches of these modules shipped with
   wrong solder jumpers — if all else fails, inspect the board against the
   pin table above.
