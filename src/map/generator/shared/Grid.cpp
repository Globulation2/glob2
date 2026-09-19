// SPDX-License-Identifier: GPL-3.0-or-later
#include "Grid.h"
#include "Map.h"
#include "Unit.h"
#include <algorithm>
namespace MapGeneration
{
Torus::Torus(const Map &map) : w(map.getW()), h(map.getH()) {}

Reach reachFrom(const Torus &t, const std::vector<int> &sources, const std::vector<unsigned char> &open,
				int limit)
{
	// The step field lives on between calls, all -1; only the tiles a flood reached are put back.
	thread_local std::vector<int> dist;
	if (dist.size() != size_t(t.size()))
		dist.assign(size_t(t.size()), -1);
	Reach reach;
	// floodFrom queues its sources in tile order, once each.
	std::vector<int> ordered = sources;
	std::sort(ordered.begin(), ordered.end());
	ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
	for (int tile : ordered)
	{
		dist[tile] = 0;
		reach.tiles.push_back(tile);
	}
	for (size_t head = 0; head < reach.tiles.size(); ++head)
	{
		const int tile = reach.tiles[head], here = dist[tile];
		if (here >= limit)
			continue;
		const int x = tile % t.w, y = tile / t.w, stepped = here + 1;
		for (int dy = -1; dy <= 1; ++dy)
		{
			const int row = t.y(y + dy) * t.w;
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (!dx && !dy)
					continue;
				const int n = row + t.x(x + dx);
				if (dist[n] < 0 && open[n])
				{
					dist[n] = stepped;
					reach.tiles.push_back(n);
				}
			}
		}
	}
	reach.steps.reserve(reach.tiles.size());
	for (int tile : reach.tiles)
	{
		reach.steps.push_back(dist[tile]);
		dist[tile] = -1;
	}
	return reach;
}

namespace
{
// The flood, with the "every tile is open" case folded out at compile time.
//
// The open-everywhere overload of stepsFrom used to build an all-ones mask the size of the map on
// every call and then test it once per neighbour, which is an allocation, a fill and eight loads a
// tile to answer a question whose answer is always yes. It is one of the most called things in the
// generator - clearance() is built on it, and clearance is what every passage width is measured
// over - so the branch is worth removing rather than paying. Traversal order is identical either
// way; only the test disappears.
template <bool AllOpen>
Flood floodImpl(const Torus &t, const std::vector<unsigned char> &source,
				const std::vector<unsigned char> &open, int limit)
{
	Flood flood;
	std::vector<int> &dist = flood.steps, &queue = flood.visited;
	dist.assign(size_t(t.size()), -1);
	queue.reserve(dist.size());
	for (size_t i = 0; i < dist.size(); ++i)
		if (source[i])
		{
			dist[i] = 0;
			queue.push_back(int(i));
		}
	for (size_t head = 0; head < queue.size(); ++head)
	{
		const int tile = queue[head], here = dist[tile];
		if (here >= limit)
			continue;
		const int x = tile % t.w, y = tile / t.w, stepped = here + 1;
		for (int dy = -1; dy <= 1; ++dy)
		{
			// Every neighbour on this row shares one wrapped y; folding it in once here, instead
			// of inside Torus::at for each of the three dx below, is exact - at() is y() * w + x().
			const int row = t.y(y + dy) * t.w;
			for (int dx = -1; dx <= 1; ++dx)
			{
				// (0, 0) is `tile` itself, already visited (dist[tile] = here >= 0), so it always
				// fails the dist[n] < 0 test below; skipping it changes no result.
				if (!dx && !dy)
					continue;
				const int n = row + t.x(x + dx);
				if (dist[n] < 0 && (AllOpen || open[n]))
				{
					dist[n] = stepped;
					queue.push_back(n);
				}
			}
		}
	}
	return flood;
}
} // namespace

Flood floodFrom(const Torus &t, const std::vector<unsigned char> &source,
				const std::vector<unsigned char> &open, int limit)
{
	return floodImpl<false>(t, source, open, limit);
}

std::vector<int> stepsFrom(const Torus &t, const std::vector<unsigned char> &source,
						   const std::vector<unsigned char> &open)
{
	return floodFrom(t, source, open).steps;
}

std::vector<int> stepsFrom(const Torus &t, const std::vector<unsigned char> &source)
{
	// `open` is never read under AllOpen, so nothing is built for it.
	static const std::vector<unsigned char> none;
	return floodImpl<true>(t, source, none, INT_MAX).steps;
}

std::vector<unsigned char> tileMask(const Torus &t, const std::vector<int> &tiles)
{
	std::vector<unsigned char> mask(size_t(t.size()), 0);
	for (int i : tiles)
		mask[i] = 1;
	return mask;
}

std::vector<std::vector<int>> unitTilesByTeam(const Map &map, int teams)
{
	std::vector<std::vector<int>> units(size_t(std::max(teams, 0)));
	for (int y = 0; y < map.getH(); ++y)
		for (int x = 0; x < map.getW(); ++x)
		{
			const Uint16 gid = map.getGroundUnit(x, y);
			if (gid == NOGUID)
				continue;
			const int team = Unit::GIDtoTeam(gid);
			if (team >= 0 && team < teams)
				units[team].push_back(y * map.getW() + x);
		}
	return units;
}

std::vector<unsigned char> walkableTiles(const Map &map)
{
	std::vector<unsigned char> open(size_t(map.getW()) * map.getH(), 0);
	for (int y = 0; y < map.getH(); ++y)
		for (int x = 0; x < map.getW(); ++x)
			open[size_t(y) * map.getW() + x] =
				!map.isWater(x, y) && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID;
	return open;
}

std::vector<unsigned char> groundUnitTiles(const Map &map)
{
	return groundUnitTiles(map, false);
}
std::vector<unsigned char> groundUnitTiles(const Map &map, bool canSwim)
{
	std::vector<unsigned char> hard(size_t(map.getW()) * map.getH(), 0);
	for (int y = 0; y < map.getH(); ++y)
		for (int x = 0; x < map.getW(); ++x)
			hard[size_t(y) * map.getW() + x] = map.isHardSpaceForGroundUnit(x, y, canSwim, 0);
	return hard;
}

int firstColonyCutOff(const std::vector<int> &steps, const std::vector<std::vector<int>> &units,
					  int first)
{
	for (size_t team = size_t(std::max(first, 0)); team < units.size(); ++team)
	{
		bool arrived = false;
		for (int i : units[team])
			arrived = arrived || steps[i] >= 0;
		if (!arrived)
			return int(team);
	}
	return -1;
}
} // namespace MapGeneration
