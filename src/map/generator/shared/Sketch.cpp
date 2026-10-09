#include "GenerationWork.h"
#include "GenerationNumeric.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Sketch.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Map.h"
#include <algorithm>
#include <cmath>
namespace MapGeneration
{
std::vector<unsigned char> pureTiles(const Map &map, TerrainType type)
{
	const Torus t(map);
	std::vector<unsigned char> result(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		result.at(i) = map.terrainTypeAt(t.remainderX(i), i / t.w) == type;
	}
	return result;
}

void layBeaches(TerrainSketch &terrain, const Torus &t)
{
	const TerrainSketch original(terrain);
	for (int y = 0; y < t.h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const int i = y * t.w + x;
			if (original.at(i) == WATER)
				continue;
			bool shore = false;
			for (int dy = -1; dy <= 1 && !shore; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = -1; dx <= 1 && !shore; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					shore = original.at(t.at(x + dx, y + dy)) == WATER;
				}
			}
			if (shore)
				terrain.at(i) = SAND;
		}
	}
}

void writeVertices(Map &map, const TerrainSketch &terrain)
{
	std::vector<TerrainType> vertices(terrain.size());
	std::transform(terrain.begin(), terrain.end(), vertices.begin(), [](unsigned char t) { return TerrainType(t); });
	map.assignVertexTerrain(vertices);
}

void paintTile(Map &map, int x, int y, TerrainType type)
{
	map.paintVertices({{x, y}, {x + 1, y}, {x, y + 1}, {x + 1, y + 1}}, type, false);
}

int countTiles(const TerrainSketch &terrain, TerrainType type)
{
	return int(std::count(terrain.begin(), terrain.end(), (unsigned char)type));
}

std::vector<unsigned char> tileCorners(const Torus &t, const std::vector<unsigned char> &tiles)
{
	std::vector<unsigned char> corners(tiles.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (tiles.at(i))
		{
			const int x = t.remainderX(i), y = i / t.w;
			corners.at(i) = corners.at(t.at(x + 1, y)) = corners.at(t.at(x, y + 1)) =
				corners.at(t.at(x + 1, y + 1)) = 1;
		}
	}
	return corners;
}

std::vector<unsigned char> pureTiles(const TerrainSketch &terrain, const Torus &t, TerrainType type)
{
	std::vector<unsigned char> pure(terrain.size(), 0);
	const unsigned char want = (unsigned char)type;
	for (int y = 0; y < t.h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			pure.at(size_t(y) * t.w + x) =
				terrain.at(t.at(x, y)) == want && terrain.at(t.at(x + 1, y)) == want &&
				terrain.at(t.at(x, y + 1)) == want && terrain.at(t.at(x + 1, y + 1)) == want;
		}
	}
	return pure;
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
		::MapGeneration::generationCheckpoint();
		water.at(i) = terrain.at(i) == WATER;
		land.at(i) = !water.at(i);
		if (water.at(i))
			sea.push_back(int(i));
	}
	if (placement.wanted <= 0 || sea.empty())
		return islands;
	const std::vector<int> offshore = stepsFrom(t, land, water);
	// Islands are radius 4 to 6, grown with the square root of the map's shorter side over 128 (1
	// to 1.6 times): big enough on every map to hold a beach and a prize, and not a continent on a
	// 512 map.
	const double scale =
		std::clamp(::MapGeneration::Numeric::sqrt(std::min(t.w, t.h) / 128.0), 1.0, 1.6);
	for (int attempt = 0; int(islands.size()) < placement.wanted &&
						  attempt < placement.wanted * placement.attemptsPerIsland;
		 ++attempt)
	{
		::MapGeneration::generationCheckpoint();
		const int at = sea.at(context.bounded(placement.stream, sea.size()));
		const int x = t.remainderX(at), y = at / t.w;
		const RadialShape shape((4 + context.bounded(placement.stream, 3)) * scale, 0.3, context,
								placement.stream);
		const double reach = shape.maximumRadius();
		if (offshore.at(at) < reach + placement.moat)
			continue;
		bool clear = true;
		for (const Island &other : islands)
		{
			::MapGeneration::generationCheckpoint();
			const double gap = reach + other.reach + placement.moat;
			clear = clear && t.dist2(x, y, other.x, other.y) >= gap * gap;
		}
		if (!clear)
			continue;
		Island island{x, y, reach, {}};
		const int r = int(::MapGeneration::Numeric::ceil(reach));
		for (int dy = -r; dy <= r; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -r; dx <= r; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				if (::MapGeneration::Numeric::hypot(double(dx), double(dy)) <
					shape.radiusAt(::MapGeneration::Numeric::atan2(double(dy), double(dx))))
				{
					const int i = t.at(x + dx, y + dy);
					terrain.at(i) = GRASS;
					island.tiles.push_back(i);
				}
			}
		}
		islands.push_back(std::move(island));
	}
	return islands;
}

void keepRoadInland(std::vector<unsigned char> &road, const Torus &t,
					const std::vector<unsigned char> &water, int gap)
{
	const std::vector<int> shore = stepsFrom(t, water);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (road.at(i) && shore.at(i) >= 0 && shore.at(i) < gap)
			road.at(i) = 0;
	}
}

std::vector<unsigned char> roadTiles(const Torus &t, const std::vector<unsigned char> &road)
{
	std::vector<unsigned char> tiles(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (road.at(i))
			for (int dy = -1; dy <= 0; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = -1; dx <= 0; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					tiles.at(t.at(t.remainderX(i) + dx, i / t.w + dy)) = 1;
				}
			}
	}
	return tiles;
}
} // namespace MapGeneration
