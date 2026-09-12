// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "Topology.h"
#include <climits>
#include <functional>
#include <queue>
#include <utility>
#include <vector>
class Map;
namespace MapGeneration
{
/// The cheapest walk on the torus by any step cost: from any source tile (at cost 0) to any
/// goal tile, where `stepCost(from, to, dx, dy)` is the cost of stepping from one tile onto its
/// neighbour, or -1 when that step cannot be taken. Returns the walk from the goal reached back
/// to its source, or nothing. Dijkstra, so ties go to the tile queued first.
template <typename StepCost>
std::vector<int> cheapestWalk(const Torus &t, GridNeighbors neighbours,
							  const std::vector<int> &sources,
							  const std::vector<unsigned char> &goal, StepCost stepCost)
{
	const int n = t.w * t.h;
	std::vector<int> cost(n, INT_MAX), from(n, -1);
	using Entry = std::pair<int, int>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
	for (int i : sources)
	{
		cost[i] = 0;
		heap.push({0, i});
	}
	int reached = -1;
	while (!heap.empty() && reached < 0)
	{
		const auto [c, i] = heap.top();
		heap.pop();
		if (c > cost[i])
			continue;
		if (goal[i])
		{
			reached = i;
			break;
		}
		const int x = i % t.w, y = i / t.w;
		const auto consider = [&](int dx, int dy)
		{
			const int m = t.at(x + dx, y + dy);
			const int step = stepCost(i, m, dx, dy);
			if (step < 0 || c + step >= cost[m])
				return;
			cost[m] = c + step;
			from[m] = i;
			heap.push({cost[m], m});
		};
		if (neighbours == GridNeighbors::Cardinal)
			for (const auto &step : kCardinalSteps)
				consider(step[0], step[1]);
		else
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					if (dx || dy)
						consider(dx, dy);
	}
	std::vector<int> route;
	for (int i = reached; i >= 0; i = from[i])
		route.push_back(i);
	return route;
}

/// The cheapest walk from any source tile to any goal tile, eight-connected on the torus:
/// stepping onto a `costly` tile costs one, any other open tile nothing, and `blocked` tiles
/// are impassable. Returns the tiles of the walk from the goal it reached back to its source, or
/// nothing when no goal can be reached. A 0-1 breadth-first search, so ties go to the walk found
/// first in neighbour order.
std::vector<int> cheapestRoute(const Torus &, const std::vector<int> &sources,
							   const std::vector<unsigned char> &goal,
							   const std::vector<unsigned char> &blocked,
							   const std::vector<unsigned char> &costly);

/// Deposits may land anywhere, and a band of them could close a colony off from where it must
/// be able to walk. This keeps one way open: the cheapest walk from the sources to the goal
/// (deposits cost one, open ground nothing; water, buildings and `alsoBlocked` are impassable),
/// with only the deposits on it cleared. Almost always nothing is in the way and nothing is
/// cleared. False when no walk exists at all.
bool openRoad(Map &, const Torus &, const std::vector<int> &sources,
			  const std::vector<unsigned char> &goal,
			  const std::vector<unsigned char> *alsoBlocked = nullptr);
} // namespace MapGeneration
