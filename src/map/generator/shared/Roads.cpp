#include "GenerationWork.h"
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
		{
			::MapGeneration::generationCheckpoint();
			if (cost < 1 ||
				cost > INT_MAX / t.size() / (neighbours == GridNeighbors::Eight ? 14 : 1))
				return {};
		}
	}
	for (int p : sources)
	{
		::MapGeneration::generationCheckpoint();
		if (p < 0 || p >= t.size())
			return {};
	}
	std::vector<unsigned char> water(sketch.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		water.at(i) = sketch.at(i) == WATER;
	}
	// A route tile writes all four corners. Protect one additional tile around
	// protected terrain, then allow for the requested route dilation as well.
	auto blocked = dilate(t, protectedTiles, radius + 1);
	const auto wet = dilate(t, roadTiles(t, water), radius);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		// Existing passages are traversed, not repainted: their neighbours need
		// no conversion margin. Never turn permission beside protected terrain
		// into permission to occupy that protected terrain itself.
		blocked.at(i) = protectedTiles.at(i) || ((blocked.at(i) || wet.at(i)) &&
												 !(existingPassage && (*existingPassage).at(i)));
	}
	std::vector<int> valid;
	for (int p : sources)
	{
		::MapGeneration::generationCheckpoint();
		if (!blocked.at(p))
			valid.push_back(p);
	}
	const auto path = cheapestWalk(t, neighbours, valid, goal,
								   [&](int, int to, int dx, int dy)
								   {
									   if (blocked.at(to))
										   return -1;
									   const int length = neighbours == GridNeighbors::Eight
															  ? (dx && dy ? 14 : 10)
															  : 1;
									   return length * (tileCosts ? (*tileCosts).at(to) : 1);
								   });
	if (path.empty())
		return {};
	auto paint = tileMask(t, path);
	if (existingPassage)
		for (int i = 0; i < t.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			if ((*existingPassage).at(i))
				paint.at(i) = 0;
		}
	const auto corners = tileCorners(t, dilate(t, paint, radius));
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (corners.at(i))
			sketch.at(i) = SAND;
	}
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
		::MapGeneration::generationCheckpoint();
		cost.at(i) = 0;
		queue.push_back(i);
	}
	int reached = -1;
	while (!queue.empty() && reached < 0)
	{
		::MapGeneration::generationCheckpoint();
		const int i = queue.front();
		queue.pop_front();
		if (done.at(i))
			continue;
		done.at(i) = 1;
		if (goal.at(i))
		{
			reached = i;
			break;
		}
		const int x = t.remainderX(i), y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -1; dx <= 1; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				if (!dx && !dy)
					continue;
				const int next = t.at(x + dx, y + dy);
				if (blocked.at(next))
					continue;
				const int nextCost = cost.at(i) + (costly.at(next) ? 1 : 0);
				if (nextCost < cost.at(next))
				{
					cost.at(next) = nextCost;
					parent.at(next) = i;
					if (nextCost == cost.at(i))
						queue.push_front(next);
					else
						queue.push_back(next);
				}
			}
		}
	}
	std::vector<int> route;
	for (int i = reached; i >= 0; i = parent.at(i))
	{
		::MapGeneration::generationCheckpoint();
		route.push_back(i);
	}
	return route;
}

bool openRoad(Map &map, const Torus &t, const std::vector<int> &sources,
			  const std::vector<unsigned char> &goal, const std::vector<unsigned char> *alsoBlocked)
{
	const int n = t.w * t.h;
	std::vector<unsigned char> blocked(n), costly(n);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		const int x = t.remainderX(i), y = i / t.w;
		blocked.at(i) = !map.terrainPropertiesAt(x, y).walkable ||
						map.getBuilding(x, y) != NOGBID || (alsoBlocked && (*alsoBlocked).at(i));
		costly.at(i) = map.isResource(x, y);
	}
	const std::vector<int> route = cheapestRoute(t, sources, goal, blocked, costly);
	if (route.empty())
		return false;
	for (int i : route)
	{
		::MapGeneration::generationCheckpoint();
		if (map.isResource(t.remainderX(i), i / t.w))
			map.setNoResource(t.remainderX(i), i / t.w, 1);
	}
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
		::MapGeneration::generationCheckpoint();
		const std::vector<int> reach = stepsFrom(t, tileMask(t, workers.at(0)), walkableTiles(map));
		bool connected = false;
		for (int p : workers.at(team))
		{
			::MapGeneration::generationCheckpoint();
			connected = connected || reach.at(p) >= 0;
		}
		if (connected)
			continue;
		if (!openRoad(map, t, workers.at(0), tileMask(t, workers.at(team)), alsoBlocked))
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
	{
		::MapGeneration::generationCheckpoint();
		for (int dy = -radius; dy <= radius; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -radius; dx <= radius; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				const int j = t.at(t.remainderX(i) + dx, i / t.w + dy);
				if ((keep && (*keep).at(j)) || !map.isResource(t.remainderX(j), j / t.w))
					continue;
				map.setNoResource(t.remainderX(j), j / t.w, 1);
				++cleared;
			}
		}
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
		const int x = t.remainderX(i), y = i / t.w;
		return map.terrainPropertiesAt(x, y).walkable && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID;
	};
	// The open tiles round a colony's swarm.
	const auto doorstep = [&](int team)
	{
		std::vector<int> tiles;
		for (int dy = -1; dy <= 4; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -1; dx <= 4; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				if (dy == -1 || dy == 4 || dx == -1 || dx == 4)
				{
					const int i = t.at(context.bootX[team] + dx, context.bootY[team] + dy);
					if (openAt(i))
						tiles.push_back(i);
				}
			}
		}
		return tiles;
	};
	bool changed = false;
	for (int team = 1; team < teams; ++team)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<unsigned char> open(n), source(n, 0);
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			open.at(i) = openAt(i);
		}
		for (int i : doorstep(0))
		{
			::MapGeneration::generationCheckpoint();
			source.at(i) = 1;
		}
		const std::vector<int> walk = stepsFrom(t, source, open);
		std::vector<unsigned char> target(n, 0);
		bool arrived = false;
		for (int i : doorstep(team))
		{
			::MapGeneration::generationCheckpoint();
			target.at(i) = 1;
			arrived = arrived || walk.at(i) >= 0;
		}
		if (arrived)
			continue;
		// The cheapest way from anything colony 0 can reach to this colony's doorstep.
		std::vector<int> reachable;
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (walk.at(i) >= 0)
				reachable.push_back(i);
		}
		const std::vector<int> route = cheapestWalk(
			t, GridNeighbors::Cardinal, reachable, target,
			[&](int, int to, int, int)
			{
				if ((!map.terrainPropertiesAt(to).walkable && map.terrainTypeAt(to) != WATER) ||
					map.getBuilding(t.remainderX(to), to / t.w) != NOGBID || (keep && (*keep).at(to)))
					return -1;
				return stepCost(map, t.remainderX(to), to / t.w, costs);
			});
		if (radius > 0)
			clearRoute(map, t, route, radius, keep);
		// Fords are written once the whole route is walked, so every tile on it is judged by the
		// terrain the route was planned on.
		std::vector<std::pair<int, int>> fords;
		for (int i : route)
		{
			::MapGeneration::generationCheckpoint();
			const int x = t.remainderX(i), y = i / t.w;
			if (map.isWater(x, y))
			{
				// One sand corner turns this tile and its three other tiles into walkable shore.
				fords.push_back({x, y});
				for (int dy = -1; dy <= 0; ++dy)
				{
					::MapGeneration::generationCheckpoint();
					for (int dx = -1; dx <= 0; ++dx)
					{
						::MapGeneration::generationCheckpoint();
						if (map.isResource(t.x(x + dx), t.y(y + dy)))
							map.setNoResource(t.x(x + dx), t.y(y + dy), 1);
					}
				}
			}
			else if (map.isResource(x, y))
			{
				map.setNoResource(x, y, 1);
			}
			changed = true;
		}
		if (!fords.empty())
			map.paintVertices(fords, SAND, false);
	}
	return changed;
}

bool openTrail(Map &map, const Torus &t, const std::vector<int> &sources,
			   const std::vector<unsigned char> &goal, const std::vector<unsigned char> &keep,
			   const std::vector<unsigned char> &protect, const std::vector<int> *lie, int bend,
			   int radius)
{
	const auto cost = [&](int, int to, int, int)
	{
		const int x = t.remainderX(to), y = to / t.w;
		if (!map.terrainPropertiesAt(x, y).walkable || map.getBuilding(x, y) != NOGBID ||
			keep.at(to))
			return -1;
		if (!lie)
			return map.isResource(x, y) ? 11 : 10;
		return 10 + (*lie).at(to) * bend / 65536 + (map.isResource(x, y) ? 30 : 0);
	};
	const std::vector<int> route = cheapestWalk(t, GridNeighbors::Eight, sources, goal, cost);
	if (route.empty())
		return false;
	clearRoute(map, t, route, radius, &protect);
	return true;
}
} // namespace MapGeneration
