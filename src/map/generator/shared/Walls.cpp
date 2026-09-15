// SPDX-License-Identifier: GPL-3.0-or-later
#include "Walls.h"
#include "Map.h"
#include "Sketch.h"
#include "TerrainType.h"
#include "Topology.h"
#include <algorithm>
#include <stdexcept>
namespace MapGeneration
{
std::vector<unsigned char> seaMargin(const Map &map, const Torus &t,
									 const std::vector<unsigned char> &sea,
									 const std::vector<unsigned char> &notBeach)
{
	const int n = t.w * t.h;
	std::vector<unsigned char> margin(n, 0), beach(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (map.isWater(x, y))
			continue;
		beach[i] = map.getTerrainType(x, y) != GRASS && !notBeach[i];
		// A tile's corners are undermap vertices (x, y) to (x + 1, y + 1); the beach pass reaches a
		// vertex one step further, so a sea vertex anywhere in the box one wider decides the tile.
		for (int dy = -1; dy <= 2 && !margin[i]; ++dy)
			for (int dx = -1; dx <= 2; ++dx)
				if (sea[t.at(x + dx, y + dy)])
				{
					margin[i] = 1;
					break;
				}
	}
	const std::vector<int> joined = stepsFrom(t, margin, beach);
	for (int i = 0; i < n; ++i)
		if (joined[i] >= 0)
			margin[i] = 1;
	return margin;
}

std::vector<unsigned char> seaVertices(const Map &map, const Torus &t,
									   const std::vector<unsigned char> &lakes)
{
	std::vector<unsigned char> sea(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		sea[i] = map.getUMTerrain(i % t.w, i / t.w) == WATER && !lakes[i];
	return sea;
}

int seaEntry(const Map &map, const Torus &t, const std::vector<unsigned char> &margin,
			 const std::vector<unsigned char> &inside)
{
	const std::vector<unsigned char> open = walkableTiles(map);
	std::vector<unsigned char> beach(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		beach[i] = open[i] && margin[i];
	const std::vector<int> landed = stepsFrom(t, beach, open);
	for (int i = 0; i < t.size(); ++i)
		if (landed[i] >= 0 && !margin[i] && inside[i])
			return i;
	return -1;
}

std::vector<unsigned char> sealCoasts(const Map &map, const Torus &t,
									  const std::vector<unsigned char> &margin,
									  const std::vector<unsigned char> &wallable)
{
	const int n = t.w * t.h;
	std::vector<unsigned char> stone(n, 0);
	for (int i = 0; i < n; ++i)
	{
		if (!wallable[i] || map.getTerrainType(i % t.w, i / t.w) != GRASS)
			continue;
		for (int dy = -1; dy <= 1 && !stone[i]; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (margin[t.at(i % t.w + dx, i / t.w + dy)])
				{
					stone[i] = 1;
					break;
				}
	}
	return stone;
}

std::vector<unsigned char> labelBorders(const Torus &t, const std::vector<int> &labels)
{
	const int n = t.w * t.h;
	std::vector<unsigned char> wall(n, 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			if (labels[i] < 0)
				continue;
			for (int dy = -1; dy <= 1 && !wall[i]; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int other = labels[t.at(x + dx, y + dy)];
					if (other >= 0 && other < labels[i])
					{
						wall[i] = 1;
						break;
					}
				}
		}
	return wall;
}

std::vector<unsigned char> labelBorders(const Torus &t, const std::vector<int> &labels,
										int thickness)
{
	std::vector<unsigned char> wall = labelBorders(t, labels);
	for (int pass = 1; pass < std::clamp(thickness, 1, 3); ++pass)
	{
		std::vector<unsigned char> thicker = wall;
		for (int y = 0; y < t.h; ++y)
			for (int x = 0; x < t.w; ++x)
			{
				const int i = y * t.w + x;
				if (labels[i] < 0 || wall[i])
					continue;
				for (int dy = -1; dy <= 1 && !thicker[i]; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int j = t.at(x + dx, y + dy);
						// The second pass walls the other region's side of the border; the third
						// thickens either side.
						if (wall[j] && labels[j] >= 0 && (pass > 1 || labels[j] != labels[i]))
						{
							thicker[i] = 1;
							break;
						}
					}
			}
		wall.swap(thicker);
	}
	return wall;
}

DesignedStone designedStone(const Map &map, const Torus &t, const std::vector<unsigned char> &wall)
{
	const int n = t.w * t.h;
	DesignedStone result;
	result.stone.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		if (!wall[i])
			continue;
		if (map.getTerrainType(i % t.w, i / t.w) == GRASS)
			result.stone[i] = 1;
		else if (result.gaps++ == 0)
			result.firstGap = i;
	}
	return result;
}

std::vector<int> reachesWithShut(const Map &map, const Torus &t,
								 const std::vector<unsigned char> &from,
								 const std::vector<unsigned char> &shut)
{
	std::vector<unsigned char> open = walkableTiles(map);
	for (size_t i = 0; i < open.size(); ++i)
		if (shut[i])
			open[i] = 0;
	return stepsFrom(t, from, open);
}

std::array<int, 2> colonyLeak(const Map &map, const Torus &t, int teams,
							  const std::vector<unsigned char> &shut)
{
	const int n = t.w * t.h;
	std::vector<unsigned char> open(n, 0);
	for (int i = 0; i < n; ++i)
		open[i] = !shut[i] && !map.isWater(i % t.w, i / t.w) &&
				  !(map.isResource(i % t.w, i / t.w) && map.getResource(i % t.w, i / t.w).type == STONE);
	const std::vector<std::vector<int>> units = unitTilesByTeam(map, teams);
	for (int k = 0; k < teams; ++k)
	{
		const std::vector<int> steps = stepsFrom(t, tileMask(t, units[k]), open);
		for (int other = 0; other < teams; ++other)
			if (other != k)
				for (int tile : units[other])
					if (steps[tile] >= 0)
						return {k, other};
	}
	return {-1, -1};
}

int pieceLeak(const Map &map, const Torus &t, const std::vector<int> &piece,
			  const std::vector<unsigned char> &shut)
{
	const int n = t.w * t.h;
	// Crops, fruit and buildings go in time, so only water and stone part two pieces for good.
	std::vector<unsigned char> open(n, 0);
	for (int i = 0; i < n; ++i)
		open[i] =
			!shut[i] && !map.isWater(i % t.w, i / t.w) &&
			!(map.isResource(i % t.w, i / t.w) && map.getResource(i % t.w, i / t.w).type == STONE);
	int pieces = 0;
	for (int p : piece)
		pieces = std::max(pieces, p + 1);
	for (int p = 0; p < pieces; ++p)
	{
		std::vector<unsigned char> from(n, 0);
		for (int i = 0; i < n; ++i)
			from[i] = piece[i] == p && open[i];
		const std::vector<int> reach = stepsFrom(t, from, open);
		for (int i = 0; i < n; ++i)
			if (reach[i] >= 0 && piece[i] >= 0 && piece[i] != p)
				return i;
	}
	return -1;
}

GatePartitionCheck checkGatePartition(const Torus &t, const std::vector<unsigned char> &passable,
									  const std::vector<int> &labels,
									  const std::vector<TileGate> &gates)
{
	if (passable.size() != size_t(t.size()) || labels.size() != passable.size())
		throw std::invalid_argument("Gate partition grids do not match the torus");
	GatePartitionCheck result;
	auto sealed = passable;
	for (size_t k = 0; k < gates.size(); ++k)
	{
		const auto &gate = gates[k];
		// Reject malformed gates before sealing; an empty plug cannot close its
		// crossing, which would otherwise obscure the useful gate diagnostic.
		if (gate.tiles.empty() || gate.regions[0] < 0 || gate.regions[1] < 0 ||
			gate.regions[0] == gate.regions[1])
		{
			result.badGate = int(k);
			return result;
		}
		for (int tile : gate.tiles)
		{
			if (tile < 0 || tile >= t.size())
				throw std::invalid_argument("Gate tile outside the torus");
			sealed[tile] = 0;
		}
	}
	const auto regions = connectedRegions(sealed, t.w, t.h, true, GridNeighbors::Eight);
	const auto ownership = labelComponents(regions, labels);
	result.leakTile = ownership.conflictTile;
	if (result.leakTile >= 0)
		return result;
	// Reuse one scratch mask. A successful flood consumes every marked plug tile;
	// a disconnected plug fails immediately, so its leftover marks never affect
	// another gate. The work scales with gate area instead of a map flood per gate.
	std::vector<unsigned char> pending(t.size(), 0);
	for (size_t k = 0; k < gates.size(); ++k)
	{
		const auto &gate = gates[k];
		for (int tile : gate.tiles)
			pending[tile] = 1;
		std::vector<int> queue{gate.tiles.front()};
		pending[queue.front()] = 0;
		bool sides[2] = {false, false};
		bool extraSide = false;
		for (size_t head = 0; head < queue.size(); ++head)
		{
			const int tile = queue[head];
			for (int oy = -1; oy <= 1; ++oy)
				for (int ox = -1; ox <= 1; ++ox)
				{
					const int neighbour = t.at(tile % t.w + ox, tile / t.w + oy);
					if (pending[neighbour])
					{
						pending[neighbour] = 0;
						queue.push_back(neighbour);
					}
					if (sealed[neighbour])
					{
						const int owner = ownership.owners[regions[neighbour]];
						for (int side = 0; side < 2; ++side)
							sides[side] = sides[side] || owner == gate.regions[side];
						extraSide = extraSide || (owner >= 0 && owner != gate.regions[0] &&
												  owner != gate.regions[1]);
					}
				}
		}
		if (queue.size() != gate.tiles.size() || !sides[0] || !sides[1] || extraSide)
		{
			result.badGate = int(k);
			return result;
		}
	}
	return result;
}

int towerReach(const Torus &t, const std::vector<unsigned char> &buildable,
			   const std::vector<unsigned char> &target)
{
	// With every tile open, the eight-connected flood counts Chebyshev steps. A tower scans rings round
	// its whole 2x2 footprint (BuildingUtils::turretScanTile), so its reach is the nearest of the four.
	const std::vector<int> distance = stepsFrom(t, target);
	int best = INT_MAX;
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			bool fits = true;
			int nearest = INT_MAX;
			for (int dy = 0; dy < kTowerFootprint && fits; ++dy)
				for (int dx = 0; dx < kTowerFootprint && fits; ++dx)
				{
					const int i = t.at(x + dx, y + dy);
					fits = buildable[i];
					if (distance[i] >= 0)
						nearest = std::min(nearest, distance[i]);
				}
			if (fits)
				best = std::min(best, nearest);
		}
	return best;
}

WalkSpread walkSpread(const Map &map, const Torus &t, const std::vector<std::vector<int>> &workers,
					  const std::vector<int> &targets)
{
	WalkSpread spread;
	const std::vector<unsigned char> open = walkableTiles(map);
	for (size_t team = 0; team < workers.size(); ++team)
	{
		const std::vector<int> steps = stepsFrom(t, tileMask(t, workers[team]), open);
		const int walk = steps[targets[team]];
		spread.steps.push_back(walk);
		if (walk < 0)
		{
			if (spread.unreached < 0)
				spread.unreached = int(team);
			continue;
		}
		spread.shortest = std::min(spread.shortest, walk);
		spread.longest = std::max(spread.longest, walk);
	}
	return spread;
}
std::vector<unsigned char> islandSeaMargin(const Map &map, const Torus &t,
										   const std::vector<unsigned char> &ponds,
										   const std::vector<unsigned char> &roadTile,
										   const std::vector<unsigned char> &otherSand)
{
	std::vector<unsigned char> notBeach = roadTile;
	const std::vector<unsigned char> sandTiles = roadTiles(t, otherSand);
	for (int i = 0; i < t.size(); ++i)
		notBeach[i] = notBeach[i] || sandTiles[i];
	return seaMargin(map, t, seaVertices(map, t, ponds), notBeach);
}

std::vector<unsigned char> sealedIslandStone(const Map &map, const Torus &t,
											 const std::vector<unsigned char> &margin,
											 const std::vector<unsigned char> &land,
											 const std::vector<unsigned char> &designed)
{
	std::vector<unsigned char> stone = sealCoasts(map, t, margin, land);
	const DesignedStone wall = designedStone(map, t, designed);
	for (int i = 0; i < t.size(); ++i)
		if (wall.stone[i])
			stone[i] = 1;
	return stone;
}
} // namespace MapGeneration
