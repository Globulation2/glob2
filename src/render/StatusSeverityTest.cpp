// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for StatusSeverity, the rules that decide when a status badge
// appears and how far its colour has gone from yellow toward red. The
// properties of interest: each badge starts yellow where its condition begins,
// reaches red at the condition's worst, and never moves back toward yellow as
// the condition gets worse.

#include "Glob2Test.h"

#include "StatusSeverity.h"

TEST_SUITE("StatusSeverity")
{
	TEST_CASE("Hunger is yellow where the badge appears and red once the unit starves")
	{
		const int capacity = 1000;
		CHECK_FALSE(StatusSeverity::unitHungry(200, capacity));
		CHECK(StatusSeverity::unitHungry(199, capacity));
		CHECK(StatusSeverity::unitHunger(199, capacity) < 0.01f);
		CHECK(StatusSeverity::unitHunger(100, capacity) == doctest::Approx(0.5));
		CHECK(StatusSeverity::unitHunger(0, capacity) == 1.f);
		CHECK(StatusSeverity::unitHungry(-50, capacity));
		CHECK(StatusSeverity::unitHunger(-50, capacity) == 1.f);
	}

	TEST_CASE("Injury is yellow at 60% health and red at death")
	{
		CHECK_FALSE(StatusSeverity::unitHurt(61, 100));
		CHECK(StatusSeverity::unitHurt(60, 100));
		CHECK(StatusSeverity::unitInjury(60, 100) == 0.f);
		CHECK(StatusSeverity::unitInjury(30, 100) == doctest::Approx(0.5));
		CHECK(StatusSeverity::unitInjury(0, 100) == 1.f);
	}

	TEST_CASE("Building damage runs from the first lost hit point to destruction")
	{
		CHECK(StatusSeverity::buildingDamage(999, 1000) < 0.01f);
		CHECK(StatusSeverity::buildingDamage(250, 1000) == doctest::Approx(0.75));
		CHECK(StatusSeverity::buildingDamage(0, 1000) == 1.f);
	}

	TEST_CASE("Staffing runs from the first shortfall to nobody working")
	{
		// 4 slots: the badge appears at 1 worker (3 missing); red at none.
		CHECK_FALSE(StatusSeverity::buildingUnderstaffed(2, 4));
		CHECK(StatusSeverity::buildingUnderstaffed(1, 4));
		CHECK(StatusSeverity::buildingStaffing(1, 4) == 0.f);
		CHECK(StatusSeverity::buildingStaffing(0, 4) == 1.f);
		// 8 slots: the badge appears at 3 workers; each one fewer steps a third.
		CHECK(StatusSeverity::buildingStaffing(3, 8) == 0.f);
		CHECK(StatusSeverity::buildingStaffing(2, 8) == doctest::Approx(1.0 / 3));
		CHECK(StatusSeverity::buildingStaffing(1, 8) == doctest::Approx(2.0 / 3));
		CHECK(StatusSeverity::buildingStaffing(0, 8) == 1.f);
		CHECK_FALSE(StatusSeverity::buildingUnderstaffed(0, 0));
	}

	TEST_CASE("A building with no step between first shortfall and none stays yellow")
	{
		CHECK(StatusSeverity::buildingUnderstaffed(0, 1));
		CHECK(StatusSeverity::buildingStaffing(0, 1) == 0.f);
		CHECK(StatusSeverity::buildingUnderstaffed(0, 2));
		CHECK(StatusSeverity::buildingStaffing(0, 2) == 0.f);
		// 3 slots does have a step: 1 worker is yellow, none is red.
		CHECK(StatusSeverity::buildingStaffing(1, 3) == 0.f);
		CHECK(StatusSeverity::buildingStaffing(0, 3) == 1.f);
	}

	TEST_CASE("A zero maximum gives no severity rather than a broken one")
	{
		CHECK_FALSE(StatusSeverity::unitHungry(0, 0));
		CHECK(StatusSeverity::unitHunger(0, 0) == 0.f);
		CHECK(StatusSeverity::unitInjury(0, 0) == 0.f);
		CHECK(StatusSeverity::buildingDamage(0, 0) == 0.f);
	}

	TEST_CASE("Severity never falls as a condition worsens")
	{
		float last = 0;
		for (int hp = 60; hp >= 0; --hp)
		{
			const float now = StatusSeverity::unitInjury(hp, 100);
			CHECK(now >= last);
			last = now;
		}
		last = 0;
		for (int hp = 1000; hp >= 0; hp -= 10)
		{
			const float now = StatusSeverity::buildingDamage(hp, 1000);
			CHECK(now >= last);
			last = now;
		}
		last = 0;
		for (int hungry = 199; hungry >= -100; --hungry)
		{
			const float now = StatusSeverity::unitHunger(hungry, 1000);
			CHECK(now >= last);
			last = now;
		}
		for (int slots = 1; slots <= 16; ++slots)
		{
			last = 0;
			for (int working = slots; working >= 0; --working)
			{
				if (!StatusSeverity::buildingUnderstaffed(working, slots))
					continue;
				const float now = StatusSeverity::buildingStaffing(working, slots);
				CHECK(now >= last);
				CHECK(now <= 1.f);
				last = now;
			}
		}
	}
}
