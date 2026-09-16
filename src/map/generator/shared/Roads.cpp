// SPDX-License-Identifier: GPL-3.0-or-later
#include "Roads.h"
#include "GenerationContext.h"
#include "Map.h"
#include "Morphology.h"
#include <climits>
#include <deque>
#include <string>
namespace MapGeneration
{
std::vector<int> reserveSandRoute(TerrainSketch &sketch, const Torus &t,
								  const std::vector<int> &sources,
								  const std::vector<unsigned char> &goal,
								  const std::vector<unsigned char> &protectedTiles, int radius,
								  const std::vector<int> *tileCosts, GridNeighbors neighbours,
								  const std::vector<unsigned char> *existingPassage)
{
	if (sketch.size() != size_t(t.size()) || goal.size() != sketch.size() ||
		protectedTiles.size() != sketch.size() || radius < 0 || radius >= std::min(t.w, t.h) / 2)
		return {};
	if (existingPassage && (radius != 0 || existingPassage->size() != sketch.size()))
		return {};
	if (tileCosts)
	{
		if (tileCosts->size() != sketch.size())
			return {};
		for (int cost : *tileCosts)
			if (cost < 1 ||
				cost > INT_MAX / t.size() / (neighbours == GridNeighbors::Eight ? 14 : 1))
				return {};
	}
	for (int p : sources)
		if (p < 0 || p >= t.size())
			return {};
	std::vector<unsigned char> water(sketch.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		water[i] = sketch[i] == WATER;
	// A route tile writes all four corners. Protect one additional tile around
	// protected terrain, then allow for the requested route dilation as well.
	auto blocked = dilate(t, protectedTiles, radius + 1);
	const auto wet = dilate(t, roadTiles(t, water), radius);
	for (int i = 0; i < t.size(); ++i)
	{
		// Existing passages are traversed, not repainted: their neighbours need
		// no conversion margin. Never turn permission beside protected terrain
		// into permission to occupy that protected terrain itself.
		blocked[i] = protectedTiles[i] ||
					 ((blocked[i] || wet[i]) && !(existingPassage && (*existingPassage)[i]));
	}
	std::vector<int> valid;
	for (int p : sources)
		if (!blocked[p])
			valid.push_back(p);
	const auto path = cheapestWalk(t, neighbours, valid, goal,
								   [&](int, int to, int dx, int dy)
								   {
									   if (blocked[to])
										   return -1;
									   const int length = neighbours == GridNeighbors::Eight
															  ? (dx && dy ? 14 : 10)
															  : 1;
									   return length * (tileCosts ? (*tileCosts)[to] : 1);
								   });
	if (path.empty())
		return {};
	auto paint = tileMask(t, path);
	if (existingPassage)
		for (int i = 0; i < t.size(); ++i)
			if ((*existingPassage)[i])
				paint[i] = 0;
	const auto corners = tileCorners(t, dilate(t, paint, radius));
	for (int i = 0; i < t.size(); ++i)
		if (corners[i])
			sketch[i] = SAND;
	return path;
}

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
int connectColonies(Map &map, int teams, const std::vector<unsigned char> *alsoBlocked,
					std::string &detail)
{
	const Torus t(map);
	const auto workers = unitTilesByTeam(map, teams);
	int opened = 0;
	for (int team = 1; team < teams; ++team)
	{
		const std::vector<int> reach = stepsFrom(t, tileMask(t, workers[0]), walkableTiles(map));
		bool connected = false;
		for (int p : workers[team])
			connected = connected || reach[p] >= 0;
		if (connected)
			continue;
		if (!openRoad(map, t, workers[0], tileMask(t, workers[team]), alsoBlocked))
		{
			detail = "colony " + std::to_string(team) + " has no land route to colony 0";
			return -1;
		}
		++opened;
	}
	return opened;
}
int clearRoute(Map &map, const Torus &t, const std::vector<int> &route, int radius,
			   const std::vector<unsigned char> *keep)
{
	int cleared = 0;
	for (int i : route)
		for (int dy = -radius; dy <= radius; ++dy)
			for (int dx = -radius; dx <= radius; ++dx)
			{
				const int j = t.at(i % t.w + dx, i / t.w + dy);
				if ((keep && (*keep)[j]) || !map.isResource(j % t.w, j / t.w))
					continue;
				map.setNoResource(j % t.w, j / t.w, 1);
				++cleared;
			}
	return cleared;
}

bool openColonyRoutes(Map &map, const GenerationContext &context, const Torus &t,
					  const StepCosts &costs, int radius, const std::vector<unsigned char> *keep)
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
							 if (map.getBuilding(to % t.w, to / t.w) != NOGBID ||
								 (keep && (*keep)[to]))
								 return -1;
							 return stepCost(map, to % t.w, to / t.w, costs);
						 });
		if (radius > 0)
			clearRoute(map, t, route, radius, keep);
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
