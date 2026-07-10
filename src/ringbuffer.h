// SPDX-License-Identifier: GPL-3.0-or-later
//
// GuitarDAWLiteOS — lock-free single-producer broadcast ring buffer
//
// One writer (core 0, I2S DMA-completion IRQ) publishes u32 audio words;
// each reader (core 1 pitch, core 2 SD) keeps its OWN cursor and never
// writes shared state, so the writer is wait-free — it never inspects
// reader positions and simply overwrites the oldest data. A slow reader
// loses old audio (detected and counted), never stalls the audio path.
// This is the "readers may lag; that's correct by design" model from
// planning/build-plan.md task 3.2.
//
// Torn-read protection is a seqlock-style RESERVE/COMMIT index pair.
// A single index is NOT enough: the writer scribbles payload before
// publishing, so a reader validating against the commit index alone can
// copy words mid-overwrite and still pass (this exact bug was caught by
// tests/ringbuffer_test.cpp before the reserve index existed).
//
//   writer:  reserve += n            (RELAXED store)
//            fence(RELEASE)          -- reserve visible before payload
//            payload word stores     (RELAXED atomics)
//            commit  += n            (RELEASE store, orders payload first)
//
//   reader:  h = commit              (ACQUIRE load: how much is readable)
//            copy region             (RELAXED atomic word loads)
//            fence(ACQUIRE)          -- copy ordered before revalidation
//            r = reserve             (RELAXED load)
//            valid iff r - cursor <= CAPACITY   (nobody STARTED overwriting
//                                                the region while we copied)
//
// Payload accesses are word-wise __atomic RELAXED on purpose: the copy
// can race with the writer by design (we discard on validation failure),
// and per-word atomics make that formally defined behavior instead of UB
// — which also keeps ThreadSanitizer clean in the host stress tests.
// Cores on the Pi 5 are cache-coherent (inner-shareable WB memory), so
// no cache maintenance is needed for core-to-core sharing.
//
// Freestanding: Circle's bare-metal build has no C++ std headers
// (-nostdinc++), so this header uses only <stdint.h>/<stddef.h> and GCC
// __atomic builtins (verified lock-free for 8-byte objects on
// aarch64-elf-gcc 16.1, emits LSE) — which also makes it compile host-side
// for the unit tests in tests/.
//
#ifndef _gdaw_ringbuffer_h
#define _gdaw_ringbuffer_h

#include <stdint.h>
#include <stddef.h>

template <unsigned LOG2_WORDS>
class CBroadcastRing
{
public:
	static const uint64_t CAPACITY = 1ull << LOG2_WORDS;

	CBroadcastRing (void)
	:	m_nReserve (0),
		m_nCommit (0)
	{
	}

	// ── Writer side (single producer; wait-free) ────────────────────────
	// Safe from IRQ context: bounded work, no locks, no allocation.
	void Write (const uint32_t *pWords, unsigned nWords)
	{
		// Only the writer mutates the indices; plain read of our own
		// last store is fine.
		uint64_t nHead = m_nCommit;

		// Announce intent BEFORE touching payload, so readers copying
		// this region can detect the overwrite (see header comment).
		__atomic_store_n (&m_nReserve, nHead + nWords, __ATOMIC_RELAXED);
		__atomic_thread_fence (__ATOMIC_RELEASE);

		for (unsigned i = 0; i < nWords; i++)
		{
			__atomic_store_n (&m_Buffer[(nHead + i) & (CAPACITY-1)],
					  pWords[i], __ATOMIC_RELAXED);
		}

		// Publish: the RELEASE store orders the payload stores above
		// before the new commit index.
		__atomic_store_n (&m_nCommit, nHead + nWords, __ATOMIC_RELEASE);
	}

	uint64_t GetHead (void) const
	{
		return __atomic_load_n (&m_nCommit, __ATOMIC_ACQUIRE);
	}

	// ── Reader side ─────────────────────────────────────────────────────
	// Each reader owns one TReader and is the only thread touching it.
	struct TReader
	{
		uint64_t nCursor;	// absolute word index, monotonic
		uint64_t nOverruns;	// times the writer lapped this reader
		uint64_t nInvalid;	// copies discarded by validation
	};

	void InitReader (TReader *pReader) const
	{
		pReader->nCursor   = GetHead ();	// start at "now"
		pReader->nOverruns = 0;
		pReader->nInvalid  = 0;
	}

	// Words available to this reader right now.
	uint64_t Available (const TReader *pReader) const
	{
		return GetHead () - pReader->nCursor;
	}

	// Copy up to nMaxWords into pOut. Returns words actually copied
	// (0 if nothing new). Detects and repairs overruns: if the writer
	// lapped us, the cursor is resynced to the oldest still-valid word
	// (plus headroom) and nOverruns is bumped — audio is lost, the
	// stream stays sane.
	unsigned Read (TReader *pReader, uint32_t *pOut, unsigned nMaxWords)
	{
		for (unsigned nAttempt = 0; nAttempt < 16; nAttempt++)
		{
			// Lap check against RESERVE (the writer's true frontier):
			// if anything in [cursor, cursor+CAPACITY) may already be
			// dirty, resync before wasting a copy. Headroom of 1/4
			// capacity so the writer doesn't immediately re-lap us.
			uint64_t nReserve = __atomic_load_n (&m_nReserve, __ATOMIC_ACQUIRE);
			if (nReserve - pReader->nCursor > CAPACITY)
			{
				pReader->nCursor = nReserve - CAPACITY + CAPACITY/4;
				pReader->nOverruns++;
			}

			uint64_t nHead = GetHead ();
			if (nHead <= pReader->nCursor)
			{
				return 0;
			}
			uint64_t nAvail = nHead - pReader->nCursor;
			if (nAvail == 0)
			{
				return 0;
			}

			unsigned nWords = nAvail < nMaxWords ? (unsigned) nAvail : nMaxWords;

			for (unsigned i = 0; i < nWords; i++)
			{
				pOut[i] = __atomic_load_n (
					&m_Buffer[(pReader->nCursor + i) & (CAPACITY-1)],
					__ATOMIC_RELAXED);
			}

			// Revalidate: the ACQUIRE fence orders the payload loads
			// above before the reserve re-read below. If a writer
			// STARTED overwriting our region during the copy, the
			// data may be torn — discard and retry.
			__atomic_thread_fence (__ATOMIC_ACQUIRE);
			uint64_t nReserveAfter =
				__atomic_load_n (&m_nReserve, __ATOMIC_RELAXED);
			if (nReserveAfter - pReader->nCursor <= CAPACITY)
			{
				pReader->nCursor += nWords;
				return nWords;
			}

			pReader->nInvalid++;
		}

		return 0;	// pathological contention; caller just retries later
	}

private:
	// Indices on their own cache line: the writer dirties them at IRQ
	// rate and the payload lines are shared with readers — don't alias.
	alignas (64) uint64_t m_nReserve;	// writer frontier (pre-payload)
	uint64_t m_nCommit;			// published data (post-payload)
	alignas (64) uint32_t m_Buffer[CAPACITY];
};

#endif
