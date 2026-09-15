// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationContext.h"
#include "Geometry.h"
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

/// Deals a design's start sites to the colonies at random: the design decides where the homes are,
/// and which colony gets which is a draw from `stream`, so a team number never lands on the same
/// ground map after map. A design's sites often come in a fixed order (farthest-point spreading
/// starts from cell 0, a lattice from its first row), and without this team 0 always started top
/// right of Old town (FEEDBACK 2026-09-13). Every designed generator calls it on its list of home
/// sites, cells or slots before anything is keyed by colony index, so kits, towers and validators
/// follow the deal without knowing about it; a validator rebuilding the design from a fresh context
/// gets the same deal.
template <typename Sites>
void dealStarts(GenerationContext &context, Sites &sites, const char *stream = "starts-deal")
{
	context.shuffle(sites.begin(), sites.end(), stream);
}

/// The registry's validateRequest for a designed generator: rebuilds the design from the request on
/// a probe context and reports its failure, so the lobby refuses what the design refuses, with the
/// design's own message. Pass the generator's design function: designFailure<design>.
template <auto Design> std::string designFailure(const GenerationRequest &r)
{
	GenerationContext probe(r);
	return Design(r, probe).failure;
}

/// The ground a colony's swarm and workers may stand on: its own tiles of `homeOf` that are pure
/// grass on the written map (a beach, a road or a plot's ring has spoiled the rest).
std::vector<unsigned char> homeGrassMask(const Map &, const Torus &, const std::vector<int> &homeOf,
										 int team);

/// A validator's check that no colony's home pond was lost to the map's furnishing: water within two
/// tiles of every kit's centre (`kits`, one per colony). Returns "" or "Colony k's <place> has lost
/// its <water>." for the first colony without.
std::string homePondMissing(const Map &, const Torus &, const std::vector<ShapePoint> &kits,
							int teams, const char *place, const char *water);

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

/// The usual last word on every colony's crops: clear the ground round the swarms, run the
/// wheat/wood guarantee (guaranteeStartingResources) and clear round the swarms again, since a
/// topped-up deposit may land there. Tiles of `keep` are designed deposits (walls) that are never
/// cleared and never looked past. `allowedTopup` is an independent row-major mask for where new
/// emergency crops may go; it clips the entire clump to a designed farm belt. Keeping these
/// roles separate lets a map protect both structural deposits and crop-free town ground.
void secureStartingCrops(Game &, GenerationContext &, const Torus &, int wheatRange = 24,
						 int woodRange = 32, int clearRadius = 0,
						 const std::vector<unsigned char> *keep = nullptr,
						 const std::vector<unsigned char> *allowedTopup = nullptr);

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

/// settleColonies for round homes (stampRoundHomes): every colony on its own grass (homeGrassMask),
/// its swarm where homeSwarmSite puts it in a home of `radius` facing out from the middle.
bool settleRoundColonies(Game &, GenerationContext &, const char *stream,
						 const std::vector<int> &homeOf, const std::vector<ShapePoint> &homes,
						 double radius);

/// validateWorld's first check on a design rebuilt from the request: it rebuilt, and it is the
/// map's size. `L` needs a `failure` string and a `t` torus.
template <typename Layout>
std::string designMismatch(const Layout &L, const Map &map, const char *name);

/// A finished colony must be able to harvest `type` within `range` walking steps. `name` is
/// used in failure diagnostics. The last step means gathering from a neighboring walkable tile,
/// not walking through the resource. Permanent resources are valid targets too.
struct ResourceAccessRule
{
	int type, range;
	const char *name;
};
/// Read-only counterpart to starting-resource/room repairs, for maps whose resource policy
/// forbids those repairs from planting freely (dry reserves, protected farmland, shore-only wood).
/// Floods from each colony's actual workers with the engine's non-swimmer ground predicate.
/// Requires `minimumSites` overlapping 4x4 anchors within `buildingRange`; these are placement
/// options, not disjoint buildings. Returns the first unmet rule with colony and observed distance
/// or site count. No mutation, RNG draws or silent weakening of requirements. Invalid budgets or
/// resource rules throw GenerationFailure; no worker for a colony is an explicit failure.
std::string startingAccessFailure(const Map &, int teams, const std::vector<ResourceAccessRule> &,
								  int minimumSites = 16, int buildingRange = 24);

/// Every colony's walk from colony 0's workers, with water, buildings and every resource
/// blocking (units don't, since they move): the check every validator makes.
struct ColonyWalk
{
	std::vector<std::vector<int>> workers; // each colony's unit tiles
	std::vector<int> steps;                // from colony 0's workers
	std::string error;
};
/// The opposite promise, for a map of islands: no colony's units can walk to any other colony's on
/// the engine's own ground rule (groundUnitTiles: no swimming, deposits and buildings block). "" when
/// every colony is cut off from every other, else "Colony a can walk to colony b <route>."
std::string coloniesApart(const Map &, int teams, const std::string &route);

/// `ground` names what the workers walk ("the flats"); `route` finishes the cut-off message
/// ("over the flats", or empty).
ColonyWalk walkFromFirstColony(const Map &, int teams, const std::string &ground,
							   const std::string &route);

/// Which crops a walk stands beside: whether any wheat deposit, and any wood deposit, has a tile
/// of `reach` (a flood's steps, -1 where nothing was reached) within one tile of it, diagonals
/// included, the way a worker harvests from the tile next to a deposit. The check a validator
/// makes after flooding from a colony's workers over walkable land.
struct CropsInReach
{
	bool wheat = false, wood = false;
	/// "" when both are in reach, else which is not: "cannot walk to wheat."
	std::string missing() const;
};
CropsInReach cropsBesideReach(const Map &, const std::vector<int> &reach);
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
