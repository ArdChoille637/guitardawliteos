// SPDX-License-Identifier: GPL-3.0-or-later
//
// GuitarDAWLiteOS — Milestone 3 secondary-core workers
//
// Core assignments (build plan 3.3):
//   core 1: pitch/BPM analysis — for now a peak level meter over the
//           capture ring (the YIN tuner lands here in M6)
//   core 2: SD writer — for now drains the ring in large blocks and
//           counts bytes (FatFs f_write lands here in M4; FatFs must
//           only ever be called from this core, never from IRQ)
//   core 3: UI — for now a periodic stats line via the logger (the
//           framebuffer waveform UI lands here in M5)
//
// Startup contract: CMultiCoreSupport::Initialize() (called LAST in
// CKernel::Initialize(), which also arms Circle's spinlocks) launches
// cores 1-3 immediately — before core 0 has started the audio device.
// Workers therefore spin on an atomic start flag until core 0 releases
// them from CKernel::Run(). If a worker's Run() ever returns, that core
// halts permanently (Circle semantics), so workers loop forever.
//
#ifndef _gdaw_cores_h
#define _gdaw_cores_h

#include <circle/multicore.h>
#include <circle/memory.h>
#include <circle/types.h>

#include "config.h"
#include "ringbuffer.h"
#include "audiostats.h"
#include "audioengine.h"	// for TCaptureRing

class CAppCores : public CMultiCoreSupport
{
public:
	CAppCores (CMemorySystem *pMemorySystem, TCaptureRing *pRing,
		   TAudioStats *pStats);

	// Called by core 0 once the audio device is running; releases the
	// worker loops on cores 1-3.
	void StartWorkers (void);

	void Run (unsigned nCore) override;

private:
	void WaitForStart (void);

	void AnalysisLoop (void);	// core 1
	void StorageLoop (void);	// core 2
	void StatsLoop (void);		// core 3

private:
	TCaptureRing	*m_pRing;
	TAudioStats	*m_pStats;

	u32		m_nStartFlag;	// __atomic; 0 = hold, 1 = go
};

#endif
