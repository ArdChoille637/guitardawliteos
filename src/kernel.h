// SPDX-License-Identifier: GPL-3.0-or-later
//
// GuitarDAWLiteOS — Milestone 3 kernel: multi-core audio engine
//
// Evolves the Milestone 0 bring-up kernel (headless-safe boot, Active
// Cooler thermal management, ACT-LED heartbeat — all retained) into the
// audio engine skeleton from planning/build-plan.md Milestone 3:
//
//   core 0: I2S full-duplex DMA callbacks (capture->monitor->playback
//           mix in IRQ context) + thermal/heartbeat main loop
//   core 1: analysis worker (level meter now, YIN pitch in M6)
//   core 2: storage drain worker (FatFs SD writer lands here in M4)
//   core 3: stats reporter (framebuffer UI lands here in M5)
//
// Capture audio fans out through a lock-free single-producer broadcast
// ring (ringbuffer.h) with independent per-reader cursors.
//
// HEADLESS-SAFE BY DESIGN (unchanged from M0): screen and serial are
// attempted but never gate boot; with neither attached the logger falls
// back to CNullDevice and the ACT-LED heartbeat + fan ARE the diagnostic.
//
// BUILD REQUIREMENT: the whole Circle tree must be built with
// -DARM_ALLOW_MULTI_CORE (scripts/build.sh enforces this) — the flag
// changes CSpinLock layout and atomic strength, so an app-only define
// would silently produce broken synchronization.
//
#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/nulldevice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/cputhrottle.h>
#include <circle/types.h>

#include "audioengine.h"
#include "cores.h"

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);

	TShutdownMode Run (void);

private:
	// Called by CCPUThrottle::Update() when the firmware reports an event
	// (under-voltage, frequency capping, hard throttling, soft temp limit).
	static void SystemThrottledHandler (TSystemThrottledState CurrentState, void *pParam);

private:
	// do not change this order
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CScreenDevice		m_Screen;
	CSerialDevice		m_Serial;
	CNullDevice		m_Null;		// logger fallback: no monitor + no serial cable is fine
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogger			m_Logger;
	CCPUThrottle		m_CPUThrottle;
	CAudioEngine		m_AudioEngine;	// needs m_Interrupt: keep after it
	CAppCores		m_Cores;	// initialized LAST (arms spinlocks)

	// Set by Initialize(); TRUE only if that peripheral actually came up
	// (screen needs a monitor attached; neither is ever fatal to boot).
	boolean			m_bScreenOK;
	boolean			m_bSerialOK;
	boolean			m_bAudioOK;
};

#endif
