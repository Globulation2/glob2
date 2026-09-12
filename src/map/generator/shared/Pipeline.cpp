// SPDX-License-Identifier: GPL-3.0-or-later
#include "Pipeline.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Planting.h"
#include "Resources.h"
namespace MapGeneration
{
bool reopenCrampedStarts(Game &game, GenerationContext &context, const ResourceAmounts &amounts,
						 int wheatRange, int woodRange, int clearRadius,
						 const std::vector<unsigned char> *protectedWalls)
{
	if (!amounts.scaled())
		return false;
	openCrampedStarts(game, context, 16, 24, protectedWalls);
	guaranteeStartingResources(game, context, wheatRange, woodRange, clearRadius, protectedWalls);
	return true;
}

void secureStartingCrops(Game &game, GenerationContext &context, const Torus &t, int wheatRange,
						 int woodRange, int clearRadius, const std::vector<unsigned char> *keep)
{
	clearAroundSwarms(game.map, context, t, keep);
	guaranteeStartingResources(game, context, wheatRange, woodRange, clearRadius, keep);
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
} // namespace MapGeneration
