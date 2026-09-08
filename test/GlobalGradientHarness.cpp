// SPDX-License-Identifier: GPL-3.0-or-later
// Compare the real global gradient implementation with an independent frontier solver.
#include "GlobalContainer.h"
#include "Map.h"

#include <cstdio>
#include <cstdlib>
#include <queue>
#include <random>
#include <utility>
#include <vector>

GlobalContainer* globalContainer = nullptr;

namespace
{
// Only grid geometry is needed for propagation; avoid allocating game state.
struct GradientMap : Map
{
	GradientMap(int widthShift, int heightShift)
	{
		wDec = widthShift;
		hDec = heightShift;
		w = 1 << wDec;
		h = 1 << hDec;
		wMask = w - 1;
		hMask = h - 1;
		size = static_cast<size_t>(w) * h;
	}

	~GradientMap()
	{
		// Map::clear expects zero geometry when setSize has not allocated its arrays.
		w = h = wMask = hMask = wDec = hDec = 0;
		size = 0;
	}
};

// Process the strongest contributions first instead of using directional sweeps.
// The frontier includes intermediate seeds, not just 255-valued destinations.
std::vector<Uint8> referenceGradient(std::vector<Uint8> values, int width, int height)
{
	using Entry = std::pair<int, size_t>;
	std::priority_queue<Entry> frontier;
	for (size_t i = 0; i < values.size(); ++i)
		if (values[i] >= 3)
			frontier.emplace(values[i], i);

	while (!frontier.empty())
	{
		const auto [value, index] = frontier.top();
		frontier.pop();
		if (values[index] != value || value < 3)
			continue;
		const int x = index % width;
		const int y = index / width;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (dx == 0 && dy == 0)
					continue;
				const int nx = (x + dx + width) % width;
				const int ny = (y + dy + height) % height;
				const size_t neighbor = static_cast<size_t>(ny) * width + nx;
				if (values[neighbor] != 0 && values[neighbor] < value - 1)
				{
					values[neighbor] = value - 1;
					frontier.emplace(value - 1, neighbor);
				}
			}
	}
	return values;
}

void check(int widthShift, int heightShift, const std::vector<Uint8>& input)
{
	GradientMap map(widthShift, heightShift);
	if (input.size() != static_cast<size_t>(map.getW()) * map.getH())
		std::abort();
	const auto expected = referenceGradient(input, map.getW(), map.getH());
	auto actual = input;
	map.updateGlobalGradient(actual.data());
	if (actual != expected)
	{
		std::fprintf(stderr, "Global gradient differs on %dx%d grid\n", map.getW(), map.getH());
		std::abort();
	}
	map.updateGlobalGradient(actual.data());
	if (actual != expected)
	{
		std::fprintf(stderr, "Global gradient is not idempotent\n");
		std::abort();
	}
}

void checkBoundariesAndObstacles()
{
	// Exercise both wrap seams and diagonals, including aliased neighbors on thin grids.
	for (const auto [widthShift, heightShift] : {std::pair{0, 0}, {0, 5}, {5, 0}, {3, 2}, {2, 3}})
	{
		const size_t size = size_t{1} << (widthShift + heightShift);
		std::vector<Uint8> values(size, 1);
		values.front() = 255;
		check(widthShift, heightShift, values);
		values.assign(size, 1);
		values.back() = 254;
		check(widthShift, heightShift, values);
	}

	// A path longer than the byte-valued propagation range must retain unreachable cells.
	std::vector<Uint8> corridor(1024, 1);
	corridor.front() = 255;
	check(10, 0, corridor);

	// Alternating connectors force repeated sweep direction changes without seam shortcuts.
	std::vector<Uint8> maze(64 * 64, 0);
	for (int y = 1; y < 63; y += 2)
	{
		for (int x = 1; x < 63; ++x)
			maze[y * 64 + x] = 1;
		if (y < 61)
			maze[(y + 1) * 64 + ((y / 2) % 2 == 0 ? 62 : 1)] = 1;
	}
	maze[65] = 255;
	check(6, 6, maze);
}

void checkInertFields()
{
	for (Uint8 value : {0, 1, 2})
	{
		check(0, 0, std::vector<Uint8>(1, value));
		check(6, 6, std::vector<Uint8>(64 * 64, value));
	}

	std::vector<Uint8> values(64 * 64);
	for (size_t i = 0; i < values.size(); ++i)
		values[i] = i % 3;
	check(6, 6, values);

	// Even the weakest contributing source must prevent the early return.
	// Put it last so the source scan must inspect the entire buffer.
	for (Uint8 source : {3, 254, 255})
	{
		values.assign(values.size(), 1);
		values.back() = source;
		check(6, 6, values);
	}
}

void checkRandomFields()
{
	std::mt19937 random(424242);
	for (int trial = 0; trial < 3000; ++trial)
	{
		const int widthShift = trial % 9;
		const int heightShift = (trial / 9) % 9;
		std::vector<Uint8> values(size_t{1} << (widthShift + heightShift));
		for (auto& value : values)
		{
			const auto choice = random() % 100;
			value = choice < 25 ? 0 : choice < 90 ? 1 : 2 + random() % 254;
		}
		check(widthShift, heightShift, values);
	}
}
}

int main()
{
	checkBoundariesAndObstacles();
	checkInertFields();
	checkRandomFields();
	std::puts("GlobalGradientHarness: boundaries, obstacles, cutoff, inert fields, mixed seeds and 3000 random fields PASS");
}
