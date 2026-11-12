// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "AStar.h"
#include "AStarReference.h"
#include <queue>
#include <random>

namespace
{
// Entry costs per cell; goalEntry, when set, is what a blocked goal cell may be entered at.
struct ArrayDomain
{
	std::vector<std::uint16_t> entries;
	std::uint16_t goalEntry = 0;
	unsigned entry(std::uint32_t i, bool goal) const { return entries[i] ? entries[i] : goal ? goalEntry : 0; }
};

std::uint16_t pack(unsigned cardinal, unsigned diagonal) { return std::uint16_t(cardinal | diagonal << 8); }

ArrayDomain randomWorld(std::mt19937 &random, int w, int h, unsigned blockedPercent, bool weighted)
{
	ArrayDomain domain;
	domain.entries.resize(std::size_t(w) * h);
	for (auto &e : domain.entries)
	{
		if (random() % 100 < blockedPercent)
			e = 0;
		else if (weighted)
		{
			const unsigned c = 5 + random() % 40;
			e = pack(c, std::min(255u, unsigned(c * 14 / 10 + random() % 3)));
		}
		else
			e = pack(10, 14);
	}
	return domain;
}

astar::Floor floorOf(const ArrayDomain &domain)
{
	unsigned c = 255, d = 255;
	for (auto e : domain.entries)
		if (e)
		{
			c = std::min(c, unsigned(e & 0xff));
			d = std::min(d, unsigned(e >> 8));
		}
	return astar::consistentFloor(c, d);
}

// Exact cheapest costs from start, by Dijkstra; goal cells are terminal.
std::vector<std::uint32_t> dijkstra(const ArrayDomain &domain, int w, int h, std::uint32_t start, std::uint32_t target,
									unsigned radius)
{
	std::vector<std::uint32_t> cost(std::size_t(w) * h, 0xffffffffu);
	using Entry = std::pair<std::uint32_t, std::uint32_t>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
	cost[start] = 0;
	open.emplace(0, start);
	const auto dist = [](int a, int b, int size) {
		const int d = std::abs(a - b);
		return std::min(d, size - d);
	};
	while (!open.empty())
	{
		const auto [c, p] = open.top();
		open.pop();
		if (c != cost[p])
			continue;
		const int x = int(p % unsigned(w)), y = int(p / unsigned(w));
		for (unsigned d = 0; d < 8; ++d)
		{
			const int nx = (x + field::Surrounding[d].x + w) % w, ny = (y + field::Surrounding[d].y + h) % h;
			const auto n = std::uint32_t(ny * w + nx);
			const bool goal = unsigned(dist(nx, int(target % unsigned(w)), w)) <= radius &&
							  unsigned(dist(ny, int(target / unsigned(w)), h)) <= radius;
			const unsigned e = domain.entry(n, goal);
			if (!e)
				continue;
			const auto candidate = c + (astar::detail::diagonal(d) ? e >> 8 : e & 0xff);
			if (candidate < cost[n])
			{
				cost[n] = candidate;
				open.emplace(candidate, n);
			}
		}
	}
	return cost;
}

void requireValidPath(const ArrayDomain &domain, int w, int h, const astar::Query &q, const astar::Result &r)
{
	std::uint32_t previous = q.start, sum = 0;
	for (auto at : r.cells)
	{
		const int dx = int(at % unsigned(w)) - int(previous % unsigned(w));
		const int dy = int(at / unsigned(w)) - int(previous / unsigned(w));
		const int ax = std::min(std::abs(dx), w - std::abs(dx)), ay = std::min(std::abs(dy), h - std::abs(dy));
		REQUIRE((ax <= 1 && ay <= 1 && (ax || ay)));
		const unsigned e = domain.entry(at, at == r.goal);
		REQUIRE(e != 0);
		sum += (ax && ay) ? e >> 8 : e & 0xff;
		previous = at;
	}
	CHECK(sum == r.cost);
	if (!r.cells.empty())
		CHECK(r.cells.back() == r.goal);
}
} // namespace

TEST_SUITE("AStar")
{
	TEST_CASE("bucket queue pops the lowest f, latest push first")
	{
		astar::detail::BucketQueue queue;
		queue.reset(100);
		queue.push(105, 1);
		queue.push(100, 2);
		queue.push(105, 3);
		queue.push(100 + astar::detail::Ring - 1, 4);
		queue.push(100, 5);
		CHECK(queue.pop() == 5);
		CHECK(queue.pop() == 2);
		queue.push(103, 6);
		CHECK(queue.pop() == 6);
		CHECK(queue.pop() == 3);
		CHECK(queue.pop() == 1);
		CHECK(queue.pop() == 4);
		CHECK(queue.empty());
	}

	TEST_CASE("open ground: optimal octile cost and a straight diagonal")
	{
		ArrayDomain domain;
		domain.entries.assign(64 * 64, pack(10, 14));
		astar::Workspace workspace;
		astar::Query q{64, 64, 10 * 64 + 10, 20 * 64 + 30};
		q.floor = floorOf(domain);
		q.wantPath = true;
		const auto r = workspace.search(domain, q);
		REQUIRE(r.status == astar::Status::Found);
		CHECK(r.cost == 10 * 14 + 10 * 10);
		requireValidPath(domain, 64, 64, q, r);
		// Consistent octile with deepest-first ties expands only the path cells.
		CHECK(r.expansions == 21);
	}

	TEST_CASE("start on the goal, wrapped goals and unreachable targets")
	{
		ArrayDomain domain;
		domain.entries.assign(32 * 16, pack(10, 14));
		astar::Workspace workspace;
		astar::Query q{32, 16, 5, 5};
		q.floor = floorOf(domain);
		auto r = workspace.search(domain, q);
		REQUIRE(r.status == astar::Status::Found);
		CHECK(r.cost == 0);
		CHECK((r.firstDx == 0 && r.firstDy == 0));

		// Across both seams: (31, 15) -> (1, 1) is two diagonal steps.
		q.start = 15 * 32 + 31;
		q.target = 1 * 32 + 1;
		r = workspace.search(domain, q);
		REQUIRE(r.status == astar::Status::Found);
		CHECK(r.cost == 28);
		CHECK((r.firstDx == 1 && r.firstDy == 1));

		// Wall the target in.
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (dx || dy)
					domain.entries[std::size_t((8 + dy) * 32 + 8 + dx)] = 0;
		q.start = 0;
		q.target = 8 * 32 + 8;
		r = workspace.search(domain, q);
		CHECK(r.status == astar::Status::Unreachable);
		CHECK((r.firstDx == 0 && r.firstDy == 0));
		// A radius-one goal is the ring itself: blocked, so still unreachable.
		q.goalRadius = 1;
		CHECK(workspace.search(domain, q).status == astar::Status::Unreachable);
		q.goalRadius = 2;
		CHECK(workspace.search(domain, q).status == astar::Status::Found);
	}

	TEST_CASE("goal sets that cannot be entered are unreachable without expanding")
	{
		ArrayDomain domain;
		domain.entries.assign(32 * 32, pack(10, 14));
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				domain.entries[std::size_t((10 + dy) * 32 + 10 + dx)] = 0;
		astar::Workspace workspace;
		astar::Query q{32, 32, 0, 10 * 32 + 10, 1};
		q.floor = floorOf(domain);
		auto r = workspace.search(domain, q);
		CHECK(r.status == astar::Status::Unreachable);
		CHECK(r.expansions == 0);
		// Starting inside the goal set still succeeds at once.
		q.start = 9 * 32 + 9;
		r = workspace.search(domain, q);
		CHECK(r.status == astar::Status::Found);
		CHECK(r.expansions == 1);
		// Many goals: an unenterable goal is unreached and does not hold the search open.
		const std::vector<std::uint32_t> goals{10 * 32 + 10, 3 * 32 + 3};
		std::vector<astar::GoalResult> results(goals.size());
		workspace.searchGoals(domain, astar::GoalQuery{32, 32, 0, 1000, q.floor}, goals, results);
		CHECK_FALSE(results[0].reached);
		CHECK(results[1].reached);
		CHECK(results[1].cost == 42);
	}

	TEST_CASE("cost and expansion budgets")
	{
		ArrayDomain domain;
		domain.entries.assign(64 * 64, pack(10, 14));
		astar::Workspace workspace;
		astar::Query q{64, 64, 0, 10};
		q.floor = floorOf(domain);
		q.maximumCost = 99;
		CHECK(workspace.search(domain, q).status == astar::Status::CostLimit);
		q.maximumCost = 100;
		CHECK(workspace.search(domain, q).status == astar::Status::Found);
		q.maximumExpansions = 5;
		const auto r = workspace.search(domain, q);
		CHECK(r.status == astar::Status::ExpansionLimit);
		CHECK(r.expansions == 5);
	}

	TEST_CASE("blocked goals may be entered when the domain allows it")
	{
		ArrayDomain domain;
		domain.entries.assign(32 * 32, pack(10, 14));
		domain.entries[5 * 32 + 9] = 0;
		astar::Workspace workspace;
		astar::Query q{32, 32, 5 * 32 + 5, 5 * 32 + 9};
		q.floor = floorOf(domain);
		CHECK(workspace.search(domain, q).status == astar::Status::Unreachable);
		domain.goalEntry = pack(10, 14);
		const auto r = workspace.search(domain, q);
		REQUIRE(r.status == astar::Status::Found);
		CHECK(r.cost == 40);
	}

	TEST_CASE("optimal costs and valid paths against Dijkstra")
	{
		std::mt19937 random(7177);
		astar::Workspace workspace;
		for (unsigned trial = 0; trial < 400; ++trial)
		{
			const int w = 1 << (3 + random() % 4), h = 1 << (3 + random() % 4);
			auto domain = randomWorld(random, w, h, random() % 40, trial % 2);
			astar::Query q{w, h, std::uint32_t(random() % unsigned(w * h)), std::uint32_t(random() % unsigned(w * h))};
			q.goalRadius = random() % 4 == 0 ? random() % 3 : 0;
			q.floor = floorOf(domain);
			q.wantPath = true;
			if (random() % 3 == 0)
				q.maximumCost = 20 + random() % 300;
			const auto r = workspace.search(domain, q);
			const auto costs = dijkstra(domain, w, h, q.start, q.target, q.goalRadius);
			std::uint32_t best = 0xffffffffu;
			const auto dist = [](int a, int b, int size) {
				const int d = std::abs(a - b);
				return std::min(d, size - d);
			};
			for (int y = 0; y < h; ++y)
				for (int x = 0; x < w; ++x)
					if (unsigned(dist(x, int(q.target % unsigned(w)), w)) <= q.goalRadius &&
						unsigned(dist(y, int(q.target / unsigned(w)), h)) <= q.goalRadius)
						best = std::min(best, costs[std::size_t(y) * w + x]);
			INFO("trial " << trial);
			if (best <= std::min(q.maximumCost, astar::MaximumCost))
			{
				REQUIRE(r.status == astar::Status::Found);
				CHECK(r.cost == best);
				requireValidPath(domain, w, h, q, r);
			}
			else
				CHECK(r.status != astar::Status::Found);
		}
	}

	TEST_CASE("every window size reproduces the reference expansion sequence")
	{
		std::mt19937 random(9241);
		astar::Workspace workspace;
		std::vector<std::uint32_t> expected, actual;
		workspace.expansionLog = &actual;
		for (unsigned trial = 0; trial < 300; ++trial)
		{
			// From 2 wide (every neighbour aliases across a seam) to 128.
			const int w = 1 << (1 + random() % 7), h = 1 << (1 + random() % 7);
			auto domain = randomWorld(random, w, h, random() % 35, trial % 3 != 0);
			astar::Query q{w, h, std::uint32_t(random() % unsigned(w * h)), std::uint32_t(random() % unsigned(w * h))};
			q.goalRadius = random() % 3 == 0 ? 1 : 0;
			q.floor = trial % 7 == 0 ? astar::Floor{} : floorOf(domain);
			q.wantPath = true;
			q.maximumExpansions = random() % 4 == 0 ? 1 + random() % 200 : 0xffffffffu;
			const auto reference = astar::referenceSearch(domain, q, &expected);
			for (unsigned forced : {0u, 16u, 32u, 64u, 4096u})
			{
				workspace.forcedWindow = forced;
				for (auto kernel : {astar::Kernel::Branchy, astar::Kernel::Scalar, astar::Kernel::SSE2, astar::Kernel::NEON})
				{
				if (!astar::kernelAvailable(kernel))
					continue;
				workspace.kernel = kernel;
				const auto r = workspace.search(domain, q);
				INFO("trial " << trial << " window " << forced << " kernel " << int(kernel));
				REQUIRE(r.status == reference.status);
				CHECK(r.cost == reference.cost);
				CHECK(r.expansions == reference.expansions);
				CHECK(r.cells == reference.cells);
				CHECK((r.firstDx == reference.firstDx && r.firstDy == reference.firstDy));
				CHECK(actual == expected);
				}
			}
		}
	}

	TEST_CASE("search stamps wrap without stale cells")
	{
		// 600 searches on one geometry cross the 8-bit stamp's wrap twice;
		// different worlds each time make any stale cell visible.
		std::mt19937 random(31337);
		astar::Workspace workspace;
		std::vector<std::uint32_t> actual, expected;
		workspace.expansionLog = &actual;
		workspace.forcedWindow = 32;
		for (unsigned trial = 0; trial < 600; ++trial)
		{
			auto domain = randomWorld(random, 64, 64, random() % 30, trial % 2);
			astar::Query q{64, 64, std::uint32_t(random() % 4096), std::uint32_t(random() % 4096)};
			q.floor = floorOf(domain);
			const auto reference = astar::referenceSearch(domain, q, &expected);
			const auto r = workspace.search(domain, q);
			INFO("trial " << trial);
			REQUIRE(r.status == reference.status);
			CHECK(r.cost == reference.cost);
			CHECK(actual == expected);
		}
	}

	TEST_CASE("many goals: reachability and cost of separate searches, steps of the reference")
	{
		std::mt19937 random(5123);
		astar::Workspace workspace;
		for (unsigned trial = 0; trial < 200; ++trial)
		{
			const int w = 1 << (4 + random() % 3), h = 1 << (4 + random() % 3);
			auto domain = randomWorld(random, w, h, random() % 30, trial % 2);
			domain.goalEntry = pack(12, 17);
			const auto start = std::uint32_t(random() % unsigned(w * h));
			std::vector<std::uint32_t> goals(1 + random() % 6);
			for (auto &goal : goals)
			{
				goal = std::uint32_t(random() % unsigned(w * h));
				if (random() % 2)
					domain.entries[goal] = 0; // occupied, like an enemy
			}
			astar::GoalQuery q{w, h, start, std::uint32_t(30 + random() % 200), floorOf(domain)};
			std::vector<astar::GoalResult> results(goals.size());
			std::vector<std::uint32_t> actual, expected;
			workspace.expansionLog = &actual;
			workspace.forcedWindow = std::array{0u, 16u, 32u, 4096u}[trial % 4];
			workspace.kernel = astar::Kernel(trial % 4);
			if (!astar::kernelAvailable(workspace.kernel))
				workspace.kernel = astar::Kernel::Scalar;
			workspace.searchGoals(domain, q, goals, results);
			workspace.expansionLog = nullptr;
			const auto reference = astar::referenceGoals(domain, q, goals, &expected);
			CHECK(actual == expected);
			for (std::size_t i = 0; i < goals.size(); ++i)
			{
				CHECK(results[i].reached == reference[i].reached);
				CHECK(results[i].cost == reference[i].cost);
				CHECK((results[i].firstDx == reference[i].firstDx && results[i].firstDy == reference[i].firstDy));
			}
			for (std::size_t i = 0; i < goals.size(); ++i)
			{
				std::vector<std::uint32_t> costs;
				if (goals[i] == start)
				{
					CHECK(results[i].reached);
					CHECK(results[i].cost == 0);
					continue;
				}
				// Goal cells other than this one are terminal: compute costs with them blocked.
				ArrayDomain terminal = domain;
				for (auto other : goals)
					if (other != goals[i] && other != start)
						terminal.entries[other] = 0;
				terminal.goalEntry = 0;
				if (!domain.entries[goals[i]])
					terminal.entries[goals[i]] = domain.goalEntry;
				costs = dijkstra(terminal, w, h, start, goals[i], 0);
				INFO("trial " << trial << " goal " << i);
				const bool reachable = costs[goals[i]] <= q.maximumCost;
				REQUIRE(results[i].reached == reachable);
				if (reachable)
				{
					CHECK(results[i].cost == costs[goals[i]]);
					CHECK((results[i].firstDx || results[i].firstDy));
				}
			}
		}
	}
}
