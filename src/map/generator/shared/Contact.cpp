// SPDX-License-Identifier: GPL-3.0-or-later
#include "Contact.h"
#include "Map.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <functional>
#include <queue>
#include <utility>
namespace MapGeneration
{
int stepCost(const Map &map, int x, int y, const StepCosts &costs)
{
	if (map.isWater(x, y))
		return costs.water;
	if (map.getBuilding(x, y) != NOGBID)
		return costs.building;
	if (map.isResource(x, y))
	{
		const int type = map.getResource(x, y).type;
		return type == STONE || (type >= CHERRY && type <= CHERRY + 2) ? costs.eternal
																	   : costs.clearable;
	}
	return costs.open;
}

std::vector<int> costsFrom(const Map &map, const Torus &t, const std::vector<int> &sources,
						   const StepCosts &costs, GridNeighbors neighbours)
{
	const int n = t.w * t.h;
	std::vector<int> step(n), cost(n, INT_MAX);
	for (int i = 0; i < n; ++i)
		step[i] = stepCost(map, i % t.w, i / t.w, costs);
	using Entry = std::pair<int, int>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
	for (int i : sources)
		if (cost[i] > 0)
		{
			cost[i] = 0;
			heap.push({0, i});
		}
	while (!heap.empty())
	{
		const auto [c, i] = heap.top();
		heap.pop();
		if (c > cost[i])
			continue;
		const int x = i % t.w, y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if ((!dx && !dy) || (neighbours == GridNeighbors::Cardinal && dx && dy))
					continue;
				const int m = t.at(x + dx, y + dy);
				if (step[m] < 0 || c + step[m] >= cost[m])
					continue;
				cost[m] = c + step[m];
				heap.push({cost[m], m});
			}
	}
	for (int &c : cost)
		if (c == INT_MAX)
			c = -1;
	return cost;
}

std::vector<int> ContactReport::nearestRival() const
{
	std::vector<int> nearest(cost.size(), -1);
	for (size_t from = 0; from < cost.size(); ++from)
		for (size_t to = 0; to < cost.size(); ++to)
			if (from != to && cost[from][to] >= 0 &&
				(nearest[from] < 0 || cost[from][to] < nearest[from]))
				nearest[from] = cost[from][to];
	return nearest;
}

int ContactReport::spread() const
{
	return costSpread(nearestRival());
}

ContactReport contactMatrix(const Map &map, int teams, const StepCosts &costs)
{
	const Torus t(map);
	const auto units = unitTilesByTeam(map, teams);
	ContactReport report;
	report.cost.assign(size_t(teams), std::vector<int>(size_t(teams), -1));
	for (int from = 0; from < teams; ++from)
	{
		const std::vector<int> field = costsFrom(map, t, units[from], costs);
		for (int to = 0; to < teams; ++to)
		{
			int best = from == to ? 0 : -1;
			if (from != to)
				for (int i : units[to])
					if (field[i] >= 0 && (best < 0 || field[i] < best))
						best = field[i];
			report.cost[from][to] = best;
		}
	}
	return report;
}

std::vector<int> costsToTarget(const Map &map, int teams, const std::vector<unsigned char> &targets,
							   const StepCosts &costs)
{
	const Torus t(map);
	const auto units = unitTilesByTeam(map, teams);
	std::vector<int> result(size_t(teams), -1);
	for (int team = 0; team < teams; ++team)
	{
		const std::vector<int> field = costsFrom(map, t, units[team], costs);
		for (size_t i = 0; i < targets.size(); ++i)
			if (targets[i] && field[i] >= 0 && (result[team] < 0 || field[i] < result[team]))
				result[team] = field[i];
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
		if (costs[team] < 0)
			return "Colony " + std::to_string(team) + " cannot reach " + what + ".";
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
		int bestGap = tolerance + 1;
		for (size_t i = 0; i < eligible.size(); ++i)
		{
			const int own = costs[k][i];
			if (!eligible[i] || own < 0)
				continue;
			const int gap = std::abs(own - target);
			if (gap >= bestGap)
				continue;
			bool nearest = true;
			for (int j = 0; j < teams && nearest; ++j)
				nearest = j == k || costs[j][i] < 0 || costs[j][i] > own;
			if (nearest)
			{
				bestGap = gap;
				sites[k] = int(i);
			}
		}
	}
	return sites;
}
} // namespace MapGeneration
