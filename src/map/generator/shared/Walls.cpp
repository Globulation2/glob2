#include "GenerationWork.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Walls.h"
#include "Map.h"
#include "ResourceSemantics.h"
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
		::MapGeneration::generationCheckpoint();
		const int x = t.remainderX(i), y = i / t.w;
		if (!map.terrainPropertiesAt(i).walkable)
			continue;
		beach.at(i) = map.terrainPropertiesAt(i).shoreline && !notBeach.at(i);
		// A tile's corners are terrain vertices (x, y) to (x + 1, y + 1); the beach pass reaches a
		// vertex one step further, so a sea vertex anywhere in the box one wider decides the tile.
		for (int dy = -1; dy <= 2 && !margin.at(i); ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -1; dx <= 2; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				if (sea.at(t.at(x + dx, y + dy)))
				{
					margin.at(i) = 1;
					break;
				}
			}
		}
	}
	const std::vector<int> joined = stepsFrom(t, margin, beach);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (joined.at(i) >= 0)
			margin.at(i) = 1;
	}
	return margin;
}

std::vector<unsigned char> seaVertices(const Map &map, const Torus &t,
									   const std::vector<unsigned char> &lakes)
{
	std::vector<unsigned char> sea(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		sea.at(i) = map.vertexTerrainAt(t.remainderX(i), i / t.w) == WATER && !lakes.at(i);
	}
	return sea;
}

int seaEntry(const Map &map, const Torus &t, const std::vector<unsigned char> &margin,
			 const std::vector<unsigned char> &inside)
{
	const std::vector<unsigned char> open = walkableTiles(map);
	std::vector<unsigned char> beach(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		beach.at(i) = open.at(i) && margin.at(i);
	}
	const std::vector<int> landed = stepsFrom(t, beach, open);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (landed.at(i) >= 0 && !margin.at(i) && inside.at(i))
			return i;
	}
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
		::MapGeneration::generationCheckpoint();
		if (!wallable.at(i) || !map.terrainSupportsResourceAtByIndex(t.remainderX(i), i / t.w, STONE))
			continue;
		for (int dy = -1; dy <= 1 && !stone.at(i); ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -1; dx <= 1; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				if (margin.at(t.at(t.remainderX(i) + dx, i / t.w + dy)))
				{
					stone.at(i) = 1;
					break;
				}
			}
		}
	}
	return stone;
}

std::vector<unsigned char> labelBorders(const Torus &t, const std::vector<int> &labels)
{
	const int n = t.w * t.h;
	std::vector<unsigned char> wall(n, 0);
	for (int y = 0; y < t.h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const int i = y * t.w + x;
			if (labels.at(i) < 0)
				continue;
			for (int dy = -1; dy <= 1 && !wall.at(i); ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = -1; dx <= 1; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					const int other = labels.at(t.at(x + dx, y + dy));
					if (other >= 0 && other < labels.at(i))
					{
						wall.at(i) = 1;
						break;
					}
				}
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
		::MapGeneration::generationCheckpoint();
		std::vector<unsigned char> thicker = wall;
		for (int y = 0; y < t.h; ++y)
		{
			::MapGeneration::generationCheckpoint();
			for (int x = 0; x < t.w; ++x)
			{
				::MapGeneration::generationCheckpoint();
				const int i = y * t.w + x;
				if (labels.at(i) < 0 || wall.at(i))
					continue;
				for (int dy = -1; dy <= 1 && !thicker.at(i); ++dy)
				{
					::MapGeneration::generationCheckpoint();
					for (int dx = -1; dx <= 1; ++dx)
					{
						::MapGeneration::generationCheckpoint();
						const int j = t.at(x + dx, y + dy);
						// The second pass walls the other region's side of the border; the third
						// thickens either side.
						if (wall.at(j) && labels.at(j) >= 0 &&
							(pass > 1 || labels.at(j) != labels.at(i)))
						{
							thicker.at(i) = 1;
							break;
						}
					}
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
		::MapGeneration::generationCheckpoint();
		if (!wall.at(i))
			continue;
		if (map.terrainSupportsResourceAtByIndex(t.remainderX(i), i / t.w, STONE))
			result.stone.at(i) = 1;
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
	{
		::MapGeneration::generationCheckpoint();
		if (shut.at(i))
			open.at(i) = 0;
	}
	return stepsFrom(t, from, open);
}

std::array<int, 2> colonyLeak(const Map &map, const Torus &t, int teams,
							  const std::vector<unsigned char> &shut)
{
	const int n = t.w * t.h;
	std::vector<unsigned char> open(n, 0);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		open.at(i) =
			!shut.at(i) && map.terrainPropertiesAt(i).walkable && !permanentResourceBarrier(map, i);
	}
	const std::vector<std::vector<int>> units = unitTilesByTeam(map, teams);
	for (int k = 0; k < teams; ++k)
	{
		::MapGeneration::generationCheckpoint();
		const std::vector<int> steps = stepsFrom(t, tileMask(t, units.at(k)), open);
		for (int other = 0; other < teams; ++other)
		{
			::MapGeneration::generationCheckpoint();
			if (other != k)
				for (int tile : units.at(other))
				{
					::MapGeneration::generationCheckpoint();
					if (steps.at(tile) >= 0)
						return {k, other};
				}
		}
	}
	return {-1, -1};
}

int pieceLeak(const Map &map, const Torus &t, const std::vector<int> &piece,
			  const std::vector<unsigned char> &shut)
{
	const int n = t.w * t.h;
	// Removable deposits and buildings do not permanently separate the pieces.
	std::vector<unsigned char> open(n, 0);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		open.at(i) =
			!shut.at(i) && map.terrainPropertiesAt(i).walkable && !permanentResourceBarrier(map, i);
	}
	int pieces = 0;
	for (int p : piece)
	{
		::MapGeneration::generationCheckpoint();
		pieces = std::max(pieces, p + 1);
	}
	for (int p = 0; p < pieces; ++p)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<unsigned char> from(n, 0);
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			from.at(i) = piece.at(i) == p && open.at(i);
		}
		const std::vector<int> reach = stepsFrom(t, from, open);
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (reach.at(i) >= 0 && piece.at(i) >= 0 && piece.at(i) != p)
				return i;
		}
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
		::MapGeneration::generationCheckpoint();
		const auto &gate = gates.at(k);
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
			::MapGeneration::generationCheckpoint();
			if (tile < 0 || tile >= t.size())
				throw std::invalid_argument("Gate tile outside the torus");
			sealed.at(tile) = 0;
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
		::MapGeneration::generationCheckpoint();
		const auto &gate = gates.at(k);
		for (int tile : gate.tiles)
		{
			::MapGeneration::generationCheckpoint();
			pending.at(tile) = 1;
		}
		std::vector<int> queue{gate.tiles.front()};
		pending.at(queue.front()) = 0;
		bool sides[2] = {false, false};
		bool extraSide = false;
		for (size_t head = 0; head < queue.size(); ++head)
		{
			::MapGeneration::generationCheckpoint();
			const int tile = queue.at(head);
			for (int oy = -1; oy <= 1; ++oy)
			{
				::MapGeneration::generationCheckpoint();
				for (int ox = -1; ox <= 1; ++ox)
				{
					::MapGeneration::generationCheckpoint();
					const int neighbour = t.at(t.remainderX(tile) + ox, tile / t.w + oy);
					if (pending.at(neighbour))
					{
						pending.at(neighbour) = 0;
						queue.push_back(neighbour);
					}
					if (sealed.at(neighbour))
					{
						const int owner = ownership.owners.at(regions.at(neighbour));
						for (int side = 0; side < 2; ++side)
						{
							::MapGeneration::generationCheckpoint();
							sides[side] = sides[side] || owner == gate.regions[side];
						}
						extraSide = extraSide || (owner >= 0 && owner != gate.regions[0] &&
												  owner != gate.regions[1]);
					}
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
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			bool fits = true;
			int nearest = INT_MAX;
			for (int dy = 0; dy < kTowerFootprint && fits; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = 0; dx < kTowerFootprint && fits; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					const int i = t.at(x + dx, y + dy);
					fits = buildable.at(i);
					if (distance.at(i) >= 0)
						nearest = std::min(nearest, distance.at(i));
				}
			}
			if (fits)
				best = std::min(best, nearest);
		}
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
		::MapGeneration::generationCheckpoint();
		const std::vector<int> steps = stepsFrom(t, tileMask(t, workers.at(team)), open);
		const int walk = steps.at(targets.at(team));
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
	{
		::MapGeneration::generationCheckpoint();
		notBeach.at(i) = notBeach.at(i) || sandTiles.at(i);
	}
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
	{
		::MapGeneration::generationCheckpoint();
		if (wall.stone.at(i))
			stone.at(i) = 1;
	}
	return stone;
}
std::string wallStanding(const Map &map, const Torus &t, const std::vector<unsigned char> &wall,
						 const std::vector<unsigned char> &doors, const char *what)
{
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		const int x = t.remainderX(i), y = i / t.w;
		const bool stone = permanentResourceBarrier(map, i);
		if (wall.at(i) && !stone)
			return std::string("A ") + what + " has lost its permanent barrier at (" + std::to_string(x) + ", " +
				   std::to_string(y) + ").";
		if (doors.at(i) && stone)
			return std::string("A ") + what + "'s gate is walled up at (" + std::to_string(x) +
				   ", " + std::to_string(y) + ").";
	}
	return "";
}
} // namespace MapGeneration
