#pragma once
#include "Regions.h"
namespace MapGeneration
{
void adjustHeightmapFromPoints(Map &map, std::vector<MapGeneratorPoint> &points,
							   std::vector<int> &heightmap, int value);
void adjustHeightmapFromPerlinNoise(Map &map, GenerationContext &context, std::vector<int> &heights,
									int spread);
void computeDistances(Map &map, std::vector<MapGeneratorPoint> &sources,
					  std::vector<MapGeneratorPoint> &obstacles, std::vector<int> &heightmap);
int computeAverageDistance(Map &map, std::vector<int> &grid, int areaN,
						   const std::vector<int> &heightmap);
} // namespace MapGeneration
