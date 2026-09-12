// SPDX-License-Identifier: GPL-3.0-or-later
#include "Roads.h"
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
} // namespace MapGeneration
