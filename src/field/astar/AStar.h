// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Point-to-point search on a power-of-two toroidal grid of 8-connected cells.
//
// The engine knows nothing about maps. A Domain supplies each cell's entry costs
// on first touch: entry(worldIndex, goal) returns cardinal | diagonal << 8, or 0
// for a cell that cannot be entered. Costs are positive and below 256, the same
// bound the gradient queue imposes on every terrain rule.
//
// Order contract (simulation state depends on it; every variant must agree):
//  * f = g + h, where h is the octile distance to the goal set at the per-search
//    step floors (Floor), clamped so the heuristic is consistent.
//  * The open set is a ring of f buckets. The lowest f pops first; within one f
//    the most recently pushed cell pops first (LIFO), which prefers deeper cells.
//  * A popped cell is expanded in field::Surrounding order, and each improved
//    neighbour is pushed in that order. A cell closes on its first pop.
//  * Expansions count non-stale pops; the budget is checked before the goal test.
// The window used to hold the search is an implementation detail: a search that
// reaches the edge of its window restarts in a larger one and so reproduces the
// same expansion sequence. Searches sample the Domain lazily, once per touched cell.
#include "../Grid.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace astar
{
enum class Status : std::uint8_t
{
	Found,
	Unreachable,
	CostLimit,
	ExpansionLimit
};

// Lower bounds of one cardinal and one diagonal step anywhere in the search.
// {0, 0} turns the search into Dijkstra.
struct Floor
{
	unsigned cardinal = 0, diagonal = 0;
};
// The tightest consistent octile pair below the given per-map minima.
constexpr Floor consistentFloor(unsigned cardinal, unsigned diagonal)
{
	cardinal = std::min(cardinal, diagonal);
	diagonal = std::min(diagonal, 2 * cardinal);
	return {cardinal, diagonal};
}

constexpr std::uint32_t MaximumCost = 65533;

struct Query
{
	int width = 0, height = 0; // powers of two
	std::uint32_t start = 0;   // row-major world indices
	std::uint32_t target = 0;
	// Goal cells: toroidal Chebyshev distance to target at most goalRadius.
	unsigned goalRadius = 0;
	std::uint32_t maximumCost = MaximumCost;
	std::uint32_t maximumExpansions = std::numeric_limits<std::uint32_t>::max();
	Floor floor;
	bool wantPath = false;
};

struct Result
{
	Status status = Status::Unreachable;
	std::uint32_t cost = 0, expansions = 0;
	std::uint32_t goal = 0; // the reached cell, when Found
	int firstDx = 0, firstDy = 0;
	std::vector<std::uint32_t> cells; // excludes start, includes goal; only with wantPath
	unsigned restarts = 0;            // window regrowths, for benchmarks
};

// Many goals at once: one search resolves every goal as separate searches with
// the same passability would. Goals are terminal: a goal is never expanded, so
// no path passes through one goal to another. The heuristic is the octile
// distance to the nearest goal (consistent, being a minimum of consistent
// bounds); the search ends once every goal is closed.
struct GoalQuery
{
	int width = 0, height = 0;
	std::uint32_t start = 0;
	std::uint32_t maximumCost = MaximumCost;
	Floor floor;
};
struct GoalResult
{
	bool reached = false;
	std::uint32_t cost = 0;
	int firstDx = 0, firstDy = 0;
};

namespace detail
{
enum : std::uint8_t
{
	Direction = 0x07,
	Goal = 0x08,
	Blocked = 0x10,
	Edge = 0x20,
	Closed = 0x80,
};
constexpr bool diagonal(unsigned d) { return field::Surrounding[d].x != 0 && field::Surrounding[d].y != 0; }
constexpr unsigned RingBits = 9; // f grows by at most 255 + 255 per expansion
constexpr unsigned Ring = 1u << RingBits;

inline unsigned torusDistance(int a, int b, int size)
{
	const unsigned d = unsigned(a - b) & unsigned(size - 1);
	return std::min(d, unsigned(size) - d);
}
inline unsigned octile(unsigned a, unsigned b, Floor floor)
{
	const unsigned lo = std::min(a, b), hi = std::max(a, b);
	return (hi - lo) * floor.cardinal + lo * floor.diagonal;
}

// Buckets of f, popped lowest first and LIFO within one value.
class BucketQueue
{
	std::array<std::vector<std::uint32_t>, Ring> buckets;
	std::array<std::uint64_t, Ring / 64> occupied{};
	std::uint32_t base = 0;
	std::size_t count = 0;

  public:
	void reset(std::uint32_t f)
	{
		for (unsigned w = 0; w < occupied.size(); ++w)
			for (auto bits = occupied[w]; bits; bits &= bits - 1)
				buckets[w * 64 + std::countr_zero(bits)].clear();
		occupied = {};
		base = f;
		count = 0;
	}
	bool empty() const { return count == 0; }
	void push(std::uint32_t f, std::uint32_t cell)
	{
		assert(f >= base && f - base < Ring);
		const auto slot = f & (Ring - 1);
		buckets[slot].push_back(cell);
		occupied[slot >> 6] |= std::uint64_t(1) << (slot & 63);
		++count;
	}
	// Precondition: !empty().
	std::uint32_t pop()
	{
		auto slot = base & (Ring - 1);
		if (!(occupied[slot >> 6] >> (slot & 63) & 1))
		{
			// Scan the ring from base for the next occupied bucket.
			unsigned word = slot >> 6;
			auto bits = occupied[word] & (~std::uint64_t(0) << (slot & 63));
			unsigned steps = 0;
			while (!bits)
			{
				word = (word + 1) & (occupied.size() - 1);
				bits = occupied[word];
				++steps;
				assert(steps <= occupied.size());
			}
			const auto next = word * 64 + unsigned(std::countr_zero(bits));
			base += (next - slot) & (Ring - 1);
			slot = next;
		}
		auto &bucket = buckets[slot];
		const auto cell = bucket.back();
		bucket.pop_back();
		if (bucket.empty())
			occupied[slot >> 6] &= ~(std::uint64_t(1) << (slot & 63));
		--count;
		return cell;
	}
};
} // namespace detail

// Reusable scratch for one thread. Results never depend on what a workspace
// held before: every cell a search reads is initialized by that search.
class Workspace
{
  public:
	template <class Domain> Result search(const Domain &domain, const Query &query);
	template <class Domain>
	void searchGoals(const Domain &domain, const GoalQuery &query, std::span<const std::uint32_t> goals,
					 std::span<GoalResult> results);

	// Benchmarks and tests may pin the first window side; 0 sizes it from the query.
	unsigned forcedWindow = 0;
	// The side of the window the last search finished in; 0 for the whole map.
	int windowSide() const { return torus ? 0 : side; }
	// Tests record the world index of every expansion of the final attempt.
	std::vector<std::uint32_t> *expansionLog = nullptr;

  private:
	// Plane geometry. Strides are powers of two. A bounded window keeps an Edge
	// halo in plane row/column 0 and side - 1, so neighbours need no wrap or
	// bounds checks; a torus window is the whole map, wrapped with masks.
	bool torus = false;
	int side = 0, stride = 0, rows = 0;
	int mapW = 0, mapH = 0, originX = 0, originY = 0;
	unsigned wShift = 0, strideShift = 0;
	std::array<int, 8> offsets{};

	// Each cell is sampled from the Domain on first touch in a search; mark
	// holds the search stamp of that touch. Each geometry keeps its own planes,
	// so searches of different sizes alternate without reallocating.
	struct Planes
	{
		int width = 0, height = 0, side = -1;
		std::vector<std::uint16_t> g;
		std::vector<std::uint8_t> state, mark;
		std::vector<std::uint16_t> steps; // cardinal | diagonal << 8
		std::uint8_t stamp = 0;
	};
	std::array<Planes, 16> cached{}; // by log2(side); slot 0 is the whole map
	Planes *planes = &cached[0];

	detail::BucketQueue queue;
	int targetX = 0, targetY = 0;
	unsigned goalRadius = 0;
	std::vector<std::uint32_t> goalCells; // sorted world indices for searchGoals
	std::vector<std::array<int, 2>> goalPoints;

	int planeX(std::uint32_t p) const { return int(p & unsigned(stride - 1)); }
	int planeY(std::uint32_t p) const { return int(p >> strideShift); }
	int worldX(int px) const { return (originX + px) & (mapW - 1); }
	int worldY(int py) const { return (originY + py) & (mapH - 1); }
	std::uint32_t worldIndex(std::uint32_t p) const
	{
		return std::uint32_t((worldY(planeY(p)) << wShift) + worldX(planeX(p)));
	}
	std::uint32_t planeOf(std::uint32_t world) const
	{
		const int x = int(world & unsigned(mapW - 1)), y = int(world >> wShift);
		return std::uint32_t((((y - originY) & (mapH - 1)) << strideShift) + ((x - originX) & (mapW - 1)));
	}
	template <bool Torus> std::uint32_t step(std::uint32_t p, unsigned d) const
	{
		if constexpr (!Torus)
			return std::uint32_t(int(p) + offsets[d]);
		else
			return std::uint32_t((((planeY(p) + field::Surrounding[d].y) & (rows - 1)) << strideShift) +
								 ((planeX(p) + field::Surrounding[d].x) & (stride - 1)));
	}
	std::uint32_t neighbour(std::uint32_t p, unsigned d) const { return torus ? step<true>(p, d) : step<false>(p, d); }
	// Visits the eight neighbours in field::Surrounding order with constant directions.
	template <class Visit> static void forEachDirection(Visit &&visit)
	{
		[&]<unsigned... D>(std::integer_sequence<unsigned, D...>) {
			(visit(std::integral_constant<unsigned, D>{}), ...);
		}(std::make_integer_sequence<unsigned, 8>{});
	}

	void layout(int width, int height, std::uint32_t start, int requestedSide);
	template <class Domain, class GoalTest> void sample(const Domain &domain, std::uint32_t p, GoalTest &isGoal);
	void trace(std::uint32_t p, std::uint32_t startPlane, Result *result, int *dx, int *dy) const;
	static int firstSide(int width, int height, std::uint32_t reach);

	// One attempt in the current window; false when the search met the halo.
	template <bool Torus, class Domain, class Heuristic, class GoalTest, class OnGoal>
	bool run(const Domain &domain, std::uint32_t startPlane, std::uint32_t maximumCost, std::uint32_t maximumExpansions,
			 Heuristic &&h, GoalTest &isGoal, OnGoal &&onGoal, Result &result);
};

inline int Workspace::firstSide(int width, int height, std::uint32_t reach)
{
	// Hold every cell within reach of the start, plus the halo.
	const auto wanted = std::bit_ceil(std::max<std::uint32_t>(16, 2 * std::min<std::uint32_t>(reach, 4096) + 4));
	return int(std::min<std::uint32_t>(wanted, std::uint32_t(std::min(width, height))));
}

inline void Workspace::layout(int width, int height, std::uint32_t start, int requestedSide)
{
	assert(std::has_single_bit(unsigned(width)) && std::has_single_bit(unsigned(height)));
	torus = requestedSide >= width || requestedSide >= height;
	side = torus ? 0 : requestedSide;
	mapW = width;
	mapH = height;
	wShift = unsigned(std::countr_zero(unsigned(width)));
	const int sx = int(start & unsigned(width - 1)), sy = int(start >> wShift);
	if (torus)
	{
		stride = width;
		rows = height;
		originX = originY = 0;
	}
	else
	{
		// Interior plane cells 1..side-2 hold world cells origin+1 .. origin+side-2.
		stride = rows = side;
		originX = sx - side / 2;
		originY = sy - side / 2;
	}
	strideShift = unsigned(std::countr_zero(unsigned(stride)));
	for (unsigned d = 0; d < 8; ++d)
		offsets[d] = field::Surrounding[d].y * stride + field::Surrounding[d].x;
	const unsigned slot = torus ? 0 : strideShift;
	assert(slot < cached.size());
	planes = &cached[slot];
	auto &P = *planes;
	if (P.width != width || P.height != height || P.side != side)
	{
		P.width = width;
		P.height = height;
		P.side = side;
		const auto cells = std::size_t(stride) * rows;
		P.g.assign(cells, 0xffff);
		P.state.assign(cells, 0);
		P.steps.assign(cells, 0);
		P.mark.assign(cells, 0);
		P.stamp = 0;
		if (!torus)
			for (int py = 0; py < rows; ++py)
				for (int px = 0; px < stride; ++px)
					if (px == 0 || py == 0 || px == stride - 1 || py == rows - 1)
						P.state[std::size_t(py) * stride + px] = detail::Edge | detail::Blocked;
	}
	if (++P.stamp == 0)
	{
		std::fill(P.mark.begin(), P.mark.end(), 0);
		P.stamp = 1;
	}
}

template <class Domain, class GoalTest>
inline void Workspace::sample(const Domain &domain, std::uint32_t p, GoalTest &isGoal)
{
	auto &P = *planes;
	P.mark[p] = P.stamp;
	if (!torus && (P.state[p] & detail::Edge))
		return; // the halo is fixed for the window's lifetime
	const int px = planeX(p), py = planeY(p);
	const auto world = std::uint32_t((worldY(py) << wShift) + worldX(px));
	const bool goal = isGoal(px, py, world);
	const unsigned entry = domain.entry(world, goal);
	assert(!entry || ((entry & 0xff) && (entry >> 8)));
	P.g[p] = 0xffff;
	P.steps[p] = std::uint16_t(entry);
	P.state[p] = std::uint8_t((entry ? 0 : detail::Blocked) | (goal ? detail::Goal : 0));
}

inline void Workspace::trace(std::uint32_t p, std::uint32_t startPlane, Result *result, int *dx, int *dy) const
{
	*dx = *dy = 0;
	if (result)
		result->cells.clear();
	while (p != startPlane)
	{
		const unsigned d = planes->state[p] & detail::Direction;
		if (result)
			result->cells.push_back(worldIndex(p));
		*dx = field::Surrounding[d].x;
		*dy = field::Surrounding[d].y;
		p = neighbour(p, 7 - d); // Surrounding is symmetric: 7 - d is the opposite step
	}
	if (result)
		std::reverse(result->cells.begin(), result->cells.end());
}

template <bool Torus, class Domain, class Heuristic, class GoalTest, class OnGoal>
inline bool Workspace::run(const Domain &domain, std::uint32_t startPlane, std::uint32_t maximumCost,
						   std::uint32_t maximumExpansions, Heuristic &&h, GoalTest &isGoal, OnGoal &&onGoal,
						   Result &result)
{
	auto &P = *planes;
	std::uint8_t *const st = P.state.data();
	std::uint8_t *const mark = P.mark.data();
	std::uint16_t *const gs = P.g.data();
	const std::uint16_t *const costs = P.steps.data();
	const std::uint8_t stamp = P.stamp;
	sample(domain, startPlane, isGoal);
	gs[startPlane] = 0;
	st[startPlane] &= detail::Goal; // the start is never tested for passability
	queue.reset(h(startPlane));
	queue.push(h(startPlane), startPlane);
	bool limited = false;
	if (expansionLog)
		expansionLog->clear();
	while (!queue.empty())
	{
		const auto p = queue.pop();
		const auto sp = st[p];
		if (sp & detail::Closed)
			continue;
		if (result.expansions >= maximumExpansions)
		{
			result.status = Status::ExpansionLimit;
			return true;
		}
		++result.expansions;
		st[p] = sp | detail::Closed;
		if (expansionLog) [[unlikely]]
			expansionLog->push_back(worldIndex(p));
		if (sp & detail::Goal)
		{
			if (onGoal(p))
				return true;
			if (p != startPlane)
				continue; // goals are terminal
		}
		const std::uint32_t gp = gs[p];
		bool edge = false;
		forEachDirection([&](auto direction) {
			constexpr unsigned d = decltype(direction)::value;
			const auto n = step<Torus>(p, d);
			if (mark[n] != stamp)
				sample(domain, n, isGoal);
			const auto sn = st[n];
			if (sn & (detail::Closed | detail::Blocked))
			{
				edge |= (sn & detail::Edge) != 0;
				return;
			}
			const std::uint32_t candidate = gp + (detail::diagonal(d) ? costs[n] >> 8 : costs[n] & 0xff);
			if (candidate > maximumCost)
			{
				limited = true;
				return;
			}
			if (candidate >= gs[n])
				return;
			gs[n] = std::uint16_t(candidate);
			st[n] = std::uint8_t((sn & ~detail::Direction) | d);
			queue.push(candidate + h(n), n);
		});
		if (edge) [[unlikely]]
			return false; // the window is too small; restart larger
	}
	result.status = limited ? Status::CostLimit : Status::Unreachable;
	return true;
}

template <class Domain> inline Result Workspace::search(const Domain &domain, const Query &q)
{
	assert(q.start < std::uint32_t(q.width) * q.height && q.target < std::uint32_t(q.width) * q.height);
	const unsigned shift = unsigned(std::countr_zero(unsigned(q.width)));
	targetX = int(q.target & unsigned(q.width - 1));
	targetY = int(q.target >> shift);
	goalRadius = q.goalRadius;
	const int sx = int(q.start & unsigned(q.width - 1)), sy = int(q.start >> shift);
	const std::uint32_t apart = std::max(detail::torusDistance(sx, targetX, q.width), detail::torusDistance(sy, targetY, q.height));
	const std::uint32_t reach = q.floor.cardinal ? std::min<std::uint32_t>(q.maximumCost / q.floor.cardinal, 64) : 64;
	int windowSide = forcedWindow ? int(forcedWindow) : firstSide(q.width, q.height, std::max(apart + 4, reach));
	const Floor floor = q.floor;
	const auto h = [this, floor](std::uint32_t p) -> std::uint32_t {
		const unsigned ax = detail::torusDistance(worldX(planeX(p)), targetX, mapW);
		const unsigned ay = detail::torusDistance(worldY(planeY(p)), targetY, mapH);
		return detail::octile(ax > goalRadius ? ax - goalRadius : 0, ay > goalRadius ? ay - goalRadius : 0, floor);
	};
	auto isGoal = [this](int px, int py, std::uint32_t) {
		return detail::torusDistance(worldX(px), targetX, mapW) <= goalRadius &&
			   detail::torusDistance(worldY(py), targetY, mapH) <= goalRadius;
	};
	const std::uint32_t maximumCost = std::min(q.maximumCost, MaximumCost);
	Result result;
	unsigned restarts = 0;
	for (;; windowSide *= 2, ++restarts)
	{
		layout(q.width, q.height, q.start, windowSide);
		const auto startPlane = planeOf(q.start);
		result = {};
		const auto found = [&](std::uint32_t p) {
			result.status = Status::Found;
			result.cost = planes->g[p];
			result.goal = worldIndex(p);
			trace(p, startPlane, q.wantPath ? &result : nullptr, &result.firstDx, &result.firstDy);
			return true;
		};
		const bool complete = torus ? run<true>(domain, startPlane, maximumCost, q.maximumExpansions, h, isGoal, found, result)
									: run<false>(domain, startPlane, maximumCost, q.maximumExpansions, h, isGoal, found, result);
		if (complete)
			break;
	}
	result.restarts = restarts;
	return result;
}

template <class Domain>
inline void Workspace::searchGoals(const Domain &domain, const GoalQuery &q, std::span<const std::uint32_t> goals,
								   std::span<GoalResult> results)
{
	assert(goals.size() == results.size());
	if (goals.empty())
		return;
	const unsigned shift = unsigned(std::countr_zero(unsigned(q.width)));
	goalCells.assign(goals.begin(), goals.end());
	std::sort(goalCells.begin(), goalCells.end());
	goalCells.erase(std::unique(goalCells.begin(), goalCells.end()), goalCells.end());
	goalPoints.clear();
	for (auto g : goalCells)
		goalPoints.push_back({int(g & unsigned(q.width - 1)), int(g >> shift)});
	const Floor floor = q.floor;
	const auto h = [this, floor](std::uint32_t p) -> std::uint32_t {
		const int x = worldX(planeX(p)), y = worldY(planeY(p));
		std::uint32_t best = std::numeric_limits<std::uint32_t>::max();
		for (const auto &goal : goalPoints)
			best = std::min(best, std::uint32_t(detail::octile(detail::torusDistance(x, goal[0], mapW),
															   detail::torusDistance(y, goal[1], mapH), floor)));
		return best;
	};
	auto isGoal = [this](int, int, std::uint32_t world) {
		return std::binary_search(goalCells.begin(), goalCells.end(), world);
	};
	const std::uint32_t reach = q.floor.cardinal ? std::min<std::uint32_t>(q.maximumCost / q.floor.cardinal, 64) : 64;
	int windowSide = forcedWindow ? int(forcedWindow) : firstSide(q.width, q.height, reach + 1);
	const std::uint32_t maximumCost = std::min(q.maximumCost, MaximumCost);
	for (;; windowSide *= 2)
	{
		layout(q.width, q.height, q.start, windowSide);
		const auto startPlane = planeOf(q.start);
		for (auto &r : results)
			r = {};
		std::size_t remaining = goalCells.size();
		const auto reached = [&](std::uint32_t p) {
			const auto world = worldIndex(p);
			int dx = 0, dy = 0;
			trace(p, startPlane, nullptr, &dx, &dy);
			for (std::size_t i = 0; i < goals.size(); ++i)
				if (goals[i] == world)
					results[i] = {true, planes->g[p], dx, dy};
			return --remaining == 0;
		};
		Result scratch;
		const bool complete =
			torus ? run<true>(domain, startPlane, maximumCost, std::numeric_limits<std::uint32_t>::max(), h, isGoal, reached, scratch)
				  : run<false>(domain, startPlane, maximumCost, std::numeric_limits<std::uint32_t>::max(), h, isGoal, reached, scratch);
		if (complete)
			return;
	}
}
} // namespace astar
