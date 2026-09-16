// SPDX-License-Identifier: GPL-3.0-or-later
#include "StartProbe.h"
#include "FertilityField.h"
#include "Game.h"
#include "Grid.h"
#include "Map.h"
#include <algorithm>
#include <cmath>

namespace MapGeneration
{
namespace
{
constexpr int kOpeningCourt = 20;  ///< inside this, ground belongs to the first swarm
constexpr int kExpansionReach = 64; ///< beyond this a second base is a different colony's problem
constexpr int kFeedingReach = 12;  ///< a second swarm needs grain about this close
constexpr int kContestedSlack = 6; ///< a rival this much behind is still racing for the deposit
constexpr int kChokeWindow = 2;    ///< 5x5 window, as a radius
constexpr int kHarvestReach = 48;  ///< beyond this nothing is part of the opening harvest
constexpr int kHarvestSteps = 8;   ///< time to gather one load, in walking-step equivalents
constexpr int kInnSize = 2;        ///< a level-0 inn

/// Walking steps from a colony's starting workers to every tile it can reach. Workers rather
/// than the boot tile: the swarm occupies the boot tile and nobody walks out of it.
std::vector<int> walkFromWorkers(const Map &map, const std::vector<int> &workers)
{
	const Torus t(map);
	return stepsFrom(t, tileMask(t, workers), groundUnitTiles(map));
}

bool neighbourHolds(const Map &map, int x, int y, int resource)
{
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx)
		{
			if (!dx && !dy)
				continue;
			const Resource &r = map.getResource(map.normalizeX(x + dx), map.normalizeY(y + dy));
			if (r.type == resource)
				return true;
		}
	return false;
}
} // namespace

int removeResourceNear(Game &game, int team, int resource, int radius)
{
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();
	if (team < 0 || team >= game.teamsCount())
		return -1;
	const auto workers = unitTilesByTeam(map, team + 1);
	if (int(workers.size()) <= team || workers[team].empty())
		return -1;
	const std::vector<int> dist = walkFromWorkers(map, workers[team]);
	int removed = 0;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			if (map.getResource(x, y).type != resource)
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
			if (nearest < 0 || nearest > radius)
				continue;
			// Clear in place: setResource/decResource draw from the synchronised random stream,
			// and a perturbation must leave every other tile and the game's RNG untouched.
			map.getTile(x, y).resource.clear();
			++removed;
		}
	return removed;
}

StartProbeReport probeStarts(Game &game, int requestedTeams, const StartQualityScale &scale)
{
	StartProbeReport report;
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();
	const int nbTeams = std::min(game.teamsCount(), requestedTeams);
	if (nbTeams <= 0)
		return report;
	const std::vector<std::vector<int>> workers = unitTilesByTeam(map, nbTeams);
	for (const auto &team : workers)
		if (team.empty())
			return report;

	const Fertility::Field fertility = Fertility::forMap(map);
	// Ground within feeding reach of standing wheat, marked once for the whole map rather
	// than re-searched around every candidate building site.
	std::vector<unsigned char> fedGround(w * h, 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			if (map.getResource(x, y).type != WHEAT)
				continue;
			for (int dy = -kFeedingReach; dy <= kFeedingReach; ++dy)
				for (int dx = -kFeedingReach; dx <= kFeedingReach; ++dx)
					fedGround[map.normalizeY(y + dy) * w + map.normalizeX(x + dx)] = 1;
		}

	std::vector<std::vector<int>> fields;
	fields.reserve(nbTeams);
	for (int team = 0; team < nbTeams; ++team)
		fields.push_back(walkFromWorkers(map, workers[team]));

	report.colonies.resize(nbTeams);
	for (int team = 0; team < nbTeams; ++team)
	{
		ColonyProbe &colony = report.colonies[team];
		const std::vector<int> &dist = fields[team];
		for (int p = 0; p < w * h; ++p)
		{
			const int d = dist[p];
			if (d < 0)
				continue;
			const int x = p % w, y = p / w;
			if (d <= scale.catchmentSteps)
			{
				for (int r = 0; r < MAX_RESOURCES; ++r)
					if (neighbourHolds(map, x, y, r))
						++colony.harvestFrontage[r];
				// Regrowth capacity: grass that is allowed to take a crop, has a nonzero
				// growth chance, and already touches grain for the crop to spread from.
				const bool growable =
					map.isGrass(p) && map.tiles[p].canResourcesGrow && fertility.at(x, y) > 0;
				const double chance = double(fertility.at(x, y)) / Fertility::kScale;
				if (growable && neighbourHolds(map, x, y, WHEAT))
				{
					colony.renewableWheat += chance;
					colony.encroachingWheat += chance;
				}
				// The forest's own front, measured the same way. Ground the trees can take is
				// ground this colony cannot build on later.
				if (growable && neighbourHolds(map, x, y, WOOD))
				{
					colony.encroachingWood += chance;
					++colony.woodFrontTiles;
					if (map.isFreeForBuilding(x, y, 4, 4))
						++colony.threatenedBuildSites;
				}
			}
			if (d <= kHarvestReach)
			{
				const double trip = 1.0 / (2.0 * d + kHarvestSteps);
				if (neighbourHolds(map, x, y, WHEAT))
					colony.harvestThroughput += trip;
				if (neighbourHolds(map, x, y, WOOD))
					colony.woodThroughput += trip;
				// A 2x2 inn anchored here, with grain on the ring of tiles around it.
				if (map.isFreeForBuilding(x, y, kInnSize, kInnSize))
				{
					bool fed = false;
					for (int dy = -1; dy <= kInnSize && !fed; ++dy)
						for (int dx = -1; dx <= kInnSize && !fed; ++dx)
						{
							const bool ring = dx < 0 || dy < 0 || dx >= kInnSize || dy >= kInnSize;
							if (ring && map.getResource(map.normalizeX(x + dx),
														map.normalizeY(y + dy)).type == WHEAT)
								fed = true;
						}
					if (fed)
					{
						++colony.innNextToWheatSites;
						if (colony.innNextToWheatDistance < 0 || d < colony.innNextToWheatDistance)
							colony.innNextToWheatDistance = d;
					}
				}
			}
			if (d >= kOpeningCourt && d <= kExpansionReach && fedGround[p] &&
				map.isFreeForBuilding(x, y, 4, 4))
			{
				++colony.secondSwarmSites;
				if (colony.secondSwarmDistance < 0 || d < colony.secondSwarmDistance)
					colony.secondSwarmDistance = d;
			}
		}

		// Wheat that is genuinely a race: this colony's approach cost against the best rival's.
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				if (map.getResource(x, y).type != WHEAT)
					continue;
				int mine = -1, best = -1;
				for (int other = 0; other < nbTeams; ++other)
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							if (!dx && !dy)
								continue;
							const int d = fields[other][map.normalizeY(y + dy) * w +
													   map.normalizeX(x + dx)];
							if (d < 0)
								continue;
							int &target = other == team ? mine : best;
							if (target < 0 || d < target)
								target = d;
						}
				if (mine < 0 || best < 0 || best > mine + kContestedSlack)
					continue; // nobody else is near it: this is not contested ground
				++colony.contestedWheatDeposits;
				if (colony.contestedWheatDistance < 0 || mine < colony.contestedWheatDistance)
					colony.contestedWheatDistance = mine;
			}

		// Walk the route to the nearest rival downhill through this colony's own field, and
		// take the narrowest window on it.
		int target = -1, targetDistance = -1;
		for (int other = 0; other < nbTeams; ++other)
		{
			if (other == team)
				continue;
			for (int p : workers[other])
				if (dist[p] >= 0 && (targetDistance < 0 || dist[p] < targetDistance))
				{
					targetDistance = dist[p];
					target = p;
				}
		}
		if (target >= 0)
		{
			int here = target;
			colony.chokeWidth = w * h;
			while (dist[here] > 0)
			{
				const int x = here % w, y = here / w;
				int open = 0;
				for (int dy = -kChokeWindow; dy <= kChokeWindow; ++dy)
					for (int dx = -kChokeWindow; dx <= kChokeWindow; ++dx)
						open += dist[map.normalizeY(y + dy) * w + map.normalizeX(x + dx)] >= 0;
				colony.chokeWidth = std::min(colony.chokeWidth, open);
				int next = -1;
				for (int dy = -1; dy <= 1 && next < 0; ++dy)
					for (int dx = -1; dx <= 1 && next < 0; ++dx)
					{
						if (!dx && !dy)
							continue;
						const int q = map.normalizeY(y + dy) * w + map.normalizeX(x + dx);
						if (dist[q] >= 0 && dist[q] < dist[here])
							next = q;
					}
				if (next < 0)
					break; // no downhill neighbour: stop rather than loop
				here = next;
			}
		}
	}
	report.measured = true;
	return report;
}
} // namespace MapGeneration
