// SPDX-License-Identifier: GPL-3.0-or-later
#include <cppunit/extensions/HelperMacros.h>
#include "UnitTiming.h"
#include <initializer_list>

class UnitTimingTest : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(UnitTimingTest);
	CPPUNIT_TEST(testTravelTiming);
	CPPUNIT_TEST(testNonTravelActions);
	CPPUNIT_TEST(testIntegerQuantization);
	CPPUNIT_TEST_SUITE_END();

public:
	void testTravelTiming()
	{
		for (int action : {WALK, SWIM, FLY})
			for (int dx = -1; dx <= 1; ++dx)
				for (int dy = -1; dy <= 1; ++dy)
				{
					const int advance = unitActionStepSpeed(32, action, dx, dy);
					if (dx != 0 && dy != 0)
					{
						// At speed 32, arrival crosses delta 256 on tick 12, not 11.
						CPPUNIT_ASSERT(11 * advance < UNIT_DELTA_QUANTUM);
						CPPUNIT_ASSERT(12 * advance >= UNIT_DELTA_QUANTUM);
					}
					else
						CPPUNIT_ASSERT_EQUAL(32, advance);
				}
	}

	void testNonTravelActions()
	{
		for (int action : {STOP_WALK, STOP_SWIM, STOP_FLY, BUILD, HARVEST,
			ATTACK_SPEED, MAGIC_ATTACK_AIR, MAGIC_CREATE_WOOD, HEAL, FEED})
			CPPUNIT_ASSERT_EQUAL(32, unitActionStepSpeed(32, action, 1, -1));
	}

	void testIntegerQuantization()
	{
		// Fixed expected values protect the replay-visible integer rounding.
		CPPUNIT_ASSERT_EQUAL(0, unitActionStepSpeed(0, WALK, 1, 1));
		CPPUNIT_ASSERT_EQUAL(11, unitActionStepSpeed(16, WALK, 1, 1));
		CPPUNIT_ASSERT_EQUAL(22, unitActionStepSpeed(32, WALK, 1, 1));
		CPPUNIT_ASSERT_EQUAL(45, unitActionStepSpeed(64, WALK, 1, 1));
		CPPUNIT_ASSERT_EQUAL(180, unitActionStepSpeed(255, WALK, 1, 1));
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(UnitTimingTest);
