// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host-side unit + stress tests for src/ringbuffer.h (CBroadcastRing).
//
// The ring is freestanding-safe (no std headers inside), which also makes
// it compile host-side — so the lock-free logic gets hammered on a real
// multi-core machine before it ever runs on the Pi 5. Run via
// tests/run-tests.sh: once plain, once under ThreadSanitizer.
//
// Integrity scheme (exact, no heuristics): every writer stores value ==
// (uint32_t of) the word's ABSOLUTE ring index. After any Read returning
// nGot words, the region's base index is (cursor_after - nGot), so every
// word is individually checkable: Buf[i] == (uint32_t)(base + i). This
// catches tears, duplicates, skips, wrong resync positions, and cursor
// bugs unconditionally — an overrun just moves `base`, it never excuses
// a wrong value. (Replaced an earlier jump-classification heuristic that
// a reviewer showed could misclassify real corruption as overrun noise.)
//
#include "../src/ringbuffer.h"

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <vector>

// Small ring (2^14 words = 16K) so overruns actually happen in seconds.
typedef CBroadcastRing<14> TTestRing;

static const uint64_t TOTAL_WORDS = 20'000'000;
static const unsigned CHUNK = 32;		// mirrors GDAW_CHUNK_WORDS

// Each test gets its own ring so the value==absolute-index invariant
// holds from index 0 in every test independently.
static TTestRing s_Ring1, s_Ring2, s_Ring3, s_Ring4;

static unsigned s_nFailures = 0;

#define CHECK(cond, ...) \
	do { if (!(cond)) { printf ("FAIL: " __VA_ARGS__); printf ("\n"); s_nFailures++; } } while (0)

// Verify an entire Read result against the absolute-index invariant.
static bool CheckRegion (const uint32_t *pBuf, unsigned nGot, uint64_t nBase)
{
	for (unsigned i = 0; i < nGot; i++)
	{
		if (pBuf[i] != (uint32_t) (nBase + i))
		{
			return false;
		}
	}
	return true;
}

// Write nWords sequential absolute-index values starting at the ring's
// current commit position (assumes single-threaded control of the ring).
static void WriteSeq (TTestRing *pRing, uint64_t nFrom, unsigned nWords)
{
	uint32_t Buf[256];
	while (nWords > 0)
	{
		unsigned n = nWords < 256 ? nWords : 256;
		for (unsigned i = 0; i < n; i++)
		{
			Buf[i] = (uint32_t) (nFrom + i);
		}
		pRing->Write (Buf, n);
		nFrom += n;
		nWords -= n;
	}
}

// ── Test 1: single-threaded exactness ────────────────────────────────────────

static void TestExactness (void)
{
	TTestRing::TReader Rd;
	s_Ring1.InitReader (&Rd);

	uint32_t Out[CHUNK * 2];
	WriteSeq (&s_Ring1, 0, CHUNK);

	CHECK (s_Ring1.Available (&Rd) == CHUNK, "available %llu != %u",
	       (unsigned long long) s_Ring1.Available (&Rd), CHUNK);

	unsigned nGot = s_Ring1.Read (&Rd, Out, CHUNK * 2);
	CHECK (nGot == CHUNK, "read %u != %u", nGot, CHUNK);
	CHECK (CheckRegion (Out, nGot, 0), "data mismatch");
	CHECK (Rd.nOverruns == 0 && Rd.nInvalid == 0, "spurious overrun/invalid");

	printf ("PASS: single-threaded write/read exactness\n");
}

// ── Test 2: wraparound, reads straddling the seam ─────────────────────────────
// 24 does not divide 2^14, so over 3 laps both the writer's copy and the
// reader's copy repeatedly straddle the index-mask seam — deterministic
// coverage of the per-word wrap arithmetic.

static void TestWraparound (void)
{
	TTestRing::TReader Rd;
	s_Ring2.InitReader (&Rd);

	const unsigned BITE = 24;
	uint32_t Out[BITE];
	uint64_t nWritten = 0;

	for (uint64_t n = 0; n < TTestRing::CAPACITY * 3; n += BITE)
	{
		WriteSeq (&s_Ring2, nWritten, BITE);
		nWritten += BITE;

		unsigned nGot = s_Ring2.Read (&Rd, Out, BITE);
		CHECK (nGot == BITE, "lockstep read %u != %u", nGot, BITE);
		CHECK (CheckRegion (Out, nGot, Rd.nCursor - nGot),
		       "wrap mismatch at cursor %llu",
		       (unsigned long long) Rd.nCursor);
		if (s_nFailures)
		{
			return;
		}
	}
	CHECK (Rd.nOverruns == 0, "spurious overrun in lockstep");

	printf ("PASS: wraparound correctness (3 laps, seam-straddling reads)\n");
}

// ── Test 3: deterministic overrun + resync ────────────────────────────────────
// The single most intricate path — exercised exactly, not probabilistically.

static void TestOverrunResync (void)
{
	TTestRing::TReader Rd;
	s_Ring3.InitReader (&Rd);		// cursor = 0

	const uint64_t CAP = TTestRing::CAPACITY;

	// Writer laps the reader once: CAP + 1024 words with zero reads.
	WriteSeq (&s_Ring3, 0, (unsigned) (CAP + 1024));

	// One Read must: detect the lap ONCE, resync to
	// reserve - CAP + CAP/4 = 1024 + CAP/4, and return valid data there.
	uint32_t Out[4096];
	unsigned nGot = s_Ring3.Read (&Rd, Out, 4096);
	uint64_t nExpectBase = 1024 + CAP/4;

	CHECK (Rd.nOverruns == 1, "overruns %llu != 1 after single lap",
	       (unsigned long long) Rd.nOverruns);
	CHECK (nGot == 4096, "post-resync read %u != 4096", nGot);
	CHECK (Rd.nCursor == nExpectBase + nGot, "resync cursor %llu != %llu",
	       (unsigned long long) Rd.nCursor,
	       (unsigned long long) (nExpectBase + nGot));
	CHECK (CheckRegion (Out, nGot, nExpectBase), "post-resync data wrong");

	// Multi-lap gap (3x CAPACITY in one go) must cost exactly ONE more
	// overrun increment and land on valid data again.
	WriteSeq (&s_Ring3, CAP + 1024, (unsigned) (3 * CAP));
	nGot = s_Ring3.Read (&Rd, Out, 4096);
	uint64_t nReserveNow = 4 * CAP + 1024;
	nExpectBase = nReserveNow - CAP + CAP/4;

	CHECK (Rd.nOverruns == 2, "overruns %llu != 2 after multi-lap",
	       (unsigned long long) Rd.nOverruns);
	CHECK (nGot == 4096, "multi-lap read %u != 4096", nGot);
	CHECK (CheckRegion (Out, nGot, nExpectBase), "multi-lap data wrong");

	// Drain-to-empty afterwards must work and stay overrun-free.
	uint64_t nDrained = 0;
	while ((nGot = s_Ring3.Read (&Rd, Out, 4096)) != 0)
	{
		CHECK (CheckRegion (Out, nGot, Rd.nCursor - nGot), "drain data wrong");
		nDrained += nGot;
		if (s_nFailures)
		{
			return;
		}
	}
	CHECK (Rd.nOverruns == 2, "drain caused spurious overrun");
	CHECK (s_Ring3.Available (&Rd) == 0, "drain left data behind");
	CHECK (nDrained > 0, "drain read nothing");

	printf ("PASS: deterministic overrun/resync (single lap, multi-lap, drain)\n");
}

// ── Test 4: concurrent stress — 1 writer, 3 readers ──────────────────────────

struct TReaderResult
{
	uint64_t nWordsRead = 0;
	uint64_t nCorrupt = 0;		// invariant violations = torn data (BUG)
	uint64_t nOverruns = 0;
	uint64_t nInvalid = 0;
	bool bHang = false;		// persistent 0-reads with data available
};

static void StressWriter (void)
{
	uint32_t Buf[CHUNK];
	uint64_t nSeq = 0;

	for (uint64_t n = 0; n < TOTAL_WORDS; n += CHUNK)
	{
		for (unsigned i = 0; i < CHUNK; i++)
		{
			Buf[i] = (uint32_t) (nSeq++);
		}
		s_Ring4.Write (Buf, CHUNK);
	}
}

static void StressReader (TReaderResult *pResult, unsigned nMaxBite, bool bNapper)
{
	TTestRing::TReader Rd;
	s_Ring4.InitReader (&Rd);

	std::vector<uint32_t> Buf (nMaxBite);
	uint64_t nZeroReadsAtEnd = 0;
	uint64_t nNextNap = TOTAL_WORDS / 8;

	while (pResult->nWordsRead < TOTAL_WORDS * 6 / 10)	// plenty of coverage
	{
		// The designated napper guarantees lap coverage on any host:
		// 5 ms is thousands of CAPACITYs of writer progress, so the
		// overrun/resync path is exercised deterministically, not by
		// scheduler luck.
		if (bNapper && pResult->nWordsRead >= nNextNap)
		{
			nNextNap += TOTAL_WORDS / 8;
			std::this_thread::sleep_for (std::chrono::milliseconds (5));
		}
		unsigned nGot = s_Ring4.Read (&Rd, Buf.data (), nMaxBite);
		if (nGot == 0)
		{
			std::this_thread::yield ();
			if (s_Ring4.GetHead () >= TOTAL_WORDS)
			{
				if (s_Ring4.Available (&Rd) == 0)
				{
					break;	// writer done, fully drained
				}
				// Writer done but Read keeps returning 0 with
				// data available: with correct code this is
				// impossible (no writer -> validation passes).
				// Bound it so a regression FAILS instead of
				// hanging the suite.
				if (++nZeroReadsAtEnd > 1000000)
				{
					pResult->bHang = true;
					break;
				}
			}
			continue;
		}
		nZeroReadsAtEnd = 0;

		if (!CheckRegion (Buf.data (), nGot, Rd.nCursor - nGot))
		{
			pResult->nCorrupt++;
		}

		pResult->nWordsRead += nGot;
	}

	pResult->nOverruns = Rd.nOverruns;
	pResult->nInvalid  = Rd.nInvalid;
}

static void TestStress (void)
{
	TReaderResult R1, R2, R3;

	std::thread tw (StressWriter);
	std::thread tr1 (StressReader, &R1, 64u, true);	// napper: forced laps
	std::thread tr2 (StressReader, &R2, 512u, false);
	std::thread tr3 (StressReader, &R3, 4096u, false);

	tw.join (); tr1.join (); tr2.join (); tr3.join ();

	const TReaderResult *All[] = { &R1, &R2, &R3 };
	uint64_t nTotalOverruns = 0, nTotalInvalid = 0;
	for (unsigned i = 0; i < 3; i++)
	{
		const TReaderResult *p = All[i];
		printf ("reader %u: read %llu words, overruns %llu, "
			"invalid-copies %llu, CORRUPT %llu%s\n",
			i + 1,
			(unsigned long long) p->nWordsRead,
			(unsigned long long) p->nOverruns,
			(unsigned long long) p->nInvalid,
			(unsigned long long) p->nCorrupt,
			p->bHang ? "  [HANG DETECTED]" : "");

		CHECK (p->nCorrupt == 0, "reader %u: torn/duplicated data", i + 1);
		CHECK (!p->bHang, "reader %u: livelocked with data available", i + 1);
		// Anti-vacuous-pass: a reader that read (almost) nothing —
		// e.g. started after the writer finished — proves nothing.
		// Floor is deliberately low (1%): under TSan a big-bite reader
		// is lapped constantly and legitimately reads far less, but a
		// genuinely vacuous run reads ~0 words with ~0 overruns.
		CHECK (p->nWordsRead >= TOTAL_WORDS / 100,
		       "reader %u: only %llu words read - vacuous run",
		       i + 1, (unsigned long long) p->nWordsRead);

		nTotalOverruns += p->nOverruns;
		nTotalInvalid  += p->nInvalid;
	}

	// The 2^14 ring is sized so the writer MUST lap somebody; a run where
	// nobody was ever lapped never exercised the resync path at all.
	CHECK (nTotalOverruns > 0, "no reader was ever lapped - overrun path untested");
	if (nTotalInvalid == 0)
	{
		printf ("note: validation-retry path (nInvalid) did not fire this "
			"run - seqlock discard coverage came from other runs\n");
	}

	if (s_nFailures == 0)
	{
		printf ("PASS: concurrent stress (1 writer, 3 readers, %llu words)\n",
			(unsigned long long) TOTAL_WORDS);
	}
}

int main (void)
{
	TestExactness ();
	TestWraparound ();
	TestOverrunResync ();
	TestStress ();

	if (s_nFailures)
	{
		printf ("%u FAILURE(S)\n", s_nFailures);
		return 1;
	}
	printf ("ALL TESTS PASSED\n");
	return 0;
}
