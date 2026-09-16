// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Ressource.h"
#include <array>
#include <vector>
class Game;

namespace MapGeneration
{
// How good a start each colony actually got, and how evenly the map shared that out.
//
// Walking distance to wood and wheat is what placement optimises, but it is a thin account of
// a starting position: a deposit at the door that runs dry, a colony with no room to build,
// one boxed between two rivals, and one on ground where wheat never grows back all look alike
// to it. These factors are measured on the finished map, where the swarm is built, its
// clearing is cleared and the workers are standing where they will actually start walking.
//
// What the measurements are worth is not decided here. FairnessModel.h turns them into a
// start's fitness with coefficients fitted to thousands of real games, and the map's fairness
// is how evenly that fitness shares out the chance of winning. See
// docs/map-generators/FAIRNESS_MODEL.md.

// The two numbers that decide what gets measured in the first place, as opposed to what a
// measurement is then worth. Both describe a young colony's working range on the ground.
struct StartQualityScale
{
	int catchmentSteps = 24; ///< walking steps a young colony works within
	int threatRadius = 40;   ///< rivals nearer than this crowd a colony
};

struct ColonyQuality
{
	struct DistanceBand
	{
		int walkingSteps = 0;
		int reachedTiles = 0, grassTiles = 0, buildableTiles = 0, fertileGrassTiles = 0;
		int exclusiveNearestTiles = 0, tiedNearestTiles = 0;
		std::array<int, MAX_RESOURCES> depositTiles{}, storedAmount{};
		std::array<int, MAX_RESOURCES> exclusiveDepositTiles{}, exclusiveStoredAmount{};
		std::array<int, MAX_RESOURCES> tiedDepositTiles{}, tiedStoredAmount{};
	};
	struct ResourceAccess
	{
		int nearestDistance = -1; ///< neighboring walking tile plus one gathering step
		int catchmentDeposits = 0, catchmentAmount = 0;
		int exclusiveCatchmentDeposits = 0, exclusiveCatchmentAmount = 0;
		int tiedCatchmentDeposits = 0, tiedCatchmentAmount = 0;
	};
	// As measured.
	int wheatDistance = -1, woodDistance = -1;
	int catchmentTiles = 0, buildSites = 0, resourceAmount = 0;
	int reachableTiles = 0, catchmentGrass = 0, catchmentBuildable = 0;
	int catchmentFertileGrass = 0, catchmentGrowthEnabledGrass = 0;
	int exclusiveNearestTiles = 0, tiedNearestTiles = 0;
	int exclusiveCatchmentTiles = 0, tiedCatchmentTiles = 0;
	std::array<ResourceAccess, MAX_RESOURCES> resources{};
	std::array<DistanceBand, 3> distanceBands{{DistanceBand{12}, DistanceBand{24},
																		 DistanceBand{48}}};
	int rivalDistance = -1, rivalsWithinThreat = 0;
	int reachableRivals = 0, farthestRivalDistance = -1;
	double meanFertility = 0;
	/// What the fitted model makes of the above, and the chance of winning it implies.
	double fitness = 0;
	double winProbability = 0;
};

struct StartQualityReport
{
	std::vector<ColonyQuality> colonies;
	double worstFitness = 0, bestFitness = 0, meanFitness = 0;
	/// 1 minus the colony-count-normalised Gini of the predicted win probabilities: 1 when every
	/// colony is equally likely to win, 0 when one colony would take the map. 1 for a single
	/// colony, which has nobody to be unfair to.
	double fairness = 0;
	/// What the lobby keeps its best candidate roll by. This is the fairness: a roll is kept for
	/// sharing the map out evenly, not for how rich it made everyone.
	double score = 0;
	bool measured = false;
};

/// Win probability per colony: softmax over their fitnesses.
std::vector<double> winProbabilities(const std::vector<double> &fitness);

/// 1 minus the normalised Gini of a probability vector. The raw Gini of n numbers cannot exceed
/// (n-1)/n, so it is divided by that ceiling and means the same thing at every colony count.
double mapFairness(const std::vector<double> &probability);

/// Requires a finished map: colonies built, workers placed, resources final. Also stamps
/// Map::Tile::fertility and Map::fertilityMaximum from the same Fertility::Field this already
/// computes for scoring, so a freshly generated map carries real fertility data instead of the
/// zeroes a never-computed tile defaults to (see FertilityCalculator, which is otherwise the
/// only thing that ever populates these two fields, and never runs as part of generation).
StartQualityReport scoreStarts(Game &game, int nbTeams, const StartQualityScale &scale = {});
} // namespace MapGeneration
