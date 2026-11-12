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
#include <cstdlib>
#include <cstring>
#if defined(__SSE2__) && !defined(GLOB2_ASTAR_SCALAR)
#include <emmintrin.h>
#define GLOB2_ASTAR_SSE2 1
#endif
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

// Neighbour expansion kernels. All produce the same expansion sequence; tests
// and tools/astar_benchmark.cpp compare them against the reference search.
//  Branchy: one neighbour at a time, branching on each test (the reference).
//  Scalar:  all eight neighbours classified into bit masks without branching.
//  SSE2:    the same classification in two four-lane vectors.
enum class Kernel : std::uint8_t
{
	Branchy,
	Scalar,
	SSE2
};
inline bool kernelAvailable(Kernel kernel)
{
#ifdef GLOB2_ASTAR_SSE2
	return true;
#else
	return kernel != Kernel::SSE2;
#endif
}
// GLOB2_ASTAR_KERNEL=branchy|scalar|sse2 overrides the choice for diagnosis.
inline Kernel automaticKernel()
{
	static const Kernel chosen = [] {
		if (const char *name = std::getenv("GLOB2_ASTAR_KERNEL"))
		{
			if (!std::strcmp(name, "branchy"))
				return Kernel::Branchy;
			if (!std::strcmp(name, "scalar"))
				return Kernel::Scalar;
			if (!std::strcmp(name, "sse2") && kernelAvailable(Kernel::SSE2))
				return Kernel::SSE2;
		}
		return kernelAvailable(Kernel::SSE2) ? Kernel::SSE2 : Kernel::Scalar;
	}();
	return chosen;
}

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

[[gnu::always_inline]] inline unsigned torusDistance(int a, int b, int size)
{
	const unsigned d = unsigned(a - b) & unsigned(size - 1);
	return std::min(d, unsigned(size) - d);
}
[[gnu::always_inline]] inline unsigned octile(unsigned a, unsigned b, Floor floor)
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
	[[gnu::always_inline]] void push(std::uint32_t f, std::uint32_t cell)
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
		occupied[slot >> 6] &= ~(std::uint64_t(bucket.empty()) << (slot & 63));
		--count;
		return cell;
	}
};
struct Lanes
{
	unsigned improve = 0, over = 0;
	bool edge = false;
};
constexpr std::uint32_t StopBits = std::uint32_t(Closed | Blocked) << 16;
constexpr std::uint32_t EdgeBit = std::uint32_t(Edge) << 16;

// Classifies the eight neighbours of an interior plane cell p (offsets in
// field::Surrounding order): which improve within the cost limit, which only
// exceed it, and whether any is the window halo. word and candidate receive
// each neighbour's node word and candidate g.
template <Kernel K>
[[gnu::always_inline]] inline Lanes classify(const std::uint32_t *nodes, const std::uint16_t *costs, const int *off,
											 std::uint32_t p, std::uint32_t gp, std::uint32_t maximumCost,
											 std::uint32_t *word, std::uint32_t *candidate)
{
	Lanes lanes;
#ifdef GLOB2_ASTAR_SSE2
	if constexpr (K == Kernel::SSE2)
	{
		// Rows above, beside and below p, regrouped into stencil order:
		// lo = (-1,-1) (0,-1) (1,-1) (-1,0), hi = (1,0) (-1,1) (0,1) (1,1).
		const int stride = off[6] - off[3] - 1; // (0,1) - (-1,0) - 1
		const auto *base = reinterpret_cast<const float *>(nodes + p);
		const __m128 above = _mm_loadu_ps(base - stride - 1), beside = _mm_loadu_ps(base - 1),
					 below = _mm_loadu_ps(base + stride - 1);
		const __m128 x = _mm_shuffle_ps(above, beside, _MM_SHUFFLE(0, 0, 2, 2));
		const __m128 y = _mm_shuffle_ps(beside, below, _MM_SHUFFLE(0, 0, 2, 2));
		const __m128i lo = _mm_castps_si128(_mm_shuffle_ps(above, x, _MM_SHUFFLE(2, 0, 1, 0)));
		const __m128i hi = _mm_castps_si128(_mm_shuffle_ps(y, below, _MM_SHUFFLE(2, 1, 2, 0)));
		const auto at = [&](int d) { return int(costs[int(p) + off[d]]); };
		const __m128i costLo = _mm_setr_epi32(at(0), at(1), at(2), at(3));
		const __m128i costHi = _mm_setr_epi32(at(4), at(5), at(6), at(7));
		// Diagonal lanes take the high byte: lanes 0 and 2 of lo, 1 and 3 of hi.
		const __m128i byte = _mm_set1_epi32(0xff);
		const __m128i diagonalLo = _mm_setr_epi32(-1, 0, -1, 0), diagonalHi = _mm_setr_epi32(0, -1, 0, -1);
		const auto stepOf = [&](__m128i c, __m128i diagonal) {
			return _mm_or_si128(_mm_and_si128(diagonal, _mm_srli_epi32(c, 8)),
								_mm_andnot_si128(diagonal, _mm_and_si128(c, byte)));
		};
		const __m128i g = _mm_set1_epi32(int(gp)), limit = _mm_set1_epi32(int(maximumCost));
		const __m128i candLo = _mm_add_epi32(g, stepOf(costLo, diagonalLo));
		const __m128i candHi = _mm_add_epi32(g, stepOf(costHi, diagonalHi));
		const __m128i zero = _mm_setzero_si128(), stop = _mm_set1_epi32(int(StopBits)), low = _mm_set1_epi32(0xffff);
		const auto classifyHalf = [&](__m128i w, __m128i cand, unsigned &improve, unsigned &over) {
			const __m128i open = _mm_cmpeq_epi32(_mm_and_si128(w, stop), zero);
			const __m128i beyond = _mm_cmpgt_epi32(cand, limit);
			const __m128i better = _mm_cmpgt_epi32(_mm_and_si128(w, low), cand);
			improve = unsigned(_mm_movemask_ps(_mm_castsi128_ps(_mm_andnot_si128(beyond, _mm_and_si128(open, better)))));
			over = unsigned(_mm_movemask_ps(_mm_castsi128_ps(_mm_and_si128(open, beyond))));
		};
		unsigned improveLo, overLo, improveHi, overHi;
		classifyHalf(lo, candLo, improveLo, overLo);
		classifyHalf(hi, candHi, improveHi, overHi);
		lanes.improve = improveLo | improveHi << 4;
		lanes.over = overLo | overHi << 4;
		const __m128i edge = _mm_and_si128(_mm_or_si128(lo, hi), _mm_set1_epi32(int(EdgeBit)));
		lanes.edge = _mm_movemask_epi8(_mm_cmpeq_epi32(edge, zero)) != 0xffff;
		_mm_store_si128(reinterpret_cast<__m128i *>(word), lo);
		_mm_store_si128(reinterpret_cast<__m128i *>(word + 4), hi);
		_mm_store_si128(reinterpret_cast<__m128i *>(candidate), candLo);
		_mm_store_si128(reinterpret_cast<__m128i *>(candidate + 4), candHi);
		return lanes;
	}
#endif
	std::uint32_t edge = 0;
	[&]<unsigned... D>(std::integer_sequence<unsigned, D...>) {
		(
			[&] {
				const auto n = std::uint32_t(int(p) + off[D]);
				const std::uint32_t w = nodes[n], c = costs[n];
				const std::uint32_t cand = gp + (diagonal(D) ? c >> 8 : c & 0xff);
				const unsigned open = (w & StopBits) == 0;
				const unsigned beyond = cand > maximumCost;
				lanes.improve |= (open & !beyond & (cand < (w & 0xffff))) << D;
				lanes.over |= (open & beyond) << D;
				edge |= w & EdgeBit;
				word[D] = w;
				candidate[D] = cand;
			}(),
			...);
	}(std::make_integer_sequence<unsigned, 8>{});
	lanes.edge = edge != 0;
	return lanes;
}
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
	// The neighbour kernel; results never depend on it.
	Kernel kernel = automaticKernel();
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

	// Each cell is sampled from the Domain on first touch in a search. Its node
	// word packs g (bits 0-15), the state flags (16-23) and the stamp of the
	// search that sampled it (24-31), so one load answers a neighbour visit.
	// Each geometry keeps its own planes, so searches of different sizes
	// alternate without reallocating.
	struct Planes
	{
		int width = 0, height = 0, side = -1;
		std::vector<std::uint32_t> nodes;
		std::vector<std::uint16_t> steps; // cardinal | diagonal << 8
		std::uint32_t stamp = 0;
	};
	static constexpr std::uint32_t flags(std::uint32_t node) { return (node >> 16) & 0xff; }
	std::uint32_t costOf(std::uint32_t p) const { return planes->nodes[p] & 0xffff; }
	unsigned directionOf(std::uint32_t p) const { return flags(planes->nodes[p]) & detail::Direction; }
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
	template <class Run> bool dispatch(Run &&run) const
	{
		const auto with = [&]<Kernel K>() {
			return torus ? run.template operator()<K, true>() : run.template operator()<K, false>();
		};
		switch (kernel)
		{
		case Kernel::Branchy:
			return with.template operator()<Kernel::Branchy>();
#ifdef GLOB2_ASTAR_SSE2
		case Kernel::SSE2:
			return with.template operator()<Kernel::SSE2>();
#endif
		default:
			return with.template operator()<Kernel::Scalar>();
		}
	}
	template <class Domain, class GoalTest> void sample(const Domain &domain, std::uint32_t p, GoalTest &isGoal);
	void trace(std::uint32_t p, std::uint32_t startPlane, Result *result, int *dx, int *dy) const;
	static int firstSide(int width, int height, std::uint32_t reach);

	// One attempt in the current window; false when the search met the halo.
	template <Kernel K, bool Torus, class Domain, class Heuristic, class GoalTest, class OnGoal>
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
		// Vector kernels read one cell past a neighbour row.
		P.nodes.assign(cells + 4, 0);
		P.steps.assign(cells + 4, 0);
		P.stamp = 0;
	}
	if (++P.stamp == 256)
	{
		std::fill(P.nodes.begin(), P.nodes.end(), 0);
		P.stamp = 1;
	}
}

template <class Domain, class GoalTest>
[[gnu::always_inline]] inline void Workspace::sample(const Domain &domain, std::uint32_t p, GoalTest &isGoal)
{
	auto &P = *planes;
	const int px = planeX(p), py = planeY(p);
	const std::uint32_t stamped = P.stamp << 24 | 0xffff;
	if (!torus && (px == 0 || py == 0 || px == stride - 1 || py == rows - 1))
	{
		P.nodes[p] = stamped | std::uint32_t(detail::Edge | detail::Blocked) << 16; // the halo
		return;
	}
	const auto world = std::uint32_t((worldY(py) << wShift) + worldX(px));
	const bool goal = isGoal(px, py, world);
	const unsigned entry = domain.entry(world, goal);
	assert(!entry || ((entry & 0xff) && (entry >> 8)));
	P.steps[p] = std::uint16_t(entry);
	P.nodes[p] = stamped | std::uint32_t((entry ? 0 : detail::Blocked) | (goal ? detail::Goal : 0)) << 16;
}

inline void Workspace::trace(std::uint32_t p, std::uint32_t startPlane, Result *result, int *dx, int *dy) const
{
	*dx = *dy = 0;
	if (result)
		result->cells.clear();
	while (p != startPlane)
	{
		const unsigned d = directionOf(p);
		if (result)
			result->cells.push_back(worldIndex(p));
		*dx = field::Surrounding[d].x;
		*dy = field::Surrounding[d].y;
		p = neighbour(p, 7 - d); // Surrounding is symmetric: 7 - d is the opposite step
	}
	if (result)
		std::reverse(result->cells.begin(), result->cells.end());
}

template <Kernel K, bool Torus, class Domain, class Heuristic, class GoalTest, class OnGoal>
inline bool Workspace::run(const Domain &domain, std::uint32_t startPlane, std::uint32_t maximumCost,
						   std::uint32_t maximumExpansions, Heuristic &&h, GoalTest &isGoal, OnGoal &&onGoal,
						   Result &result)
{
	auto &P = *planes;
	std::uint32_t *const nodes = P.nodes.data();
	const std::uint16_t *const costs = P.steps.data();
	const std::uint32_t stamp = P.stamp;
	constexpr std::uint32_t ClosedBit = std::uint32_t(detail::Closed) << 16;
	constexpr std::uint32_t Stop = std::uint32_t(detail::Closed | detail::Blocked) << 16;
	constexpr std::uint32_t DirectionBits = std::uint32_t(detail::Direction) << 16;
	sample(domain, startPlane, isGoal);
	// The start is never tested for passability.
	nodes[startPlane] &= ~(std::uint32_t(0xffff) | std::uint32_t(detail::Blocked) << 16);
	queue.reset(h(startPlane));
	queue.push(h(startPlane), startPlane);
	bool limited = false;
	if (expansionLog)
		expansionLog->clear();
	while (!queue.empty())
	{
		const auto p = queue.pop();
		const auto np = nodes[p];
		if (np & ClosedBit)
			continue;
		if (result.expansions >= maximumExpansions)
		{
			result.status = Status::ExpansionLimit;
			return true;
		}
		++result.expansions;
		nodes[p] = np | ClosedBit;
		if (expansionLog) [[unlikely]]
			expansionLog->push_back(worldIndex(p));
		if (flags(np) & detail::Goal)
		{
			if (onGoal(p))
				return true;
			if (p != startPlane)
				continue; // goals are terminal
		}
		const std::uint32_t gp = np & 0xffff;
		bool edge = false;
		// Off the seams a torus cell's neighbours are plain offsets too.
		const bool wrapped = Torus && (unsigned(planeX(p) - 1) >= unsigned(stride - 2) ||
									   unsigned(planeY(p) - 1) >= unsigned(rows - 2));
		if (K == Kernel::Branchy || wrapped)
		{
			forEachDirection([&](auto direction) {
				constexpr unsigned d = decltype(direction)::value;
				const auto n = wrapped ? step<true>(p, d) : step<false>(p, d);
				auto nn = nodes[n];
				if ((nn >> 24) != stamp)
				{
					sample(domain, n, isGoal);
					nn = nodes[n];
				}
				if (nn & Stop)
				{
					if constexpr (!Torus)
						edge |= (nn & (std::uint32_t(detail::Edge) << 16)) != 0;
					return;
				}
				const std::uint32_t candidate = gp + (detail::diagonal(d) ? costs[n] >> 8 : costs[n] & 0xff);
				if (candidate > maximumCost)
				{
					limited = true;
					return;
				}
				if (candidate >= (nn & 0xffff))
					return;
				nodes[n] = (nn & ~(DirectionBits | 0xffff)) | std::uint32_t(d) << 16 | candidate;
				queue.push(candidate + h(n), n);
			});
		}
		else
		{
			const int *const off = offsets.data();
			// Sample the neighbours first touched by this expansion.
			unsigned fresh = 0;
			forEachDirection([&](auto direction) {
				constexpr unsigned d = decltype(direction)::value;
				fresh |= unsigned((nodes[p + off[d]] >> 24) != stamp) << d;
			});
			for (; fresh; fresh &= fresh - 1)
				sample(domain, std::uint32_t(int(p) + off[std::countr_zero(fresh)]), isGoal);
			alignas(16) std::uint32_t word[8], candidate[8];
			const auto lanes = detail::classify<K>(nodes, costs, off, p, gp, maximumCost, word, candidate);
			limited |= lanes.over != 0;
			if constexpr (!Torus)
				edge = lanes.edge;
			for (unsigned improve = lanes.improve; improve; improve &= improve - 1)
			{
				const unsigned d = unsigned(std::countr_zero(improve));
				const auto n = std::uint32_t(int(p) + off[d]);
				nodes[n] = (word[d] & ~(DirectionBits | 0xffff)) | std::uint32_t(d) << 16 | candidate[d];
				queue.push(candidate[d] + h(n), n);
			}
		}
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
	const auto h = [this, floor](std::uint32_t p) __attribute__((always_inline)) -> std::uint32_t {
		const unsigned ax = detail::torusDistance(worldX(planeX(p)), targetX, mapW);
		const unsigned ay = detail::torusDistance(worldY(planeY(p)), targetY, mapH);
		return detail::octile(ax > goalRadius ? ax - goalRadius : 0, ay > goalRadius ? ay - goalRadius : 0, floor);
	};
	auto isGoal = [this](int px, int py, std::uint32_t) {
		return (detail::torusDistance(worldX(px), targetX, mapW) <= goalRadius) &
			   (detail::torusDistance(worldY(py), targetY, mapH) <= goalRadius);
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
			result.cost = costOf(p);
			result.goal = worldIndex(p);
			trace(p, startPlane, q.wantPath ? &result : nullptr, &result.firstDx, &result.firstDy);
			return true;
		};
		const bool complete = dispatch([&]<Kernel K, bool Torus>() {
			return run<K, Torus>(domain, startPlane, maximumCost, q.maximumExpansions, h, isGoal, found, result);
		});
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
					results[i] = {true, costOf(p), dx, dy};
			return --remaining == 0;
		};
		Result scratch;
		const bool complete = dispatch([&]<Kernel K, bool Torus>() {
			return run<K, Torus>(domain, startPlane, maximumCost, std::numeric_limits<std::uint32_t>::max(), h, isGoal,
								 reached, scratch);
		});
		if (complete)
			return;
	}
}
} // namespace astar
