// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Ressource.h"
#include "StartQuality.h"
#include <vector>
class Game;

namespace MapGeneration
{
// Candidate start measurements under test, on maps regenerated from a tournament's own
// requests. None of these is in the fitted model: they exist so tools/fairness_model.py can
// ask whether a new idea predicts winning, against outcomes already played, without running
// another game. A candidate that earns its place moves into ColonyQuality and scoreStarts,
// where it becomes eligible for the model; one that does not is deleted.
//
// Everything here reuses the walking field and fertility field scoreStarts already builds, so
// a promoted measurement costs the generator roughly nothing beyond what it pays today.
struct ColonyProbe
{
	/// Walkable tiles inside the catchment with a deposit next door. A deposit can be worked
	/// from several tiles at once; a colony with one gathering edge queues its workers behind
	/// each other whatever the stock is.
	std::array<int, MAX_RESOURCES> harvestFrontage{};
	/// Summed growth chance of catchment grass that may take a crop and already touches wheat:
	/// how fast the field comes BACK, as opposed to how much grain is standing on it today.
	double renewableWheat = 0;
	/// Ground beyond the opening court that a second swarm could actually use: a 4x4 site with
	/// wheat near enough to feed it. Distance to the nearest, and how many exist at all.
	int secondSwarmDistance = -1;
	int secondSwarmSites = 0;
	/// The narrowest point on the walking route to the nearest rival, as walkable tiles in a
	/// 5x5 window. A low number is a door this colony can hold; a high one is open ground.
	int chokeWidth = -1;
	/// Forest that can come TO the colony rather than wood the colony can go and cut.
	/// Wood spreads onto growth-enabled grass next to an existing deposit, so the summed
	/// growth chance along that front is the rate at which trees advance on this start, and
	/// the build sites standing in the way are the room it stands to lose. A start can be
	/// rich in timber and still be a bad start if the timber is going to grow over it.
	double encroachingWood = 0;
	int threatenedBuildSites = 0;
	int woodFrontTiles = 0;
	/// The same front for wheat, so the two can be compared: a colony whose forest advances
	/// faster than its field is losing ground it cannot get back without clearing.
	double encroachingWheat = 0;
	/// The early harvest as a queue. Every walkable tile next to wheat is a place one worker
	/// can gather from, and a worker there completes trips at a rate set by the round trip back
	/// to the swarm. Summing 1/(round trip) over those tiles is the colony's grain throughput
	/// if it had workers to spare -- which, early, it is short of, so the nearest edges count
	/// most. Stock says how long the food lasts; this says how fast it can be brought home.
	double harvestThroughput = 0;
	double woodThroughput = 0;
	/// Food buildings. A level-0 inn is 2x2 and feeds from its own stock, which workers fill;
	/// an inn standing next to grain is filled by the shortest trips on the map. Walking steps
	/// to the nearest such site, and how many there are within reach.
	int innNextToWheatDistance = -1;
	int innNextToWheatSites = 0;
	/// Steps to the nearest wheat a rival reaches within a few steps of when this colony does.
	/// Food that has to be fought for, as opposed to food nobody else is near.
	int contestedWheatDistance = -1;
	int contestedWheatDeposits = 0;
};

struct StartProbeReport
{
	std::vector<ColonyProbe> colonies;
	bool measured = false;
};

/// A controlled edit to one colony's start, for counterfactual experiments: play the same map
/// with and without it, from the same game seed, and the difference in that colony's economy is
/// what this one property is worth -- a cause, where every screen so far measured correlations.
/// Removes every deposit of `resource` whose nearest gathering tile is within `radius` walking
/// steps of `team`'s starting workers. Touches only those deposits and draws no random numbers,
/// so the rest of the world is exactly the unperturbed map. Returns deposits removed, or -1 when
/// the team does not exist.
int removeResourceNear(Game &game, int team, int resource, int radius);

/// Requires the same finished map scoreStarts does. Read-only apart from nothing: unlike
/// scoreStarts it does not stamp fertility, so call it after scoreStarts or alone.
StartProbeReport probeStarts(Game &game, int nbTeams, const StartQualityScale &scale = {});
} // namespace MapGeneration
