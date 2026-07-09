// SPDX-License-Identifier: GPL-3.0-or-later
//
// GuitarDAWLiteOS — Milestone 0 bring-up kernel
//
// First-boot payload for the Pi 5: proves the boot chain (firmware -> Circle
// -> HDMI/UART) with an ACT-LED heartbeat (BCM2712 on-die GPIO), and keeps
// the SoC cool during bench testing by driving the Active Cooler via
// CCPUThrottle (cmdline: gpiofanpin=45 socmaxtemp=60 fast=true) with
// continuous temperature telemetry on screen/serial. RP1 GPIO access
// (Spike A) is proven by the fan itself: GPIO45 lives on RP1 bank 2, so a
// switching fan == working RP1 GPIO path.
//
// HEADLESS-SAFE BY DESIGN: on Pi 4/5, Circle's CScreenDevice::Initialize()
// fails outright when no HDMI monitor is attached (documented in
// doc/issues.txt — the firmware can't allocate a framebuffer without a
// negotiated display mode). We do NOT gate boot on screen or serial
// succeeding: both are attempted, but if neither is present the logger
// falls back to CNullDevice so Initialize() still succeeds and Run() (the
// fan/thermal loop + LED heartbeat) always executes regardless of what is
// plugged in. With nothing attached, the ACT LED heartbeat and fan cycling
// ARE the diagnostic.
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

	// Set by Initialize(); TRUE only if that peripheral actually came up
	// (screen needs a monitor attached; neither is ever fatal to boot).
	boolean			m_bScreenOK;
	boolean			m_bSerialOK;
};

#endif
