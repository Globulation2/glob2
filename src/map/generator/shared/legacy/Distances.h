// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Height and distance fields over the legacy area grid (Regions.h). computeDistances is
// Grid.h's stepsFrom in the historical encoding; new code should call stepsFrom directly.
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
