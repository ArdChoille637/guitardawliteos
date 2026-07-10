// SPDX-License-Identifier: GPL-3.0-or-later
//
// GuitarDAWLiteOS — Milestone 3 secondary-core workers, see cores.h
//
#include "cores.h"

#include <circle/logger.h>
#include <circle/timer.h>
#include <assert.h>

static const char FromCores[] = "gdawcores";

// Per-core drain buffers. File-scope (not function-local statics: the
// __cxa_guard for function-local statics is one global spinlock — keep it
// off the worker hot paths entirely). Sized for the intended consumers:
//   analysis: 50 ms bites (4800 words = 2400 frames = 19.2 KB)
//   storage:  256 KB blocks (65536 words) — the M4 SD write granularity
static u32 s_AnalysisBuf[4800];
static u32 s_StorageBuf[65536];

CAppCores::CAppCores (CMemorySystem *pMemorySystem, TCaptureRing *pRing,
		      TAudioStats *pStats)
:	CMultiCoreSupport (pMemorySystem),
	m_pRing (pRing),
	m_pStats (pStats),
	m_nStartFlag (0)
{
	assert (m_pRing != 0);
	assert (m_pStats != 0);
}

void CAppCores::StartWorkers (void)
{
	__atomic_store_n (&m_nStartFlag, 1, __ATOMIC_RELEASE);
}

void CAppCores::WaitForStart (void)
{
	while (!__atomic_load_n (&m_nStartFlag, __ATOMIC_ACQUIRE))
	{
		CTimer::SimpleMsDelay (1);
	}
}

void CAppCores::Run (unsigned nCore)
{
	WaitForStart ();

	switch (nCore)
	{
	case 1:		AnalysisLoop ();	break;
	case 2:		StorageLoop ();		break;
	case 3:		StatsLoop ();		break;
	default:	break;			// core halts (Circle semantics)
	}
}

// ── core 1: analysis (level meter now, YIN pitch in M6) ─────────────────────

void CAppCores::AnalysisLoop (void)
{
	TCaptureRing::TReader Reader;
	m_pRing->InitReader (&Reader);

	CLogger::Get ()->Write (FromCores, LogNotice, "analysis running on core 1");

	u64 nWindowPeak = 0;
	u64 nWindowFrames = 0;
	const u64 WINDOW_FRAMES = GDAW_SAMPLE_RATE / 4;	// 250 ms meter window

	while (1)
	{
		unsigned nWords = m_pRing->Read (&Reader, s_AnalysisBuf,
						 sizeof s_AnalysisBuf / sizeof s_AnalysisBuf[0]);
		if (nWords == 0)
		{
			CTimer::SimpleMsDelay (5);
			continue;
		}

		for (unsigned i = 0; i < nWords; i++)
		{
			int32_t nSample = SignExtend24 (s_AnalysisBuf[i]);
			u64 nAbs = nSample < 0 ? (u64) -(int64_t) nSample : (u64) nSample;
			if (nAbs > nWindowPeak)
			{
				nWindowPeak = nAbs;
			}
		}

		StatAdd (&m_pStats->nAnalysisFrames, nWords / 2);
		StatSet (&m_pStats->nAnalysisOverruns, Reader.nOverruns);

		nWindowFrames += nWords / 2;
		if (nWindowFrames >= WINDOW_FRAMES)
		{
			uint64_t currentMax = StatGet (&m_pStats->nPeakAbs24);
			if (nWindowPeak > currentMax)
			{
				StatSet (&m_pStats->nPeakAbs24, nWindowPeak);
			}
			nWindowPeak = 0;
			nWindowFrames = 0;
		}
	}
}

// ── core 2: storage drain (FatFs f_write lands here in M4) ───────────────────

void CAppCores::StorageLoop (void)
{
	TCaptureRing::TReader Reader;
	m_pRing->InitReader (&Reader);

	CLogger::Get ()->Write (FromCores, LogNotice, "storage drain running on core 2");

	const unsigned BLOCK_WORDS = sizeof s_StorageBuf / sizeof s_StorageBuf[0];

	while (1)
	{
		// Batch up: wait until a full block is available, then drain it
		// in one go — the exact cadence the M4 SD writer will use
		// (large 4-byte-aligned multiples of 512 straight into f_write).
		if (m_pRing->Available (&Reader) < BLOCK_WORDS)
		{
			CTimer::SimpleMsDelay (20);
			continue;
		}

		unsigned nWords = m_pRing->Read (&Reader, s_StorageBuf, BLOCK_WORDS);

		StatAdd (&m_pStats->nStorageBytes, (u64) nWords * sizeof (u32));
		StatSet (&m_pStats->nStorageOverruns, Reader.nOverruns);
	}
}

// ── core 3: stats reporter (framebuffer UI lands here in M5) ─────────────────

void CAppCores::StatsLoop (void)
{
	CLogger::Get ()->Write (FromCores, LogNotice, "stats reporter running on core 3");

	while (1)
	{
		CTimer::SimpleMsDelay (2000);

		u64 nCaptured = StatGet (&m_pStats->nFramesCaptured);
		u64 nPlayed   = StatGet (&m_pStats->nFramesPlayed);
		u64 nStarved  = StatGet (&m_pStats->nMonitorStarved);
		u64 nDropped  = StatGet (&m_pStats->nMonitorDropped);
		u64 nPeak     = StatGet (&m_pStats->nPeakAbs24);
		StatSet (&m_pStats->nPeakAbs24, 0); // clear after reading
		u64 nDrained  = StatGet (&m_pStats->nStorageBytes);
		u64 nOverA    = StatGet (&m_pStats->nAnalysisOverruns);
		u64 nOverS    = StatGet (&m_pStats->nStorageOverruns);

		// Peak as percent of 24-bit full scale (2^23-1) — integer math,
		// no float needed for a status line.
		unsigned nPeakPct = (unsigned) (nPeak * 100u / 8388607u);

		CLogger::Get ()->Write (FromCores, LogNotice,
			"cap %llu  play %llu  starve %llu  drop %llu  "
			"peak %u%%  sd %llu KB  laps %llu/%llu",
			(unsigned long long) nCaptured,
			(unsigned long long) nPlayed,
			(unsigned long long) nStarved,
			(unsigned long long) nDropped,
			nPeakPct,
			(unsigned long long) (nDrained / 1024),
			(unsigned long long) nOverA,
			(unsigned long long) nOverS);
	}
}
