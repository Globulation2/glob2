// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "Distances.h"
#include "GenerationContext.h"
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
	// Every cell is enqueued at most once - the `side == 0` guard below stops a second push
	// once it's been visited - so a flood over the whole map never needs more than w*h queue
	// slots. A flat, preallocated FIFO fills that bound with sequential writes; std::queue<int>
	// (std::deque-backed) instead grows by separately heap-allocated blocks, turning most
	// pushes and pops into a pointer chase to a freshly touched cache line.
	const size_t mapSize = size_t(map.getW()) * map.getH();
	std::vector<int> places(mapSize);
	size_t head = 0, tail = 0;
	heightmap.clear();
	heightmap.resize(mapSize, 0);
	for (unsigned int i = 0; i < sources.size(); ++i)
	{
		// Callers can list a tile more than once (Isles' land-bridge lines cross each other). Queueing
		// a repeat takes a slot the w*h bound above never counted, so each one overran `places`, and
		// the flood then read tile indices back from past its end. A repeated source adds nothing
		// to the flood, so skip it.
		const int index = sources[i].y * map.getW() + sources[i].x;
		if (heightmap[index] == 1)
			continue;
		heightmap[index] = 1;
		places[tail++] = index;
	}
	for (unsigned int i = 0; i < obstacles.size(); ++i)
	{
		heightmap[obstacles[i].y * map.getW() + obstacles[i].x] = -1;
	}

	Uint32 wDec = map.wDec;
	Uint32 hMask = map.hMask;
	Uint32 wMask = map.wMask;
	while (head < tail)
	{
		int deltaAddrG = places[head++];

		size_t y = deltaAddrG >> wDec; // Calculate the coordinates of
		size_t x = deltaAddrG & wMask; // the current field and of the

		size_t yu = ((y - 1) & hMask); // fields next to it.
		size_t yd = ((y + 1) & hMask); // We live on a torus! If we are on
		size_t xl = ((x - 1) & wMask); // the "last line" of the map, the
		size_t xr = ((x + 1) & wMask); // next line is the line 0 again.

		int g = heightmap[(y << wDec) | x] + 1;

		size_t deltaAddrC[8];
		int *addr;
		int side;

		deltaAddrC[0] = (yu << wDec) | xl; // Calculate the positions of the
		deltaAddrC[1] = (yu << wDec) | x;  // 8 fields next to us from their
		deltaAddrC[2] = (yu << wDec) | xr; // coordinates.
		deltaAddrC[3] = (y << wDec) | xr;
		deltaAddrC[4] = (yd << wDec) | xr;
		deltaAddrC[5] = (yd << wDec) | x;
		deltaAddrC[6] = (yd << wDec) | xl;
		deltaAddrC[7] = (y << wDec) | xl;
		for (int ci = 0; ci < 8; ci++) // Check for each of this fields if we
		{							   // can improve its gradient value
			addr = &heightmap[deltaAddrC[ci]];
			side = *addr;
			if (side == 0)
			{
				*addr = g;
				places[tail++] = deltaAddrC[ci];
			}
		}
	}
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
