#include "GenerationWork.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Towers.h"
#include "Morphology.h"
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
	const int dx = t.offsetX(t.remainderX(a), t.remainderX(b)), dy = t.offsetY(a / t.w, b / t.w);
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
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const int i = y * t.w + x, k = owner.at(i);
			if (k < 0 || k >= colonies)
				continue;
			bool fits = true;
			for (int fy = 0; fy < 2 && fits; ++fy)
			{
				::MapGeneration::generationCheckpoint();
				for (int fx = 0; fx < 2 && fits; ++fx)
				{
					::MapGeneration::generationCheckpoint();
					const int j = t.at(x + fx, y + fy);
					fits = buildable.at(j) && owner.at(j) == k;
				}
			}
			if (fits && request.against)
			{
				bool touches = false;
				for (int fy = -1; fy <= 2 && !touches; ++fy)
				{
					::MapGeneration::generationCheckpoint();
					for (int fx = -1; fx <= 2 && !touches; ++fx)
					{
						::MapGeneration::generationCheckpoint();
						touches = (fx < 0 || fx > 1 || fy < 0 || fy > 1) &&
								  (*request.against).at(t.at(x + fx, y + fy));
					}
				}
				fits = touches;
			}
			if (!fits)
				continue;
			int score = 0;
			for (int dy = -r; dy <= r + 1; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = -r; dx <= r + 1; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					const int j = t.at(x + dx, y + dy);
					if (owner.at(j) >= 0 && owner.at(j) != k)
					{
						score += (target.at(j) != 0) * request.otherWeight;
						if (keepOutOfRange.at(j))
							protectedInRange.at(i) = 1;
					}
					else if (owner.at(j) == k)
						score += (target.at(j) != 0) * request.ownWeight;
				}
			}
			ranked.at(k).push_back({-score, i});
		}
	}
	for (auto &list : ranked)
	{
		::MapGeneration::generationCheckpoint();
		std::stable_sort(list.begin(), list.end());
	}
	const auto spaced = [&](const std::vector<int> &sites, int i)
	{
		for (int s : sites)
		{
			::MapGeneration::generationCheckpoint();
			if (t.chebyshev(t.remainderX(s), s / t.w, t.remainderX(i), i / t.w) < request.spacing)
				return false;
		}
		return true;
	};
	// Towers, a colony at a time in turn. Two passes: first only sites that reach someone, then any.
	std::vector<size_t> next(colonies, 0);
	for (int pass = 0; pass < 2; ++pass)
	{
		::MapGeneration::generationCheckpoint();
		std::fill(next.begin(), next.end(), 0);
		for (bool progress = true; progress;)
		{
			::MapGeneration::generationCheckpoint();
			progress = false;
			for (int k = 0; k < colonies; ++k)
			{
				::MapGeneration::generationCheckpoint();
				if (int(plan.towers.at(k).size()) >= request.towers)
					continue;
				while (next.at(k) < ranked.at(k).size())
				{
					::MapGeneration::generationCheckpoint();
					const auto [negative, i] = ranked.at(k).at(next.at(k)++);
					if ((pass == 0 && negative >= 0) || protectedInRange.at(i) ||
						!spaced(plan.towers.at(k), i))
						continue;
					plan.towers.at(k).push_back(i);
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
	{
		::MapGeneration::generationCheckpoint();
		for (int i : plan.towers.at(k))
		{
			::MapGeneration::generationCheckpoint();
			bool alone = true;
			for (int other = 0; other < colonies && alone; ++other)
			{
				::MapGeneration::generationCheckpoint();
				if (other != k)
					for (int s : plan.towers.at(other))
					{
						::MapGeneration::generationCheckpoint();
						if (footprintsInRange(t, i, s, r))
						{
							alone = false;
							break;
						}
					}
			}
			plan.stocked.at(k).push_back(alone);
		}
	}
	// Pads: the best remaining sites, spaced from the colony's towers and pads.
	for (int k = 0; k < colonies; ++k)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<int> taken = plan.towers.at(k);
		for (const auto &[negative, i] : ranked.at(k))
		{
			::MapGeneration::generationCheckpoint();
			if (int(plan.pads.at(k).size()) >= request.pads)
				break;
			if (!spaced(taken, i))
				continue;
			plan.pads.at(k).push_back(i);
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
		{
			::MapGeneration::generationCheckpoint();
			if (footprints.at(i))
				walk.at(i) = 0;
		}
		const std::vector<int> steps = stepsFrom(t, tileMask(t, sources.at(k)), walk);
		for (int i = 0; i < t.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (goals.at(k).at(i) && steps.at(i) >= 0)
				return false;
		}
		return true;
	};
	for (size_t k = 0; k < plan.towers.size() && k < sources.size(); ++k)
	{
		::MapGeneration::generationCheckpoint();
		while (blocked(k) && (!plan.pads.at(k).empty() || !plan.towers.at(k).empty()))
		{
			::MapGeneration::generationCheckpoint();
			if (!plan.pads.at(k).empty())
				plan.pads.at(k).pop_back();
			else
			{
				plan.towers.at(k).pop_back();
				plan.stocked.at(k).pop_back();
			}
			++dropped;
		}
	}
	return dropped;
}

int evenTowerPlan(TowerPlan &plan)
{
	if (plan.towers.empty())
		return 0;
	size_t towers = plan.towers.at(0).size(), pads = plan.pads.at(0).size();
	for (size_t k = 0; k < plan.towers.size(); ++k)
	{
		::MapGeneration::generationCheckpoint();
		towers = std::min(towers, plan.towers.at(k).size());
		pads = std::min(pads, plan.pads.at(k).size());
	}
	for (size_t k = 0; k < plan.towers.size(); ++k)
	{
		::MapGeneration::generationCheckpoint();
		plan.towers.at(k).resize(towers);
		plan.stocked.at(k).resize(towers);
		plan.pads.at(k).resize(pads);
	}
	return int(towers);
}

std::vector<unsigned char> roomyGround(const Torus &t, const std::vector<unsigned char> &open,
									   int room)
{
	// `room` steps from anything closed is a whole square of radius room - 1 open round the tile.
	return erode(t, open, room - 1);
}

bool settleStartingTowers(Game &game, GenerationContext &context, TowerPlan &plan, int level,
						  bool everyColonyNeedsOne, const std::vector<unsigned char> *goal)
{
	const Map &map = game.map;
	const Torus t(map);
	if (context.telemetry.enabled())
		for (size_t k = 0; k < plan.towers.size(); ++k)
		{
			::MapGeneration::generationCheckpoint();
			context.telemetry.measure("towers.planned_towers", int(plan.towers.at(k).size()),
									  int(k));
			context.telemetry.measure("towers.planned_pads", int(plan.pads.at(k).size()), int(k));
		}
	if (goal)
	{
		// Every colony walks out from the ring of tiles round its swarm's 4x4 footprint.
		const int teams = int(plan.towers.size());
		std::vector<unsigned char> open(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			const int x = t.remainderX(i), y = i / t.w;
			open.at(i) = map.terrainPropertiesAt(x, y).walkable && !map.isResource(x, y) &&
						 map.getBuilding(x, y) == NOGBID;
		}
		std::vector<std::vector<int>> sources(teams);
		for (int k = 0; k < teams; ++k)
		{
			::MapGeneration::generationCheckpoint();
			for (int dy = -1; dy <= 4; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = -1; dx <= 4; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					if (dx < 0 || dy < 0 || dx > 3 || dy > 3)
						sources.at(k).push_back(t.at(context.bootX[k] + dx, context.bootY[k] + dy));
				}
			}
		}
		std::vector<std::vector<unsigned char>> goals(teams);
		for (auto &g : goals)
		{
			::MapGeneration::generationCheckpoint();
			g = *goal;
			for (int i = 0; i < t.size(); ++i)
			{
				::MapGeneration::generationCheckpoint();
				g.at(i) = g.at(i) && open.at(i);
			}
		}
		const int dropped = dropBlockingSites(t, plan, open, sources, goals);
		context.telemetry.measure("towers.blocking_sites_dropped", dropped);
		if (dropped)
			context.telemetry.fallback("towers.route_clearance", "removed obstructing sites");
	}
	const int towersPerColony = evenTowerPlan(plan);
	context.telemetry.measure("towers.equalized_towers_per_colony", towersPerColony);
	if (context.telemetry.enabled())
		for (size_t k = 0; k < plan.pads.size(); ++k)
		{
			::MapGeneration::generationCheckpoint();
			context.telemetry.measure("towers.equalized_pads", int(plan.pads.at(k).size()), int(k));
		}
	if (towersPerColony < (everyColonyNeedsOne ? 1 : 0))
	{
		context.detail = "a colony has no room for its towers";
		context.telemetry.choice("towers.outcome", "required tower missing");
		return false;
	}
	if (!raiseTowers(game, plan, std::clamp(level - 1, 0, 2)))
	{
		context.detail = "a tower site no longer fits";
		context.telemetry.choice("towers.outcome", "placement failed");
		return false;
	}
	context.telemetry.choice("towers.outcome", "placed");
	return true;
}

bool raiseTowers(Game &game, const TowerPlan &plan, int level)
{
	const Torus t(game.map);
	for (size_t k = 0; k < plan.towers.size(); ++k)
	{
		::MapGeneration::generationCheckpoint();
		for (size_t j = 0; j < plan.towers.at(k).size(); ++j)
		{
			::MapGeneration::generationCheckpoint();
			const int site = plan.towers.at(k).at(j);
			std::vector<unsigned char> footprint(t.size(), 0);
			for (int dy = 0; dy < 2; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = 0; dx < 2; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					footprint.at(t.at(t.remainderX(site) + dx, site / t.w + dy)) = 1;
				}
			}
			// placeTower measures from the footprint's middle, so the site's middle finds it exactly.
			if (placeTower(game, int(k), level, t.remainderX(site) + 1, site / t.w + 1, 1, footprint,
						   plan.stocked.at(k).at(j) != 0) != site)
				return false;
		}
	}
	return true;
}

std::vector<unsigned char> towerFootprints(const Torus &t, const TowerPlan &plan)
{
	std::vector<unsigned char> tiles(t.size(), 0);
	for (const auto *lists : {&plan.towers, &plan.pads})
	{
		::MapGeneration::generationCheckpoint();
		for (const auto &sites : *lists)
		{
			::MapGeneration::generationCheckpoint();
			for (int s : sites)
			{
				::MapGeneration::generationCheckpoint();
				for (int dy = 0; dy < 2; ++dy)
				{
					::MapGeneration::generationCheckpoint();
					for (int dx = 0; dx < 2; ++dx)
					{
						::MapGeneration::generationCheckpoint();
						tiles.at(t.at(t.remainderX(s) + dx, s / t.w + dy)) = 1;
					}
				}
			}
		}
	}
	return tiles;
}
} // namespace MapGeneration
