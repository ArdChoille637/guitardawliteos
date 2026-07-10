// SPDX-License-Identifier: GPL-3.0-or-later
//
// GuitarDAWLiteOS — Milestone 3 audio engine (core 0), see audioengine.h
//
#include "audioengine.h"

#include <circle/util.h>	// memset — no libc <string.h> in this freestanding build
#include <assert.h>

CAudioEngine::CAudioEngine (CInterruptSystem *pInterrupt, TCaptureRing *pRing,
			    TAudioStats *pStats)
:	// Clock topology per config.h: PCM1808 is bus master (divides the
	// external 12.288 MHz SCKI), Pi consumes BCK/LRCK as slave
	// (GDAW_I2S_SLAVE -> Circle uses the RP1 I2S1 instance, AltFn4 on
	// the same GPIO18-21). No I2C controller: the PCM5102A is
	// strap-configured and the PCM1808 has no control port at all
	// (pI2CMaster=0 makes the driver's codec-probe factory a no-op).
	CI2SSoundBaseDevice (pInterrupt, GDAW_SAMPLE_RATE, GDAW_CHUNK_WORDS,
			     GDAW_I2S_SLAVE, 0, 0, DeviceModeTXRX),
	m_pRing (pRing),
	m_pStats (pStats),
	m_nMonitorHead (0),
	m_nMonitorTail (0)
{
	assert (m_pRing != 0);
	assert (m_pStats != 0);
}

void CAudioEngine::PutChunk (const u32 *pBuffer, unsigned nChunkSize)
{
	// 1. Broadcast to the analysis (core 1) and storage (core 2) readers.
	//    Wait-free; a lagging reader loses old audio, never delays us.
	m_pRing->Write (pBuffer, nChunkSize);

	// 2. Queue for the monitor path (drop-oldest if TX has stalled, so
	//    monitor latency stays bounded instead of growing unbounded).
	u64 nFree = MONITOR_WORDS - (m_nMonitorHead - m_nMonitorTail);
	if (nFree < nChunkSize)
	{
		u64 nDrop = nChunkSize - nFree;
		m_nMonitorTail += nDrop;
		StatAdd (&m_pStats->nMonitorDropped, nDrop / 2);	// frames
	}

	for (unsigned i = 0; i < nChunkSize; i++)
	{
		m_MonitorFifo[(m_nMonitorHead + i) & (MONITOR_WORDS-1)] = pBuffer[i];
	}
	m_nMonitorHead += nChunkSize;

	StatAdd (&m_pStats->nFramesCaptured, nChunkSize / 2);
}

unsigned CAudioEngine::GetChunk (u32 *pBuffer, unsigned nChunkSize)
{
	// Live monitor: guitar in -> guitar out. (M4/M5 will mix recorded
	// loop playback in here as well — this is the mix point.)
	u64 nAvail = m_nMonitorHead - m_nMonitorTail;
	unsigned nCopy = nAvail < nChunkSize ? (unsigned) nAvail : nChunkSize;

	for (unsigned i = 0; i < nCopy; i++)
	{
		pBuffer[i] = m_MonitorFifo[(m_nMonitorTail + i) & (MONITOR_WORDS-1)];
	}
	m_nMonitorTail += nCopy;

	if (nCopy < nChunkSize)
	{
		// RX hasn't produced yet (startup phase skew) or fell behind:
		// pad with silence. NEVER return short — that kills the stream.
		memset (&pBuffer[nCopy], 0, (nChunkSize - nCopy) * sizeof (u32));
		StatAdd (&m_pStats->nMonitorStarved, (nChunkSize - nCopy) / 2);
	}

	StatAdd (&m_pStats->nFramesPlayed, nChunkSize / 2);

	return nChunkSize;
}
