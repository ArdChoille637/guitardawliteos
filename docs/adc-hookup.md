# ADC capture hookup — Pi 5 + PCM1808 breakout + Nano ESP32 (bench card)

> 🔒 **For the permanent soldered board, [solder-build.md](solder-build.md) overrides this card.**
> This file is the *solderless bench* procedure. Three things differ on the permanent build:
> the analog-5 V RC is **unconditional** (the "before adding parts, measure" gate below is
> unrunnable — the silence FFT needs a working capture path); the 5 V rail **splits**, with the
> DAC tapped upstream of the 10 Ω; and the Nano's **D2→D3 link stays** as a permanent MCLK health
> readout instead of being removed. Straps, pin map and clock topology below are unchanged and correct.

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
| **GND** (right) | AGND/DGND | second ground pin — on the **soldered** build use it as the origin of the return conductor bundled with BCK/LRC/OUT ([solder-build.md](solder-build.md) §3); on a solderless bench one pin is enough |
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

**Two configurations. The bench runs A today; B is the target.**

**A — BENCH (current, three supplies):**

| Device | Powered by |
|---|---|
| Pi 5 | official 45 W USB-C PD |
| Nano ESP32 | **preferred:** Gator 9 V → **VIN** (in spec: VIN wants 6–21 V) · *fallback:* its own USB-C. **Never 5 V into VIN.** |
| PCM1808 module | **+5V** ← Pi J8 **pin 2** and **3.3** ← Pi J8 **pin 1** — both required |
| TL072 front-end | Gator 9 V → CopperSound DC jack (separate domain, shared ground) |

**B — TARGET (single 18 V pack):** see [power-tree-18v.md](power-tree-18v.md).
Only the *upstream* source differs; the codec rails below are identical in both.

> **Taking the Nano off USB.** The Gator already supplies 9 V on the bench, and
> the Nano's VIN window is 6–21 V, so tapping it for VIN frees the USB slot *and*
> rehearses the target topology (where VIN comes off the 7809 rail). Two
> consequences to plan for:
> 1. **You lose the 1 Hz MCLK health print**, which goes out over the Nano's USB
>    serial. That print is currently the only continuous proof the clock domain is
>    alive — see `esp32-mclk/esp32-mclk.ino`. Replace it with the Pi-side check in
>    [boot-selfcheck.md](boot-selfcheck.md) before unplugging.
> 2. **The Nano now shares the analog 9 V supply with the TL072.** Keep them as
>    separate spokes, rely on the `C_byp` 100 µF ∥ 0.1 µF at U1 pin 8, and disable
>    the ESP32's WiFi/BT radio — this board is a dedicated clock generator and the
>    radio buys nothing but current spikes on an audio rail.
>
> ⚠ **The three-supply bench inherits two rules that config B removes**: the
> power-up ordering in [Order of operations](#order-of-operations) below, and the
> load-bearing star-ground jumper — with independent supplies, a dropped ground
> jumper routes MCLK return plus inter-supply Y-cap leakage through the PCM1808's
> SCKI input clamp. Both matter **now**.

Star ground = Pi J8 **pin 6**: module GND, Nano GND, CopperSound GND rail,
input **–** pad — one **DC** point, no DC ground loops.

> **⚠ Analog-5V filter was dropped from this card — restore it (M2 review A1).** The
> PCM1808's full-scale is *ratiometric* (FS = 0.6·VCC, VREF = 0.5·VCC), so ripple on
> the +5 V pin multiplies onto every sample (Science: 50 mVpp/5 V ≈ −52 dBc sidebands;
> the chip's own floor is ~12 µVrms, so even faint in-band rail coupling can exceed it).
> The original power tree specified a **ferrite + 10 µF + 0.1 µF** at the VCC pin; it
> never made it onto the as-built sheets. **Before adding parts, measure:** record
> silence and FFT it — if the Pi rail is already quiet in-band, you're done. If not, a
> **ferrite alone is ≈0 dB in-band** (Science) — use an **RC (≈10 Ω + 470 µF ∥ 0.1 µF)**
> on the +5 V pin (≈90 mV drop at ~9 mA, still ≥ 4.5 V VCC min).

> **HF clock returns (M2 review A3) — a DC star is right for audio, wrong for MHz clocks.**
> "One point" is a *DC* rule. The 12.288 MHz MCLK (Nano D2→SCK) and the 3.072 MHz
> BCK/LRC/OUT wires want their return current running *beside* them, which the lone star
> spoke doesn't provide. Best practice, and it does **not** break the DC star: run a
> **dedicated ground wire paired/twisted with the MCLK line** (Nano→ADC) and another with
> the BCK/LRC/OUT bundle, each still landing at pin 6 — a paired HF return is *not* a
> forbidden "ground island." Add **~33 Ω series at the Nano D2 pin** (tames edge ringing
> and doubles as the fault-current limit if a ground jumper drops — review B2). Give the
> **Nano two ground wires** so a single dropped jumper never routes clock return through
> the SCK clamp. *(Science: the audio-band residue of this bounce is small — MHz energy
> folds out of band — so these are robustness/EMI hygiene, not a measured-noise fix.)*

## Order of operations

1. Wire everything **unpowered**. Straps: FMT→GND, **MD0 and MD1→3.3 V rail**
   (TI requires mode pins set before power-on). Double-check no 5 V touches
   a 3.3 pin — **and verify it**: with the module's two power leads *not yet
   landed*, bring the supplies up and meter the two rail rows (expect 5.0 V and
   3.3 V), then power down again and connect the module. J8 pins 1 (3.3 V) and 2 (5 V) are
   physically adjacent, so a one-row slip puts 5 V on the 4 V-abs-max VDD pin.
2. **Leave the D2→D3 link in place** — the frozen M2 wiring keeps it as a
   permanent MCLK health readout, tapped *before* the 33 Ω (see the banner at
   the top of this file). *(This step previously said to remove it; that
   predates the freeze.)*
3. **Config A (bench, three supplies) — order matters.** Power the **Nano
   first** (D2 carries 12.288 MHz), then the **Pi**, then the **Gator 9 V**.
   **Power down in reverse** (Gator, Pi, Nano). *(Nano-first is verified safe —
   review B1: the PCM1808's SCK/MD/FMT inputs are rated −0.3…+6.5 V independent
   of VDD, so 3.3 V into an unpowered ADC injects no fault current.)* Before
   touching any wire later, **kill all three supplies first.**
   *(Config B collapses this to one master switch —
   [power-tree-18v.md](power-tree-18v.md).)*
4. Boot log: `I2S full duplex (TXRX, slave - ADC masters the bus): RUNNING`.
   **`cap`/`play` only start counting once the ADC is wired, powered, and
   clocked** — a bare-board boot correctly shows 0s now (unlike the old
   master-mode kernel).
5. Feed LIN a signal → `peak %` comes alive. `starve`/`drop`/`laps` stay 0.

## Debug ladder (capture dead?)

> **Power down all three supplies before reseating any wire or strap.** This ladder
> sends your hands to live rows; a strap jumper slipping from the 3.3 V row into the
> adjacent 5 V row is the exact way the ADC's 4 V-abs-max VDD pin dies.

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
