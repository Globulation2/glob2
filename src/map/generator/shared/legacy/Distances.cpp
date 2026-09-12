// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "Distances.h"
#include "GenerationContext.h"
#include "Grid.h"
#include "HeightMap.h"
#include "Map.h"
#include "Regions.h"
#include "Resources.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include <algorithm>
#include <cmath>
using namespace MapGeneration;

namespace MapGeneration
{
void adjustHeightmapFromPoints(Map &map, std::vector<MapGeneratorPoint> &points,
							   std::vector<int> &heightmap, int value)
{
	for (unsigned int i = 0; i < points.size(); ++i)
	{
		heightmap[points[i].y * map.getW() + points[i].x] += value;
	}
}

void adjustHeightmapFromPerlinNoise(Map &map, GenerationContext &context, std::vector<int> &heights,
									int spread)
{
	HeightMap noise(map.getW(), map.getH(), context.stream("noise"));
	noise.makePlain(4);
	// Every cell's result depends only on its own (x, y), not on visitation order, so nesting
	// y outside x walks `heights` (and noise's own w*h-sized field) with the grain of its
	// row-major layout instead of across it: the inner loop's writes land in consecutive
	// memory instead of striding by a full row on every step.
	for (int y = 0; y < map.getH(); ++y)
	{
		for (int x = 0; x < map.getW(); ++x)
		{
			heights[y * map.getW() + x] += noise.uiLevel(x, y, spread * 2) - spread;
		}
	}
}

void computeDistances(Map &map, std::vector<MapGeneratorPoint> &sources,
					  std::vector<MapGeneratorPoint> &obstacles, std::vector<int> &heightmap)
{
	// Callers can list a tile more than once (Isles' land-bridge lines cross each other); a mask
	// takes it once. Sources and obstacles never overlap in any caller.
	const Torus t(map);
	std::vector<unsigned char> source(size_t(t.size()), 0), open(size_t(t.size()), 1);
	for (const MapGeneratorPoint &p : sources)
		source[t.at(p.x, p.y)] = 1;
	for (const MapGeneratorPoint &p : obstacles)
		open[t.at(p.x, p.y)] = 0;
	const std::vector<int> steps = stepsFrom(t, source, open);
	// The historical encoding: a source reads 1, each ring one more, obstacles -1, unreached 0.
	heightmap.assign(steps.size(), 0);
	for (size_t i = 0; i < steps.size(); ++i)
		heightmap[i] = !open[i] ? -1 : steps[i] < 0 ? 0 : steps[i] + 1;
}

int computeAverageDistance(Map &map, std::vector<int> &grid, int areaN,
						   const std::vector<int> &heightmap)
{
	long total = 0;
	int count = 0;
	// Addition is commutative, so summing in whichever order touches memory sequentially -
	// y outside x, matching grid/heightmap's row-major layout - gives the exact same total.
	for (int y = 0; y < map.getH(); ++y)
	{
		for (int x = 0; x < map.getW(); ++x)
		{
			if (grid[y * map.getW() + x] == areaN)
			{
				total += heightmap[y * map.getW() + x];
				count += 1;
			}
		}
	}
	return count > 0 ? total / count : 0;
}
} // namespace MapGeneration
