// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FertilityField.h"
#include "Grid.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
struct GenerationContext;
namespace MapGeneration
{
// Starts for a natural map round one shared objective (Central Quarry's isle, Hidden Oasis' gorge):
// no home is stamped, so fairness is by walk. Colonies are spread by walking distance, but only inside
// a narrow band of walking distance from the objective, on watered ground of a similar food yield.

/// What a walk-band search asks for. The defaults are Central Quarry's; its comments there say which
/// review finding each answers.
struct WalkBandRequest
{
	// The walk from the objective every colony starts at is a percentile of the candidates' walks,
	// tried in this order until the colonies spread `siteSpacing` apart.
	std::vector<int> percentiles{40, 50, 60, 70, 30, 80};
	// The walk is capped in steps, inside the working range of the AIs: `greatestWalk`, plus
	// `walkPerExtraColony` for every colony beyond `extraColoniesFrom`, up to `greatestWalkCeiling`, all
	// grown by `walkGrowth` of the map's scale beyond 1. The whole ladder is tried again at `walkStretch`.
	int greatestWalk = 70, walkPerExtraColony = 6, extraColoniesFrom = 6, greatestWalkCeiling = 110;
	double walkGrowth = 0.3, walkStretch = 1.08;
	// The band either side of the walk is walk / bandDivisor, at least leastBand.
	int leastBand = 5, bandDivisor = 14;
	// The spacing worth searching for: siteSpacing, or ringSpacingShare of what a ring of colonies at
	// this walk round an objective of `ringRadius` gives each, whichever is less, never under
	// leastSpacing. A preferred spread reaching goodEnoughSpacingPercent of it is kept.
	int siteSpacing = 60, leastSpacing = 28, goodEnoughSpacingPercent = 85;
	double ringSpacingShare = 0.45;
	// Every site's straight-line steps to the objective are at least this share of the walk.
	int leastSwimPercent = 80;
	// Picks prefer sites whose yield (crop growth summed over the ground within yieldSteps) lies
	// between these percentiles of a sample of the band's candidates.
	int yieldSteps = 48, yieldSamples = 300, yieldLowPercentile = 70, yieldHighPercentile = 90;
	// A preferred site is watered (mean crop growth over a square of roomRadius at least
	// fertilityFloor) and reaches catchmentFloor tiles within catchmentSteps.
	int catchmentSteps = 24, catchmentFloor = 900, roomRadius = 10;
	std::uint32_t fertilityFloor = 2500;
	int trials = 6;
};

/// What the search found: the sites (undealt; run dealStarts), the walk and band they were picked at,
/// their closest walking pair, and which preference the kept spread used ("evenly-fed", "roomy",
/// "any"). `sites` is empty when no band held enough candidates.
struct WalkBandStarts
{
	std::vector<int> sites;
	int distance = 0, band = 0, spacing = -1;
	const char *preference = "";
};

/// Spreads `teams` sites over `roomy` ground (already limited to where a colony may stand) by walking
/// distance over `walkable`, inside a band of `toGoal` (steps on foot from the objective), with
/// `swimToGoal` (straight steps) as the floor on how near the objective a site may be. `field` is the
/// crop growth field of the beached sketch. Draws from `stream`; measures the candidates of every
/// walk tried under `candidatesKey`, with the walk as the subject.
WalkBandStarts spreadInWalkBand(const Torus &, const std::vector<unsigned char> &roomy,
								const std::vector<unsigned char> &walkable,
								const std::vector<int> &toGoal, const std::vector<int> &swimToGoal,
								const Fertility::Field &field, int teams, double mapScale,
								double ringRadius, GenerationContext &, const std::string &stream,
								std::string_view candidatesKey, const WalkBandRequest & = {});

/// Each tile of `land` labelled with the site it walks to first: a breadth-first flood from the square
/// of `room` tiles round every site at once, eight-connected, ties to the lower site. Cheap, and enough
/// to decide where a colony's ponds, kit and swarm may go; -1 where no site reaches.
std::vector<int> firstWalkTerritories(const Torus &, const std::vector<unsigned char> &land,
									  const std::vector<int> &sites, int room);
} // namespace MapGeneration
