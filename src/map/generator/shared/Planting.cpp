// SPDX-License-Identifier: GPL-3.0-or-later
#include "Planting.h"
#include "GenerationContext.h"
#include "Resources.h"
namespace MapGeneration
{
bool clearGround(const Map &map, int x, int y)
{
	return map.isGrass(x, y) && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID &&
		   map.getGroundUnit(x, y) == NOGUID;
}

std::vector<unsigned char> swarmSurroundings(const Torus &t, const GenerationContext &context,
											 int clearance)
{
	std::vector<unsigned char> reserved(size_t(t.w) * t.h, 0);
	for (int team = 0; team < context.request.nbTeams; ++team)
		for (int dy = -clearance; dy < 4 + clearance; ++dy)
			for (int dx = -clearance; dx < 4 + clearance; ++dx)
				reserved[t.at(context.bootX[team] + dx, context.bootY[team] + dy)] = 1;
	return reserved;
}

void clearAroundSwarms(Map &map, const GenerationContext &context, const Torus &t,
					   const std::vector<unsigned char> *keep)
{
	for (int team = 0; team < context.request.nbTeams; ++team)
		for (int dy = -kSwarmClearance; dy < 4 + kSwarmClearance; ++dy)
			for (int dx = -kSwarmClearance; dx < 4 + kSwarmClearance; ++dx)
			{
				const int x = t.x(context.bootX[team] + dx), y = t.y(context.bootY[team] + dy);
				if (map.isResource(x, y) && !(keep && (*keep)[y * t.w + x]))
					map.setNoResource(x, y, 1);
			}
}

void seedAlgae(Map &map, GenerationContext &context, const Torus &t, const char *stream,
			   int algaePercent, const AlgaeBand &band)
{
	const int n = t.w * t.h;
	std::vector<MapGeneratorPoint> water;
	if (band.nearestOffshore < 0)
	{
		for (int i = 0; i < n; ++i)
			if (map.isWater(i % t.w, i / t.w))
				water.emplace_back(i % t.w, i / t.w);
	}
	else
	{
		std::vector<unsigned char> wet(n), dry(n);
		for (int y = 0; y < t.h; ++y)
			for (int x = 0; x < t.w; ++x)
			{
				wet[y * t.w + x] = map.isWater(x, y);
				dry[y * t.w + x] = !map.isWater(x, y);
			}
		const std::vector<int> offshore = stepsFrom(t, dry, wet);
		for (int i = 0; i < n; ++i)
			if (offshore[i] >= band.nearestOffshore && offshore[i] <= band.farthestOffshore)
				water.emplace_back(i % t.w, i / t.w);
	}
	if (water.empty())
		return;
	for (int clump = 0; clump < scaledCount(int(water.size()) / band.tilesPerClump, algaePercent);
		 ++clump)
		placeResourceClump(map, context, water[context.bounded(stream, water.size())], ALGA,
						   band.clumpRadius);
}

void stockIslands(Map &map, GenerationContext &context, const std::vector<Island> &islands,
				  const char *stream)
{
	const int width = map.getW();
	for (const Island &island : islands)
	{
		std::vector<MapGeneratorPoint> grass;
		for (int i : island.tiles)
			if (map.isGrass(i % width, i / width))
				grass.emplace_back(i % width, i / width);
		if (grass.empty())
			continue;
		MapGeneratorPoint centre(island.x, island.y);
		if (!map.isGrass(centre.x, centre.y))
			centre = grass[context.bounded(stream, grass.size())];
		switch (context.bounded(stream, 3))
		{
		case 0:
			placeResourceClump(map, context, centre, STONE, 2);
			break;
		case 1:
			placeResourceClump(map, context, centre, CHERRY + int(context.bounded(stream, 3)), 2);
			break;
		default:
			placeResourceClump(map, context, centre, CORN, 2);
			break;
		}
	}
}
} // namespace MapGeneration
