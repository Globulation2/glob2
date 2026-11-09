#include "GenerationWork.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Contact.h"
#include "GenerationWorld.h"
#include "ResourceSemantics.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <functional>
#include <queue>
#include <utility>
namespace MapGeneration
{
int stepCost(const GenerationMap &map, int x, int y, const StepCosts &costs)
{
    const auto index=map.coordToIndex(x,y);
    const auto& terrain=map.terrainPropertiesAt(index);
    const int open=terrain.walkable ? costs.open : terrain.swimmable ? costs.water : -1;
    if (open<0) return open;
    if (map.getBuilding(x,y)!=NOGBID) return costs.building;
    if (map.resourceBlocksGround(index))
        return permanentResourceBarrier(map,index) ? costs.eternal : costs.clearable;
    return open;
}

namespace
{
// Native generation charges no work budget, so it may find the same costs another way.
// Shortest-path costs do not depend on the order equal-cost tiles leave the queue, so a
// bucket queue over the small integer step costs (Dial's algorithm) returns exactly the
// vector the binary heap does, without its logarithmic pops.
std::vector<int> bucketCostsFrom(const Torus &t, const std::vector<int> &step,
								 const std::vector<int> &sources, GridNeighbors neighbours, int maxStep)
{
	const int n = t.w * t.h;
	std::vector<int> cost(n, INT_MAX);
	std::vector<std::vector<int>> buckets(size_t(maxStep) + 1);
	size_t queued = 0;
	for (int i : sources)
		if (cost.at(i) > 0)
		{
			cost[i] = 0;
			buckets[0].push_back(i);
			++queued;
		}
	const int w = t.w;
	for (int c = 0; queued; ++c)
	{
		auto &bucket = buckets[size_t(c) % buckets.size()];
		// Entries pushed into this bucket while it drains (zero-cost steps) are taken too.
		for (size_t k = 0; k < bucket.size(); ++k)
		{
			const int i = bucket[k];
			--queued;
			if (c > cost[i])
				continue;
			const int x = t.remainderX(i), y = i / w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if ((!dx && !dy) || (neighbours == GridNeighbors::Cardinal && dx && dy))
						continue;
					const int m = t.at(x + dx, y + dy);
					const int s = step[m];
					if (s < 0 || c + s >= cost[m])
						continue;
					cost[m] = c + s;
					buckets[size_t(c + s) % buckets.size()].push_back(m);
					++queued;
				}
		}
		bucket.clear();
	}
	for (int &c : cost)
		if (c == INT_MAX)
			c = -1;
	return cost;
}
} // namespace

std::vector<int> costsFrom(const GenerationMap &map, const Torus &t,
						   const std::vector<int> &sources, const StepCosts &costs,
						   GridNeighbors neighbours)
{
	const int n = t.w * t.h;
	if (!generationWorkActive())
	{
		std::vector<int> step(n);
		int maxStep = 0;
		for (int i = 0; i < n; ++i)
			maxStep = std::max(maxStep, step[i] = stepCost(map, t.remainderX(i), i / t.w, costs));
		if (maxStep <= 4096)
			return bucketCostsFrom(t, step, sources, neighbours, maxStep);
	}
	std::vector<int> step(n), cost(n, INT_MAX);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		step.at(i) = stepCost(map, t.remainderX(i), i / t.w, costs);
	}
	using Entry = std::pair<int, int>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
	for (int i : sources)
	{
		::MapGeneration::generationCheckpoint();
		if (cost.at(i) > 0)
		{
			cost.at(i) = 0;
			heap.push({0, i});
		}
	}
	while (!heap.empty())
	{
		::MapGeneration::generationCheckpoint();
		const auto [c, i] = heap.top();
		heap.pop();
		if (c > cost.at(i))
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -1; dx <= 1; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				if ((!dx && !dy) || (neighbours == GridNeighbors::Cardinal && dx && dy))
					continue;
				const int m = t.at(x + dx, y + dy);
				if (step.at(m) < 0 || c + step.at(m) >= cost.at(m))
					continue;
				cost.at(m) = c + step.at(m);
				heap.push({cost.at(m), m});
			}
		}
	}
	for (int &c : cost)
	{
		::MapGeneration::generationCheckpoint();
		if (c == INT_MAX)
			c = -1;
	}
	return cost;
}

std::vector<int> ContactReport::nearestRival() const
{
	std::vector<int> nearest(cost.size(), -1);
	for (size_t from = 0; from < cost.size(); ++from)
	{
		::MapGeneration::generationCheckpoint();
		for (size_t to = 0; to < cost.size(); ++to)
		{
			::MapGeneration::generationCheckpoint();
			if (from != to && cost.at(from).at(to) >= 0 &&
				(nearest.at(from) < 0 || cost.at(from).at(to) < nearest.at(from)))
				nearest.at(from) = cost.at(from).at(to);
		}
	}
	return nearest;
}

int ContactReport::spread() const
{
	return costSpread(nearestRival());
}

ContactReport contactMatrix(const GenerationMap &map, int teams, const StepCosts &costs)
{
	const Torus t(map);
	const auto units = unitTilesByTeam(map, teams);
	ContactReport report;
	report.cost.assign(size_t(teams), std::vector<int>(size_t(teams), -1));
	for (int from = 0; from < teams; ++from)
	{
		::MapGeneration::generationCheckpoint();
		const std::vector<int> field = costsFrom(map, t, units.at(from), costs);
		for (int to = 0; to < teams; ++to)
		{
			::MapGeneration::generationCheckpoint();
			int best = from == to ? 0 : -1;
			if (from != to)
				for (int i : units.at(to))
				{
					::MapGeneration::generationCheckpoint();
					if (field.at(i) >= 0 && (best < 0 || field.at(i) < best))
						best = field.at(i);
				}
			report.cost.at(from).at(to) = best;
		}
	}
	return report;
}

std::vector<int> costsToTarget(const GenerationMap &map, int teams,
							   const std::vector<unsigned char> &targets, const StepCosts &costs)
{
	const Torus t(map);
	const auto units = unitTilesByTeam(map, teams);
	std::vector<int> result(size_t(teams), -1);
	for (int team = 0; team < teams; ++team)
	{
		::MapGeneration::generationCheckpoint();
		const std::vector<int> field = costsFrom(map, t, units.at(team), costs);
		for (size_t i = 0; i < targets.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (targets.at(i) && field.at(i) >= 0 &&
				(result.at(team) < 0 || field.at(i) < result.at(team)))
				result.at(team) = field.at(i);
		}
	}
	return result;
}

int costSpread(const std::vector<int> &costs)
{
	if (costs.empty())
		return 0;
	if (std::find(costs.begin(), costs.end(), -1) != costs.end())
		return -1;
	const auto [lo, hi] = std::minmax_element(costs.begin(), costs.end());
	return *hi - *lo;
}

std::string unevenCosts(const std::vector<int> &costs, int tolerance, const std::string &what)
{
	for (size_t team = 0; team < costs.size(); ++team)
	{
		::MapGeneration::generationCheckpoint();
		if (costs.at(team) < 0)
			return "Colony " + std::to_string(team) + " cannot reach " + what + ".";
	}
	const int spread = costSpread(costs);
	if (spread <= tolerance)
		return "";
	const size_t worst = size_t(std::max_element(costs.begin(), costs.end()) - costs.begin());
	return "Colony " + std::to_string(worst) + " is " + std::to_string(spread) + " further from " +
		   what + " than the nearest colony (tolerance " + std::to_string(tolerance) + ").";
}

std::vector<int> equalCostSites(const std::vector<std::vector<int>> &costs,
								const std::vector<unsigned char> &eligible, int target,
								int tolerance)
{
	const int teams = int(costs.size());
	std::vector<int> sites(size_t(teams), -1);
	for (int k = 0; k < teams; ++k)
	{
		::MapGeneration::generationCheckpoint();
		int bestGap = tolerance + 1;
		for (size_t i = 0; i < eligible.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			const int own = costs.at(k).at(i);
			if (!eligible.at(i) || own < 0)
				continue;
			const int gap = std::abs(own - target);
			if (gap >= bestGap)
				continue;
			bool nearest = true;
			for (int j = 0; j < teams && nearest; ++j)
			{
				::MapGeneration::generationCheckpoint();
				nearest = j == k || costs.at(j).at(i) < 0 || costs.at(j).at(i) > own;
			}
			if (nearest)
			{
				bestGap = gap;
				sites.at(k) = int(i);
			}
		}
	}
	return sites;
}
} // namespace MapGeneration
