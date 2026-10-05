// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingGradientBuild.h"
#include "Map.h"
#include "Game.h"
#include "BuildingType.h"

namespace building_gradient
{
inline Building *refreshDestination(Game *game, int gid)
{
	if (gid < 0 || gid >= Building::MAX_COUNT * Team::MAX_COUNT)
		return nullptr;
	const int team = Building::GIDtoTeam(gid);
	return team < game->teamsCount() && game->teams[team]
			   ? game->teams[team]->myBuildings[Building::GIDtoID(gid)]
			   : nullptr;
}
inline building_gradient::Destination refreshDescription(const Building &b, int swim,
														 std::uint32_t epoch)
{
	building_gradient::Destination d;
	d.gid = b.gid;
	d.identity = b.scriptIdentity;
	d.epoch = epoch;
	d.x = b.posX;
	d.y = b.posY;
	d.width = b.type->width;
	d.radius = b.unitStayRange;
	d.swim = swim;
	d.teamMask = b.owner->me;
	d.allies = b.owner->allies;
	d.virtualBuilding = b.type->isVirtual;
	d.clearing = d.virtualBuilding && b.type->zonable[WORKER];
	d.war = d.virtualBuilding && b.type->zonable[WARRIOR];
	for (int r = 0; r < BASIC_COUNT; ++r)
		d.clearingResources[r] = b.clearingResources[r];
	return d;
}
} // namespace building_gradient
