// SPDX-License-Identifier: GPL-3.0-or-later
// Replays a captured point-to-point corpus (field/astar/AStarCapture.h) through
// the unified search, its plain reference, and the two searches it replaced.
// Built and driven by tools/astar_benchmark.py; see docs/development/gradient-benchmarks.md.
//
//   astar_benchmark CORPUS [--repeat N] [--only point|goals|claims|all]
//                   [--variant new|torus|reference|legacy] [--verify]
#include "field/astar/AStar.h"
#include "field/astar/AStarReference.h"
#ifdef GLOB2_ASTAR_BASELINE_CLAIMS
#include "ClaimRouteSearch.h" // the replaced claim search, extracted from git by the driver
#endif
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <queue>
#include <string>
#include <tuple>

namespace
{
using Sint16 = std::int16_t;
struct Record
{
	unsigned kind = 0;
	int width = 0, height = 0, boxX = 0, boxY = 0, boxW = 0, boxH = 0;
	std::uint32_t start = 0, target = 0, radius = 0, maximumCost = 0, maximumExpansions = 0;
	astar::Floor floor;
	std::vector<std::uint32_t> goals;
	std::vector<std::uint16_t> entries;
	bool claim() const { return kind == 0 && radius == 1 && maximumExpansions != 0xffffffffu; }
};

// Entries inside the captured box; everything outside it is blocked.
struct BoxDomain
{
	const Record &r;
	unsigned shift;
	explicit BoxDomain(const Record &record) : r(record), shift(unsigned(std::countr_zero(unsigned(record.width)))) {}
	static inline std::uint64_t evaluations = 0;
	unsigned entry(std::uint32_t i, bool) const
	{
		++evaluations;
		const int x = int(i & unsigned(r.width - 1)), y = int(i >> shift);
		const int dx = (x - r.boxX) & (r.width - 1), dy = (y - r.boxY) & (r.height - 1);
		if (dx >= r.boxW || dy >= r.boxH)
			return 0;
		return r.entries[std::size_t(dy) * r.boxW + dx];
	}
};

struct OthersBlocked
{
	const BoxDomain &domain;
	const std::vector<std::uint32_t> &goals;
	std::uint32_t goal;
	unsigned entry(std::uint32_t i, bool g) const
	{
		if (i != goal && std::find(goals.begin(), goals.end(), i) != goals.end())
			return 0;
		return domain.entry(i, g);
	}
};

std::vector<Record> load(const char *path)
{
	std::ifstream in(path, std::ios::binary);
	std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
	std::vector<Record> records;
	std::size_t at = 0;
	const auto u32 = [&]() {
		std::uint32_t v;
		std::memcpy(&v, data.data() + at, 4);
		at += 4;
		return v;
	};
	while (at + 64 <= data.size())
	{
		if (std::memcmp(data.data() + at, "AST1", 4))
			throw std::runtime_error("corrupt corpus");
		at += 4;
		Record r;
		r.kind = u32();
		r.width = int(u32());
		r.height = int(u32());
		r.boxX = int(u32());
		r.boxY = int(u32());
		r.boxW = int(u32());
		r.boxH = int(u32());
		r.start = u32();
		r.target = u32();
		r.radius = u32();
		r.maximumCost = u32();
		r.maximumExpansions = u32();
		r.floor.cardinal = u32();
		r.floor.diagonal = u32();
		r.goals.resize(u32());
		for (auto &g : r.goals)
			g = u32();
		r.entries.resize(std::size_t(r.boxW) * r.boxH);
		if (at + r.entries.size() * 2 > data.size())
			break; // a capture cut short
		std::memcpy(r.entries.data(), data.data() + at, r.entries.size() * 2);
		at += r.entries.size() * 2;
		records.push_back(std::move(r));
	}
	return records;
}

astar::Query queryOf(const Record &r)
{
	astar::Query q;
	q.width = r.width;
	q.height = r.height;
	q.start = r.start;
	q.target = r.target;
	q.goalRadius = r.radius;
	q.maximumCost = r.maximumCost;
	q.maximumExpansions = r.maximumExpansions;
	q.floor = r.floor;
	q.wantPath = r.claim();
	return q;
}

// The replaced unit search (MapRouting::pathfindPointToPoint before the unified
// engine), on the captured entries: a full-map scratch of 20-byte points, a
// binary heap ordered by (f, column-major index) and a Chebyshev heuristic.
class LegacyUnitSearch
{
	struct Point
	{
		Sint16 x = -1, y = -1, dx = -1, dy = -1;
		std::uint32_t moveCost = ~0u, totalCost = ~0u;
		bool isClosed = false;
	};
	std::vector<Point> points;
	std::vector<int> examined;

  public:
	std::uint32_t expansions = 0;
	template <class Domain> bool find(const Domain &domain, const astar::Query &q, int *outDx, int *outDy)
	{
		const int w = q.width, h = q.height, wMask = w - 1, hMask = h - 1;
		const int hDec = std::countr_zero(unsigned(h)), wDec = std::countr_zero(unsigned(w));
		if (points.size() != std::size_t(w) * h)
			points.assign(std::size_t(w) * h, {});
		const int x = int(q.start & unsigned(wMask)), y = int(q.start >> wDec);
		const int targetX = int(q.target & unsigned(wMask)), targetY = int(q.target >> wDec);
		// The old heuristic floor: the cheapest cardinal step, never above a land step.
		const int heuristicStep = int(std::min(q.floor.cardinal ? q.floor.cardinal : 10u, 10u));
		const int maximumCost = int(q.maximumCost);
		const auto warp = [](int a, int b, int size) {
			const int d = std::abs(a - b);
			return std::min(d, size - d);
		};
		const auto warpDistMax = [&](int px, int py, int qx, int qy) { return std::max(warp(px, qx, w), warp(py, qy, h)); };
		*outDx = *outDy = 0;
		Point reached;
		using Entry = std::tuple<unsigned, int, unsigned>;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> openList;
		openList.emplace(0, (x << hDec) + y, 0);
		points[std::size_t((x << hDec) + y)] = {Sint16(x), Sint16(y), 0, 0, 0, 0, false};
		examined.push_back((x << hDec) + y);
		expansions = 0;
		while (!openList.empty())
		{
			const auto [estimate, position, queuedCost] = openList.top();
			openList.pop();
			Point &pos = points[std::size_t(position)];
			if (pos.isClosed || pos.moveCost != queuedCost)
				continue;
			pos.isClosed = true;
			++expansions;
			if (warpDistMax(pos.x, pos.y, targetX, targetY) <= int(q.goalRadius))
			{
				reached = pos;
				break;
			}
			for (int lx = -1; lx <= 1; ++lx)
				for (int ly = -1; ly <= 1; ++ly)
				{
					const int nx = (pos.x + lx + w) & wMask, ny = (pos.y + ly + h) & hMask;
					const int n = (nx << hDec) + ny;
					Point &npos = points[std::size_t(n)];
					if (npos.isClosed)
						continue;
					const unsigned entry = domain.entry(std::uint32_t((ny << wDec) + nx), false);
					const int step = int(lx && ly ? entry >> 8 : entry & 0xff);
					const int moveCost = int(pos.moveCost) + step;
					if (moveCost > maximumCost)
						continue;
					const int totalCost = moveCost + heuristicStep * warpDistMax(targetX, targetY, nx, ny);
					if (npos.x == -1)
					{
						if (entry)
						{
							npos = {Sint16(nx), Sint16(ny), Sint16(pos.dx || pos.dy ? pos.dx : lx),
									Sint16(pos.dx || pos.dy ? pos.dy : ly), std::uint32_t(moveCost), std::uint32_t(totalCost), false};
							openList.emplace(totalCost, n, moveCost);
							examined.push_back(n);
						}
					}
					else if (int(npos.moveCost) > moveCost)
					{
						npos.moveCost = std::uint32_t(moveCost);
						npos.totalCost = std::uint32_t(totalCost);
						npos.dx = pos.dx;
						npos.dy = pos.dy;
						openList.emplace(totalCost, n, moveCost);
					}
				}
		}
		for (auto i : examined)
			points[std::size_t(i)] = {};
		examined.clear();
		if (reached.x == -1)
			return false;
		*outDx = reached.dx;
		*outDy = reached.dy;
		return true;
	}
};

using Clock = std::chrono::steady_clock;

struct Totals
{
	std::size_t queries = 0;
	std::uint64_t expansions = 0, restarts = 0, found = 0;
	double seconds = 0;
	std::uint64_t digest = 1469598103934665603ull;
	void mix(std::uint64_t v)
	{
		digest ^= v;
		digest *= 1099511628211ull;
	}
};

std::string category(const Record &r) { return r.kind == 1 ? "goals" : r.claim() ? "claims" : "point"; }
} // namespace

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		std::cerr << "usage: astar_benchmark CORPUS [--repeat N] [--only point|goals|claims|all] "
					 "[--variant new|torus|reference|legacy] [--verify]\n";
		return 2;
	}
	unsigned repeat = 5;
	std::string only = "all", variant = "new";
	bool verify = false;
	int window = -1;
	for (int i = 2; i < argc; ++i)
	{
		const std::string a = argv[i];
		if (a == "--repeat" && i + 1 < argc)
			repeat = unsigned(std::stoul(argv[++i]));
		else if (a == "--only" && i + 1 < argc)
			only = argv[++i];
		else if (a == "--variant" && i + 1 < argc)
			variant = argv[++i];
		else if (a == "--verify")
			verify = true;
		else if (a == "--window" && i + 1 < argc)
			window = std::stoi(argv[++i]);
		else
		{
			std::cerr << "unknown argument " << a << "\n";
			return 2;
		}
	}
	const auto records = load(argv[1]);
	std::vector<const Record *> chosen;
	for (const auto &r : records)
		if (only == "all" || category(r) == only)
			chosen.push_back(&r);
	std::cerr << records.size() << " records, " << chosen.size() << " selected\n";

	if (verify)
	{
		// Every point record: the engine must reproduce the reference expansion sequence.
		astar::Workspace workspace;
		std::vector<std::uint32_t> actual, expected;
		workspace.expansionLog = &actual;
		std::size_t mismatches = 0, checked = 0;
		for (const auto *r : chosen)
		{
			if (r->kind != 0)
				continue;
			const BoxDomain domain(*r);
			auto q = queryOf(*r);
			q.wantPath = true;
			const auto reference = astar::referenceSearch(domain, q, &expected);
			for (unsigned forced : {0u, 4096u})
			{
				workspace.forcedWindow = forced;
				const auto result = workspace.search(domain, q);
				++checked;
				if (result.status != reference.status || result.cost != reference.cost ||
					result.cells != reference.cells || actual != expected)
					++mismatches;
			}
		}
		std::printf("verify: %zu searches, %zu mismatches\n", checked, mismatches);
		return mismatches ? 1 : 0;
	}

	std::map<std::string, Totals> totals;
	astar::Workspace workspace;
	if (variant == "torus")
		workspace.forcedWindow = 1u << 20;
	if (window >= 0)
		workspace.forcedWindow = unsigned(window);

	LegacyUnitSearch legacy;
#ifdef GLOB2_ASTAR_BASELINE_CLAIMS
	ResourceClaims::RouteSearch legacyClaims;
#endif
	std::vector<astar::GoalResult> goalResults;
	for (unsigned pass = 0; pass < repeat; ++pass)
		for (const auto *r : chosen)
		{
			const BoxDomain domain(*r);
			auto &t = totals[category(*r)];
			const auto q = queryOf(*r);
			const auto begin = Clock::now();
			std::uint64_t expansions = 0, restarts = 0, found = 0, digest = 0;
			if (variant == "legacy")
			{
				if (r->kind == 1)
				{
					// The replaced scan searched candidate by candidate; in game the
					// other candidates' cells are occupied, so they are blocked.
					for (auto goal : r->goals)
					{
						auto single = q;
						single.target = goal;
						single.goalRadius = 0;
						int dx, dy;
						const OthersBlocked alone{domain, r->goals, goal};
						found += legacy.find(alone, single, &dx, &dy);
						expansions += legacy.expansions;
						digest = digest * 31 + std::uint64_t(dx + 2) * 5 + std::uint64_t(dy + 2);
					}
				}
				else if (r->claim())
				{
#ifdef GLOB2_ASTAR_BASELINE_CLAIMS
					const field::Grid grid(r->width, r->height);
					const int tx = int(r->target % unsigned(r->width)), ty = int(r->target / unsigned(r->width));
					const unsigned c = r->floor.cardinal, d = r->floor.diagonal;
					const auto route = legacyClaims.find(
						grid, r->start, r->maximumCost, [&](std::uint32_t i) { return domain.entry(i, false) != 0; },
						[&](std::uint32_t i, bool diagonal) {
							const auto e = domain.entry(i, false);
							return diagonal ? e >> 8 : e & 0xff;
						},
						[&](std::uint32_t i) {
							const auto dx = unsigned(std::abs(int(i % unsigned(r->width)) - tx));
							const auto dy = unsigned(std::abs(int(i / unsigned(r->width)) - ty));
							return std::max(std::min(dx, unsigned(r->width) - dx), std::min(dy, unsigned(r->height) - dy)) <= 1;
						},
						[&](std::uint32_t i) {
							const unsigned ax = unsigned(std::abs(int(i % unsigned(r->width)) - tx)),
										   ay = unsigned(std::abs(int(i / unsigned(r->width)) - ty));
							const unsigned wx = std::min(ax, unsigned(r->width) - ax), wy = std::min(ay, unsigned(r->height) - ay);
							const unsigned x = wx > 1 ? wx - 1 : 0, y = wy > 1 ? wy - 1 : 0;
							return std::uint64_t(std::max(x, y) - std::min(x, y)) * c + std::uint64_t(std::min(x, y)) * d;
						},
						r->maximumExpansions);
					found += route.status == ResourceClaims::RouteStatus::Found;
					expansions += route.expansions;
					digest = route.cost;
#else
					continue;
#endif
				}
				else
				{
					int dx, dy;
					found += legacy.find(domain, q, &dx, &dy);
					expansions += legacy.expansions;
					digest = std::uint64_t(dx + 2) * 5 + std::uint64_t(dy + 2);
				}
			}
			else if (variant == "reference")
			{
				if (r->kind == 1)
					continue;
				const auto result = astar::referenceSearch(domain, q);
				found += result.status == astar::Status::Found;
				expansions += result.expansions;
				digest = result.cost * 64 + std::uint64_t(result.firstDx + 2) * 5 + std::uint64_t(result.firstDy + 2);
			}
			else if (r->kind == 1)
			{
				astar::GoalQuery g{r->width, r->height, r->start, r->maximumCost, r->floor};
				goalResults.resize(r->goals.size());
				workspace.searchGoals(domain, g, r->goals, goalResults);
				expansions += 0;
				for (const auto &result : goalResults)
				{
					found += result.reached;
					digest = digest * 31 + std::uint64_t(result.firstDx + 2) * 5 + std::uint64_t(result.firstDy + 2);
				}
			}
			else
			{
				const auto result = workspace.search(domain, q);
				found += result.status == astar::Status::Found;
				expansions += result.expansions;
				restarts += result.restarts;
				digest = result.cost * 64 + std::uint64_t(result.firstDx + 2) * 5 + std::uint64_t(result.firstDy + 2);
			}
			t.seconds += std::chrono::duration<double>(Clock::now() - begin).count();
			if (pass == 0)
			{
				++t.queries;
				t.expansions += expansions;
				t.restarts += restarts;
				t.found += found;
				t.mix(digest);
			}
		}
	std::printf("domain evaluations per query: %.1f\n", double(BoxDomain::evaluations) / double(chosen.size() * repeat));
	std::printf("%-8s %-10s %8s %12s %10s %9s %9s %18s\n", "kind", "variant", "queries", "ns/query", "exp/query",
				"found", "restarts", "digest");
	for (const auto &[kind, t] : totals)
		if (t.queries)
			std::printf("%-8s %-10s %8zu %12.1f %10.1f %9llu %9llu %018llx\n", kind.c_str(), variant.c_str(), t.queries,
						1e9 * t.seconds / double(t.queries * repeat), double(t.expansions) / double(t.queries),
						(unsigned long long)t.found, (unsigned long long)t.restarts, (unsigned long long)t.digest);
	return 0;
}
