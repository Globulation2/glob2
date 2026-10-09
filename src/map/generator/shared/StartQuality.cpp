#include "GenerationWork.h"
#include "GenerationFertilityWork.h"
#include "GenerationNumeric.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PowerOfTwo.h"
#include "StartQuality.h"
#include "Material.h"
#include "FairnessModel.h"
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
/// Distance in walking steps from a colony's starting workers to every tile it can reach.
/// Workers, not the boot tile: the swarm occupies the boot tile and nobody walks out of it.
std::vector<int> walkFromWorkers(const Map &map, const std::vector<int> &workers)
{
	const Torus t(map);
	return stepsFrom(t, tileMask(t, workers), groundUnitTiles(map));
}
} // namespace

std::vector<double> winProbabilities(const std::vector<double> &fitness)
{
	std::vector<double> probability(fitness.size(), 0.0);
	if (fitness.empty())
		return probability;
	const double top = *std::max_element(fitness.begin(), fitness.end());
	double total = 0;
	for (std::size_t i = 0; i < fitness.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		probability.at(i) = ::MapGeneration::Numeric::exp(fitness.at(i) - top);
		total += probability.at(i);
	}
	for (double &value : probability)
	{
		::MapGeneration::generationCheckpoint();
		value = total > 0 ? value / total : 1.0 / double(fitness.size());
	}
	return probability;
}

double mapFairness(const std::vector<double> &probability)
{
	const std::size_t n = probability.size();
	if (n < 2)
		return 1.0; // one colony has nobody to be unfair to
	double total = 0;
	for (double value : probability)
	{
		::MapGeneration::generationCheckpoint();
		total += value;
	}
	if (total <= 0)
		return 1.0;
	std::vector<double> ordered(probability);
	std::sort(ordered.begin(), ordered.end());
	double weighted = 0;
	for (std::size_t i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		weighted += (2.0 * double(i + 1) - double(n) - 1.0) * ordered.at(i);
	}
	// Divided by the (n-1)/n ceiling a raw Gini cannot exceed, so two colonies and eight
	// colonies are read off the same scale.
	const double gini = weighted / total * (1.0 / double(n - 1));
	return 1.0 - std::min(1.0, std::max(0.0, gini));
}

StartQualityReport scoreStarts(Game &game, int requestedTeams, const StartQualityScale &scale)
{
	StartQualityReport report;
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();
	const int nbTeams = std::min(game.teamsCount(), requestedTeams);
	if (nbTeams <= 0)
		return report;

	const std::vector<std::vector<int>> workers = unitTilesByTeam(map, nbTeams);
	for (const auto &team : workers)
	{
		::MapGeneration::generationCheckpoint();
		if (team.empty())
			return report;
	} // nothing walked out of this colony; there is nothing to score

	generationFertilityMapWork(map);
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
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const Uint16 value =
				static_cast<Uint16>(std::min(fertility.at(x, y), kFertilityCeiling));
			map.setFertility(x, y, value);
			fertilityMax = std::max(fertilityMax, value);
		}
	}
	map.fertilityMaximum = fertilityMax;

	report.colonies.resize(nbTeams);
	std::vector<std::vector<int>> walkingFields;
	walkingFields.reserve(nbTeams);

	for (int team = 0; team < nbTeams; ++team)
	{
		::MapGeneration::generationCheckpoint();
		ColonyQuality &colony = report.colonies.at(team);
		walkingFields.push_back(walkFromWorkers(map, workers.at(team)));
		const std::vector<int> &dist = walkingFields.back();

		for (int p = 0; p < w * h; ++p)
		{
			::MapGeneration::generationCheckpoint();
			if (dist.at(p) >= 0)
				++colony.reachableTiles;
			if (dist.at(p) >= 0)
				for (auto &band : colony.distanceBands)
				{
					::MapGeneration::generationCheckpoint();
					if (dist.at(p) <= band.walkingSteps)
					{
						const int x = powerOfTwoRemainder(p, w), y = p / w;
						++band.reachedTiles;
						band.buildableTiles += map.isFreeForBuilding(x, y);
						if (map.terrainSupportsMaterialAt(x,y,MaterialId::Food))
						{
							++band.grassTiles;
							band.fertileGrassTiles += fertility.at(x, y) > 0;
						}
					}
				}
			if (dist.at(p) < 0 || dist.at(p) > scale.catchmentSteps)
				continue;
			const int x = powerOfTwoRemainder(p, w), y = p / w;
			++colony.catchmentTiles;
			colony.meanFertility += fertility.at(x, y);
			if (map.terrainSupportsMaterialAt(x,y,MaterialId::Food))
			{
				++colony.catchmentGrass;
				colony.catchmentFertileGrass += fertility.at(x, y) > 0;
				colony.catchmentGrowthEnabledGrass += map.canResourcesGrow(x,y);
			}
			colony.catchmentBuildable += map.isFreeForBuilding(x, y);
			if (map.isFreeForBuilding(x, y, 4, 4))
				++colony.buildSites;
		}
		if (colony.catchmentTiles)
			colony.meanFertility /= colony.catchmentTiles;

		// A deposit counts once however many catchment tiles touch it; workers gather from
		// beside a deposit, so what matters is that the colony can stand next to it at all.
		for (int y = 0; y < h; ++y)
		{
			::MapGeneration::generationCheckpoint();
			for (int x = 0; x < w; ++x)
			{
				::MapGeneration::generationCheckpoint();
				const Resource &resource = map.getResource(x, y);
				if (resource.type == NO_RES_TYPE)
					continue;
				int nearest = -1;
				for (int dy = -1; dy <= 1; ++dy)
				{
					::MapGeneration::generationCheckpoint();
					for (int dx = -1; dx <= 1; ++dx)
					{
						::MapGeneration::generationCheckpoint();
						if (!dx && !dy)
							continue;
						const int d = dist.at(map.normalizeY(y + dy) * w + map.normalizeX(x + dx));
						if (d >= 0 && (nearest < 0 || d < nearest))
							nearest = d;
					}
				}
				if (nearest < 0)
					continue;
				const int reach = nearest + 1;
                for (unsigned material=0; material<MaterialCount; ++material)
				{
					::MapGeneration::generationCheckpoint();
					const auto amount=map.materialAmountAtSlot(y*w+x,material);
                    if (!amount) continue;
				auto &access = colony.materials[material];
				if (access.nearestDistance < 0 || reach < access.nearestDistance)
					access.nearestDistance = reach;
				if (nearest <= scale.catchmentSteps)
				{
					++access.catchmentDeposits;
					access.catchmentAmount += amount;
				}
				for (auto &band : colony.distanceBands)
				{
					::MapGeneration::generationCheckpoint();
					if (nearest <= band.walkingSteps)
					{
						++band.depositTiles[material];
						band.storedAmount[material] += amount;
					}
				}
				}
			}
		}
		colony.wheatDistance = colony.materials[materialIndex(MaterialId::Food)].nearestDistance;
		colony.woodDistance = colony.materials[materialIndex(MaterialId::Wood)].nearestDistance;
		colony.resourceAmount = colony.materials[materialIndex(MaterialId::Food)].catchmentAmount +
								colony.materials[materialIndex(MaterialId::Wood)].catchmentAmount;

		for (int rival = 0; rival < nbTeams; ++rival)
		{
			::MapGeneration::generationCheckpoint();
			if (rival == team)
				continue;
			int nearest = -1;
			for (int p : workers.at(rival))
			{
				::MapGeneration::generationCheckpoint();
				if (dist.at(p) >= 0 && (nearest < 0 || dist.at(p) < nearest))
					nearest = dist.at(p);
			}
			if (nearest < 0)
				continue; // no land route to this rival, which is isolation rather than threat
			++colony.reachableRivals;
			colony.farthestRivalDistance = std::max(colony.farthestRivalDistance, nearest);
			if (colony.rivalDistance < 0 || nearest < colony.rivalDistance)
				colony.rivalDistance = nearest;
			if (nearest <= scale.threatRadius)
				++colony.rivalsWithinThreat;
		}
	}

	// Assign each walkable tile to its uniquely closest colony, or to every colony
	// tied for first. A catchment tile is also counted in that colony's local share.
	for (int p = 0; p < w * h; ++p)
	{
		::MapGeneration::generationCheckpoint();
		int best = -1, ties = 0, owner = -1;
		for (int team = 0; team < nbTeams; ++team)
		{
			::MapGeneration::generationCheckpoint();
			const int d = walkingFields.at(team).at(p);
			if (d < 0)
				continue;
			if (best < 0 || d < best)
			{
				best = d;
				owner = team;
				ties = 1;
			}
			else if (d == best)
				++ties;
		}
		if (best < 0)
			continue;
		for (int team = 0; team < nbTeams; ++team)
		{
			::MapGeneration::generationCheckpoint();
			if (ties == 1 && team != owner)
				continue;
			if (ties > 1 && walkingFields.at(team).at(p) != best)
				continue;
			auto &colony = report.colonies.at(team);
			const bool local = walkingFields.at(team).at(p) <= scale.catchmentSteps;
			for (auto &band : colony.distanceBands)
			{
				::MapGeneration::generationCheckpoint();
				if (walkingFields.at(team).at(p) <= band.walkingSteps)
				{
					if (ties == 1)
						++band.exclusiveNearestTiles;
					else
						++band.tiedNearestTiles;
				}
			}
			if (ties == 1)
			{
				++colony.exclusiveNearestTiles;
				colony.exclusiveCatchmentTiles += local;
			}
			else
			{
				++colony.tiedNearestTiles;
				colony.tiedCatchmentTiles += local;
			}
		}
	}
	// Deposits can be reachable to several colonies yet favour one approach. Track
	// nearby stock that a colony reaches strictly first versus stock approached at
	// the same cost as a rival. This is positional access, not actual ownership.
	std::vector<int> approach(nbTeams, -1);
	for (int y = 0; y < h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const Resource &resource = map.getResource(x, y);
			if (resource.type == NO_RES_TYPE)
				continue;
			int best = -1, ties = 0, owner = -1;
			std::fill(approach.begin(), approach.end(), -1);
			for (int team = 0; team < nbTeams; ++team)
			{
				::MapGeneration::generationCheckpoint();
				for (int dy = -1; dy <= 1; ++dy)
				{
					::MapGeneration::generationCheckpoint();
					for (int dx = -1; dx <= 1; ++dx)
					{
						::MapGeneration::generationCheckpoint();
						if (!dx && !dy)
							continue;
						const int d = walkingFields.at(team).at(map.normalizeY(y + dy) * w +
																map.normalizeX(x + dx));
						if (d >= 0 && (approach.at(team) < 0 || d < approach.at(team)))
							approach.at(team) = d;
					}
				}
				if (approach.at(team) < 0)
					continue;
				if (best < 0 || approach.at(team) < best)
				{
					best = approach.at(team);
					owner = team;
					ties = 1;
				}
				else if (approach.at(team) == best)
					++ties;
			}
			if (best < 0)
				continue;
			for (int team = 0; team < nbTeams; ++team)
			{
				::MapGeneration::generationCheckpoint();
				if ((ties == 1 && team != owner) || (ties > 1 && approach.at(team) != best))
					continue;
                for (unsigned material=0; material<MaterialCount; ++material)
				{
					::MapGeneration::generationCheckpoint();
					const auto amount=map.materialAmountAtSlot(y*w+x,material);
                    if (!amount) continue;
					auto &access = report.colonies.at(team).materials[material];
					for (auto &band : report.colonies.at(team).distanceBands)
					{
						::MapGeneration::generationCheckpoint();
						if (best <= band.walkingSteps)
						{
							if (ties == 1)
							{
								++band.exclusiveDepositTiles[material];
								band.exclusiveStoredAmount[material] += amount;
							}
							else
							{
								++band.tiedDepositTiles[material];
								band.tiedStoredAmount[material] += amount;
							}
						}
					}
				if (best > scale.catchmentSteps)
					continue;
				if (ties == 1)
				{
					++access.exclusiveCatchmentDeposits;
					access.exclusiveCatchmentAmount += amount;
				}
				else
				{
					++access.tiedCatchmentDeposits;
					access.tiedCatchmentAmount += amount;
				}
				}
			}
		}
	}

	// Every measurement is final now -- territory, deposits and bands included -- so the fitted
	// model can read them. Fitness first, then what it says about who wins and how evenly.
	std::vector<double> fitness(report.colonies.size());
	for (std::size_t i = 0; i < report.colonies.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		fitness.at(i) = startFitness(report.colonies, i);
	}
	const std::vector<double> probability = winProbabilities(fitness);
	double sum = 0;
	for (std::size_t i = 0; i < report.colonies.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		report.colonies.at(i).fitness = fitness.at(i);
		report.colonies.at(i).winProbability = probability.at(i);
		sum += fitness.at(i);
	}
	report.worstFitness = *std::min_element(fitness.begin(), fitness.end());
	report.bestFitness = *std::max_element(fitness.begin(), fitness.end());
	report.meanFitness = sum / double(fitness.size());
	report.fairness = mapFairness(probability);
	report.score = report.fairness;
	report.measured = true;
	return report;
}
} // namespace MapGeneration
