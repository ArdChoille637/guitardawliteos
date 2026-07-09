// SPDX-License-Identifier: GPL-3.0-or-later
//
// GuitarDAWLiteOS — Milestone 0 bring-up kernel (see kernel.h)
//
#include "kernel.h"
#include <circle/machineinfo.h>

static const char FromKernel[] = "gdawlite";

// Coarse SoC-temperature indicator via the ACT LED, so the reading is
// visible with NO screen or serial cable attached (see the blink-burst
// in Run()). Bands, not exact degrees — easy to count reliably by eye.
static unsigned TemperatureBand (unsigned nTempC)
{
	if (nTempC < 40) return 1;	// < 40 C
	if (nTempC < 50) return 2;	// 40-49 C
	if (nTempC < 60) return 3;	// 50-59 C
	if (nTempC < 70) return 4;	// 60-69 C
	if (nTempC < 80) return 5;	// 70-79 C
	return 6;			// >= 80 C
}

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Serial (&m_Interrupt),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	// m_CPUThrottle: default resolves via cmdline — CPUSpeedLow unless
	// "fast=true" (we set fast=true so the bench generates real heat and
	// the fan trip is actually exercised; in fan mode the clock is never
	// changed afterwards). With gpiofanpin= set it manages the Active
	// Cooler fan instead of throttling the clock.
	m_bScreenOK (FALSE),
	m_bSerialOK (FALSE)
{
	m_ActLED.Blink (5);	// show we are alive (also proves LED GPIO path)
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	// Screen and serial are both OPTIONAL and never gate boot. On Pi 4/5,
	// CScreenDevice::Initialize() fails outright with no monitor attached
	// (doc/issues.txt) — if that were allowed to fail Initialize() as a
	// whole, main() would halt() before Run() ever executes, and the fan/
	// LED heartbeat (our only feedback with nothing plugged in) would
	// never run either. So: try both, remember what worked, move on.
	m_bScreenOK = m_Screen.Initialize ();
	m_bSerialOK = m_Serial.Initialize (115200);

	// Logger target priority: explicit logdev= from cmdline.txt -> screen
	// (if a monitor is attached) -> serial (if a cable is attached) ->
	// CNullDevice (always succeeds, so this can never block Run()).
	CDevice *pTarget = m_DeviceNameService.GetDevice (m_Options.GetLogDevice (), FALSE);
	if (pTarget == 0 && m_bScreenOK)
	{
		pTarget = &m_Screen;
	}
	if (pTarget == 0 && m_bSerialOK)
	{
		pTarget = &m_Serial;
	}
	if (pTarget == 0)
	{
		pTarget = &m_Null;
	}
	if (!m_Logger.Initialize (pTarget))
	{
		return FALSE;
	}

	// These have no external-hardware dependency (no monitor/cable needed)
	// and genuinely must succeed for anything downstream — including fan
	// management — to work at all, so they stay fatal.
	if (!m_Interrupt.Initialize ())
	{
		return FALSE;
	}
	if (!m_Timer.Initialize ())
	{
		return FALSE;
	}

	return TRUE;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "GuitarDAWLiteOS bring-up kernel");
	m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);
	m_Logger.Write (FromKernel, LogNotice, "Machine: %s",
			CMachineInfo::Get ()->GetMachineName ());
	m_Logger.Write (FromKernel, LogNotice, "Screen: %s   Serial: %s   (log target: %s)",
			m_bScreenOK  ? "OK" : "not available (no monitor attached)",
			m_bSerialOK  ? "OK" : "not available (no cable attached)",
			(m_DeviceNameService.GetDevice (m_Options.GetLogDevice (), FALSE) != 0)
				? m_Options.GetLogDevice ()
				: (m_bScreenOK ? "screen" : (m_bSerialOK ? "serial" : "null - nothing visible!")));

	// ── Thermal management ───────────────────────────────────────────────
	// cmdline.txt must carry: gpiofanpin=45 socmaxtemp=60
	// With gpiofanpin set, CCPUThrottle switches the Active Cooler fan on
	// at socmaxtemp (hysteresis: off again 5 C below). The fan is driven ON
	// from the CCPUThrottle constructor, so it runs full-speed from power-on
	// until the first Update() — thermally fail-safe even if we hang early.
	// The firmware's own hard limit (throttling at ~85 C) stays as the last
	// line of defense; we log if it ever trips.
	//
	// NB: GetSoCMaxTemp() is the fan trip point (socmaxtemp);
	//     CCPUThrottle::GetMaxTemperature() is the FIRMWARE hard limit —
	//     do not confuse them (review finding: they differ, 60 vs 85).
	unsigned nFanPin   = m_Options.GetGPIOFanPin ();
	unsigned nTripTemp = m_Options.GetSoCMaxTemp ();
	unsigned nHardTemp = m_CPUThrottle.GetMaxTemperature ();
	if (nFanPin == 0)
	{
		m_Logger.Write (FromKernel, LogWarning,
				"gpiofanpin= is NOT set - Active Cooler will not run!");
		m_Logger.Write (FromKernel, LogWarning,
				"Add \"gpiofanpin=45 socmaxtemp=60\" to cmdline.txt");
	}
	else
	{
		m_Logger.Write (FromKernel, LogNotice,
				"Fan on GPIO%u, trips at %u C (firmware hard limit %u C)",
				nFanPin, nTripTemp, nHardTemp);
	}

	m_CPUThrottle.RegisterSystemThrottledHandler (
		  SystemStateUnderVoltageOccurred
		| SystemStateFrequencyCappingOccurred
		| SystemStateThrottlingOccurred
		| SystemStateSoftTempLimitOccurred,
		SystemThrottledHandler, this);

	m_Logger.Write (FromKernel, LogNotice,
			"LED: ~2s single blink = alive. Every ~20s, after a 1s dark "
			"pause, N quick blinks = temp band, then another 1s pause: "
			"1=<40C 2=40s 3=50s 4=60s 5=70s 6=80C+");

	// ── Main loop: heartbeat + thermal telemetry ─────────────────────────
	unsigned nLoop = 0;
	while (1)
	{
		// MANDATORY: CCPUThrottle only acts when Update() is called.
		// Header warning: "You have to repeatedly call SetOnTemperature()
		// or Update() if you use this class!"
		if (!m_CPUThrottle.Update ())
		{
			m_Logger.Write (FromKernel, LogError,
					"CCPUThrottle::Update() failed");
		}

		unsigned nTemp = m_CPUThrottle.GetTemperature ();
		if (nLoop % 5 == 0)		// every ~10 s
		{
			m_Logger.Write (FromKernel, LogNotice,
					"up %us  SoC %u C  (fan trips at %u C)",
					m_Timer.GetUptime (), nTemp, nTripTemp);
		}
		if (nTemp >= 80)		// firmware hard-throttle territory
		{
			m_Logger.Write (FromKernel, LogError,
					"SoC %u C - at firmware throttle limit! "
					"Check cooler seating/cable.", nTemp);
		}

		// Heartbeat: proves the loop is alive. NB: on Pi 5 the ACT LED
		// is on the BCM2712's on-die GPIO block, NOT behind RP1 — RP1
		// GPIO is proven by the fan on GPIO45 (RP1 bank 2) switching.
		m_ActLED.On ();
		m_Timer.MsDelay (100);
		m_ActLED.Off ();
		m_Timer.MsDelay (1900);

		// Every ~20 s (every 10th 2s cycle): blink out the temperature
		// BAND so it's readable with no screen/serial attached at all —
		// see the boot-log explanation above. Pattern: [1s dark] N quick
		// blinks [1s dark], then the normal heartbeat resumes.
		if (nLoop % 10 == 9)
		{
			m_Timer.MsDelay (1000);
			unsigned nBand = TemperatureBand (nTemp);
			for (unsigned i = 0; i < nBand; i++)
			{
				m_ActLED.On ();
				m_Timer.MsDelay (150);
				m_ActLED.Off ();
				m_Timer.MsDelay (150);
			}
			m_Timer.MsDelay (1000);
		}

		nLoop++;
	}

	return ShutdownHalt;
}

void CKernel::SystemThrottledHandler (TSystemThrottledState CurrentState, void *pParam)
{
	CKernel *pThis = static_cast<CKernel *> (pParam);

	if (CurrentState & SystemStateUnderVoltageOccurred)
	{
		pThis->m_Logger.Write (FromKernel, LogWarning,
				       "UNDER-VOLTAGE detected - check the power supply (needs 5V/5A PD)");
	}
	if (CurrentState & SystemStateFrequencyCappingOccurred)
	{
		pThis->m_Logger.Write (FromKernel, LogWarning, "Frequency capping occurred");
	}
	if (CurrentState & SystemStateThrottlingOccurred)
	{
		pThis->m_Logger.Write (FromKernel, LogWarning,
				       "FIRMWARE THROTTLING occurred - cooling is not keeping up");
	}
	if (CurrentState & SystemStateSoftTempLimitOccurred)
	{
		pThis->m_Logger.Write (FromKernel, LogWarning, "Soft temperature limit reached");
	}
}
