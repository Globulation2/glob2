// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "UnitTiming.h"
#include <initializer_list>

class UnitTimingTest
{

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
						CHECK(11 * advance < UNIT_DELTA_QUANTUM);
						CHECK(12 * advance >= UNIT_DELTA_QUANTUM);
					}
					else
						CHECK_EQ(32, advance);
				}
	}

	void testNonTravelActions()
	{
		for (int action : {STOP_WALK, STOP_SWIM, STOP_FLY, BUILD, HARVEST,
			ATTACK_SPEED, MAGIC_ATTACK_AIR, MAGIC_CREATE_WOOD, HEAL, FEED})
			CHECK_EQ(32, unitActionStepSpeed(32, action, 1, -1));
	}

	void testIntegerQuantization()
	{
		// Fixed expected values protect the replay-visible integer rounding.
		CHECK_EQ(0, unitActionStepSpeed(0, WALK, 1, 1));
		CHECK_EQ(11, unitActionStepSpeed(16, WALK, 1, 1));
		CHECK_EQ(22, unitActionStepSpeed(32, WALK, 1, 1));
		CHECK_EQ(45, unitActionStepSpeed(64, WALK, 1, 1));
		CHECK_EQ(180, unitActionStepSpeed(255, WALK, 1, 1));
	}
};

TEST_SUITE("UnitTiming")
{
	TEST_CASE_FIXTURE(UnitTimingTest, "TravelTiming") { testTravelTiming(); }
	TEST_CASE_FIXTURE(UnitTimingTest, "NonTravelActions") { testNonTravelActions(); }
	TEST_CASE_FIXTURE(UnitTimingTest, "IntegerQuantization") { testIntegerQuantization(); }
}

TEST_CASE("terrain travel speed uses deterministic bounded Q8 factors")
{
    CHECK_EQ(unitTerrainMovementSpeed(32,256),32);
    CHECK_EQ(unitTerrainMovementSpeed(32,128),16);
    CHECK_EQ(unitTerrainMovementSpeed(32,512),64);
    CHECK_EQ(unitTerrainMovementSpeed(3,128),2);
    CHECK_EQ(unitTerrainMovementSpeed(200,512),UNIT_DELTA_MAX);
    CHECK_EQ(unitTerrainMovementSpeed(0,512),0);
    CHECK_EQ(unitTerrainMovementSpeed(30,1024),120);
    CHECK_EQ(unitTerrainMovementSpeed(28,1024),112);
    CHECK_EQ(unitTerrainMovementSpeed(32,512,false),32);
    CHECK_EQ(unitTerrainMovementSpeed(32,128,false),32);
    CHECK(unitActionStepSpeed(unitTerrainMovementSpeed(1,64),WALK,1,1)>0);
}


TEST_CASE("inside service phase advances stay within one action per tick")
{
    CHECK(unitActionStepSpeed(1,WALK,1,1,true)==1);
    CHECK(unitActionStepSpeed(12,WALK,1,0,true)==12);
    CHECK(unitActionStepSpeed(12,WALK,1,1,true)==8);
    CHECK(unitActionStepSpeed(256,STOP_WALK,0,0,true)==256);
    CHECK(unitActionStepSpeed(2400,STOP_WALK,0,0,true)==256);
    CHECK(unitActionStepSpeed(51200,WALK,1,1,true)==256);
    CHECK(unitActionStepSpeed(2400,STOP_WALK,0,0)==2400);
}
