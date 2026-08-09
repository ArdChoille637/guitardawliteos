// SPDX-License-Identifier: GPL-3.0-or-later
//
// GuitarDAWLiteOS — tuning reference tables + nearest-note/cents (M6.2)
//
// Two independent pieces, per planning/build-plan.md task 6.2:
//   * Open-string frequency tables for the alternate tunings the pedal
//     needs to recognize (equal temperament, A4 = 440 Hz).
//   * Frequency -> nearest chromatic note + signed cents deviation, used
//     both for a free-running chromatic tuner display and to score how
//     close a detected pitch is to one specific open string.
//
// Freestanding-safe: only <stdint.h> and <math.h> (logf/lroundf are pure
// computation, no OS services, so they link fine in Circle's bare-metal
// build), the same rule ringbuffer.h/audiostats.h follow — which also
// makes this compile host-side for tests/tuning_test.cpp.
//
// No pitch DETECTION lives here (that's M6.1's YIN implementation) — this
// module only turns a Hz value that's already been measured into what a
// player-facing display needs.
//
#ifndef _gdaw_tuning_h
#define _gdaw_tuning_h

#include <stdint.h>
#include <math.h>

// ── Chromatic nearest-note -------------------------------------------------

#define GDAW_A4_HZ	440.0f

// C-based (MIDI note % 12), sharps only — no enharmonic choice needed for
// a tuner readout.
static const char *const g_TuningNoteNames[12] =
{
	"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

struct TNoteResult
{
	const char *pNoteName;	// points into g_TuningNoteNames (static storage)
	int nOctave;		// scientific pitch notation: A4 = 440 Hz -> octave 4
	float fCents;		// signed deviation from the nearest note, [-50, +50)
};

// Nearest equal-tempered note (A4 = 440 Hz reference) for a measured
// frequency, plus signed cents deviation (negative = flat, positive =
// sharp). fFreqHz <= 0 (e.g. "no pitch found yet" from the M6.1 tracker)
// returns a dashed placeholder rather than feeding a NaN from logf(0) into
// the display.
inline TNoteResult NoteFromFrequency (float fFreqHz)
{
	if (fFreqHz <= 0.0f)
	{
		TNoteResult NoResult = { "-", 0, 0.0f };
		return NoResult;
	}

	// Continuous semitone offset from A4.
	float fSemitonesFromA4 = 12.0f * (logf (fFreqHz / GDAW_A4_HZ) / logf (2.0f));

	int nMidiNote = 69 + (int) lroundf (fSemitonesFromA4);	// A4 == MIDI 69
	float fCents = (fSemitonesFromA4 - (float) (nMidiNote - 69)) * 100.0f;

	int nPitchClass = ((nMidiNote % 12) + 12) % 12;

	TNoteResult Result;
	Result.pNoteName = g_TuningNoteNames[nPitchClass];
	Result.nOctave = nMidiNote / 12 - 1;	// MIDI 60 == C4
	Result.fCents = fCents;
	return Result;
}

// ── Open-string tuning tables -----------------------------------------------

#define GDAW_TUNING_MAX_STRINGS	6

struct TTuning
{
	const char *pName;
	unsigned nNumStrings;
	const char *NoteNames[GDAW_TUNING_MAX_STRINGS];	// low string first
	float Frequencies[GDAW_TUNING_MAX_STRINGS];		// Hz, equal temperament A440
};

// Six-string tunings the pedal needs to recognize, low string first.
// Frequencies are equal-tempered (A4 = 440 Hz) — the same reference
// NoteFromFrequency() uses — so a string tuned exactly on-pitch reads 0
// cents from either function.
static const TTuning g_Tunings[] =
{
	{ "Standard E", 6, { "E2", "A2", "D3", "G3", "B3", "E4" },
			   { 82.41f, 110.00f, 146.83f, 196.00f, 246.94f, 329.63f } },
	{ "Drop D",     6, { "D2", "A2", "D3", "G3", "B3", "E4" },
			   { 73.42f, 110.00f, 146.83f, 196.00f, 246.94f, 329.63f } },
	{ "DADGAD",     6, { "D2", "A2", "D3", "G3", "A3", "D4" },
			   { 73.42f, 110.00f, 146.83f, 196.00f, 220.00f, 293.66f } },
	{ "Open G",     6, { "D2", "G2", "D3", "G3", "B3", "D4" },
			   { 73.42f,  98.00f, 146.83f, 196.00f, 246.94f, 293.66f } },
	{ "Open D",     6, { "D2", "A2", "D3", "F#3", "A3", "D4" },
			   { 73.42f, 110.00f, 146.83f, 185.00f, 220.00f, 293.66f } },
	{ "D Standard", 6, { "D2", "G2", "C3", "F3", "A3", "D4" },
			   { 73.42f,  98.00f, 130.81f, 174.61f, 220.00f, 293.66f } },
};
static const unsigned g_nNumTunings = sizeof (g_Tunings) / sizeof (g_Tunings[0]);

struct TStringMatch
{
	unsigned nStringIndex;	// index into pTuning->Frequencies, 0 = lowest string
	float fCents;		// signed deviation from that string's target
};

// Which open string in pTuning lies closest to fFreqHz, and how far off in
// cents. Distance is compared in cents (log-frequency), not raw Hz, so
// e.g. an E2 and an E4 two octaves apart are never confused for "close".
// Unlike NoteFromFrequency() (always chromatic-nearest), this pins the
// comparison to the string the player is actually reaching for — the
// "tuning-aware" half of task 6.2, needed because tunings like DADGAD
// repeat a note (two D strings) that a bare chromatic readout can't tell
// apart.
inline TStringMatch NearestString (const TTuning *pTuning, float fFreqHz)
{
	TStringMatch Result = { 0, 0.0f };
	if (fFreqHz <= 0.0f || pTuning->nNumStrings == 0)
	{
		return Result;
	}

	float fBestAbsCents = -1.0f;
	for (unsigned i = 0; i < pTuning->nNumStrings; i++)
	{
		float fCents = 1200.0f * (logf (fFreqHz / pTuning->Frequencies[i]) / logf (2.0f));
		float fAbsCents = fCents < 0.0f ? -fCents : fCents;
		if (fBestAbsCents < 0.0f || fAbsCents < fBestAbsCents)
		{
			fBestAbsCents = fAbsCents;
			Result.nStringIndex = i;
			Result.fCents = fCents;
		}
	}
	return Result;
}

#endif
