// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Unit test for the pure integer core of the sprite-centering helper
// (src/gui/SpriteCentering.h). Exercises only centerInBox, which has no
// SDL/Sprite dependency, so it runs headless in TestsRunner.

#include <cppunit/extensions/HelperMacros.h>

#include "SpriteCentering.h"

class SpriteCenteringTest: public CPPUNIT_NS::TestCase
{
	CPPUNIT_TEST_SUITE(SpriteCenteringTest);
		CPPUNIT_TEST(testSquareSpriteInSquareBox);
		CPPUNIT_TEST(testAxesAreIndependent);
		CPPUNIT_TEST(testOddDifferenceFloors);
		CPPUNIT_TEST(testExactFit);
		CPPUNIT_TEST(testSpriteLargerThanBox);
	CPPUNIT_TEST_SUITE_END();

protected:
	// A small sprite in a large box gets a positive, equal nudge on both axes.
	void testSquareSpriteInSquareBox()
	{
		const SpriteCenterOffset off = centerInBox(32, 32, 10, 10);
		CPPUNIT_ASSERT_EQUAL(11, off.dx);
		CPPUNIT_ASSERT_EQUAL(11, off.dy);
	}

	// The regression this helper prevents: dx must track width, dy height.
	// A wide-but-short sprite in a tall-but-narrow box yields distinct offsets;
	// swapping the axes (the original bug) would produce the transposed pair.
	void testAxesAreIndependent()
	{
		const SpriteCenterOffset off = centerInBox(20, 60, 16, 8);
		CPPUNIT_ASSERT_EQUAL(2, off.dx);   // (20 - 16) >> 1
		CPPUNIT_ASSERT_EQUAL(26, off.dy);  // (60 - 8)  >> 1
		CPPUNIT_ASSERT(off.dx != off.dy);
	}

	// >>1 floors an odd gap toward zero for positive differences.
	void testOddDifferenceFloors()
	{
		const SpriteCenterOffset off = centerInBox(46, 46, 15, 21);
		CPPUNIT_ASSERT_EQUAL(15, off.dx);  // (46 - 15) >> 1 == 31 >> 1 == 15
		CPPUNIT_ASSERT_EQUAL(12, off.dy);  // (46 - 21) >> 1 == 25 >> 1 == 12
	}

	// A sprite that exactly fills the box needs no offset.
	void testExactFit()
	{
		const SpriteCenterOffset off = centerInBox(56, 46, 56, 46);
		CPPUNIT_ASSERT_EQUAL(0, off.dx);
		CPPUNIT_ASSERT_EQUAL(0, off.dy);
	}

	// Arithmetic right shift on a negative gap floors toward -infinity, matching
	// the historical hand-written expression exactly (documented, not desired).
	void testSpriteLargerThanBox()
	{
		const SpriteCenterOffset off = centerInBox(10, 10, 15, 13);
		CPPUNIT_ASSERT_EQUAL(-3, off.dx);  // (10 - 15) >> 1 == -5 >> 1 == -3
		CPPUNIT_ASSERT_EQUAL(-2, off.dy);  // (10 - 13) >> 1 == -3 >> 1 == -2
	}
};
CPPUNIT_TEST_SUITE_REGISTRATION(SpriteCenteringTest);
