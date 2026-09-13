// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
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
struct StartQualityWeights
{
	double wheat = 0.22, wood = 0.22, fertility = 0.25, depth = 0.12, room = 0.10, isolation = 0.09;
};

// Every factor is normalised against an absolute reference, never against the map's own best
// colony: candidate maps are ranked against each other, and a per-map normalisation would
// score every map alike. The two distances are what a colony needs to be viable, the same
// bars the study tool scores against. The other four have no such bar, so they are set near
// the ninetieth percentile of what the generators actually produce: high enough that a good
// colony is not capped at the same 1.0 as a great one, low enough to separate the poor ones.
struct StartQualityScale
{
	int catchmentSteps = 24;       ///< walking steps a young colony works within
	int wheatReference = 24;       ///< beyond this a colony is not viably fed
	int woodReference = 32;        ///< beyond this a colony is not viably supplied
	int fertilityReference = 8000; ///< growth probability, on Fertility::kScale
	int depthReference = 450;      ///< wheat and wood amount within the catchment
	int roomReference = 900;       ///< swarm-sized building sites within the catchment
	int isolationReference = 60;   ///< walking steps to the nearest rival colony
	int threatRadius = 40;         ///< rivals nearer than this crowd a colony
	double crowdPenalty = 0.15;    ///< per rival past the first inside that radius
	double fairnessExponent = 1.0; ///< 0 is pure maximin, large is pure fairness
};

struct ColonyQuality
{
	// As measured.
	int wheatDistance = -1, woodDistance = -1;
	int catchmentTiles = 0, buildSites = 0, resourceAmount = 0;
	int rivalDistance = -1, rivalsWithinThreat = 0;
	double meanFertility = 0;
	// Normalised to [0,1].
	double wheat = 0, wood = 0, fertility = 0, depth = 0, room = 0, isolation = 0;
	/// Weighted mean of the six, or 0 outright when wheat or wood cannot be reached at all.
	double total = 0;
};

struct StartQualityReport
{
	std::vector<ColonyQuality> colonies;
	double worst = 0, best = 0;
	double fairness = 0; ///< worst/best, 1 when a map has a single colony
	double score = 0;    ///< worst * pow(fairness, fairnessExponent)
	bool measured = false;
};

/// Requires a finished map: colonies built, workers placed, resources final. Also stamps
/// Map::Tile::fertility and Map::fertilityMaximum from the same Fertility::Field this already
/// computes for scoring, so a freshly generated map carries real fertility data instead of the
/// zeroes a never-computed tile defaults to (see FertilityCalculator, which is otherwise the
/// only thing that ever populates these two fields, and never runs as part of generation).
StartQualityReport scoreStarts(Game &game, int nbTeams, const StartQualityWeights &weights = {},
							   const StartQualityScale &scale = {});
} // namespace MapGeneration
