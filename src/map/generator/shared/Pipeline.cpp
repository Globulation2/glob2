// SPDX-License-Identifier: GPL-3.0-or-later
#include "Pipeline.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationResult.h"
#include "Homes.h"
#include "Planting.h"
#include "Resources.h"
namespace MapGeneration
{
std::string startingAccessFailure(const Map &map, int teams,
								  const std::vector<ResourceAccessRule> &rules, int minimumSites,
								  int buildingRange)
{
	if (teams < 1 || teams > Team::MAX_COUNT || minimumSites < 0 || buildingRange < 0)
		throw GenerationFailure("Invalid starting-access colony or building budget");
	int range = buildingRange;
	for (const auto &rule : rules)
	{
		if (rule.type < 0 || rule.type >= MAX_NB_RESOURCES || rule.range < 1 || !rule.name)
			throw GenerationFailure("Invalid starting-access resource rule");
		range = std::max(range, rule.range);
	}
	const Torus t(map);
	const auto workers = unitTilesByTeam(map, teams);
	const auto passable = groundUnitTiles(map);
	for (int k = 0; k < teams; ++k)
	{
		const std::string colony = "Colony " + std::to_string(k) + " ";
		if (workers[k].empty())
			return colony + "has no workers to reach its supplies.";
		const auto reached = floodFrom(t, tileMask(t, workers[k]), passable, range);
		std::vector<int> distances(rules.size(), -1);
		int sites = 0;
		for (int i : reached.visited)
		{
			const int x = i % t.w, y = i / t.w, distance = reached.steps[i];
			if (distance <= buildingRange && map.isFreeForBuilding(x, y, 4, 4))
				++sites;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (!dx && !dy)
						continue;
					const auto &resource = map.getResource(t.x(x + dx), t.y(y + dy));
					if (!resource.amount)
						continue;
					for (size_t r = 0; r < rules.size(); ++r)
						if (resource.type == rules[r].type &&
							(distances[r] < 0 || distance + 1 < distances[r]))
							distances[r] = distance + 1;
				}
		}
		for (size_t r = 0; r < rules.size(); ++r)
			if (distances[r] < 0 || distances[r] > rules[r].range)
				return colony + "cannot harvest " + rules[r].name + " within " +
					   std::to_string(rules[r].range) + " steps (observed " +
					   std::to_string(distances[r]) + "; -1 means unreachable).";
		if (sites < minimumSites)
			return colony + "has only " + std::to_string(sites) +
				   " reachable 4x4 building origins; " + std::to_string(minimumSites) +
				   " required within " + std::to_string(buildingRange) + " steps.";
	}
	return "";
}

bool reopenCrampedStarts(Game &game, GenerationContext &context, const ResourceAmounts &amounts,
						 int wheatRange, int woodRange, int clearRadius,
						 const std::vector<unsigned char> *protectedWalls)
{
	context.telemetry.measure("pipeline.cramped_relief.enabled", amounts.scaled());
	if (!amounts.scaled())
		return false;
	// At least 16 free 4x4 building sites within 24 steps of every swarm: room for a first base
	// (inns, huts, a school) without clearing anything.
	openCrampedStarts(game, context, 16, 24, protectedWalls);
	guaranteeStartingResources(game, context, wheatRange, woodRange, clearRadius, protectedWalls);
	return true;
}

void secureStartingCrops(Game &game, GenerationContext &context, const Torus &t, int wheatRange,
						 int woodRange, int clearRadius, const std::vector<unsigned char> *keep,
						 const std::vector<unsigned char> *allowedTopup)
{
	clearAroundSwarms(game.map, context, t, keep);
	guaranteeStartingResources(game, context, wheatRange, woodRange, clearRadius, keep,
							   allowedTopup);
	clearAroundSwarms(game.map, context, t, keep);
}

ColonyWalk walkFromFirstColony(const Map &map, int teams, const std::string &ground,
							   const std::string &route)
{
	ColonyWalk walk;
	walk.workers = unitTilesByTeam(map, teams);
	if (teams < 1 || walk.workers[0].empty())
	{
		walk.error = "Colony 0 has no workers to walk " + ground + ".";
		return walk;
	}
	const Torus t(map);
	walk.steps = stepsFrom(t, tileMask(t, walk.workers[0]), walkableTiles(map));
	if (const int cut = firstColonyCutOff(walk.steps, walk.workers); cut >= 0)
		walk.error = "Colony " + std::to_string(cut) + " cannot walk to colony 0" +
					 (route.empty() ? "" : " " + route) + ".";
	return walk;
}

std::string CropsInReach::missing() const
{
	if (wheat && wood)
		return "";
	return std::string("cannot walk to ") + (wheat ? "wood." : "wheat.");
}

CropsInReach cropsBesideReach(const Map &map, const std::vector<int> &reach)
{
	CropsInReach crops;
	const int w = map.getW(), h = map.getH();
	for (int y = 0; y < h && !(crops.wheat && crops.wood); ++y)
		for (int x = 0; x < w; ++x)
		{
			const int type = map.getResource(x, y).type;
			if (type != WHEAT && type != WOOD)
				continue;
			bool beside = false;
			for (int dy = -1; dy <= 1 && !beside; ++dy)
				for (int dx = -1; dx <= 1 && !beside; ++dx)
					beside =
						reach[size_t(map.normalizeY(y + dy)) * w + map.normalizeX(x + dx)] >= 0;
			(type == WHEAT ? crops.wheat : crops.wood) =
				(type == WHEAT ? crops.wheat : crops.wood) || beside;
		}
	return crops;
}

std::string coloniesApart(const Map &map, int teams, const std::string &route)
{
	const std::vector<std::vector<int>> units = unitTilesByTeam(map, teams);
	const Torus t(map);
	const std::vector<unsigned char> ground = groundUnitTiles(map);
	for (int a = 0; a < teams; ++a)
	{
		if (units[a].empty())
			continue;
		const std::vector<int> steps = stepsFrom(t, tileMask(t, units[a]), ground);
		for (int b = 0; b < teams; ++b)
			if (b != a)
				for (int tile : units[b])
					if (steps[tile] >= 0)
						return "Colony " + std::to_string(a) + " can walk to colony " +
							   std::to_string(b) + (route.empty() ? "" : " " + route) + ".";
	}
	return "";
}

std::vector<unsigned char> homeGrassMask(const Map &map, const Torus &t,
										 const std::vector<int> &homeOf, int team)
{
	std::vector<unsigned char> ground(size_t(t.size()), 0);
	for (int i = 0; i < t.size(); ++i)
		ground[i] = homeOf[i] == team && map.isGrass(i % t.w, i / t.w);
	return ground;
}

bool settleRoundColonies(Game &game, GenerationContext &context, const char *stream,
						 const std::vector<int> &homeOf, const std::vector<ShapePoint> &homes,
						 double radius)
{
	const Torus t(game.map.getW(), game.map.getH());
	return settleColonies(
		game, context, stream, [&](int team) { return homeGrassMask(game.map, t, homeOf, team); },
		[&](int team) { return homeSwarmSite(homes[team], 0.0, radius); });
}

std::string homePondMissing(const Map &map, const Torus &t, const std::vector<ShapePoint> &kits,
							int teams, const char *place, const char *water)
{
	for (int k = 0; k < teams; ++k)
	{
		bool pond = false;
		for (int dy = -2; dy <= 2 && !pond; ++dy)
			for (int dx = -2; dx <= 2 && !pond; ++dx)
				pond = map.isWater(t.x(int(kits[k].x) + dx), t.y(int(kits[k].y) + dy));
		if (!pond)
			return "Colony " + std::to_string(k) + "'s " + place + " has lost its " + water + ".";
	}
	return "";
}
} // namespace MapGeneration
