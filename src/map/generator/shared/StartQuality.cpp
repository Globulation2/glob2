// SPDX-License-Identifier: GPL-3.0-or-later
#include "StartQuality.h"
#include "FertilityField.h"
#include "Game.h"
#include "Grid.h"
#include "Map.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace MapGeneration
{
namespace
{
double clampUnit(double v)
{
	return v < 0 ? 0 : (v > 1 ? 1 : v);
}

/// Distance in walking steps from a colony's starting workers to every tile it can reach.
/// Workers, not the boot tile: the swarm occupies the boot tile and nobody walks out of it.
std::vector<int> walkFromWorkers(const Map &map, const std::vector<int> &workers)
{
	const Torus t(map);
	return stepsFrom(t, tileMask(t, workers), groundUnitTiles(map));
}
} // namespace

StartQualityReport scoreStarts(Game &game, int requestedTeams, const StartQualityWeights &weights,
							   const StartQualityScale &scale)
{
	StartQualityReport report;
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();
	const int nbTeams = std::min(game.teamsCount(), requestedTeams);
	if (nbTeams <= 0)
		return report;

	const std::vector<std::vector<int>> workers = unitTilesByTeam(map, nbTeams);
	for (const auto &team : workers)
		if (team.empty())
			return report; // nothing walked out of this colony; there is nothing to score

	const Fertility::Field fertility = Fertility::forMap(map);

	// scoreStarts already pays for this field on every roll; stamping it into the map's own
	// tiles is a free byproduct of that, not a new cost, and it is otherwise the only way a
	// freshly generated map ever gets one. FertilityCalculator (the map editor's "compute
	// fertility" action, and Game_io.cpp's load-time migration for saves older than version 63)
	// is the only other thing that ever populates these two fields, and neither runs as part of
	// generation - so without this, every custom or lobby game starts with an all-zero fertility
	// overlay regardless of how the field itself is computed. Same clamp FertilityCalculator
	// uses: kScale's own ceiling (an all-water neighbourhood) is one past what a Uint16 holds.
	constexpr std::uint32_t kFertilityCeiling = std::numeric_limits<Uint16>::max();
	Uint16 fertilityMax = 0;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const Uint16 value =
				static_cast<Uint16>(std::min(fertility.at(x, y), kFertilityCeiling));
			map.getTile(x, y).fertility = value;
			fertilityMax = std::max(fertilityMax, value);
		}
	map.fertilityMaximum = fertilityMax;

	report.colonies.resize(nbTeams);

	for (int team = 0; team < nbTeams; ++team)
	{
		ColonyQuality &colony = report.colonies[team];
		const std::vector<int> dist = walkFromWorkers(map, workers[team]);

		for (int p = 0; p < w * h; ++p)
		{
			if (dist[p] < 0 || dist[p] > scale.catchmentSteps)
				continue;
			const int x = p % w, y = p / w;
			++colony.catchmentTiles;
			colony.meanFertility += fertility.at(x, y);
			if (map.isFreeForBuilding(x, y, 4, 4))
				++colony.buildSites;
		}
		if (colony.catchmentTiles)
			colony.meanFertility /= colony.catchmentTiles;

		// A deposit counts once however many catchment tiles touch it; workers gather from
		// beside a deposit, so what matters is that the colony can stand next to it at all.
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				const Resource &resource = map.getResource(x, y);
				if (resource.type != CORN && resource.type != WOOD)
					continue;
				int nearest = -1;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						if (!dx && !dy)
							continue;
						const int d = dist[map.normalizeY(y + dy) * w + map.normalizeX(x + dx)];
						if (d >= 0 && (nearest < 0 || d < nearest))
							nearest = d;
					}
				if (nearest < 0)
					continue;
				const int reach = nearest + 1;
				int &best = resource.type == CORN ? colony.wheatDistance : colony.woodDistance;
				if (best < 0 || reach < best)
					best = reach;
				if (nearest <= scale.catchmentSteps)
					colony.resourceAmount += resource.amount;
			}

		for (int rival = 0; rival < nbTeams; ++rival)
		{
			if (rival == team)
				continue;
			int nearest = -1;
			for (int p : workers[rival])
				if (dist[p] >= 0 && (nearest < 0 || dist[p] < nearest))
					nearest = dist[p];
			if (nearest < 0)
				continue; // no land route to this rival, which is isolation rather than threat
			if (colony.rivalDistance < 0 || nearest < colony.rivalDistance)
				colony.rivalDistance = nearest;
			if (nearest <= scale.threatRadius)
				++colony.rivalsWithinThreat;
		}

		colony.wheat = colony.wheatDistance < 0
						   ? 0
						   : clampUnit(1.0 - double(colony.wheatDistance) / scale.wheatReference);
		colony.wood = colony.woodDistance < 0
						  ? 0
						  : clampUnit(1.0 - double(colony.woodDistance) / scale.woodReference);
		colony.fertility = clampUnit(colony.meanFertility / scale.fertilityReference);
		colony.depth = clampUnit(double(colony.resourceAmount) / scale.depthReference);
		colony.room = clampUnit(double(colony.buildSites) / scale.roomReference);
		// No reachable rival is the safest a colony can be; being one of several crowded into
		// the same neighbourhood is the case the raw distance alone does not describe.
		const double spacing =
			colony.rivalDistance < 0
				? 1.0
				: clampUnit(double(colony.rivalDistance) / scale.isolationReference);
		const int crowd = std::max(0, colony.rivalsWithinThreat - 1);
		colony.isolation = spacing * std::max(0.0, 1.0 - scale.crowdPenalty * crowd);

		const double sum = weights.wheat + weights.wood + weights.fertility + weights.depth +
						   weights.room + weights.isolation;
		colony.total = sum <= 0
						   ? 0
						   : (weights.wheat * colony.wheat + weights.wood * colony.wood +
							  weights.fertility * colony.fertility + weights.depth * colony.depth +
							  weights.room * colony.room + weights.isolation * colony.isolation) /
								 sum;
		// A colony that cannot reach one of its primary resources has not got a start at all,
		// whatever room and fertility it was given.
		if (colony.wheatDistance < 0 || colony.woodDistance < 0)
			colony.total = 0;
	}

	report.worst = report.best = report.colonies[0].total;
	for (const ColonyQuality &colony : report.colonies)
	{
		report.worst = std::min(report.worst, colony.total);
		report.best = std::max(report.best, colony.total);
	}
	report.fairness = report.best > 0 ? report.worst / report.best : 0;
	report.score = report.worst * std::pow(report.fairness, scale.fairnessExponent);
	report.measured = true;
	return report;
}
} // namespace MapGeneration
