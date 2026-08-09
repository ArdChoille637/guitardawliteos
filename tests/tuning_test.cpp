// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host-side unit tests for src/tuning.h (build plan task 6.2).
//
// tuning.h is freestanding-safe the same way ringbuffer.h/audiostats.h
// are (only <stdint.h>/<math.h>), so it compiles standalone here without
// pulling in the Circle tree. Run via tests/run-tests.sh.
//
#include "../src/tuning.h"

#include <cstdio>
#include <cstring>
#include <cmath>

static unsigned s_nFailures = 0;

#define CHECK(cond, ...) \
	do { if (!(cond)) { printf ("FAIL: " __VA_ARGS__); printf ("\n"); s_nFailures++; } } while (0)

static bool NearlyEqual (float a, float b, float fTol)
{
	float d = a - b;
	if (d < 0.0f) d = -d;
	return d <= fTol;
}

// A4 = 440 Hz must resolve to exactly A, octave 4, 0 cents.
static void TestReferencePitch (void)
{
	TNoteResult R = NoteFromFrequency (440.0f);
	CHECK (strcmp (R.pNoteName, "A") == 0, "A4 note name: got %s", R.pNoteName);
	CHECK (R.nOctave == 4, "A4 octave: got %d", R.nOctave);
	CHECK (NearlyEqual (R.fCents, 0.0f, 0.01f), "A4 cents: got %f", (double) R.fCents);
}

// Every open string in every table must round-trip through
// NoteFromFrequency() to its own note name/octave at ~0 cents - the two
// tables are built off the same A440 equal-tempered reference, so an
// in-tune string must read as in-tune chromatically too.
static void TestOpenStringsRoundTrip (void)
{
	for (unsigned t = 0; t < g_nNumTunings; t++)
	{
		const TTuning *pTuning = &g_Tunings[t];
		for (unsigned i = 0; i < pTuning->nNumStrings; i++)
		{
			char ExpectName[4];
			int nLen = 0;
			const char *pSrc = pTuning->NoteNames[i];
			// Note names in the table carry an octave digit suffix
			// (and an optional '#'); split it off to compare against
			// NoteFromFrequency()'s separate name/octave fields.
			while (pSrc[nLen] && !(pSrc[nLen] >= '0' && pSrc[nLen] <= '9'))
			{
				ExpectName[nLen] = pSrc[nLen];
				nLen++;
			}
			ExpectName[nLen] = '\0';
			int nExpectOctave = pSrc[nLen] - '0';

			TNoteResult R = NoteFromFrequency (pTuning->Frequencies[i]);
			CHECK (strcmp (R.pNoteName, ExpectName) == 0,
			       "%s string %u (%s, %.2f Hz): note name got %s want %s",
			       pTuning->pName, i, pSrc, (double) pTuning->Frequencies[i],
			       R.pNoteName, ExpectName);
			CHECK (R.nOctave == nExpectOctave,
			       "%s string %u (%s): octave got %d want %d",
			       pTuning->pName, i, pSrc, R.nOctave, nExpectOctave);
			CHECK (NearlyEqual (R.fCents, 0.0f, 1.0f),
			       "%s string %u (%s): cents got %f, expected ~0",
			       pTuning->pName, i, pSrc, (double) R.fCents);
		}
	}
}

// A frequency a known number of cents off a reference must come back
// with (roughly) that many cents of deviation, same note.
static void TestCentsDeviation (void)
{
	// +25 cents sharp of A4.
	float fSharp = 440.0f * powf (2.0f, 25.0f / 1200.0f);
	TNoteResult R = NoteFromFrequency (fSharp);
	CHECK (strcmp (R.pNoteName, "A") == 0, "25c sharp of A4: note got %s", R.pNoteName);
	CHECK (NearlyEqual (R.fCents, 25.0f, 0.1f), "25c sharp of A4: cents got %f", (double) R.fCents);

	// -30 cents (flat) of A4 - must still read as A4, not G#4: cents
	// deviation stays in [-50, +50), it doesn't roll over to the
	// neighboring note until past the halfway point.
	float fFlat = 440.0f * powf (2.0f, -30.0f / 1200.0f);
	R = NoteFromFrequency (fFlat);
	CHECK (strcmp (R.pNoteName, "A") == 0, "30c flat of A4: note got %s", R.pNoteName);
	CHECK (NearlyEqual (R.fCents, -30.0f, 0.1f), "30c flat of A4: cents got %f", (double) R.fCents);
}

// fFreqHz <= 0 (no pitch found yet) must not produce a NaN/garbage result.
static void TestNoPitch (void)
{
	TNoteResult R = NoteFromFrequency (0.0f);
	CHECK (strcmp (R.pNoteName, "-") == 0, "0 Hz: note got %s", R.pNoteName);
	CHECK (!isnan (R.fCents), "0 Hz: cents is NaN");

	R = NoteFromFrequency (-10.0f);
	CHECK (strcmp (R.pNoteName, "-") == 0, "negative Hz: note got %s", R.pNoteName);
}

// DADGAD repeats D across three strings an octave apart (D2/D3/D4) -
// NearestString() must pick the CLOSEST one, not just the first match,
// proving the comparison is octave-aware (cents/log-frequency) rather
// than picking on note-name alone.
static void TestNearestStringOctaveAware (void)
{
	const TTuning *pDADGAD = NULL;
	for (unsigned t = 0; t < g_nNumTunings; t++)
	{
		if (strcmp (g_Tunings[t].pName, "DADGAD") == 0)
		{
			pDADGAD = &g_Tunings[t];
		}
	}
	CHECK (pDADGAD != NULL, "DADGAD tuning not found in table");
	if (!pDADGAD) return;

	// Slightly sharp of the D3 string (index 2) - must NOT match D2
	// (index 0) or D4 (index 5) just because they share the note name.
	TStringMatch M = NearestString (pDADGAD, 146.83f * powf (2.0f, 10.0f / 1200.0f));
	CHECK (M.nStringIndex == 2, "DADGAD near-D3: matched string %u, want 2", M.nStringIndex);
	CHECK (NearlyEqual (M.fCents, 10.0f, 0.5f), "DADGAD near-D3: cents got %f", (double) M.fCents);

	// Dead-on the low D2 (index 0).
	M = NearestString (pDADGAD, 73.42f);
	CHECK (M.nStringIndex == 0, "DADGAD near-D2: matched string %u, want 0", M.nStringIndex);
	CHECK (NearlyEqual (M.fCents, 0.0f, 1.0f), "DADGAD near-D2: cents got %f", (double) M.fCents);
}

static void TestNearestStringNoPitch (void)
{
	TStringMatch M = NearestString (&g_Tunings[0], 0.0f);
	CHECK (M.nStringIndex == 0 && M.fCents == 0.0f, "0 Hz: expected zeroed result");
}

int main (void)
{
	TestReferencePitch ();
	TestOpenStringsRoundTrip ();
	TestCentsDeviation ();
	TestNoPitch ();
	TestNearestStringOctaveAware ();
	TestNearestStringNoPitch ();

	if (s_nFailures)
	{
		printf ("%u FAILURE(S)\n", s_nFailures);
		return 1;
	}
	printf ("ALL TESTS PASSED\n");
	return 0;
}
