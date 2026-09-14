// SPDX-License-Identifier: GPL-3.0-or-later
#include "Roads.h"
#include "GenerationContext.h"
#include "Map.h"
#include <climits>
#include <deque>
namespace MapGeneration
{
std::vector<int> cheapestRoute(const Torus &t, const std::vector<int> &sources,
							   const std::vector<unsigned char> &goal,
							   const std::vector<unsigned char> &blocked,
							   const std::vector<unsigned char> &costly)
{
	const int n = t.w * t.h;
	std::vector<int> cost(n, INT_MAX), parent(n, -1);
	std::vector<unsigned char> done(n, 0);
	std::deque<int> queue;
	for (int i : sources)
	{
		cost[i] = 0;
		queue.push_back(i);
	}
	int reached = -1;
	while (!queue.empty() && reached < 0)
	{
		const int i = queue.front();
		queue.pop_front();
		if (done[i])
			continue;
		done[i] = 1;
		if (goal[i])
		{
			reached = i;
			break;
		}
		const int x = i % t.w, y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (!dx && !dy)
					continue;
				const int next = t.at(x + dx, y + dy);
				if (blocked[next])
					continue;
				const int nextCost = cost[i] + (costly[next] ? 1 : 0);
				if (nextCost < cost[next])
				{
					cost[next] = nextCost;
					parent[next] = i;
					if (nextCost == cost[i])
						queue.push_front(next);
					else
						queue.push_back(next);
				}
			}
	}
	std::vector<int> route;
	for (int i = reached; i >= 0; i = parent[i])
		route.push_back(i);
	return route;
}

bool openRoad(Map &map, const Torus &t, const std::vector<int> &sources,
			  const std::vector<unsigned char> &goal, const std::vector<unsigned char> *alsoBlocked)
{
	const int n = t.w * t.h;
	std::vector<unsigned char> blocked(n), costly(n);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		blocked[i] = map.isWater(x, y) || map.getBuilding(x, y) != NOGBID ||
					 (alsoBlocked && (*alsoBlocked)[i]);
		costly[i] = map.isResource(x, y);
	}
	const std::vector<int> route = cheapestRoute(t, sources, goal, blocked, costly);
	if (route.empty())
		return false;
	for (int i : route)
		if (map.isResource(i % t.w, i / t.w))
			map.setNoResource(i % t.w, i / t.w, 1);
	return true;
}
bool openColonyRoutes(Map &map, const GenerationContext &context, const Torus &t,
					  const StepCosts &costs)
{
	const int n = t.w * t.h, teams = context.request.nbTeams;
	if (teams < 2)
		return false;
	const auto openAt = [&](int i)
	{
		const int x = i % t.w, y = i / t.w;
		return !map.isWater(x, y) && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID;
	};
	// The open tiles round a colony's swarm.
	const auto doorstep = [&](int team)
	{
		std::vector<int> tiles;
		for (int dy = -1; dy <= 4; ++dy)
			for (int dx = -1; dx <= 4; ++dx)
				if (dy == -1 || dy == 4 || dx == -1 || dx == 4)
				{
					const int i = t.at(context.bootX[team] + dx, context.bootY[team] + dy);
					if (openAt(i))
						tiles.push_back(i);
				}
		return tiles;
	};
	bool changed = false, forded = false;
	for (int team = 1; team < teams; ++team)
	{
		std::vector<unsigned char> open(n), source(n, 0);
		for (int i = 0; i < n; ++i)
			open[i] = openAt(i);
		for (int i : doorstep(0))
			source[i] = 1;
		const std::vector<int> walk = stepsFrom(t, source, open);
		std::vector<unsigned char> target(n, 0);
		bool arrived = false;
		for (int i : doorstep(team))
		{
			target[i] = 1;
			arrived = arrived || walk[i] >= 0;
		}
		if (arrived)
			continue;
		// The cheapest way from anything colony 0 can reach to this colony's doorstep.
		std::vector<int> reachable;
		for (int i = 0; i < n; ++i)
			if (walk[i] >= 0)
				reachable.push_back(i);
		const std::vector<int> route =
			cheapestWalk(t, GridNeighbors::Cardinal, reachable, target,
						 [&](int, int to, int, int)
						 {
							 return map.getBuilding(to % t.w, to / t.w) != NOGBID
										? -1
										: stepCost(map, to % t.w, to / t.w, costs);
						 });
		for (int i : route)
		{
			const int x = i % t.w, y = i / t.w;
			if (map.isWater(x, y))
			{
				// One sand corner turns this tile and its three other tiles into walkable shore.
				map.setUMTerrain(x, y, SAND);
				for (int dy = -1; dy <= 0; ++dy)
					for (int dx = -1; dx <= 0; ++dx)
						if (map.isResource(t.x(x + dx), t.y(y + dy)))
							map.setNoResource(t.x(x + dx), t.y(y + dy), 1);
				forded = true;
			}
			else if (map.isResource(x, y))
			{
				map.setNoResource(x, y, 1);
			}
			changed = true;
		}
		if (forded)
			map.rebuildTerrain();
	}
	return changed;
}
} // namespace MapGeneration
