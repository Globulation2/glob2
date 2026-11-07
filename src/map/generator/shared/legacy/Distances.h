// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Height and distance fields over the legacy area grid (Regions.h). computeDistances is
// Grid.h's stepsFrom in the historical encoding; new code should call stepsFrom directly.
#include "Regions.h"
namespace MapGeneration
{
void adjustHeightmapFromPoints(GenerationMap &map, std::vector<MapGeneratorPoint> &points,
							   std::vector<int> &heightmap, int value);
void adjustHeightmapFromPerlinNoise(GenerationMap &map, GenerationContext &context,
									std::vector<int> &heights, int spread);
void computeDistances(GenerationMap &map, std::vector<MapGeneratorPoint> &sources,
					  std::vector<MapGeneratorPoint> &obstacles, std::vector<int> &heightmap);
int computeAverageDistance(GenerationMap &map, std::vector<int> &grid, int areaN,
						   const std::vector<int> &heightmap);
} // namespace MapGeneration
