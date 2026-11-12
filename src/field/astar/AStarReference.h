// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The order contract of AStar.h written as plainly as possible: a whole-torus
// search over a binary heap, ordered by f and then by the latest push. Tests
// require every optimized variant to reproduce its expansion sequence.
#include "AStar.h"
#include <queue>
#include <tuple>

namespace astar
{
template <class Domain>
Result referenceSearch(const Domain &domain, const Query &q, std::vector<std::uint32_t> *log = nullptr)
{
	const int W = q.width, H = q.height;
	const auto cells = std::size_t(W) * H;
	std::vector<std::uint32_t> g(cells, 0xffffffffu);
	std::vector<std::uint8_t> closed(cells, 0), parent(cells, 0);
	const auto torus = [](int a, int b, int size) {
		const int d = std::abs(a - b);
		return std::min(d, size - d);
	};
	const int tx = int(q.target % unsigned(W)), ty = int(q.target / unsigned(W));
	const auto goal = [&](std::uint32_t i) {
		return unsigned(torus(int(i % unsigned(W)), tx, W)) <= q.goalRadius &&
			   unsigned(torus(int(i / unsigned(W)), ty, H)) <= q.goalRadius;
	};
	const auto h = [&](std::uint32_t i) -> std::uint32_t {
		const int r = int(q.goalRadius);
		const unsigned a = unsigned(std::max(0, torus(int(i % unsigned(W)), tx, W) - r));
		const unsigned b = unsigned(std::max(0, torus(int(i / unsigned(W)), ty, H) - r));
		return (std::max(a, b) - std::min(a, b)) * q.floor.cardinal + std::min(a, b) * q.floor.diagonal;
	};
	// (f, -sequence, cell): the smallest f first, then the latest push.
	using Entry = std::tuple<std::uint32_t, std::int64_t, std::uint32_t>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
	std::int64_t sequence = 0;
	if (log)
		log->clear();
	if (!goal(q.start))
	{
		bool enterable = false;
		for (std::uint32_t i = 0; i < cells; ++i)
			enterable |= goal(i) && domain.entry(i, true) != 0;
		if (!enterable)
			return {};
	}
	g[q.start] = 0;
	open.emplace(h(q.start), -sequence++, q.start);
	Result result;
	bool limited = false;
	const std::uint32_t maximumCost = std::min(q.maximumCost, MaximumCost);
	while (!open.empty())
	{
		const auto p = std::get<2>(open.top());
		open.pop();
		if (closed[p])
			continue;
		if (result.expansions >= q.maximumExpansions)
		{
			result.status = Status::ExpansionLimit;
			return result;
		}
		++result.expansions;
		closed[p] = 1;
		if (log)
			log->push_back(p);
		if (goal(p))
		{
			result.status = Status::Found;
			result.cost = g[p];
			result.goal = p;
			for (auto at = p; at != q.start;)
			{
				const auto d = parent[at];
				if (q.wantPath)
					result.cells.push_back(at);
				result.firstDx = field::Surrounding[d].x;
				result.firstDy = field::Surrounding[d].y;
				const int x = int(at % unsigned(W)) - field::Surrounding[d].x, y = int(at / unsigned(W)) - field::Surrounding[d].y;
				at = std::uint32_t(((y + H) % H) * W + (x + W) % W);
			}
			std::reverse(result.cells.begin(), result.cells.end());
			return result;
		}
		const int x = int(p % unsigned(W)), y = int(p / unsigned(W));
		for (unsigned d = 0; d < 8; ++d)
		{
			const auto n = std::uint32_t(((y + field::Surrounding[d].y + H) % H) * W + (x + field::Surrounding[d].x + W) % W);
			if (closed[n])
				continue;
			const unsigned entry = domain.entry(n, goal(n));
			if (!entry)
				continue;
			const auto candidate = g[p] + (detail::diagonal(d) ? entry >> 8 : entry & 0xff);
			if (candidate > maximumCost)
			{
				limited = true;
				continue;
			}
			if (candidate >= g[n])
				continue;
			g[n] = candidate;
			parent[n] = std::uint8_t(d);
			open.emplace(candidate + h(n), -sequence++, n);
		}
	}
	result.status = limited ? Status::CostLimit : Status::Unreachable;
	return result;
}

// Many goals, as searchGoals defines them: terminal goals, the nearest-goal
// octile heuristic, and the same queue order.
template <class Domain>
std::vector<GoalResult> referenceGoals(const Domain &domain, const GoalQuery &q, std::span<const std::uint32_t> goals,
									   std::vector<std::uint32_t> *log = nullptr)
{
	const int W = q.width, H = q.height;
	const auto cells = std::size_t(W) * H;
	std::vector<std::uint32_t> g(cells, 0xffffffffu);
	std::vector<std::uint8_t> closed(cells, 0), parent(cells, 0);
	std::vector<std::uint32_t> live;
	for (auto goal : goals)
		if (goal == q.start || domain.entry(goal, true))
			live.push_back(goal);
	const auto isGoal = [&](std::uint32_t i) { return std::find(live.begin(), live.end(), i) != live.end(); };
	const auto h = [&](std::uint32_t i) {
		std::uint32_t best = 0xffffffffu;
		for (auto goal : live)
			best = std::min(best, std::uint32_t(detail::octile(
									  detail::torusDistance(int(i % unsigned(W)), int(goal % unsigned(W)), W),
									  detail::torusDistance(int(i / unsigned(W)), int(goal / unsigned(W)), H), q.floor)));
		return best;
	};
	using Entry = std::tuple<std::uint32_t, std::int64_t, std::uint32_t>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
	std::int64_t sequence = 0;
	if (log)
		log->clear();
	std::vector<GoalResult> results(goals.size());
	std::size_t remaining = 0;
	{
		std::vector<std::uint32_t> unique(live.begin(), live.end());
		std::sort(unique.begin(), unique.end());
		remaining = std::size_t(std::unique(unique.begin(), unique.end()) - unique.begin());
	}
	g[q.start] = 0;
	open.emplace(h(q.start), -sequence++, q.start);
	const std::uint32_t maximumCost = std::min(q.maximumCost, MaximumCost);
	while (!open.empty() && remaining)
	{
		const auto p = std::get<2>(open.top());
		open.pop();
		if (closed[p])
			continue;
		closed[p] = 1;
		if (log)
			log->push_back(p);
		if (isGoal(p))
		{
			int dx = 0, dy = 0;
			for (auto at = p; at != q.start;)
			{
				const auto d = parent[at];
				dx = field::Surrounding[d].x;
				dy = field::Surrounding[d].y;
				const int x = int(at % unsigned(W)) - dx, y = int(at / unsigned(W)) - dy;
				at = std::uint32_t(((y + H) % H) * W + (x + W) % W);
			}
			for (std::size_t i = 0; i < goals.size(); ++i)
				if (goals[i] == p)
					results[i] = {true, g[p], dx, dy};
			if (--remaining == 0)
				break;
			if (p != q.start)
				continue;
		}
		const int x = int(p % unsigned(W)), y = int(p / unsigned(W));
		for (unsigned d = 0; d < 8; ++d)
		{
			const auto n = std::uint32_t(((y + field::Surrounding[d].y + H) % H) * W + (x + field::Surrounding[d].x + W) % W);
			if (closed[n])
				continue;
			const unsigned entry = domain.entry(n, isGoal(n));
			if (!entry)
				continue;
			const auto candidate = g[p] + (detail::diagonal(d) ? entry >> 8 : entry & 0xff);
			if (candidate > maximumCost || candidate >= g[n])
				continue;
			g[n] = candidate;
			parent[n] = std::uint8_t(d);
			open.emplace(candidate + h(n), -sequence++, n);
		}
	}
	return results;
}
} // namespace astar
