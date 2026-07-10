// SPDX-License-Identifier: GPL-3.0-or-later
//
// GuitarDAWLiteOS — cross-core audio statistics
//
// Plain shared counters, written by their owning core, read by the stats
// reporter on core 3. Every field is accessed exclusively through the
// __atomic helpers below: RELAXED is sufficient — these are monotonic
// telemetry counters, not synchronization; nobody makes control-flow
// decisions that require ordering against the audio payload. (Circle's
// own AtomicX() API is 32-bit only, and there is no <atomic> in the
// freestanding build — 64-bit counters use the builtins directly.)
//
#ifndef _gdaw_audiostats_h
#define _gdaw_audiostats_h

#include <stdint.h>

inline uint64_t StatGet (const volatile uint64_t *p)
{
	return __atomic_load_n (p, __ATOMIC_RELAXED);
}

inline void StatSet (volatile uint64_t *p, uint64_t v)
{
	__atomic_store_n (p, v, __ATOMIC_RELAXED);
}

inline void StatAdd (volatile uint64_t *p, uint64_t v)
{
	__atomic_fetch_add (p, v, __ATOMIC_RELAXED);
}

struct TAudioStats
{
	// core 0 (I2S IRQ callbacks)
	volatile uint64_t nFramesCaptured;	// frames received from the ADC
	volatile uint64_t nFramesPlayed;	// frames handed to the DAC
	volatile uint64_t nMonitorStarved;	// GetChunk found the monitor FIFO empty
	volatile uint64_t nMonitorDropped;	// PutChunk found the monitor FIFO full

	// core 1 (analysis)
	volatile uint64_t nAnalysisFrames;	// frames consumed by the level meter
	volatile uint64_t nPeakAbs24;		// running peak |sample|, 24-bit domain
	volatile uint64_t nAnalysisOverruns;	// ring laps seen by reader 0

	// core 2 (storage drain)
	volatile uint64_t nStorageBytes;	// bytes drained (future: written to SD)
	volatile uint64_t nStorageOverruns;	// ring laps seen by reader 1
};

// Sign-extend a 24-bit-in-u32 I2S sample to a proper int32_t.
//
// Circle's Signed24_32 puts the value in the LOW 24 bits — NOT left-
// justified in bits [31:8]. Evidence in Circle source: GetRangeMin/Max
// for Signed24_32 is ±(2^23-1) (soundbasedevice.cpp Setup), and
// ConvertReadSoundFormat treats Signed24_32 identically to Signed24
// (which masks & 0xFFFFFF) with no shift. So: shift the 24-bit value up
// to the sign bit, then arithmetic-shift back down.
inline int32_t SignExtend24 (uint32_t nWord)
{
	return (int32_t) (nWord << 8) >> 8;
}

#endif
