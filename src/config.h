// SPDX-License-Identifier: GPL-3.0-or-later
//
// GuitarDAWLiteOS — Milestone 3 audio-engine configuration
//
// Central knobs for the real-time path. Everything here is a compile-time
// constant on purpose: the audio engine has no runtime configuration
// surface yet, and constants let the compiler fold the ring-index masks.
//
#ifndef _gdaw_config_h
#define _gdaw_config_h

// ── Audio format (fixed by the hardware path, do not change) ────────────────
// PCM1808 <-> RP1 I2S <-> PCM5102A. Circle's RP1 I2S driver is hard-wired to
// SoundFormatSigned24_32: each sample is a 24-bit two's-complement value in
// the LOW 24 bits of a u32 word, stereo interleaved L,R (L at even index).
#define GDAW_SAMPLE_RATE	48000

// ── Clock topology: the PCM1808 is the I2S clock MASTER ─────────────────────
// (2026-07-10 pivot, see docs/claim-verification.md.) The ADC divides the
// Nano ESP32's 12.288 MHz SCKI into BCK (64fs) + LRCK itself (strap
// MD0=MD1=HIGH: master, 256fs), and the Pi consumes those clocks as I2S
// SLAVE (Circle: bSlave=TRUE -> RP1 I2S1 instance, same GPIO18-21 pads via
// AltFn4). Why not Pi-as-master: the PCM1808 in slave mode requires SCKI
// frequency-locked to LRCK — with the Nano and the Pi free-running on
// separate crystals they drift tens of ppm apart, and the chip's clock-halt/
// resync logic (datasheet 7.4.2) would mute-and-fade every fraction of a
// second. One clock domain, rooted at the Nano's crystal, fixes it.
// The PCM5102A doesn't care who drives the bus: its PLL locks off BCK.
// NOTE: as slave, the engine produces NO callbacks until the ADC is wired,
// powered, and clocked — a bare-board boot shows cap/play frozen at 0.
// That is expected, not a fault.
#define GDAW_I2S_SLAVE	true

// ── Latency budget → I2S chunk size ─────────────────────────────────────────
// Circle chunk units: TOTAL u32 words per DMA buffer across BOTH channels,
// so frames per chunk = CHUNK_WORDS/2. The RP1 driver double-buffers each
// direction; worst-case monitor (in→out) latency is ~3 chunks:
//   RX fill (1 chunk) + wait for next TX refill (≤1) + queued TX buffer (1).
// CHUNK_WORDS=32 → 16 frames/chunk = 333 us/chunk → ≤1.0 ms worst case,
// inside the build plan's 0.67–1.33 ms target. IRQ rate: 3 kHz/direction —
// trivial for a dedicated 2.4 GHz A76 core doing a 128-byte copy per IRQ.
#define GDAW_CHUNK_WORDS	32

// ── Capture ring buffer ──────────────────────────────────────────────────────
// Single producer (core 0 IRQ) broadcasting to independent readers
// (core 1 = pitch, core 2 = SD writer). Capacity must be a power of two.
// 2^20 u32 words = 4 MiB = 524288 stereo frames ≈ 10.9 s @ 48 kHz — deep
// enough that the SD writer can drain in large 64–256 KiB blocks with
// enormous slack (Pi 5 has GiBs of RAM; 4 MiB is nothing).
#define GDAW_RING_LOG2_WORDS	20

// ── Ring reader identities ───────────────────────────────────────────────────
#define GDAW_READER_PITCH	0	// core 1: pitch/level analysis
#define GDAW_READER_STORAGE	1	// core 2: SD writer (M4)
#define GDAW_NUM_READERS	2

// ── Monitor pass-through FIFO (core 0 IRQ only) ──────────────────────────────
// Couples PutChunk (RX) to GetChunk (TX) for the live monitor path. Both
// callbacks run on core 0 in DMA-completion IRQ context under the driver's
// own spinlock, so this FIFO needs no atomics — but it must absorb the
// startup phase skew between the RX and TX streams. 8 chunks = 2.7 ms.
#define GDAW_MONITOR_FIFO_CHUNKS	8

// ── Core roles (build plan 3.3) ──────────────────────────────────────────────
// core 0: I2S DMA callbacks + mix (IRQ) + thermal/heartbeat (main loop)
// core 1: pitch/BPM analysis (M6: YIN; for now a level meter)
// core 2: SD writer (M4: FatFs; for now a draining stub)
// core 3: UI (M5: framebuffer; for now periodic stats over the logger)

#endif
