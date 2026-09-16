// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StartQuality.h"
#include <vector>
class Game;

namespace MapGeneration
{
// Measurements of a start that ColonyQuality does not take, for diagnosing play rather than
// for scoring it: why an AI does well or badly on a kind of terrain, what a generator gives
// each colony to work with. None of them is in the fairness model. Each was tested against
// played games; the fitting tool can still read them, and did, which is how the duplicates of
// ColonyQuality fields and of each other were found and removed (FAIRNESS_MODEL.md).
//
// Every measurement reuses the walking field and the fertility field scoreStarts builds, so
// computing them costs a few whole-map passes, not a search.
struct ColonyDiagnostics
{
	// --- Economy ---
	/// Summed growth chance of catchment grass that may take a crop and already touches wheat:
	/// how fast the field comes BACK, as opposed to how much grain stands on it today.
	double renewableWheat = 0;
	/// The harvest as a queue: every walkable tile next to a deposit is a place one worker can
	/// gather from, completing trips at a rate set by the round trip home. Summed 1/(round trip)
	/// over those tiles within 48 steps. Stock says how long a supply lasts; this says how fast it
	/// can be brought in. Fruit counts cherry, orange and prune together.
	double wheatThroughput = 0, woodThroughput = 0, stoneThroughput = 0, fruitThroughput = 0;
	/// A level-0 inn is 2x2. Steps to the nearest site where one stands with grain on the ring
	/// around it, and how many such sites are within 48 steps.
	int innNextToWheatDistance = -1;
	int innNextToWheatSites = 0;
	/// 4x4 sites between 20 and 64 steps out with wheat within 12 tiles: room for a second swarm.
	int secondSwarmSites = 0;

	// --- Pressure on the start ---
	/// The forest's front: summed growth chance of grass that may take a tree and touches one.
	double encroachingWood = 0;
	/// 4x4 build sites standing on that front, which the forest can take.
	int threatenedBuildSites = 0;
	/// Steps to the nearest wheat a rival reaches within six steps of when this colony does.
	int contestedWheatDistance = -1;
	/// The narrowest point on the walking route to the nearest rival, as walkable tiles in a 5x5
	/// window: a low number is a door this colony can hold, a high one open ground.
	int chokeWidth = -1;
};

struct StartDiagnosticsReport
{
	std::vector<ColonyDiagnostics> colonies;
	bool measured = false;
};

/// Requires the same finished map scoreStarts does. Reads the map and changes nothing.
StartDiagnosticsReport diagnoseStarts(Game &game, int nbTeams, const StartQualityScale &scale = {});

/// A controlled edit to one colony's start, for counterfactual experiments rather than for any
/// map a player sees: play the same map from the same game seed with and without it, and the
/// difference in that colony's economy is what the removed property is worth.
/// Removes every deposit of `resource` whose nearest gathering tile is within `radius` walking
/// steps of `team`'s starting workers. Clears tiles in place and draws no random number, so the
/// rest of the world is exactly the unedited map. Returns deposits removed, or -1 when the team
/// does not exist.
int removeResourceNear(Game &game, int team, int resource, int radius);
} // namespace MapGeneration
