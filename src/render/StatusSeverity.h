// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

//! When a unit or building raises a status badge, and how bad its condition is.
//!
//! Each severity runs from 0, when the condition has just begun and the badge
//! is yellow, to 1 at its worst, when it is red; MapOverlayQueue blends the
//! colour in between. Every function is pure so the rules can be tested
//! without a renderer. A severity is only meaningful while its predicate holds.
namespace StatusSeverity
{
	//! Clamps to [0, 1]; anything not a number, as from a zero maximum, is 0.
	inline float clamp01(double value)
	{
		return !(value > 0) ? 0.f : value >= 1 ? 1.f : float(value);
	}

	//! The food bar has 10 bars; a unit is hungry once it shows this many or fewer.
	constexpr int HungryFoodBars = 1;
	//! A unit is hungry once its food bar shows HungryFoodBars or fewer.
	inline bool unitHungry(int hungry, int foodCapacity)
	{
		return foodCapacity > 0 && (long long)hungry * 10 / foodCapacity <= HungryFoodBars;
	}
	//! Yellow where hunger begins, just under HungryFoodBars + 1 bars, red once
	//! the food is gone and the unit starves.
	inline float unitHunger(int hungry, int foodCapacity)
	{
		return clamp01(1 - hungry / ((HungryFoodBars + 1) * double(foodCapacity) / 10));
	}

	//! A unit is hurt at 60% of its hit points or fewer.
	inline bool unitHurt(int hp, int maxHp)
	{
		return (long long)hp * 10 <= (long long)maxHp * 6;
	}
	//! Yellow at 60% of its hit points, red as it nears death.
	inline float unitInjury(int hp, int maxHp)
	{
		return clamp01(1 - double(hp) / (0.6 * maxHp));
	}

	//! Yellow at the first lost hit point, red as the building nears destruction.
	inline float buildingDamage(int hp, int maxHp)
	{
		return clamp01(1 - double(hp) / maxHp);
	}

	//! A building is understaffed with under half of its worker slots filled.
	inline bool buildingUnderstaffed(int working, int slots)
	{
		return slots > 0 && working * 2 < slots;
	}
	//! Yellow at the first shortfall that raises the badge, red with nobody
	//! working. A building with too few slots for a step between the two, such
	//! as 0 of 1 or 0 of 2, rounds down to yellow.
	inline float buildingStaffing(int working, int slots)
	{
		// Workers missing when buildingUnderstaffed first holds.
		const int missingAtFirstShortfall = slots / 2 + 1;
		if (slots <= missingAtFirstShortfall)
			return 0;
		return clamp01(double(slots - working - missingAtFirstShortfall) / (slots - missingAtFirstShortfall));
	}
}
