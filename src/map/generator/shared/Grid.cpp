#include "GenerationWork.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Grid.h"
#include "Map.h"
#include "MapStateView.h"
#include "Topology.h"
#include "Unit.h"
#include <algorithm>
namespace MapGeneration
{
Torus::Torus(const Map &map) : w(map.getW()), h(map.getH()) {}

namespace
{
// The caller's mask owns physical obstacles, including explicitly simulated
// cleared resources. Terrain geometry is always read from the authoritative map.
bool groundStep(const Map &map, const Torus &t, const std::vector<unsigned char> &open, int x,
				int y, int dx, int dy, bool canSwim)
{
	const int movement = canSwim ? 1 : 0;
	if (!map.terrainAllowsMovementAt(x + dx, y + dy, movement) ||
		!MapState::terrainConnection(map.cellView(), x, y, dx, dy, movement))
		return false;
	return !dx || !dy ||
		   (open.at(t.at(x + dx, y)) && open.at(t.at(x, y + dy)) &&
			map.terrainAllowsMovementAt(x + dx, y, movement) &&
			map.terrainAllowsMovementAt(x, y + dy, movement));
}

template <class Connection>
Reach reachImpl(const Torus &t, const std::vector<int> &sources,
				const std::vector<unsigned char> &open, int limit, Connection connection)
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
		::MapGeneration::generationCheckpoint();
		dist.at(tile) = 0;
		reach.tiles.push_back(tile);
	}
	for (size_t head = 0; head < reach.tiles.size(); ++head)
	{
		::MapGeneration::generationCheckpoint();
		const int tile = reach.tiles.at(head), here = dist.at(tile);
		if (here >= limit)
			continue;
		const int x = t.remainderX(tile), y = tile / t.w, stepped = here + 1;
		for (int dy = -1; dy <= 1; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			const int row = t.y(y + dy) * t.w;
			for (int dx = -1; dx <= 1; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				if (!dx && !dy)
					continue;
				const int n = row + t.x(x + dx);
				if (dist.at(n) < 0 && open.at(n) && connection(x, y, dx, dy))
				{
					dist.at(n) = stepped;
					reach.tiles.push_back(n);
				}
			}
		}
	}
	reach.steps.reserve(reach.tiles.size());
	for (int tile : reach.tiles)
	{
		::MapGeneration::generationCheckpoint();
		reach.steps.push_back(dist.at(tile));
		dist.at(tile) = -1;
	}
	return reach;
}

// The flood, with the "every tile is open" case folded out at compile time.
//
// The open-everywhere overload of stepsFrom used to build an all-ones mask the size of the map on
// every call and then test it once per neighbour, which is an allocation, a fill and eight loads a
// tile to answer a question whose answer is always yes. It is one of the most called things in the
// generator - clearance() is built on it, and clearance is what every passage width is measured
// over - so the branch is worth removing rather than paying. Traversal order is identical either
// way; only the test disappears.
template <bool AllOpen, class Connection>
Flood floodImpl(const Torus &t, const std::vector<unsigned char> &source,
				const std::vector<unsigned char> &open, int limit, Connection connection)
{
	Flood flood;
	std::vector<int> &dist = flood.steps, &queue = flood.visited;
	dist.assign(size_t(t.size()), -1);
	queue.reserve(dist.size());
	for (size_t i = 0; i < dist.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (source.at(i))
		{
			dist.at(i) = 0;
			queue.push_back(int(i));
		}
	}
	for (size_t head = 0; head < queue.size(); ++head)
	{
		::MapGeneration::generationCheckpoint();
		const int tile = queue.at(head), here = dist.at(tile);
		if (here >= limit)
			continue;
		const int x = t.remainderX(tile), y = tile / t.w, stepped = here + 1;
		for (int dy = -1; dy <= 1; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			// Every neighbour on this row shares one wrapped y; folding it in once here, instead
			// of inside Torus::at for each of the three dx below, is exact - at() is y() * w + x().
			const int row = t.y(y + dy) * t.w;
			for (int dx = -1; dx <= 1; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				// (0, 0) is `tile` itself, already visited (dist[tile] = here >= 0), so it always
				// fails the dist[n] < 0 test below; skipping it changes no result.
				if (!dx && !dy)
					continue;
				const int n = row + t.x(x + dx);
				if (dist.at(n) < 0 && (AllOpen || open.at(n)) && connection(x, y, dx, dy))
				{
					dist.at(n) = stepped;
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
	return floodImpl<false>(t, source, open, limit, [](int, int, int, int) { return true; });
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
	return floodImpl<true>(t, source, none, INT_MAX, [](int, int, int, int) { return true; }).steps;
}

Reach reachFrom(const Torus &t, const std::vector<int> &sources,
				const std::vector<unsigned char> &open, int limit)
{
	return reachImpl(t, sources, open, limit, [](int, int, int, int) { return true; });
}
Flood floodFrom(const Map &map, const std::vector<unsigned char> &source,
				const std::vector<unsigned char> &open, int limit, bool canSwim)
{
	const Torus t(map);
	return floodImpl<false>(t, source, open, limit, [&](int x, int y, int dx, int dy)
							{ return groundStep(map, t, open, x, y, dx, dy, canSwim); });
}
Reach reachFrom(const Map &map, const std::vector<int> &sources,
				const std::vector<unsigned char> &open, int limit, bool canSwim)
{
	const Torus t(map);
	return reachImpl(t, sources, open, limit, [&](int x, int y, int dx, int dy)
					 { return groundStep(map, t, open, x, y, dx, dy, canSwim); });
}
std::vector<int> stepsFrom(const Map &map, const std::vector<unsigned char> &source,
						   const std::vector<unsigned char> &open, bool canSwim)
{
	return floodFrom(map, source, open, INT_MAX, canSwim).steps;
}

bool groundConnection(const Map &map, const std::vector<unsigned char> &open, int x, int y, int dx,
					  int dy, bool canSwim)
{
	const Torus t(map);
	return open.at(t.at(x + dx, y + dy)) && groundStep(map, t, open, x, y, dx, dy, canSwim);
}
std::vector<int> groundRegions(const Map &map, const std::vector<unsigned char> &open,
							   GridNeighbors neighbors, bool canSwim)
{
	const Torus t(map);
	std::vector<int> labels(t.size(), -1), pending;
	int label = 0;
	for (int origin = 0; origin < t.size(); ++origin)
	{
		generationCheckpoint();
		if (!open.at(origin) || labels[origin] >= 0 ||
			!map.terrainAllowsMovementAt(t.remainderX(origin), origin / t.w, canSwim ? 1 : 0))
			continue;
		labels[origin] = label;
		pending.push_back(origin);
		while (!pending.empty())
		{
			generationCheckpoint();
			const int tile = pending.back();
			pending.pop_back();
			const int x = t.remainderX(tile), y = tile / t.w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if ((!dx && !dy) || (neighbors == GridNeighbors::Cardinal && dx && dy))
						continue;
					const int next = t.at(x + dx, y + dy);
					if (labels[next] < 0 && groundConnection(map, open, x, y, dx, dy, canSwim))
					{
						labels[next] = label;
						pending.push_back(next);
					}
				}
		}
		++label;
	}
	return labels;
}

std::vector<unsigned char> tileMask(const Torus &t, const std::vector<int> &tiles)
{
	std::vector<unsigned char> mask(size_t(t.size()), 0);
	for (int i : tiles)
	{
		::MapGeneration::generationCheckpoint();
		mask.at(i) = 1;
	}
	return mask;
}

std::vector<std::vector<int>> unitTilesByTeam(const Map &map, int teams)
{
	std::vector<std::vector<int>> units(size_t(std::max(teams, 0)));
	for (int y = 0; y < map.getH(); ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < map.getW(); ++x)
		{
			::MapGeneration::generationCheckpoint();
			const Uint16 gid = map.getGroundUnit(x, y);
			if (gid == NOGUID)
				continue;
			const int team = Unit::GIDtoTeam(gid);
			if (team >= 0 && team < teams)
				units.at(team).push_back(y * map.getW() + x);
		}
	}
	return units;
}

std::vector<unsigned char> walkableTiles(const Map &map)
{
	return groundUnitTiles(map);
}

std::vector<unsigned char> groundUnitTiles(const Map &map)
{
	return groundUnitTiles(map, false);
}
std::vector<unsigned char> groundUnitTiles(const Map &map, bool canSwim)
{
	std::vector<unsigned char> hard(size_t(map.getW()) * map.getH(), 0);
	for (int y = 0; y < map.getH(); ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < map.getW(); ++x)
		{
			::MapGeneration::generationCheckpoint();
			hard.at(size_t(y) * map.getW() + x) = map.isHardSpaceForGroundUnit(x, y, canSwim, 0);
		}
	}
	return hard;
}

int firstColonyCutOff(const std::vector<int> &steps, const std::vector<std::vector<int>> &units,
					  int first)
{
	for (size_t team = size_t(std::max(first, 0)); team < units.size(); ++team)
	{
		::MapGeneration::generationCheckpoint();
		bool arrived = false;
		for (int i : units.at(team))
		{
			::MapGeneration::generationCheckpoint();
			arrived = arrived || steps.at(i) >= 0;
		}
		if (!arrived)
			return int(team);
	}
	return -1;
}
} // namespace MapGeneration
