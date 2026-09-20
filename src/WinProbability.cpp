/*
  Copyright (C) 2026 Globulation2 contributors

  SPDX-License-Identifier: GPL-3.0-or-later

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#include "WinProbability.h"
#include "WinProbabilityModel.h"
#include "Game.h"
#include "GameHeader.h"
#include "Team.h"
#include "TeamStat.h"
#include "Unit.h"
#include "BuildingType.h"
#include <algorithm>

namespace WinProbability
{
	Sint64 clampCount(Sint64 value)
	{
		if (value < 0)
			return 0;
		return value > COUNT_CLAMP ? COUNT_CLAMP : value;
	}

	Sint64 countValue(Sint64 value)
	{
		return clampCount(value);
	}

	Sint64 shareValue(Sint64 value, Sint64 total)
	{
		if (total <= 0)
			return 0;
		return (clampCount(value) << FRACTION_SHIFT) / total;
	}

	Sint64 squareRoot(Sint64 value)
	{
		if (value <= 0)
			return 0;
		// Bit-by-bit integer square root. Chosen over Newton's method because it
		// terminates in a fixed number of steps with no division, so there is
		// nothing for a platform to round differently.
		Sint64 remainder = value;
		Sint64 result = 0;
		Sint64 bit = (Sint64)1 << 62;
		while (bit > remainder)
			bit >>= 2;
		while (bit != 0)
		{
			if (remainder >= result + bit)
			{
				remainder -= result + bit;
				result = (result >> 1) + bit;
			}
			else
				result >>= 1;
			bit >>= 2;
		}
		return result;
	}

	Sint64 rootValue(Sint64 value)
	{
		// sqrt(v) with ROOT_SHIFT fractional bits is isqrt(v << 2*ROOT_SHIFT).
		return squareRoot(clampCount(value) << (2 * ROOT_SHIFT));
	}

	Sint64 ratioValue(Sint64 numerator, Sint64 denominator)
	{
		if (denominator <= 0)
			return 0;
		const Sint64 value = (clampCount(numerator) << FRACTION_SHIFT) / denominator;
		return std::min(value, FRACTION_ONE);
	}

	Sint64 expNegative(Sint64 value)
	{
		if (value <= 0)
			return FITNESS_ONE;
		// ln 2 with FITNESS_SHIFT fractional bits.
		const Sint64 LOG_TWO = 2977044472LL;
		const Sint64 whole = value / LOG_TWO;
		// Past this the result is smaller than one unit in the last place, so it
		// is zero for every purpose this serves.
		if (whole >= FITNESS_SHIFT + 1)
			return 0;
		const Sint64 rest = value - whole * LOG_TWO;
		// exp(-rest) for rest in [0, ln 2) as the nested form
		//   1 - r(1 - r/2(1 - r/3(...)))
		// carried at SERIES_SHIFT bits, which keeps every product inside Sint64.
		// Ten terms put the truncation error below one part in 10^9, far under the
		// permille the caller rounds to.
		const int SERIES_SHIFT = 30;
		const Sint64 SERIES_ONE = (Sint64)1 << SERIES_SHIFT;
		const Sint64 r = rest >> (FITNESS_SHIFT - SERIES_SHIFT);
		Sint64 term = SERIES_ONE;
		for (int n = 10; n >= 2; --n)
			term = SERIES_ONE - ((r * term) >> SERIES_SHIFT) / n;
		Sint64 result = SERIES_ONE - ((r * term) >> SERIES_SHIFT);
		if (result < 0)
			result = 0;
		result <<= (FITNESS_SHIFT - SERIES_SHIFT);
		return result >> whole;
	}

	std::vector<int> permille(const std::vector<Slot> &slots)
	{
		std::vector<int> result(slots.size(), 0);
		Sint64 best = 0;
		bool any = false;
		for (std::size_t i = 0; i < slots.size(); ++i)
		{
			if (!slots[i].alive)
				continue;
			const Sint64 fitness = winProbabilityFitness(slots, i);
			if (!any || fitness > best)
				best = fitness;
			any = true;
		}
		if (!any)
			return result;
		// Shifted by the largest fitness so every exponent is of a non-positive
		// number, which is the only range expNegative covers and the only one that
		// cannot overflow.
		std::vector<Sint64> weights(slots.size(), 0);
		Sint64 total = 0;
		for (std::size_t i = 0; i < slots.size(); ++i)
		{
			if (!slots[i].alive)
				continue;
			weights[i] = expNegative(best - winProbabilityFitness(slots, i));
			total += weights[i];
		}
		if (total <= 0)
			return result;
		for (std::size_t i = 0; i < slots.size(); ++i)
			if (slots[i].alive)
				result[i] = (int)((weights[i] * 1000) / total);
		return result;
	}

	int decided(const std::vector<Slot> &slots, int thresholdPermille)
	{
		const std::vector<int> chances = permille(slots);
		for (std::size_t i = 0; i < chances.size(); ++i)
			if (slots[i].alive && chances[i] >= thresholdPermille)
				return (int)i;
		return -1;
	}

	std::vector<Slot> slotsOf(const Game &game, std::vector<int> &allianceOf)
	{
		const int count = game.teamsCount();
		allianceOf.assign(count, 0);
		// One slot per alliance, in first-appearance order, because that is the
		// competitor the model was fitted on: allies share a fate.
		std::vector<int> alliances;
		for (int t = 0; t < count; ++t)
		{
			const int alliance = game.gameHeader.getAllyTeamNumber(t);
			std::vector<int>::iterator found = std::find(alliances.begin(), alliances.end(), alliance);
			if (found == alliances.end())
			{
				allianceOf[t] = (int)alliances.size();
				alliances.push_back(alliance);
			}
			else
				allianceOf[t] = (int)(found - alliances.begin());
		}
		std::vector<Slot> slots(alliances.size());
		for (int t = 0; t < count; ++t)
		{
			const Team *team = game.teams[t];
			if (!team)
				continue;
			Slot &slot = slots[allianceOf[t]];
			// An eliminated team contributes nothing and does not keep its
			// alliance alive on its own; a surviving ally still can.
			if (!team->isAlive || team->hasLost)
				continue;
			const TeamStat *stat = team->stats.getLatestStat();
			slot.alive = true;
			slot.units += stat->totalUnit;
			slot.prestige += team->prestige;
			slot.barracks += stat->numberBuildingPerType[IntBuildingType::ATTACK_BUILDING];
			slot.explorers += stat->numberUnitPerType[EXPLORER];
			slot.foodCritical += stat->needFoodCritical;
			slot.attack += stat->totalAttackPower;
		}
		return slots;
	}
}
