// SPDX-License-Identifier: GPL-3.0-or-later
#include "Sketch.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Map.h"
#include <algorithm>
#include <cmath>
namespace MapGeneration
{
void layBeaches(TerrainSketch &terrain, const Torus &t)
{
	const TerrainSketch original(terrain);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			if (original[i] == WATER)
				continue;
			bool shore = false;
			for (int dy = -1; dy <= 1 && !shore; ++dy)
				for (int dx = -1; dx <= 1 && !shore; ++dx)
					shore = original[t.at(x + dx, y + dy)] == WATER;
			if (shore)
				terrain[i] = SAND;
		}
}

void writeUndermap(Map &map, const TerrainSketch &terrain)
{
	const int w = map.getW(), h = map.getH();
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			map.setUMTerrain(x, y, TerrainType(terrain[size_t(y) * w + x]));
	map.rebuildTerrain();
}

int countTiles(const TerrainSketch &terrain, TerrainType type)
{
	return int(std::count(terrain.begin(), terrain.end(), (unsigned char)type));
}

std::vector<Island> raiseIslands(TerrainSketch &terrain, const Torus &t, GenerationContext &context,
								 const IslandPlacement &placement)
{
	std::vector<Island> islands;
	const size_t area = size_t(t.w) * t.h;
	std::vector<unsigned char> water(area), land(area);
	std::vector<int> sea;
	for (size_t i = 0; i < area; ++i)
	{
		water[i] = terrain[i] == WATER;
		land[i] = !water[i];
		if (water[i])
			sea.push_back(int(i));
	}
	if (placement.wanted <= 0 || sea.empty())
		return islands;
	const std::vector<int> offshore = stepsFrom(t, land, water);
	const double scale = std::clamp(std::sqrt(std::min(t.w, t.h) / 128.0), 1.0, 1.6);
	for (int attempt = 0; int(islands.size()) < placement.wanted &&
						  attempt < placement.wanted * placement.attemptsPerIsland;
		 ++attempt)
	{
		const int at = sea[context.bounded(placement.stream, sea.size())];
		const int x = at % t.w, y = at / t.w;
		const RadialShape shape((4 + context.bounded(placement.stream, 3)) * scale, 0.3, context,
								placement.stream);
		const double reach = shape.maximumRadius();
		if (offshore[at] < reach + placement.moat)
			continue;
		bool clear = true;
		for (const Island &other : islands)
		{
			const double gap =
				(placement.gapFromBothReaches ? reach + other.reach : 2 * reach) + placement.moat;
			clear = clear && t.dist2(x, y, other.x, other.y) >= gap * gap;
		}
		if (!clear)
			continue;
		Island island{x, y, reach, {}};
		const int r = int(std::ceil(reach));
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; ++dx)
				if (std::hypot(double(dx), double(dy)) <
					shape.radiusAt(std::atan2(double(dy), double(dx))))
				{
					const int i = t.at(x + dx, y + dy);
					terrain[i] = GRASS;
					island.tiles.push_back(i);
				}
		islands.push_back(std::move(island));
	}
	return islands;
}
} // namespace MapGeneration
