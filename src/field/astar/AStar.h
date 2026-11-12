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
// same expansion sequence.
#include "../Grid.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>
#include <span>
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

// Many goals at once, without a heuristic: one bounded Dijkstra resolves every
// goal, as separate searches with the same passability would. Goals are terminal:
// a goal is never expanded, so no path passes through one goal to another.
struct GoalQuery
{
	int width = 0, height = 0;
	std::uint32_t start = 0;
	std::uint32_t maximumCost = MaximumCost;
	Floor sizing; // only sizes the first window
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
	Open = 0x40,
	Closed = 0x80,
};
constexpr bool diagonal(unsigned d) { return field::Surrounding[d].x != 0 && field::Surrounding[d].y != 0; }
constexpr unsigned BlockShift = 3; // 8x8 fill blocks
constexpr unsigned RingBits = 9;   // f grows by at most 255 + 255 per expansion
constexpr unsigned Ring = 1u << RingBits;

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
	// Tests record the world index of every expansion of the final attempt.
	std::vector<std::uint32_t> *expansionLog = nullptr;

  private:
	// Plane geometry. Bounded windows hold side x side interior cells around
	// the start plus a one-cell Edge halo, so neighbours need no wrap or bounds
	// checks. A torus window is the whole map, wrapped with masks.
	bool torus = false;
	int side = 0, stride = 0, rows = 0;
	int mapW = 0, mapH = 0, originX = 0, originY = 0;
	unsigned wShift = 0, strideShift = 0, blockColumns = 0, blockRows = 0;
	unsigned blockShiftX = detail::BlockShift, blockShiftY = detail::BlockShift;
	std::array<int, 8> offsets{};

	std::vector<std::uint16_t> g;
	std::vector<std::uint8_t> state, cardinal, diagonal;
	std::vector<std::uint16_t> heuristicX, heuristicY; // per plane column / row
	std::vector<std::uint16_t> goalX, goalY;           // raw toroidal distances
	std::vector<std::uint16_t> blockStamp;
	std::uint16_t stamp = 0;
	detail::BucketQueue queue;
	std::vector<std::uint32_t> goalCells; // world indices for searchGoals

	// Plane coordinates. Strides are powers of two; a bounded window keeps its
	// Edge halo in plane row/column 0 and side - 1.
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

	void layout(int width, int height, std::uint32_t start, int requestedSide);
	void beginSearch();
	template <class Domain, class GoalTest> void fillBlock(const Domain &domain, unsigned bx, unsigned by, GoalTest &&isGoal);
	template <class Domain, class GoalTest>
	void ensureAround(const Domain &domain, std::uint32_t p, GoalTest &&isGoal);
	std::uint32_t neighbour(std::uint32_t p, unsigned d) const;
	void trace(std::uint32_t p, std::uint32_t startPlane, Result *result, int *dx, int *dy) const;
	static int firstSide(int width, int height, std::uint32_t reach);

	template <class Domain> bool attempt(const Domain &domain, const Query &query, int side, Result &result);
	template <class Domain>
	bool attemptGoals(const Domain &domain, const GoalQuery &query, std::span<const std::uint32_t> goals,
					  std::span<GoalResult> results, int side);
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
	const bool wholeMap = requestedSide >= width || requestedSide >= height;
	const int newSide = wholeMap ? 0 : requestedSide;
	const bool geometryChanged = torus != wholeMap || side != newSide || mapW != width || mapH != height;
	torus = wholeMap;
	side = newSide;
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
	blockShiftX = std::min<unsigned>(detail::BlockShift, strideShift);
	blockShiftY = std::min<unsigned>(detail::BlockShift, unsigned(std::countr_zero(unsigned(rows))));
	blockColumns = unsigned(stride) >> blockShiftX;
	blockRows = unsigned(rows) >> blockShiftY;
	for (unsigned d = 0; d < 8; ++d)
		offsets[d] = field::Surrounding[d].y * stride + field::Surrounding[d].x;
	const auto cells = std::size_t(stride) * rows;
	if (geometryChanged || g.size() != cells)
	{
		g.assign(cells, 0xffff);
		state.assign(cells, 0);
		cardinal.assign(cells, 0);
		diagonal.assign(cells, 0);
		if (!torus)
			for (int py = 0; py < rows; ++py)
				for (int px = 0; px < stride; ++px)
					if (px == 0 || py == 0 || px == stride - 1 || py == rows - 1)
						state[std::size_t(py) * stride + px] = detail::Edge | detail::Blocked;
		blockStamp.assign(std::size_t(blockColumns) * blockRows, 0);
		stamp = 0;
		heuristicX.resize(stride);
		goalX.resize(stride);
		heuristicY.resize(rows);
		goalY.resize(rows);
	}
}

inline void Workspace::beginSearch()
{
	if (++stamp == 0)
	{
		std::fill(blockStamp.begin(), blockStamp.end(), 0);
		stamp = 1;
	}
}

inline std::uint32_t Workspace::neighbour(std::uint32_t p, unsigned d) const
{
	if (!torus)
		return std::uint32_t(int(p) + offsets[d]);
	const int x = planeX(p), y = planeY(p);
	return std::uint32_t((((y + field::Surrounding[d].y) & (rows - 1)) << strideShift) +
						 ((x + field::Surrounding[d].x) & (stride - 1)));
}

template <class Domain, class GoalTest>
inline void Workspace::fillBlock(const Domain &domain, unsigned bx, unsigned by, GoalTest &&isGoal)
{
	blockStamp[std::size_t(by) * blockColumns + bx] = stamp;
	const int x0 = int(bx << blockShiftX), y0 = int(by << blockShiftY);
	const int x1 = x0 + (1 << blockShiftX), y1 = y0 + (1 << blockShiftY);
	for (int py = y0; py < y1; ++py)
	{
		const auto rowWorld = std::uint32_t(worldY(py) << wShift);
		auto p = std::size_t(py) * stride + x0;
		for (int px = x0; px < x1; ++px, ++p)
		{
			if (!torus && (state[p] & detail::Edge))
				continue; // the halo is fixed for the window's lifetime
			const auto world = rowWorld + std::uint32_t(worldX(px));
			const bool goal = isGoal(px, py, world);
			const unsigned entry = domain.entry(world, goal);
			g[p] = 0xffff;
			cardinal[p] = std::uint8_t(entry);
			diagonal[p] = std::uint8_t(entry >> 8);
			assert(!entry || ((entry & 0xff) && (entry >> 8)));
			state[p] = std::uint8_t((entry ? 0 : detail::Blocked) | (goal ? detail::Goal : 0));
		}
	}
}

template <class Domain, class GoalTest>
inline void Workspace::ensureAround(const Domain &domain, std::uint32_t p, GoalTest &&isGoal)
{
	const int px = planeX(p), py = planeY(p);
	const unsigned maskX = (1u << blockShiftX) - 1, maskY = (1u << blockShiftY) - 1;
	const unsigned ix = unsigned(px) & maskX, iy = unsigned(py) & maskY;
	// A cell strictly inside its block has all its neighbours in that block,
	// which was filled when the cell was first reached.
	if (ix - 1 < maskX - 1 && iy - 1 < maskY - 1 && blockStamp[(std::size_t(py) >> blockShiftY) * blockColumns +
																	 (unsigned(px) >> blockShiftX)] == stamp)
		return;
	unsigned columns[3], rowList[3], nc = 0, nr = 0;
	const auto add = [](unsigned *list, unsigned &n, unsigned b) {
		for (unsigned i = 0; i < n; ++i)
			if (list[i] == b)
				return;
		list[n++] = b;
	};
	for (int dx = -1; dx <= 1; ++dx)
		add(columns, nc, unsigned((px + dx) & (stride - 1)) >> blockShiftX);
	for (int dy = -1; dy <= 1; ++dy)
		add(rowList, nr, unsigned((py + dy) & (rows - 1)) >> blockShiftY);
	for (unsigned r = 0; r < nr; ++r)
		for (unsigned c = 0; c < nc; ++c)
			if (blockStamp[std::size_t(rowList[r]) * blockColumns + columns[c]] != stamp)
				fillBlock(domain, columns[c], rowList[r], isGoal);
}

inline void Workspace::trace(std::uint32_t p, std::uint32_t startPlane, Result *result, int *dx, int *dy) const
{
	*dx = *dy = 0;
	if (result)
		result->cells.clear();
	while (p != startPlane)
	{
		const unsigned d = state[p] & detail::Direction;
		if (result)
			result->cells.push_back(worldIndex(p));
		*dx = field::Surrounding[d].x;
		*dy = field::Surrounding[d].y;
		p = neighbour(p, 7 - d); // Surrounding is symmetric: 7 - d is the opposite step
	}
	if (result)
		std::reverse(result->cells.begin(), result->cells.end());
}

template <class Domain> inline bool Workspace::attempt(const Domain &domain, const Query &q, int windowSide, Result &result)
{
	layout(q.width, q.height, q.start, windowSide);
	beginSearch();
	const int tx = int(q.target & unsigned(q.width - 1)), ty = int(q.target >> wShift);
	const auto distance = [](int a, int b, int size) {
		const int d = std::abs(a - b);
		return std::min(d, size - d);
	};
	for (int px = 0; px < stride; ++px)
	{
		const int raw = distance(worldX(px), tx, q.width);
		goalX[px] = std::uint16_t(raw);
		heuristicX[px] = std::uint16_t(std::max(0, raw - int(q.goalRadius)));
	}
	for (int py = 0; py < rows; ++py)
	{
		const int raw = distance(worldY(py), ty, q.height);
		goalY[py] = std::uint16_t(raw);
		heuristicY[py] = std::uint16_t(std::max(0, raw - int(q.goalRadius)));
	}
	const unsigned fc = q.floor.cardinal, fd = q.floor.diagonal;
	const auto h = [&](std::uint32_t p) -> std::uint32_t {
		const unsigned a = heuristicX[planeX(p)], b = heuristicY[planeY(p)];
		const unsigned lo = std::min(a, b), hi = std::max(a, b);
		return (hi - lo) * fc + lo * fd;
	};
	const unsigned radius = q.goalRadius;
	auto isGoal = [&](int px, int py, std::uint32_t) { return goalX[px] <= radius && goalY[py] <= radius; };

	const auto startPlane = planeOf(q.start);
	ensureAround(domain, startPlane, isGoal);
	g[startPlane] = 0;
	state[startPlane] = std::uint8_t((state[startPlane] & detail::Goal) | detail::Open);
	queue.reset(h(startPlane));
	queue.push(h(startPlane), startPlane);

	result = {};
	if (expansionLog)
		expansionLog->clear();
	bool limited = false;
	const std::uint32_t maximumCost = std::min(q.maximumCost, MaximumCost);
	while (!queue.empty())
	{
		const auto p = queue.pop();
		auto st = state[p];
		if (st & detail::Closed)
			continue;
		if (result.expansions >= q.maximumExpansions)
		{
			result.status = Status::ExpansionLimit;
			return true;
		}
		++result.expansions;
		state[p] = st | detail::Closed;
		if (expansionLog) [[unlikely]]
			expansionLog->push_back(worldIndex(p));
		if (st & detail::Goal)
		{
			result.status = Status::Found;
			result.cost = g[p];
			result.goal = worldIndex(p);
			trace(p, startPlane, q.wantPath ? &result : nullptr, &result.firstDx, &result.firstDy);
			return true;
		}
		ensureAround(domain, p, isGoal);
		const std::uint32_t gp = g[p];
		for (unsigned d = 0; d < 8; ++d)
		{
			const auto n = neighbour(p, d);
			const auto sn = state[n];
			if (sn & (detail::Closed | detail::Blocked))
			{
				if (sn & detail::Edge)
					return false; // the window is too small; restart larger
				continue;
			}
			const std::uint32_t candidate = gp + (detail::diagonal(d) ? diagonal[n] : cardinal[n]);
			if (candidate > maximumCost)
			{
				limited = true;
				continue;
			}
			if (candidate >= g[n])
				continue;
			g[n] = std::uint16_t(candidate);
			state[n] = std::uint8_t((sn & ~detail::Direction) | detail::Open | d);
			queue.push(candidate + h(n), n);
		}
	}
	result.status = limited ? Status::CostLimit : Status::Unreachable;
	return true;
}

template <class Domain> inline Result Workspace::search(const Domain &domain, const Query &q)
{
	assert(q.start < std::uint32_t(q.width) * q.height && q.target < std::uint32_t(q.width) * q.height);
	const int sx = int(q.start & unsigned(q.width - 1)), sy = int(q.start / unsigned(q.width));
	const int tx = int(q.target & unsigned(q.width - 1)), ty = int(q.target / unsigned(q.width));
	const auto torusDistance = [](int a, int b, int size) {
		const int d = std::abs(a - b);
		return std::min(d, size - d);
	};
	const std::uint32_t apart = std::uint32_t(std::max(torusDistance(sx, tx, q.width), torusDistance(sy, ty, q.height)));
	const std::uint32_t reach = q.floor.cardinal ? std::min<std::uint32_t>(q.maximumCost / q.floor.cardinal, 64) : 64;
	int windowSide = forcedWindow ? int(forcedWindow) : firstSide(q.width, q.height, std::max(apart + 4, reach));
	Result result;
	unsigned restarts = 0;
	while (!attempt(domain, q, windowSide, result))
	{
		windowSide *= 2;
		++restarts;
	}
	result.restarts = restarts;
	return result;
}

template <class Domain>
inline bool Workspace::attemptGoals(const Domain &domain, const GoalQuery &q, std::span<const std::uint32_t> goals,
									std::span<GoalResult> results, int windowSide)
{
	layout(q.width, q.height, q.start, windowSide);
	beginSearch();
	goalCells.assign(goals.begin(), goals.end());
	std::sort(goalCells.begin(), goalCells.end());
	auto isGoal = [&](int, int, std::uint32_t world) {
		return std::binary_search(goalCells.begin(), goalCells.end(), world);
	};
	for (auto &r : results)
		r = {};
	const auto startPlane = planeOf(q.start);
	ensureAround(domain, startPlane, isGoal);
	g[startPlane] = 0;
	state[startPlane] = std::uint8_t((state[startPlane] & detail::Goal) | detail::Open);
	queue.reset(0);
	queue.push(0, startPlane);
	std::size_t remaining = goalCells.size();
	const std::uint32_t maximumCost = std::min(q.maximumCost, MaximumCost);
	while (remaining && !queue.empty())
	{
		const auto p = queue.pop();
		auto st = state[p];
		if (st & detail::Closed)
			continue;
		state[p] = st | detail::Closed;
		if (st & detail::Goal)
		{
			const auto world = worldIndex(p);
			int dx = 0, dy = 0;
			trace(p, startPlane, nullptr, &dx, &dy);
			for (std::size_t i = 0; i < goals.size(); ++i)
				if (goals[i] == world && !results[i].reached)
				{
					results[i] = {true, g[p], dx, dy};
					--remaining;
				}
			if (p != startPlane)
				continue; // goals are terminal
		}
		ensureAround(domain, p, isGoal);
		const std::uint32_t gp = g[p];
		for (unsigned d = 0; d < 8; ++d)
		{
			const auto n = neighbour(p, d);
			const auto sn = state[n];
			if (sn & (detail::Closed | detail::Blocked))
			{
				if (sn & detail::Edge)
					return false;
				continue;
			}
			const std::uint32_t candidate = gp + (detail::diagonal(d) ? diagonal[n] : cardinal[n]);
			if (candidate > maximumCost || candidate >= g[n])
				continue;
			g[n] = std::uint16_t(candidate);
			state[n] = std::uint8_t((sn & ~detail::Direction) | detail::Open | d);
			queue.push(candidate, n);
		}
	}
	return true;
}

template <class Domain>
inline void Workspace::searchGoals(const Domain &domain, const GoalQuery &q, std::span<const std::uint32_t> goals,
								   std::span<GoalResult> results)
{
	assert(goals.size() == results.size());
	if (goals.empty())
		return;
	const std::uint32_t reach =
		q.sizing.cardinal ? std::min<std::uint32_t>(q.maximumCost / q.sizing.cardinal, 64) : 64;
	int windowSide = forcedWindow ? int(forcedWindow) : firstSide(q.width, q.height, reach + 1);
	while (!attemptGoals(domain, q, goals, results, windowSide))
		windowSide *= 2;
}
} // namespace astar
