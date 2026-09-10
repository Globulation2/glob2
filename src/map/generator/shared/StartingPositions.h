#pragma once
#include "Regions.h"
namespace MapGeneration
{
bool divideUpPlayerLands(Game &game, GenerationContext &context, std::vector<int> &grid,
						 std::vector<int> &teamAreaNumbers, int &areaNumber);
void chooseFreeForBuildingSquares(Game &game, std::vector<MapGeneratorPoint> &points,
								  BuildingType *type, int team);
void chooseFreeForGroundUnits(Map &map, std::vector<MapGeneratorPoint> &points, int team);
void chooseTouchingBuilding(Map &map, std::vector<MapGeneratorPoint> &points, Building *building);
Building *addBuilding(Game &game, int x, int y, int team, int type, int level,
					  bool underConstruction);
} // namespace MapGeneration

namespace MapGeneration
{
bool placeStarts(Game &, GenerationContext &);
bool placeArchipelagoStarts(Game &, GenerationContext &, int islandSize);
} // namespace MapGeneration
