// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The older colony placers: divideUpPlayerLands for the point-dispersion generators and the
// boot-tile placers of the height-field and old random generators. A designed generator
// places its colonies with shared/Settlements.h; the balanced site search the height-field
// generators use is shared/BalancedStarts.h.
#include "Regions.h"
namespace MapGeneration
{
// Each colony's wheat and wood fields and its stone deposits, as percentages of the default kit.
struct PlayerLandResources
{
	int wheat = 100, wood = 100, stone = 100;
};
bool divideUpPlayerLands(Game &game, GenerationContext &context, std::vector<int> &grid,
						 std::vector<int> &teamAreaNumbers, int &areaNumber,
						 const PlayerLandResources &resources = {});
void chooseFreeForBuildingSquares(Game &game, std::vector<MapGeneratorPoint> &points,
								  BuildingType *type, int team);
void chooseFreeForGroundUnits(Map &map, std::vector<MapGeneratorPoint> &points, int team);
void chooseTouchingBuilding(Map &map, std::vector<MapGeneratorPoint> &points, Building *building);
Building *addBuilding(Game &game, int x, int y, int team, int type, int level,
					  bool underConstruction);
} // namespace MapGeneration

namespace MapGeneration
{
/// Builds every colony's swarm and workers on its chosen boot tile, clearing a small rectangle of
/// grass for them first.
bool placeStarts(Game &, GenerationContext &);
bool placeArchipelagoStarts(Game &, GenerationContext &, int islandSize);
} // namespace MapGeneration
