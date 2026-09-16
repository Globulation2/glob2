// SPDX-License-Identifier: GPL-3.0-or-later
#include "StartDiagnostics.h"
#include "FertilityField.h"
#include "Game.h"
#include "Grid.h"
#include "Map.h"
#include <algorithm>

namespace MapGeneration
{
namespace
{
constexpr int kOpeningCourt = 20;   ///< inside this, ground belongs to the first swarm
constexpr int kExpansionReach = 64; ///< beyond this a second base is another colony's problem
constexpr int kFeedingReach = 12;   ///< a second swarm needs grain about this close
constexpr int kContestedSlack = 6;  ///< a rival this much behind is still racing for the deposit
constexpr int kChokeWindow = 2;     ///< 5x5 window, as a radius
constexpr int kHarvestReach = 48;   ///< beyond this nothing is part of the opening harvest
constexpr int kHarvestSteps = 8;    ///< time to gather one load, in walking-step equivalents
constexpr int kInnSize = 2;         ///< a level-0 inn

/// Walking steps from a colony's starting workers to every tile it can reach. Workers rather
/// than the boot tile: the swarm occupies the boot tile and nobody walks out of it.
std::vector<int> walkFromWorkers(const Map &map, const std::vector<int> &workers)
{
	const Torus t(map);
	return stepsFrom(t, tileMask(t, workers), groundUnitTiles(map));
}

bool neighbourHolds(const Map &map, int x, int y, bool (*kind)(int))
{
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx)
			if ((dx || dy) &&
				kind(map.getResource(map.normalizeX(x + dx), map.normalizeY(y + dy)).type))
				return true;
	return false;
}
bool isWheat(int type) { return type == WHEAT; }
bool isWood(int type) { return type == WOOD; }
bool isStone(int type) { return type == STONE; }
bool isFruit(int type) { return type >= CHERRY && type <= PRUNE; }

/// Wheat that is a race: this colony reaches it, and some rival reaches it within the slack.
int nearestContestedWheat(const Map &map, const std::vector<std::vector<int>> &fields, int team)
{
	const int w = map.getW(), h = map.getH();
	int nearest = -1;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			if (map.getResource(x, y).type != WHEAT)
				continue;
			int mine = -1, rival = -1;
			for (int other = 0; other < int(fields.size()); ++other)
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						if (!dx && !dy)
							continue;
						const int d = fields[other][map.normalizeY(y + dy) * w + map.normalizeX(x + dx)];
						int &best = other == team ? mine : rival;
						if (d >= 0 && (best < 0 || d < best))
							best = d;
					}
			if (mine >= 0 && rival >= 0 && rival <= mine + kContestedSlack &&
				(nearest < 0 || mine < nearest))
				nearest = mine;
		}
	return nearest;
}

/// Walks the route to the nearest rival downhill through the colony's own field and returns the
/// narrowest 5x5 window on it, or -1 when no rival is reachable.
int chokeWidth(const Map &map, const std::vector<int> &dist,
			   const std::vector<std::vector<int>> &workers, int team)
{
	const int w = map.getW();
	int here = -1, reach = -1;
	for (int other = 0; other < int(workers.size()); ++other)
		if (other != team)
			for (int p : workers[other])
				if (dist[p] >= 0 && (reach < 0 || dist[p] < reach))
				{
					reach = dist[p];
					here = p;
				}
	if (here < 0)
		return -1;
	int narrowest = map.getW() * map.getH();
	while (dist[here] > 0)
	{
		const int x = here % w, y = here / w;
		int open = 0;
		for (int dy = -kChokeWindow; dy <= kChokeWindow; ++dy)
			for (int dx = -kChokeWindow; dx <= kChokeWindow; ++dx)
				open += dist[map.normalizeY(y + dy) * w + map.normalizeX(x + dx)] >= 0;
		narrowest = std::min(narrowest, open);
		int next = -1;
		for (int dy = -1; dy <= 1 && next < 0; ++dy)
			for (int dx = -1; dx <= 1 && next < 0; ++dx)
			{
				const int q = map.normalizeY(y + dy) * w + map.normalizeX(x + dx);
				if ((dx || dy) && dist[q] >= 0 && dist[q] < dist[here])
					next = q;
			}
		if (next < 0)
			break; // no downhill neighbour: stop rather than loop
		here = next;
	}
	return narrowest;
}
} // namespace

StartDiagnosticsReport diagnoseStarts(Game &game, int requestedTeams, const StartQualityScale &scale)
{
	StartDiagnosticsReport report;
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
	// Ground within feeding reach of standing wheat, marked once for the whole map rather than
	// re-searched around every candidate building site.
	std::vector<unsigned char> fedGround(w * h, 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			if (map.getResource(x, y).type == WHEAT)
				for (int dy = -kFeedingReach; dy <= kFeedingReach; ++dy)
					for (int dx = -kFeedingReach; dx <= kFeedingReach; ++dx)
						fedGround[map.normalizeY(y + dy) * w + map.normalizeX(x + dx)] = 1;

	std::vector<std::vector<int>> fields;
	fields.reserve(nbTeams);
	for (int team = 0; team < nbTeams; ++team)
		fields.push_back(walkFromWorkers(map, workers[team]));

	report.colonies.resize(nbTeams);
	for (int team = 0; team < nbTeams; ++team)
	{
		ColonyDiagnostics &colony = report.colonies[team];
		const std::vector<int> &dist = fields[team];
		for (int p = 0; p < w * h; ++p)
		{
			const int d = dist[p];
			if (d < 0)
				continue;
			const int x = p % w, y = p / w;
			if (d <= scale.catchmentSteps && map.isGrass(p) && map.tiles[p].canResourcesGrow &&
				fertility.at(x, y) > 0)
			{
				const double chance = double(fertility.at(x, y)) / Fertility::kScale;
				if (neighbourHolds(map, x, y, isWheat))
					colony.renewableWheat += chance;
				if (neighbourHolds(map, x, y, isWood))
				{
					colony.encroachingWood += chance;
					if (map.isFreeForBuilding(x, y, 4, 4))
						++colony.threatenedBuildSites;
				}
			}
			if (d <= kHarvestReach)
			{
				const double trip = 1.0 / (2.0 * d + kHarvestSteps);
				colony.wheatThroughput += neighbourHolds(map, x, y, isWheat) ? trip : 0.0;
				colony.woodThroughput += neighbourHolds(map, x, y, isWood) ? trip : 0.0;
				colony.stoneThroughput += neighbourHolds(map, x, y, isStone) ? trip : 0.0;
				colony.fruitThroughput += neighbourHolds(map, x, y, isFruit) ? trip : 0.0;
				if (map.isFreeForBuilding(x, y, kInnSize, kInnSize))
				{
					bool fed = false;
					for (int dy = -1; dy <= kInnSize && !fed; ++dy)
						for (int dx = -1; dx <= kInnSize && !fed; ++dx)
							fed = (dx < 0 || dy < 0 || dx >= kInnSize || dy >= kInnSize) &&
								  map.getResource(map.normalizeX(x + dx), map.normalizeY(y + dy))
										  .type == WHEAT;
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
				++colony.secondSwarmSites;
		}
		colony.contestedWheatDistance = nearestContestedWheat(map, fields, team);
		colony.chokeWidth = chokeWidth(map, dist, workers, team);
	}
	report.measured = true;
	return report;
}

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
					const int d = dist[map.normalizeY(y + dy) * w + map.normalizeX(x + dx)];
					if ((dx || dy) && d >= 0 && (nearest < 0 || d < nearest))
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
} // namespace MapGeneration
