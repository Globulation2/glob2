// SPDX-License-Identifier: GPL-3.0-or-later
#include "Pipeline.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Homes.h"
#include "Planting.h"
#include "Resources.h"
namespace MapGeneration
{
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
