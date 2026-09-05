// SPDX-License-Identifier: GPL-3.0-or-later

// Regression test for BH-374. GameGUIGhostBuildingManager::isGhostBuilding used
// to decide footprint collisions by materialising every tile of both footprints
// and comparing them pairwise, wrapping each with (v + size) % size. That is now
// replaced by wrappedRangesOverlap() applied once per axis.
//
// The point of this fixture is not to spot-check the new predicate but to prove
// it is *exactly* the old one: referenceOverlap() below is a transcription of
// the original nested loops, and the tests brute-force both over every start and
// length combination on small axes, including the wrapping and degenerate cases.
//
// GameGUIGhostBuildingManager.h only forward-declares Game and pulls in <GAGSys.h>
// and <vector>, and wrappedRangesOverlap is inline, so this links nothing —
// no globalContainer, no Game, no SDL runtime.

#include <cppunit/extensions/HelperMacros.h>

#include "GameGUIGhostBuildingManager.h"

namespace
{
	/// The original algorithm, transcribed from the pre-BH-374 isGhostBuilding:
	/// walk every tile of both footprints and look for a shared wrapped cell.
	/// One axis only — the original applied the same wrap to x and y and
	/// required a match on both, which is the conjunction the new code relies on.
	bool referenceOverlap(int aStart, int aLen, int bStart, int bLen, int modulus)
	{
		for (int i = 0; i < aLen; ++i)
		{
			const int la = (aStart + i + modulus) % modulus;
			for (int j = 0; j < bLen; ++j)
			{
				const int lb = (bStart + j + modulus) % modulus;
				if (la == lb)
					return true;
			}
		}
		return false;
	}
}

class GhostBuildingOverlapTest : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(GhostBuildingOverlapTest);
	CPPUNIT_TEST(testMatchesReferenceExhaustively);
	CPPUNIT_TEST(testEmptyRangeOverlapsNothing);
	CPPUNIT_TEST(testWrapAroundSeam);
	CPPUNIT_TEST(testFullAxisRangeOverlapsEverything);
	CPPUNIT_TEST_SUITE_END();

public:
	/// The old code and the new code must agree on every input the old code was
	/// ever fed: starts anywhere on the axis (the original added `+ modulus`
	/// before the %, so it tolerated starts down to -modulus), and footprint
	/// lengths from 1 up to past the axis length.
	void testMatchesReferenceExhaustively()
	{
		for (int modulus = 1; modulus <= 12; ++modulus)
		{
			for (int aStart = -modulus; aStart < 2 * modulus; ++aStart)
			for (int bStart = -modulus; bStart < 2 * modulus; ++bStart)
			for (int aLen = 1; aLen <= modulus + 2; ++aLen)
			for (int bLen = 1; bLen <= modulus + 2; ++bLen)
			{
				const bool expected = referenceOverlap(aStart, aLen, bStart, bLen, modulus);
				const bool actual = wrappedRangesOverlap(aStart, aLen, bStart, bLen, modulus);
				CPPUNIT_ASSERT_EQUAL(expected, actual);
			}
		}
	}

	/// The original's loops simply did not execute for a non-positive length,
	/// falling through to `return false`.
	void testEmptyRangeOverlapsNothing()
	{
		CPPUNIT_ASSERT(!wrappedRangesOverlap(0, 0, 0, 4, 16));
		CPPUNIT_ASSERT(!wrappedRangesOverlap(0, 4, 0, 0, 16));
		CPPUNIT_ASSERT(!wrappedRangesOverlap(0, -3, 0, 4, 16));
		CPPUNIT_ASSERT(!wrappedRangesOverlap(0, 0, 0, 0, 16));
	}

	/// A footprint straddling the map seam must still collide with one sitting
	/// at the origin — this is the case the wrap exists for.
	void testWrapAroundSeam()
	{
		// {7,0} vs {0,1} on a 8-wide axis: share cell 0.
		CPPUNIT_ASSERT(wrappedRangesOverlap(0, 2, 7, 2, 8));
		CPPUNIT_ASSERT(wrappedRangesOverlap(7, 2, 0, 2, 8));
		// {3,4} vs {0,1}: disjoint.
		CPPUNIT_ASSERT(!wrappedRangesOverlap(0, 2, 3, 2, 8));
		// A negative start is normalised, not treated as disjoint.
		CPPUNIT_ASSERT(wrappedRangesOverlap(-1, 2, 7, 1, 8));
	}

	/// A footprint as wide as the axis covers it entirely, so nothing can miss it.
	void testFullAxisRangeOverlapsEverything()
	{
		for (int start = 0; start < 8; ++start)
		{
			CPPUNIT_ASSERT(wrappedRangesOverlap(0, 8, start, 1, 8));
			CPPUNIT_ASSERT(wrappedRangesOverlap(start, 1, 0, 8, 8));
		}
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(GhostBuildingOverlapTest);
