// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationContext.h"
#include "Grid.h"
#include "Regions.h"
#include "Settlements.h"
#include <string>
#include <vector>
class Game;
class Map;
namespace MapGeneration
{
// The stages a designed generator runs, as functions a generate() body calls in the order that
// suits its map. There is no superclass: a generator that needs a different order, or a
// different step, calls what it needs and writes the rest itself.
//
// The usual order is: design the layout as a pure function of the request; stamp it into a
// TerrainSketch, lay beaches and write it out (Sketch.h); settle the colonies; plant the kits and
// the ambient layers (Planting.h, Resources.h); clear round the swarms; guarantee every colony
// its crops; open any route the deposits closed (Roads.h); and have validateWorld rebuild the
// design and check the finished world against it.

/// A swarm and its workers for every colony, each inside its own home mask and as near as the
/// mask allows to its anchor. Fails the candidate on the first colony that does not fit.
template <typename HomeMask, typename Anchor>
bool settleColonies(Game &game, GenerationContext &context, const char *stream, HomeMask homeMask,
					Anchor anchor)
{
	for (int team = 0; team < context.request.nbTeams; ++team)
		if (!placeSettlement(game, context, team, homeMask(team), anchor(team), stream))
			return false;
	return true;
}

/// The resource amounts a generator's ambient layers are scaled by; every one is 100 by default.
struct ResourceAmounts
{
	int wheat = 100, wood = 100, stone = 100, algae = 100, fruit = 100;
	bool scaled() const
	{
		return wheat != 100 || wood != 100 || stone != 100 || algae != 100 || fruit != 100;
	}
};

/// A resource amount well above the default can wall a colony into a pocket with nowhere to
/// build, which the wheat/wood guarantee doesn't address: a colony buried in wheat has wheat at
/// its feet. At any non-default amount this opens such a colony back up (openCrampedStarts) and
/// re-runs the guarantee in case the clearing took its nearest crop with the wall. At the
/// defaults nothing happens, and it returns whether anything ran.
bool reopenCrampedStarts(Game &, GenerationContext &, const ResourceAmounts &, int wheatRange = 24,
						 int woodRange = 32, int clearRadius = 0,
						 const std::vector<unsigned char> *protectedWalls = nullptr);

/// validateWorld's first check on a design rebuilt from the request: it rebuilt, and it is the
/// map's size. `L` needs a `failure` string and a `t` torus.
template <typename Layout>
std::string designMismatch(const Layout &L, const Map &map, const char *name);

/// Every colony's walk from colony 0's workers, with water, buildings and every resource
/// blocking (units don't, since they move): the check every validator makes.
struct ColonyWalk
{
	std::vector<std::vector<int>> workers; // each colony's unit tiles
	std::vector<int> steps;                // from colony 0's workers
	std::string error;
};
/// `ground` names what the workers walk ("the flats"); `route` finishes the cut-off message
/// ("over the flats", or empty).
ColonyWalk walkFromFirstColony(const Map &, int teams, const std::string &ground,
							   const std::string &route);
} // namespace MapGeneration

#include "Map.h"
template <typename Layout>
std::string MapGeneration::designMismatch(const Layout &L, const Map &map, const char *name)
{
	if (!L.failure.empty())
		return std::string("The ") + name + " design could not be rebuilt: " + L.failure;
	if (map.getW() != L.t.w || map.getH() != L.t.h)
		return std::string("The ") + name + " design does not match the map size.";
	return "";
}
