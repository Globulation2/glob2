// SPDX-License-Identifier: GPL-3.0-or-later
#include "Towers.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Settlements.h"
#include "Walls.h"
#include <algorithm>
#include <cstdlib>
#include <utility>
namespace MapGeneration
{
namespace
{
// Whether a tower on footprint `a` reaches any tile of footprint `b` (both top-left tiles).
bool footprintsInRange(const Torus &t, int a, int b, int range)
{
	const int dx = t.offsetX(a % t.w, b % t.w), dy = t.offsetY(a / t.w, b / t.w);
	// The footprints are two wide: a tile of b at dx..dx+1 is within range of a's 0..1 when the gap
	// between the spans is at most the range.
	const auto gap = [](int d) { return d > 0 ? std::max(0, d - 1) : std::max(0, -d - 1); };
	return gap(dx) <= range && gap(dy) <= range;
}
} // namespace

TowerRequest startingTowerRequest(int level, int count, int pads, int spacing)
{
	TowerRequest request;
	request.range = kTowerRange[std::clamp(level - 1, 0, 2)];
	request.towers = level > 0 ? count : 0;
	request.pads = level > 0 ? pads : count + pads;
	request.spacing = spacing;
	return request;
}

TowerPlan chooseTowerSites(const Torus &t, const std::vector<int> &owner,
						   const std::vector<unsigned char> &buildable,
						   const std::vector<unsigned char> &target,
						   const std::vector<unsigned char> &keepOutOfRange, int colonies,
						   const TowerRequest &request)
{
	const int n = t.size(), r = request.range;
	TowerPlan plan;
	plan.towers.assign(colonies, {});
	plan.pads.assign(colonies, {});
	// Every colony's candidate footprints, best first: the other colonies' target tiles in range,
	// then the fewest of other colonies' protected tiles, then row order.
	std::vector<std::vector<std::pair<int, int>>> ranked(colonies);
	std::vector<unsigned char> protectedInRange(n, 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x, k = owner[i];
			if (k < 0 || k >= colonies)
				continue;
			bool fits = true;
			for (int fy = 0; fy < 2 && fits; ++fy)
				for (int fx = 0; fx < 2 && fits; ++fx)
				{
					const int j = t.at(x + fx, y + fy);
					fits = buildable[j] && owner[j] == k;
				}
			if (fits && request.against)
			{
				bool touches = false;
				for (int fy = -1; fy <= 2 && !touches; ++fy)
					for (int fx = -1; fx <= 2 && !touches; ++fx)
						touches = (fx < 0 || fx > 1 || fy < 0 || fy > 1) &&
								  (*request.against)[t.at(x + fx, y + fy)];
				fits = touches;
			}
			if (!fits)
				continue;
			int score = 0;
			for (int dy = -r; dy <= r + 1; ++dy)
				for (int dx = -r; dx <= r + 1; ++dx)
				{
					const int j = t.at(x + dx, y + dy);
					if (owner[j] >= 0 && owner[j] != k)
					{
						score += (target[j] != 0) * request.otherWeight;
						if (keepOutOfRange[j])
							protectedInRange[i] = 1;
					}
					else if (owner[j] == k)
						score += (target[j] != 0) * request.ownWeight;
				}
			ranked[k].push_back({-score, i});
		}
	for (auto &list : ranked)
		std::stable_sort(list.begin(), list.end());
	const auto spaced = [&](const std::vector<int> &sites, int i)
	{
		for (int s : sites)
			if (t.chebyshev(s % t.w, s / t.w, i % t.w, i / t.w) < request.spacing)
				return false;
		return true;
	};
	// Towers, a colony at a time in turn. Two passes: first only sites that reach someone, then any.
	std::vector<size_t> next(colonies, 0);
	for (int pass = 0; pass < 2; ++pass)
	{
		std::fill(next.begin(), next.end(), 0);
		for (bool progress = true; progress;)
		{
			progress = false;
			for (int k = 0; k < colonies; ++k)
			{
				if (int(plan.towers[k].size()) >= request.towers)
					continue;
				while (next[k] < ranked[k].size())
				{
					const auto [negative, i] = ranked[k][next[k]++];
					if ((pass == 0 && negative >= 0) || protectedInRange[i] ||
						!spaced(plan.towers[k], i))
						continue;
					plan.towers[k].push_back(i);
					progress = true;
					break;
				}
			}
		}
	}
	// A tower starts stocked only where no other colony's tower is within its range: towers that face
	// each other start empty, so the game does not open with them shooting each other down.
	plan.stocked.assign(colonies, {});
	for (int k = 0; k < colonies; ++k)
		for (int i : plan.towers[k])
		{
			bool alone = true;
			for (int other = 0; other < colonies && alone; ++other)
				if (other != k)
					for (int s : plan.towers[other])
						if (footprintsInRange(t, i, s, r))
						{
							alone = false;
							break;
						}
			plan.stocked[k].push_back(alone);
		}
	// Pads: the best remaining sites, spaced from the colony's towers and pads.
	for (int k = 0; k < colonies; ++k)
	{
		std::vector<int> taken = plan.towers[k];
		for (const auto &[negative, i] : ranked[k])
		{
			if (int(plan.pads[k].size()) >= request.pads)
				break;
			if (!spaced(taken, i))
				continue;
			plan.pads[k].push_back(i);
			taken.push_back(i);
		}
	}
	return plan;
}

int dropBlockingSites(const Torus &t, TowerPlan &plan, const std::vector<unsigned char> &open,
					  const std::vector<std::vector<int>> &sources,
					  const std::vector<std::vector<unsigned char>> &goals)
{
	int dropped = 0;
	const auto blocked = [&](size_t k)
	{
		std::vector<unsigned char> walk = open;
		const std::vector<unsigned char> footprints = towerFootprints(t, plan);
		for (int i = 0; i < t.size(); ++i)
			if (footprints[i])
				walk[i] = 0;
		const std::vector<int> steps = stepsFrom(t, tileMask(t, sources[k]), walk);
		for (int i = 0; i < t.size(); ++i)
			if (goals[k][i] && steps[i] >= 0)
				return false;
		return true;
	};
	for (size_t k = 0; k < plan.towers.size() && k < sources.size(); ++k)
		while (blocked(k) && (!plan.pads[k].empty() || !plan.towers[k].empty()))
		{
			if (!plan.pads[k].empty())
				plan.pads[k].pop_back();
			else
			{
				plan.towers[k].pop_back();
				plan.stocked[k].pop_back();
			}
			++dropped;
		}
	return dropped;
}

int evenTowerPlan(TowerPlan &plan)
{
	if (plan.towers.empty())
		return 0;
	size_t towers = plan.towers[0].size(), pads = plan.pads[0].size();
	for (size_t k = 0; k < plan.towers.size(); ++k)
	{
		towers = std::min(towers, plan.towers[k].size());
		pads = std::min(pads, plan.pads[k].size());
	}
	for (size_t k = 0; k < plan.towers.size(); ++k)
	{
		plan.towers[k].resize(towers);
		plan.stocked[k].resize(towers);
		plan.pads[k].resize(pads);
	}
	return int(towers);
}

std::vector<unsigned char> roomyGround(const Torus &t, const std::vector<unsigned char> &open,
									   int room)
{
	std::vector<unsigned char> closed(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		closed[i] = !open[i];
	const std::vector<int> fromClosed = stepsFrom(t, closed);
	std::vector<unsigned char> roomy(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		roomy[i] = open[i] && (fromClosed[i] < 0 || fromClosed[i] >= room);
	return roomy;
}

bool settleStartingTowers(Game &game, GenerationContext &context, TowerPlan &plan, int level,
						  bool everyColonyNeedsOne, const std::vector<unsigned char> *goal)
{
	const Map &map = game.map;
	const Torus t(map);
	if (goal)
	{
		// Every colony walks out from the ring of tiles round its swarm's 4x4 footprint.
		const int teams = int(plan.towers.size());
		std::vector<unsigned char> open(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
		{
			const int x = i % t.w, y = i / t.w;
			open[i] =
				!map.isWater(x, y) && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID;
		}
		std::vector<std::vector<int>> sources(teams);
		for (int k = 0; k < teams; ++k)
			for (int dy = -1; dy <= 4; ++dy)
				for (int dx = -1; dx <= 4; ++dx)
					if (dx < 0 || dy < 0 || dx > 3 || dy > 3)
						sources[k].push_back(t.at(context.bootX[k] + dx, context.bootY[k] + dy));
		std::vector<std::vector<unsigned char>> goals(teams);
		for (auto &g : goals)
		{
			g = *goal;
			for (int i = 0; i < t.size(); ++i)
				g[i] = g[i] && open[i];
		}
		dropBlockingSites(t, plan, open, sources, goals);
	}
	if (evenTowerPlan(plan) < (everyColonyNeedsOne ? 1 : 0))
	{
		context.detail = "a colony has no room for its towers";
		return false;
	}
	if (!raiseTowers(game, plan, std::clamp(level - 1, 0, 2)))
	{
		context.detail = "a tower site no longer fits";
		return false;
	}
	return true;
}

bool raiseTowers(Game &game, const TowerPlan &plan, int level)
{
	const Torus t(game.map);
	for (size_t k = 0; k < plan.towers.size(); ++k)
		for (size_t j = 0; j < plan.towers[k].size(); ++j)
		{
			const int site = plan.towers[k][j];
			std::vector<unsigned char> footprint(t.size(), 0);
			for (int dy = 0; dy < 2; ++dy)
				for (int dx = 0; dx < 2; ++dx)
					footprint[t.at(site % t.w + dx, site / t.w + dy)] = 1;
			// placeTower measures from the footprint's middle, so the site's middle finds it exactly.
			if (placeTower(game, int(k), level, site % t.w + 1, site / t.w + 1, 1, footprint,
						   plan.stocked[k][j] != 0) != site)
				return false;
		}
	return true;
}

std::vector<unsigned char> towerFootprints(const Torus &t, const TowerPlan &plan)
{
	std::vector<unsigned char> tiles(t.size(), 0);
	for (const auto *lists : {&plan.towers, &plan.pads})
		for (const auto &sites : *lists)
			for (int s : sites)
				for (int dy = 0; dy < 2; ++dy)
					for (int dx = 0; dx < 2; ++dx)
						tiles[t.at(s % t.w + dx, s / t.w + dy)] = 1;
	return tiles;
}
} // namespace MapGeneration
