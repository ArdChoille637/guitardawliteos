# Boot landing + clock-domain self-check

**Status: proposal (2026-07-28).** Nothing here is implemented yet.

## The problem it solves

Today `CKernel::Run()` starts the audio engine and reports:

```
I2S full duplex (TXRX, slave - ADC masters the bus): RUNNING
slave mode: cap/play stay 0 until the PCM1808 is wired, powered and fed MCLK - that is expected
```

`RUNNING` here means *the Pi's own I2S peripheral configured itself successfully*
— nothing more. Because the Pi is now the **slave**, that message is printed
identically whether the rest of the rig is perfect or entirely absent. A dead
Nano, an unplugged SCKI wire, an unpowered ADC, a mis-strapped MD0/MD1, and a
correct-but-idle board all produce the same log line and the same `cap/play = 0`.

The one thing that currently distinguishes them is the Nano's 1 Hz PCNT print
over **USB serial** (`esp32-mclk/esp32-mclk.ino`) — which disappears the moment
the Nano is powered from VIN instead of USB, as the bench is moving toward
(see [adc-hookup.md](adc-hookup.md)). Removing the USB cable removes the only
continuous evidence that the clock domain is alive.

So the self-check is not a nicety: it is what replaces that evidence.

## The key observation — no new wires needed

**The ADC masters BCK. BCK only toggles if SCKI is present.**

That makes GPIO18 (J8 p12) an end-to-end health probe for the entire clock
chain — Nano MCLK → 33 Ω → SCKI → PCM1808 divider → BCK — using a pin the
design already owns. Before configuring the I2S peripheral, the landing phase
can claim GPIO18 as a plain input and sample it:

| GPIO18 | Meaning |
|---|---|
| toggling ~3.072 MHz | clock domain healthy — proceed to start I2S |
| static high/low | clock domain dead — Nano stopped, SCKI open, ADC unpowered, or MD0/MD1 strapped wrong |

Then, once I2S is running:

| BCK | frames | Diagnosis |
|---|---|---|
| toggling | counting | healthy |
| toggling | 0 | clock fine, **data path** broken — DOUT wire, DMA, or format |
| static | 0 | clock domain dead — do not blame the data path |

That split is the whole value. It converts "cap/play stay 0, that is expected"
into a specific, actionable diagnosis, and it costs zero hardware.

Sampling detail: at 3.072 MHz a poll loop will alias badly, so sample for a
fixed window (say 1 ms) and look for *any* edge, rather than trying to measure
frequency. Presence/absence is all that is needed — the Nano's own PCNT already
verifies the frequency at source.

## Phase 2 — the reset line (this is the "reboot command")

Detection alone tells you the clock is dead; it cannot fix it. One wire adds
recovery:

```
Pi GPIO ──► Nano ESP32 RST      (active low, pulse ~10 ms)
```

The landing sequence becomes:

1. Sample BCK. If alive → start I2S, done.
2. If dead, wait ~1 s (the Nano's Arduino boot is slower than the Pi's
   bare-metal boot — on a single master switch the Pi *will* usually win the
   race), re-sample.
3. Still dead → pulse Nano RST, wait for its boot, re-sample.
4. After N attempts → stop and display a specific fault on the landing screen
   rather than booting into a silently dead rig.

Step 2 matters more than it looks: **it removes the power-up ordering rule as a
correctness requirement.** The current "Nano first, then Pi, then Gator" exists
precisely because nothing downstream tolerates a late clock. A retry loop makes
the order a convenience rather than a constraint — which is exactly what the
single-master-switch 18 V target needs.

### GPIO cost — and the honest problem with it

`retro-deck-design.md` §4 allocates **all 28** J8 GPIOs. There is no free pin.
Three options, in order of preference:

1. **Borrow GPIO10 or 11** while the APA102/SK9822 bargraph is deferred
   (2026-07-28). Free today, but it collides with the standing rule to keep
   10/11 for SPI0, and would have to move when the strip returns.
2. **Put RST on the MCP23017 expander** (I²C1, GPIO2/3). Costs no J8 pin. The
   objection is bootstrap ordering — the expander must be up before the clock
   check can recover anything — but I²C is cheap to bring up early in bare
   metal, and this is a slow signal.
3. **Skip phase 2 entirely.** Phase 1 alone delivers the diagnosis; a human can
   press the Nano's reset button. Worth considering, since a wedged ESP32 has
   not actually been observed — the failure this guards against is
   hypothetical, while the *detection* it depends on is not.

**Recommendation: build phase 1 now, defer phase 2** until either a wedged Nano
is observed in practice or the expander lands. Phase 1 is free, has no GPIO
argument to settle, and is what restores the visibility lost when the Nano's USB
cable comes out.

## What "landing" should show

A boot screen that reports each check pass/fail before entering the audio loop:

| Check | Method | Failure means |
|---|---|---|
| Screen / serial | existing `m_bScreenOK` / `m_bSerialOK` | no console — already handled |
| **Clock domain** | BCK edge probe on GPIO18 | Nano, SCKI wire, ADC power, or MD straps |
| I2S peripheral | existing `m_AudioEngine.Start()` | Pi-side config fault |
| **Frames advancing** | `cap`/`play` counters non-zero after ~100 ms | data path: DOUT, DMA, format |
| Thermal | existing `CCPUThrottle` | cooling |

Failing checks should not necessarily halt — the existing design deliberately
lets the thermal loop run with dead audio — but they must be **stated**, not
inferred from a zero counter.

## Open questions

1. Does the Circle bare-metal GPIO API allow claiming GPIO18 as an input and
   then handing it to the I2S peripheral cleanly, or does the I2S driver need to
   own the pin mux for the whole run? This decides whether the probe can be a
   pre-flight step or has to be a one-shot at the very start.
2. Is a wedged-Nano failure mode real, or hypothetical? That decides phase 2.
3. If phase 2 goes ahead: borrow GPIO10/11, or expander?
