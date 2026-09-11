#pragma once
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
// Chooses each colony's boot tile so that every colony's walk to its primary resources is as
// nearly equal as the finished map allows, rather than handing the best land to whoever is
// picked first. Requires resources to already be on the map. Returns false (leaving bootX/bootY
// untouched) when no set of legal, mutually distant sites can reach both resources, so a caller
// can fall back to a terrain-only search.
bool chooseBalancedStarts(Game &game, GenerationContext &context, int minDistSquare);
bool placeStarts(Game &, GenerationContext &);
bool placeArchipelagoStarts(Game &, GenerationContext &, int islandSize);
} // namespace MapGeneration
