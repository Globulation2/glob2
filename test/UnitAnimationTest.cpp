// SPDX-License-Identifier: GPL-3.0-or-later
#include <cppunit/extensions/HelperMacros.h>
#include "../src/render/UnitAnimation.h"

class UnitAnimationTest : public CPPUNIT_NS::TestCase
{
	CPPUNIT_TEST_SUITE(UnitAnimationTest);
	CPPUNIT_TEST(testDirectionsAndProgress);
	CPPUNIT_TEST(testTurningKeepsItsCadence);
	CPPUNIT_TEST_SUITE_END();

	void testDirectionsAndProgress()
	{
		for (int base = 0; base <= 384; base += 64)
			for (int direction = 0; direction < 8; ++direction)
			{
				int changes = 0;
				int previous = -1;
				for (int delta = 0; delta < 256; ++delta)
				{
					const int frame = unitAnimationFrame(base, direction, delta);
					CPPUNIT_ASSERT(frame >= 0 && frame < 1792);
					CPPUNIT_ASSERT(frame >= base * 4 + direction * 32);
					CPPUNIT_ASSERT(frame < base * 4 + (direction + 1) * 32);
					if (delta % 32 == 0)
						CPPUNIT_ASSERT_EQUAL((base + direction * 8 + delta / 32) * 4, frame);
					if (frame != previous) ++changes;
					previous = frame;
				}
				CPPUNIT_ASSERT_EQUAL(32, changes);
			}
	}

	void testTurningKeepsItsCadence()
	{
		for (int base = 0; base <= 384; base += 64)
			for (int delta = 0; delta < 256; ++delta)
			{
				const int frame = unitAnimationFrame(base, 8, delta);
				CPPUNIT_ASSERT_EQUAL((base + 8 * (delta / 32)) * 4, frame);
				CPPUNIT_ASSERT(frame >= 0 && frame < 1792);
			}
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(UnitAnimationTest);
