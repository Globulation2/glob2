// SPDX-License-Identifier: GPL-3.0-or-later

#include "BuildingGuiState.h"

#include "building/Building.h"
#include "map/Map.h"
#include "team/Team.h"
#include "unit/Unit.h"

namespace {
const BuildingGuiState* lookup(const BuildingGuiStateMap& m, const Building& b)
{
	auto it = m.find(b.gid);
	return it == m.end() ? nullptr : &it->second;
}
}

Sint32 displayedPosX(const BuildingGuiStateMap& m, const Building& b)
{
	const BuildingGuiState* s = lookup(m, b);
	return (s && s->pendingPosX) ? *s->pendingPosX : b.posX;
}

Sint32 displayedPosY(const BuildingGuiStateMap& m, const Building& b)
{
	const BuildingGuiState* s = lookup(m, b);
	return (s && s->pendingPosY) ? *s->pendingPosY : b.posY;
}

Sint32 displayedMaxUnitWorking(const BuildingGuiStateMap& m, const Building& b)
{
	const BuildingGuiState* s = lookup(m, b);
	return (s && s->pendingMaxUnitWorking) ? *s->pendingMaxUnitWorking : b.maxUnitWorking;
}

Sint32 displayedUnitStayRange(const BuildingGuiStateMap& m, const Building& b)
{
	const BuildingGuiState* s = lookup(m, b);
	return (s && s->pendingUnitStayRange) ? *s->pendingUnitStayRange : b.unitStayRange;
}

void computeFlagStatDisplayed(const Building& b, Sint32 posX, Sint32 posY,
                              Sint32 stayRange, int* goingTo, int* onSpot)
{
	*goingTo = 0;
	*onSpot = 0;

	const Sint32 stayRangeSquare = (1 + stayRange) * (1 + stayRange);
	for (auto* unit : b.unitsWorking)
	{
		const Sint32 distSquare = b.owner->map->warpDistSquare(posX, posY, unit->posX, unit->posY);
		if (distSquare < stayRangeSquare)
			(*onSpot)++;
		else
			(*goingTo)++;
	}
}
