// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in standalone benchmark. Build/run with tools/gradient_benchmark.py.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <cstddef>
#include <new>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

struct Allocations
{
	std::size_t current = 0, peak = 0, calls = 0, bytes = 0;
} allocations;
#ifdef GLOB2_GRADIENT_BENCH_COUNTERS
// Instrumented builds account for requested allocation bytes, excluding allocator
// headers. Timed acceptance runs omit these overrides and the kernel counters.
struct alignas(std::max_align_t) AllocationHeader
{
	std::size_t size;
};
void *operator new(std::size_t n)
{
	auto *p = static_cast<AllocationHeader *>(std::malloc(n + sizeof(AllocationHeader)));
	if (!p)
		throw std::bad_alloc();
	p->size = n;
	allocations.current += n;
	allocations.peak = std::max(allocations.peak, allocations.current);
	++allocations.calls;
	allocations.bytes += n;
	return p + 1;
}
void operator delete(void *p) noexcept
{
	if (p)
	{
		auto *h = static_cast<AllocationHeader *>(p) - 1;
		allocations.current -= h->size;
		std::free(h);
	}
}
void operator delete(void *p, std::size_t) noexcept
{
	::operator delete(p);
}
void *operator new[](std::size_t n)
{
	return ::operator new(n);
}
void operator delete[](void *p) noexcept
{
	::operator delete(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
	::operator delete(p);
}
#endif

struct Counters
{
	std::uint64_t popped = 0, stale = 0, relaxations = 0, occupied = 0, chunkReserves = 0;
} benchmark_counters;
#ifdef GLOB2_GRADIENT_BENCH_COUNTERS
#define GLOB2_GRADIENT_BENCH_EVENT(name, count) (benchmark_counters.name += (count))
#endif
#include "field/TerrainGradient.h"
#include "baseline_gradient.h"
#include "field/TerrainTravel.h"
#include "baseline_travel.h"
using namespace gradient_kernel;
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point start)
{
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::uint64_t digest(const std::vector<std::uint16_t> &v)
{
	std::uint64_t h = 1469598103934665603ull;
	for (auto x : v)
		h = (h ^ x) * 1099511628211ull;
	return h;
}
std::size_t retained(const GradientWorkspace &w)
{
	std::size_t n = w.deferredSeeds.capacity() * sizeof(std::pair<int, int>);
	for (const auto &b : w.buckets)
		n += b.cells.capacity() * sizeof(std::uint32_t);
	return n;
}

struct Options
{
	int width = 128, height = 128, swim = 3, registry = 7, repeats = 5, cap = COST_LIMIT,
		travel = 1;
	std::string pattern = "dense", costs = "equivalent", seeds = "single", layout = "shared",
				mode = "terrain";
	bool oracle = true;
};

// Deliberately independent heap Dijkstra; an expanded cell supplies the cost of
// entering it in the forward direction, hence its own terrain supplies both edges.
template <std::size_t N>
std::vector<std::uint16_t>
reference(const std::vector<std::uint16_t> &seeds, const std::vector<std::uint8_t> &terrain,
		  const std::array<EntrySteps, N> &costs, field::Grid grid, int cap)
{
	auto result = seeds;
	using Node = std::pair<unsigned, std::size_t>;
	std::priority_queue<Node, std::vector<Node>, std::greater<Node>> q;
	for (std::size_t i = 0; i < seeds.size(); ++i)
		if (seeds[i] > 1)
			q.push({unsigned(GRADIENT_AT_GOAL - seeds[i]), i});
	while (!q.empty())
	{
		const auto [d, i] = q.top();
		q.pop();
		if (d > unsigned(cap))
			break;
		if (result[i] != GRADIENT_AT_GOAL - d)
			continue;
		const auto cost = costs[terrain[i]];
		const int x = i % grid.width(), y = i / grid.width();
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (dx || dy)
				{
					const auto next = grid.index(x + dx, y + dy);
					const unsigned nd = d + (dx && dy ? cost.diagonal : cost.cardinal);
					if (result[next] != 0 && nd <= unsigned(cap) &&
						unsigned(GRADIENT_AT_GOAL - nd) > result[next])
					{
						result[next] = GRADIENT_AT_GOAL - nd;
						q.push({nd, std::size_t(next)});
					}
				}
	}
	return result;
}

// Strategic fields use a different encoding and Chebyshev edge metric. Keep
// this oracle independent of both production traversal and the frozen baseline.
std::vector<std::uint16_t> referenceTravel(const std::vector<std::uint16_t> &markers,
										   const std::vector<std::uint8_t> &terrain, int width,
										   int height, int travel)
{
	constexpr auto infinity = std::numeric_limits<std::uint64_t>::max();
	using Node = std::pair<std::uint64_t, std::size_t>;
	std::priority_queue<Node, std::vector<Node>, std::greater<Node>> frontier;
	std::vector<std::uint64_t> distance(markers.size(), infinity);
	for (std::size_t i = 0; i < markers.size(); ++i)
		if (markers[i] == 2)
		{
			distance[i] = 0;
			frontier.emplace(0, i);
		}
	while (!frontier.empty())
	{
		const auto [cost, index] = frontier.top();
		frontier.pop();
		if (cost != distance[index])
			continue;
		const auto &properties = terrainProperties(static_cast<TerrainType>(terrain[index]));
		const unsigned speed = travel == 3 ? properties.airSpeedQ8 : properties.groundSpeedQ8;
		const unsigned step = std::max(1u, (10u * 256u + speed / 2u) / speed);
		const int x = index % width, y = index / width;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (!dx && !dy)
					continue;
				const auto next =
					std::size_t((y + dy + height) % height) * width + (x + dx + width) % width;
				if (markers[next] == 1 || cost + step >= distance[next])
					continue;
				distance[next] = cost + step;
				frontier.emplace(distance[next], next);
			}
	}
	auto result = markers;
	for (std::size_t i = 0; i < result.size(); ++i)
		if (distance[i] != infinity)
			result[i] = 2 + std::min<std::uint64_t>(32765, (distance[i] + 9) / 10);
	return result;
}

template <std::size_t N, class At>
void baseline(std::uint16_t *gradient, int cap, field::Grid grid, GradientWorkspace &workspace,
			  const std::array<EntrySteps, N> &costs, At at)
{
	auto *buckets = workspace.buckets.data();
	for (auto &b : workspace.buckets)
		b.clear();
	auto &deferred = workspace.deferredSeeds;
	deferred.clear();
	std::size_t pending = 0;
	for (std::size_t i = 0; i < grid.cells(); ++i)
		if (gradient[i] > 1)
		{
			int d = GRADIENT_AT_GOAL - gradient[i];
			if (d < int(BUCKETS))
			{
				buckets[d].push(i);
				++pending;
			}
			else
				deferred.push_back({d, int(i)});
		}
	std::sort(deferred.begin(), deferred.end());
	std::size_t next = 0;
	for (int cur = 0; (pending || next < deferred.size()) && cur <= cap; ++cur)
	{
		if (!pending)
			cur = deferred[next].first;
		for (; next < deferred.size() && deferred[next].first == cur; ++next)
		{
			buckets[unsigned(cur) % BUCKETS].push(deferred[next].second);
			++pending;
		}
		expandBaselineTerrainBucket(gradient, buckets, pending, cur, cap, grid, costs, at);
	}
}

template <std::size_t N> void run(const Options &o)
{
	// Generate inputs once, outside all timed samples. Keep RNG draws and seed
	// overrides in this order so historical manifests retain the same fixtures.
	const field::Grid grid(o.width, o.height);
	const auto n = grid.cells();
	std::array<EntrySteps, N> costs{};
	for (unsigned t = 0; t < N; ++t)
	{
		// Duplicate identities preserve real cost classes; distinct registries exercise
		// more edge pairs, including cardinal/diagonal aliases and ring-edge extremes.
		costs[t] = o.costs == "distinct"
					   ? EntrySteps{1 + t % (BUCKETS - 2), 1 + (t * 17 + 9) % (BUCKETS - 2)}
					   : TERRAIN_ENTRY_COSTS[o.swim][t % TERRAIN_COUNT];
	}
	std::vector<std::uint8_t> terrain(n);
	std::vector<std::uint16_t> seed(n, 1);
	std::uint32_t random = 1427;
	auto next = [&]
	{
		random ^= random << 13;
		random ^= random >> 17;
		random ^= random << 5;
		return random;
	};
	for (std::size_t i = 0; i < n; ++i)
	{
		const unsigned x = i % o.width, y = i / o.width, r = next();
		unsigned t = GRASS;
		if (o.pattern == "classic")
			t = r % 5 == 0 ? WATER : GRASS;
		else if (o.pattern == "road")
			t = TRAIL;
		else if (o.pattern == "ice")
			t = ICE;
		else if (o.pattern == "sparse")
			t = r % 97 == 0 ? TRAIL : GRASS;
		else if (o.pattern == "network")
			t = (x % 16 == 0 || y % 16 == 0) ? TRAIL : (r % 11 == 0 ? WATER : GRASS);
		else if (o.pattern == "dense")
			t = r % (o.costs == "equivalent" ? TERRAIN_COUNT : N);
		else if (o.pattern == "isolated")
		{
			t = (x > unsigned(o.width / 3) && x < unsigned(2 * o.width / 3) &&
				 y > unsigned(o.height / 3) && y < unsigned(2 * o.height / 3))
					? TRAIL
					: GRASS;
			if (((x == unsigned(o.width / 3) || x == unsigned(2 * o.width / 3)) &&
				 y >= unsigned(o.height / 3) && y <= unsigned(2 * o.height / 3)) ||
				((y == unsigned(o.height / 3) || y == unsigned(2 * o.height / 3)) &&
				 x >= unsigned(o.width / 3) && x <= unsigned(2 * o.width / 3)))
				seed[i] = 0;
		}
		else
			throw std::runtime_error("unknown pattern");
		if (o.costs == "equivalent")
			t += TERRAIN_COUNT * ((r >> 8) % ((N - 1 - t) / TERRAIN_COUNT + 1));
		terrain[i] = t;
		if ((r >> 10) % 29 == 0 || (o.swim == 0 && t % TERRAIN_COUNT == WATER))
			seed[i] = 0;
		if (o.seeds == "dense" && r % 71 == 0)
			seed[i] = GRADIENT_AT_GOAL;
	}
	seed[0] = GRADIENT_AT_GOAL;
	if (o.seeds == "deferred")
	{
		seed[n / 3] = GRADIENT_AT_GOAL - (BUCKETS + 31);
		seed[n * 2 / 3] = GRADIENT_AT_GOAL - 701;
	}
	if (o.mode == "strategic")
		for (std::size_t i = 0; i < n; ++i)
		{
			seed[i] = seed[i] > 1 ? 2 : seed[i] == 0 ? 1 : 0;
			if (seed[i] != 2 && !field::terrainTravelAllowed(
									terrainProperties(static_cast<TerrainType>(terrain[i])),
									static_cast<field::TerrainTravel>(o.travel)))
				seed[i] = 1;
		}
	// Report preparation separately from propagation. The experimental class plane
	// and snapshot are measured even when the selected kernel does not consume them.
	const auto prepareStart = Clock::now();
	const PreparedTerrainCosts<N> profile(costs);
	const double prepareMs = milliseconds(prepareStart);
	auto planeStart = Clock::now();
	std::vector<std::uint8_t> classes(n);
	for (std::size_t i = 0; i < n; ++i)
		classes[i] = profile.terrainClasses[terrain[i]];
	const double planeMs = milliseconds(planeStart);
	auto snapshotStart = Clock::now();
	auto snapshot = std::make_shared<const std::vector<std::uint8_t>>(terrain);
	const double snapshotMs = milliseconds(snapshotStart);
	std::vector<std::uint16_t> shared(n), separate(n), expected;
	if (o.oracle)
	{
		if (o.mode == "strategic")
			expected = referenceTravel(seed, terrain, o.width, o.height, o.travel);
		else
			expected = reference(seed, terrain, costs, grid, o.cap);
	}
	GradientWorkspace sharedWorkspace, otherWorkspace;
	std::uint64_t baselineHash = 0;
	// Repetition -1 measures cold storage for each implementation. Later pairs
	// alternate execution order; shared layout also matches output/scratch addresses.
	for (int repetition = -1; repetition < o.repeats; ++repetition)
		for (int order = 0; order < 2; ++order)
		{
			const bool candidate = ((order + (repetition + 2) % 2) % 2) != 0;
			auto &out = o.layout == "shared" || !candidate ? shared : separate;
			auto &workspace = o.layout == "shared" || !candidate ? sharedWorkspace : otherWorkspace;
			if (repetition < 0)
				workspace = GradientWorkspace{};
			const auto initStart = Clock::now();
			std::copy(seed.begin(), seed.end(), out.begin());
			const double initMs = milliseconds(initStart);
			// Only dispatch and propagation lie inside the wall/CPU measurement.
			// Hashes, independent-oracle checks and JSON output follow the stop time.
			benchmark_counters = {};
			const auto allocBefore = allocations;
			allocations.peak = allocations.current;
			const auto wallStart = Clock::now();
			const auto cpuStart = std::clock();
			if (o.mode == "strategic")
			{
				if (candidate)
					field::expandTerrainTravel(
						out, o.width, o.height, static_cast<field::TerrainTravel>(o.travel),
						[&](std::size_t i) { return static_cast<TerrainType>(terrain[i]); });
				else
					baseline_field::expandTerrainTravel(
						out, o.width, o.height,
						static_cast<baseline_field::TerrainTravel>(o.travel),
						[&](std::size_t i) { return static_cast<TerrainType>(terrain[i]); });
			}
			else if (o.mode == "dispatch")
			{
				if constexpr (N == TERRAIN_COUNT)
				{
					const bool modified = o.pattern != "classic";
					if (candidate)
						propagateTerrainField(
							out.data(), o.swim, o.cap, grid, workspace, [&](std::size_t i)
							{ return static_cast<TerrainType>(terrain[i]); }, modified);
					else if (!modified)
						propagateField(
							out.data(), o.swim, o.cap, grid, workspace, [&](std::size_t i)
							{ return terrainUsesSwimming(static_cast<TerrainType>(terrain[i])); });
					else
						baseline(out.data(), o.cap, grid, workspace, costs,
								 [&](std::size_t i) { return terrain[i]; });
				}
				else
					throw std::runtime_error("dispatch requires the real registry size");
			}
			else if (!candidate)
				baseline(out.data(), o.cap, grid, workspace, costs,
						 [&](std::size_t i) { return terrain[i]; });
			else if (o.mode == "plane")
				propagatePreparedTerrainField(out.data(), o.cap, grid, workspace, profile,
											  [&](std::size_t i) { return classes[i]; });
			else
				propagatePreparedTerrainField(out.data(), o.cap, grid, workspace, profile,
											  [&](std::size_t i)
											  { return profile.terrainClasses[terrain[i]]; });
			const double cpuMs = 1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC,
						 wallMs = milliseconds(wallStart);
			const auto allocAfter = allocations;
			const auto hash = digest(out);
			if (!candidate)
				baselineHash = hash;
			if (o.oracle && out != expected)
				throw std::runtime_error(std::string(candidate ? "candidate" : "baseline") +
										 " differs from independent oracle");
			if (baselineHash && candidate && hash != baselineHash)
				throw std::runtime_error("candidate/baseline mismatch");

			std::cout
				<< "{\"candidate\":" << (candidate ? "true" : "false")
				<< ",\"repetition\":" << repetition << ",\"order\":" << order
				<< ",\"width\":" << o.width << ",\"height\":" << o.height
				<< ",\"travel\":" << o.travel << ",\"swim\":" << o.swim << ",\"registry\":" << N
				<< ",\"pattern\":\"" << o.pattern << "\",\"costs\":\"" << o.costs
				<< "\",\"seeds\":\"" << o.seeds << "\",\"layout\":\"" << o.layout
				<< "\",\"mode\":\"" << o.mode << "\",\"cap\":" << o.cap << ",\"wall_ms\":" << wallMs
				<< ",\"cpu_ms\":" << cpuMs << ",\"initialization_ms\":" << initMs
				<< ",\"profile_ms\":" << prepareMs << ",\"plane_ms\":" << planeMs
				<< ",\"snapshot_ms\":" << snapshotMs
				<< ",\"allocation_calls\":" << allocAfter.calls - allocBefore.calls
				<< ",\"allocated_bytes\":" << allocAfter.bytes - allocBefore.bytes
				<< ",\"peak_extra_bytes\":" << allocAfter.peak - allocBefore.current
				<< ",\"retained_queue_bytes\":" << retained(workspace)
				<< ",\"input_bytes\":" << terrain.size()
				<< ",\"output_bytes\":" << out.size() * sizeof(out[0])
				<< ",\"workspace_object_bytes\":" << (o.mode == "strategic" ? 0 : sizeof(workspace))
				<< ",\"local_queue_object_bytes\":"
				<< (o.mode == "strategic"
						? (candidate
							   ? sizeof(std::array<GradientBucket, field::TERRAIN_TRAVEL_BUCKETS>)
							   : sizeof(
									 std::priority_queue<std::pair<unsigned, int>,
														 std::vector<std::pair<unsigned, int>>,
														 std::greater<std::pair<unsigned, int>>>))
						: 0)
				<< ",\"local_cost_table_bytes\":"
				<< (o.mode == "strategic" && candidate ? sizeof(std::array<unsigned, TERRAIN_COUNT>)
													   : 0)
				<< ",\"cost_classes\":" << profile.classCount
				<< ",\"unique_steps\":" << profile.stepCount
				<< ",\"profile_bytes\":" << (o.mode == "strategic" ? 0 : sizeof(profile))
				<< ",\"plane_bytes\":"
				<< (o.mode == "plane" ? classes.capacity() * sizeof(classes[0]) : 0)
				<< ",\"snapshot_bytes\":" << snapshot->capacity() << ",\"hash\":\"" << hash << "\""
				<< ",\"popped\":" << benchmark_counters.popped
				<< ",\"stale\":" << benchmark_counters.stale
				<< ",\"relaxations\":" << benchmark_counters.relaxations
				<< ",\"occupied\":" << benchmark_counters.occupied
				<< ",\"chunk_reserves\":" << benchmark_counters.chunkReserves << "}\n";
		}
}
namespace
{
int parseInteger(const std::string &value)
{
	std::size_t consumed = 0;
	const int result = std::stoi(value, &consumed);
	if (consumed != value.size())
		throw std::runtime_error("invalid integer: " + value);
	return result;
}

bool oneOf(const std::string &value, std::initializer_list<const char *> choices)
{
	return std::any_of(choices.begin(), choices.end(),
					   [&](const char *choice) { return value == choice; });
}

Options parseOptions(int argc, char **argv)
{
	Options options;
	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
		if (argument == "--no-oracle")
		{
			options.oracle = false;
			continue;
		}
		if (i + 1 == argc)
			throw std::runtime_error("missing value for " + argument);
		const std::string value = argv[++i];
		if (argument == "--size")
			options.width = options.height = parseInteger(value);
		else if (argument == "--width")
			options.width = parseInteger(value);
		else if (argument == "--height")
			options.height = parseInteger(value);
		else if (argument == "--swim")
			options.swim = parseInteger(value);
		else if (argument == "--travel")
			options.travel = parseInteger(value);
		else if (argument == "--registry")
			options.registry = parseInteger(value);
		else if (argument == "--repeats")
			options.repeats = parseInteger(value);
		else if (argument == "--cap")
			options.cap = parseInteger(value);
		else if (argument == "--pattern")
			options.pattern = value;
		else if (argument == "--costs")
			options.costs = value;
		else if (argument == "--seeds")
			options.seeds = value;
		else if (argument == "--layout")
			options.layout = value;
		else if (argument == "--mode")
			options.mode = value;
		else
			throw std::runtime_error("unknown argument " + argument);
	}
	if (!oneOf(options.pattern,
			   {"classic", "road", "ice", "sparse", "network", "dense", "isolated"}))
		throw std::runtime_error("invalid pattern");
	if (!oneOf(options.costs, {"equivalent", "distinct"}))
		throw std::runtime_error("invalid costs");
	if (!oneOf(options.seeds, {"single", "dense", "deferred"}))
		throw std::runtime_error("invalid seeds");
	if (!oneOf(options.layout, {"shared", "separate"}))
		throw std::runtime_error("invalid layout");
	if (!oneOf(options.mode, {"terrain", "plane", "dispatch", "strategic"}))
		throw std::runtime_error("invalid mode");
	// Fixture borders and wrapped oracle coordinates may temporarily double a
	// dimension even when the final linear index fits in a signed int.
	if (options.width < 1 || options.height < 1 ||
		options.width > std::numeric_limits<int>::max() / 2 ||
		options.height > std::numeric_limits<int>::max() / 2 ||
		options.width > std::numeric_limits<int>::max() / options.height)
		throw std::runtime_error("dimensions must be positive, at most INT_MAX/2 each, and contain at most INT_MAX cells");
	if (options.swim < 0 || options.swim > 6 || options.cap < 0 || options.cap > COST_LIMIT ||
		options.repeats < 1)
		throw std::runtime_error("invalid profile/cap/repeats");

	// Production dispatch and strategic travel consume the registered property
	// table. Synthetic costs would otherwise be reported without being used.
	if ((options.mode == "dispatch" || options.mode == "strategic") &&
		(options.registry != TERRAIN_COUNT || options.costs != "equivalent"))
		throw std::runtime_error(
			"dispatch/strategic modes require the real registry and equivalent costs");
	if (options.mode == "strategic" && (options.travel < 1 || options.travel > 3 ||
										options.cap != COST_LIMIT || options.seeds == "deferred"))
		throw std::runtime_error(
			"strategic mode requires travel 1,2,3, the default cap, and non-deferred seeds");
	return options;
}
} // namespace

int main(int argc, char **argv)
{
	try
	{
		const auto options = parseOptions(argc, argv);
		switch (options.registry)
		{
		case 7:
			run<7>(options);
			break;
		case 8:
			run<8>(options);
			break;
		case 32:
			run<32>(options);
			break;
		case 64:
			run<64>(options);
			break;
		default:
			throw std::runtime_error("registry must be 7,8,32,64");
		}
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
