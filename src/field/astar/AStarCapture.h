// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Query capture for tools/astar_benchmark.cpp. With GLOB2_ASTAR_CAPTURE=<file>
// set, every search appends its query and the entry costs of the window it ran
// in, as the Domain answered them, so the benchmark can replay real games
// without a map. Capture only reads; it never changes a search result.
//
// Record (little-endian): "AST1", kind (0 point, 1 goals), map width, height,
// box x, y, width, height (world cells, may wrap), start, target, goal radius,
// maximum cost, maximum expansions, floor cardinal, floor diagonal, goal count,
// goals, then box width x height u16 entries, row by row.
#include "AStar.h"
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace astar::capture
{
struct Sink
{
	std::FILE *file = nullptr;
	std::mutex mutex;
	unsigned long remaining = 0;
	Sink()
	{
		if (const char *path = std::getenv("GLOB2_ASTAR_CAPTURE"); path && *path)
		{
			file = std::fopen(path, "ab");
			const char *limit = std::getenv("GLOB2_ASTAR_CAPTURE_LIMIT");
			remaining = limit ? std::strtoul(limit, nullptr, 10) : 200000;
		}
	}
	~Sink()
	{
		if (file)
			std::fclose(file);
	}
};
inline Sink &sink()
{
	static Sink instance;
	return instance;
}
inline bool enabled() { return sink().file != nullptr; }

struct Box
{
	int x = 0, y = 0, width = 0, height = 0;
};

namespace detail
{
inline void put(std::vector<unsigned char> &out, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		out.push_back((unsigned char)(v >> (8 * i)));
}
inline void finish(std::vector<unsigned char> &&record)
{
	auto &s = sink();
	std::lock_guard lock(s.mutex);
	if (!s.remaining)
		return;
	--s.remaining;
	std::fwrite(record.data(), 1, record.size(), s.file);
}
template <class Domain, class GoalTest>
void write(const Domain &domain, unsigned kind, int width, int height, Box box, std::uint32_t start,
		   std::uint32_t target, unsigned radius, std::uint32_t maximumCost, std::uint32_t maximumExpansions,
		   Floor floor, std::span<const std::uint32_t> goals, GoalTest &&isGoal)
{
	std::vector<unsigned char> out;
	out.reserve(80 + std::size_t(box.width) * box.height * 2);
	out.insert(out.end(), {'A', 'S', 'T', '1'});
	for (std::uint32_t v : {std::uint32_t(kind), std::uint32_t(width), std::uint32_t(height), std::uint32_t(box.x),
							std::uint32_t(box.y), std::uint32_t(box.width), std::uint32_t(box.height), start, target,
							std::uint32_t(radius), maximumCost, maximumExpansions, floor.cardinal, floor.diagonal,
							std::uint32_t(goals.size())})
		put(out, v);
	for (auto g : goals)
		put(out, g);
	const unsigned shift = unsigned(std::countr_zero(unsigned(width)));
	for (int dy = 0; dy < box.height; ++dy)
		for (int dx = 0; dx < box.width; ++dx)
		{
			const int x = (box.x + dx) & (width - 1), y = (box.y + dy) & (height - 1);
			const auto world = std::uint32_t((y << shift) + x);
			const unsigned entry = world == start ? 0 : domain.entry(world, isGoal(x, y, world));
			out.push_back((unsigned char)entry);
			out.push_back((unsigned char)(entry >> 8));
		}
	finish(std::move(out));
}
inline int torusDistance(int a, int b, int size)
{
	const int d = std::abs(a - b);
	return std::min(d, size - d);
}
} // namespace detail

// The box covers the reach of the search: the window it finished in, or the
// whole map for a torus search.
inline Box boxOf(int width, int height, std::uint32_t start, int windowSide)
{
	if (!windowSide || windowSide >= width || windowSide >= height)
		return {0, 0, width, height};
	const unsigned shift = unsigned(std::countr_zero(unsigned(width)));
	const int sx = int(start & unsigned(width - 1)), sy = int(start >> shift);
	return {sx - windowSide / 2, sy - windowSide / 2, windowSide, windowSide};
}

template <class Domain> void point(const Domain &domain, const Query &q, int windowSide)
{
	const unsigned shift = unsigned(std::countr_zero(unsigned(q.width)));
	const int tx = int(q.target & unsigned(q.width - 1)), ty = int(q.target >> shift);
	detail::write(domain, 0, q.width, q.height, boxOf(q.width, q.height, q.start, windowSide), q.start, q.target,
				  q.goalRadius, q.maximumCost, q.maximumExpansions, q.floor, {}, [&](int x, int y, std::uint32_t) {
					  return unsigned(detail::torusDistance(x, tx, q.width)) <= q.goalRadius &&
							 unsigned(detail::torusDistance(y, ty, q.height)) <= q.goalRadius;
				  });
}

template <class Domain>
void goals(const Domain &domain, const GoalQuery &q, std::span<const std::uint32_t> goalCells, int windowSide)
{
	detail::write(domain, 1, q.width, q.height, boxOf(q.width, q.height, q.start, windowSide), q.start, 0, 0,
				  q.maximumCost, 0xffffffffu, q.sizing, goalCells, [&](int, int, std::uint32_t world) {
					  return std::find(goalCells.begin(), goalCells.end(), world) != goalCells.end();
				  });
}
} // namespace astar::capture
