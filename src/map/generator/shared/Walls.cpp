// SPDX-License-Identifier: GPL-3.0-or-later
#include "Walls.h"
#include "Map.h"
#include "TerrainType.h"
#include <algorithm>
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

std::vector<unsigned char> seaVertices(const Map &map, const Torus &t, const std::vector<unsigned char> &lakes)
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

std::vector<unsigned char> labelBorders(const Torus &t, const std::vector<int> &labels, int thickness)
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

int pieceLeak(const Map &map, const Torus &t, const std::vector<int> &piece,
			  const std::vector<unsigned char> &shut)
{
	const int n = t.w * t.h;
	std::vector<unsigned char> open = walkableTiles(map);
	for (int i = 0; i < n; ++i)
		if (shut[i])
			open[i] = 0;
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

int towerReach(const Torus &t, const std::vector<unsigned char> &buildable,
			   const std::vector<unsigned char> &target)
{
	// With every tile open, the eight-connected flood counts Chebyshev steps.
	const std::vector<int> distance = stepsFrom(t, target);
	int best = INT_MAX;
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			if (distance[i] < 0 || distance[i] >= best)
				continue;
			bool fits = true;
			for (int dy = 0; dy < kTowerFootprint && fits; ++dy)
				for (int dx = 0; dx < kTowerFootprint && fits; ++dx)
					fits = buildable[t.at(x + dx, y + dy)];
			if (fits)
				best = distance[i];
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
} // namespace MapGeneration
