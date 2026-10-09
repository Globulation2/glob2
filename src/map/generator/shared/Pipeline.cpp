#include "GenerationWork.h"
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
std::string denseColonySizeFailure(const GenerationRequest &request)
{
	if (request.nbTeams > DENSE_COLONY_BASE_LIMIT &&
		(request.wDec != DENSE_COLONY_MAP_EXPONENT || request.hDec != DENSE_COLONY_MAP_EXPONENT))
		return "Too many colonies for this map; use a bigger map or fewer colonies.";
	return {};
}

std::string startingAccessFailure(const Map &map, int teams,
								  const std::vector<MaterialAccessRule> &rules, int minimumSites,
								  int buildingRange)
{
	if (teams < 1 || teams > Team::MAX_COUNT || minimumSites < 0 || buildingRange < 0)
		throw GenerationFailure("Invalid starting-access colony or building budget");
	int range = buildingRange;
	for (const auto &rule : rules)
	{
		::MapGeneration::generationCheckpoint();
		if (!validMaterial(materialIndex(rule.material)) || rule.range < 1 || !rule.name)
			throw GenerationFailure("Invalid starting-access material rule");
		range = std::max(range, rule.range);
	}
	const Torus t(map);
	const auto workers = unitTilesByTeam(map, teams);
	const auto passable = groundUnitTiles(map);
	for (int k = 0; k < teams; ++k)
	{
		::MapGeneration::generationCheckpoint();
		const std::string colony = "Colony " + std::to_string(k) + " ";
		if (workers.at(k).empty())
			return colony + "has no workers to reach its supplies.";
		const auto reached = floodFrom(t, tileMask(t, workers.at(k)), passable, range);
		std::vector<int> distances(rules.size(), -1);
		int sites = 0;
		for (int i : reached.visited)
		{
			::MapGeneration::generationCheckpoint();
			const int x = t.remainderX(i), y = i / t.w, distance = reached.steps.at(i);
			if (distance <= buildingRange && map.isFreeForBuilding(x, y, 4, 4))
				++sites;
			for (int dy = -1; dy <= 1; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = -1; dx <= 1; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					if (!dx && !dy)
						continue;
					const auto &resource = map.getResource(t.x(x + dx), t.y(y + dy));
					if (!resource.amount)
						continue;
					for (size_t r = 0; r < rules.size(); ++r)
					{
						::MapGeneration::generationCheckpoint();
						if (map.materialAmountAt(map.coordToIndex(x + dx, y + dy),
												 rules.at(r).material) > 0 &&
							(distances.at(r) < 0 || distance + 1 < distances.at(r)))
							distances.at(r) = distance + 1;
					}
				}
			}
		}
		for (size_t r = 0; r < rules.size(); ++r)
		{
			::MapGeneration::generationCheckpoint();
			if (distances.at(r) < 0 || distances.at(r) > rules.at(r).range)
				return colony + "cannot harvest " + rules.at(r).name + " within " +
					   std::to_string(rules.at(r).range) + " steps (observed " +
					   std::to_string(distances.at(r)) + "; -1 means unreachable).";
		}
		if (sites < minimumSites)
			return colony + "has only " + std::to_string(sites) +
				   " reachable 4x4 building origins; " + std::to_string(minimumSites) +
				   " required within " + std::to_string(buildingRange) + " steps.";
	}
	return "";
}

std::string startingFloorFailure(const Map &map, int teams, int wheatRange, int woodRange)
{
	return startingAccessFailure(map, teams,
								 {{MaterialId::Food, wheatRange, "food"}, {MaterialId::Wood, woodRange, "wood"}});
}

bool reopenCrampedStarts(Game &game, GenerationContext &context, const ResourceAmounts &amounts,
						 int wheatRange, int woodRange, int clearRadius,
						 const std::vector<unsigned char> *protectedWalls)
{
	context.telemetry.measure("pipeline.cramped_relief.enabled", amounts.scaled());
	if (!amounts.scaled())
		return false;
	openStartsBuriedByResources(game, context, wheatRange, woodRange, clearRadius, protectedWalls);
	return true;
}

void openStartsBuriedByResources(Game &game, GenerationContext &context, int wheatRange,
								 int woodRange, int clearRadius,
								 const std::vector<unsigned char> *protectedWalls)
{
	// At least 16 free 4x4 building sites within 24 steps of every swarm: room for a first base
	// (inns, huts, a school) without clearing anything.
	openCrampedStarts(game, context, 16, 24, protectedWalls);
	guaranteeStartingResources(game, context, wheatRange, woodRange, clearRadius, protectedWalls);
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
	if (teams < 1 || walk.workers.at(0).empty())
	{
		walk.error = "Colony 0 has no workers to walk " + ground + ".";
		return walk;
	}
	const Torus t(map);
	walk.steps = stepsFrom(t, tileMask(t, walk.workers.at(0)), walkableTiles(map));
	if (const int cut = firstColonyCutOff(walk.steps, walk.workers); cut >= 0)
		walk.error = "Colony " + std::to_string(cut) + " cannot walk to colony 0" +
					 (route.empty() ? "" : " " + route) + ".";
	return walk;
}

std::string CropsInReach::missing() const
{
	if (food && wood)
		return "";
	return std::string("cannot walk to ") + (food ? "wood." : "food.");
}

CropsInReach cropsBesideReach(const Map &map, const std::vector<int> &reach)
{
	CropsInReach crops;
	const int w = map.getW(), h = map.getH();
	for (int y = 0; y < h && !(crops.food && crops.wood); ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const auto index = map.coordToIndex(x, y);
			const bool food = map.materialAmountAt(index, MaterialId::Food) > 0;
			const bool wood = map.materialAmountAt(index, MaterialId::Wood) > 0;
			if (!food && !wood)
				continue;
			bool beside = false;
			for (int dy = -1; dy <= 1 && !beside; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = -1; dx <= 1 && !beside; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					beside =
						reach.at(size_t(map.normalizeY(y + dy)) * w + map.normalizeX(x + dx)) >= 0;
				}
			}
			crops.food |= food && beside;
			crops.wood |= wood && beside;
		}
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
		::MapGeneration::generationCheckpoint();
		if (units.at(a).empty())
			continue;
		const std::vector<int> steps = stepsFrom(t, tileMask(t, units.at(a)), ground);
		for (int b = 0; b < teams; ++b)
		{
			::MapGeneration::generationCheckpoint();
			if (b != a)
				for (int tile : units.at(b))
				{
					::MapGeneration::generationCheckpoint();
					if (steps.at(tile) >= 0)
						return "Colony " + std::to_string(a) + " can walk to colony " +
							   std::to_string(b) + (route.empty() ? "" : " " + route) + ".";
				}
		}
	}
	return "";
}

std::vector<unsigned char> homeGrassMask(const Map &map, const Torus &t,
										 const std::vector<int> &homeOf, int team)
{
	std::vector<unsigned char> ground(size_t(t.size()), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		ground.at(i) = homeOf.at(i) == team && map.terrainPropertiesAt(i).buildable;
	}
	return ground;
}

bool settleRoundColonies(Game &game, GenerationContext &context, const char *stream,
						 const std::vector<int> &homeOf, const std::vector<ShapePoint> &homes,
						 double radius)
{
	const Torus t(game.map.getW(), game.map.getH());
	return settleColonies(
		game, context, stream, [&](int team) { return homeGrassMask(game.map, t, homeOf, team); },
		[&](int team) { return homeSwarmSite(homes.at(team), 0.0, radius); });
}

std::string homePondMissing(const Map &map, const Torus &t, const std::vector<ShapePoint> &kits,
							int teams, const char *place, const char *water)
{
	for (int k = 0; k < teams; ++k)
	{
		::MapGeneration::generationCheckpoint();
		bool pond = false;
		for (int dy = -2; dy <= 2 && !pond; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -2; dx <= 2 && !pond; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				pond = map.terrainPropertiesAt(t.x(int(kits.at(k).x) + dx),
											   t.y(int(kits.at(k).y) + dy))
						   .fertilitySource;
			}
		}
		if (!pond)
			return "Colony " + std::to_string(k) + "'s " + place + " has lost its " + water + ".";
	}
	return "";
}
} // namespace MapGeneration
