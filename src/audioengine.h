// SPDX-License-Identifier: GPL-3.0-or-later
//
// GuitarDAWLiteOS — Milestone 3 audio engine (core 0)
//
// Full-duplex I2S on the RP1 (one device object, DeviceModeTXRX, Pi is
// clock master): PCM1808 capture in, PCM5102A playback out. The data
// path overrides GetChunk/PutChunk directly — the lowest-latency pattern
// Circle offers — instead of the base class's spinlocked byte-copy queues.
//
// Execution context contract (verified against Circle source, see
// docs/claim-verification.md and the M3 design notes in build-plan.md):
//   * Both callbacks run on CORE 0 in RP1-DMA-completion IRQ context,
//     serialized under the driver's own spinlock — so the monitor FIFO
//     they share needs no atomics of its own.
//   * GetChunk MUST return the full word count every time: returning
//     less permanently stops the stream (Cancel + StopI2S in the driver).
//   * There is no hardware underrun/overrun signal in the DMA path; the
//     cyclic DMA silently replays stale data if a refill is late. Our
//     starved/dropped counters are the only visibility.
//
#ifndef _gdaw_audioengine_h
#define _gdaw_audioengine_h

#include <circle/sound/i2ssoundbasedevice.h>
#include <circle/interrupt.h>
#include <circle/types.h>

#include "config.h"
#include "ringbuffer.h"
#include "audiostats.h"

typedef CBroadcastRing<GDAW_RING_LOG2_WORDS> TCaptureRing;

class CAudioEngine : public CI2SSoundBaseDevice
{
public:
	CAudioEngine (CInterruptSystem *pInterrupt, TCaptureRing *pRing,
		      TAudioStats *pStats);

private:
	// TX refill: mix the monitor FIFO (and, later, loop playback) into
	// the outgoing DMA buffer. IRQ context, core 0, driver spinlock held.
	unsigned GetChunk (u32 *pBuffer, unsigned nChunkSize) override;

	// RX complete: publish captured audio to the broadcast ring for the
	// analysis/storage cores and queue it for the monitor path.
	// IRQ context, core 0, driver spinlock held.
	void PutChunk (const u32 *pBuffer, unsigned nChunkSize) override;

private:
	TCaptureRing	*m_pRing;
	TAudioStats	*m_pStats;

	// Monitor pass-through FIFO, RX -> TX. Plain (non-atomic) indices:
	// both ends are touched only from the serialized IRQ callbacks above.
	static const unsigned MONITOR_WORDS =
		GDAW_MONITOR_FIFO_CHUNKS * GDAW_CHUNK_WORDS;
	static_assert ((MONITOR_WORDS & (MONITOR_WORDS-1)) == 0,
		       "monitor FIFO indexing masks require a power of two");
	u32		m_MonitorFifo[MONITOR_WORDS];
	u64		m_nMonitorHead;		// words pushed
	u64		m_nMonitorTail;		// words popped
};

#endif
