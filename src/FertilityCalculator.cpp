// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#include "FertilityCalculator.h"

#include "Map.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

namespace
{
	// 31x31 weighting kernel: water tiles within Chebyshev distance kFertilityRadius
	// of a grass tile contribute weight = int(kSqrtScale * sqrt((R-|dx|)*(R-|dy|))).
	constexpr int kFertilityRadius = 15;
	constexpr int kKernelSide = 2 * kFertilityRadius + 1;
	constexpr float kSqrtScale = 4.2f;

	constexpr std::array<std::pair<int, int>, 8> kBfsNeighbors{{
		{-1, -1}, { 0, -1}, { 1, -1},
		{-1,  0},           { 1,  0},
		{-1,  1}, { 0,  1}, { 1,  1},
	}};

	using DistanceMap = std::vector<std::optional<Uint16>>;

	const std::array<Uint16, kKernelSide * kKernelSide>& fertilityKernel()
	{
		static const auto kernel = []() {
			std::array<Uint16, kKernelSide * kKernelSide> k{};
			for (int ny = -kFertilityRadius; ny <= kFertilityRadius; ++ny)
			{
				for (int nx = -kFertilityRadius; nx <= kFertilityRadius; ++nx)
				{
					const int value = (kFertilityRadius - std::abs(nx))
					                  * (kFertilityRadius - std::abs(ny));
					const int idx = (ny + kFertilityRadius) * kKernelSide
					                + (nx + kFertilityRadius);
					k[idx] = static_cast<Uint16>(
						int(kSqrtScale * std::sqrt(static_cast<float>(value))));
				}
			}
			return k;
		}();
		return kernel;
	}

	/// 8-connected BFS from every takeable corn/wood tile, traversing only grass
	/// cells. Cells that are unreachable (or non-grass and not seeded) stay nullopt.
	DistanceMap computeResourceDistance(const Map& map)
	{
		DistanceMap distance(static_cast<size_t>(map.getW()) * map.getH());
		std::queue<std::pair<int, int>> frontier;

		for (int x = 0; x < map.getW(); ++x)
		{
			for (int y = 0; y < map.getH(); ++y)
			{
				if (map.isRessourceTakeable(x, y, CORN)
				    || map.isRessourceTakeable(x, y, WOOD))
				{
					distance[map.coordToIndex(x, y)] = 0;
					frontier.emplace(x, y);
				}
			}
		}

		while (!frontier.empty())
		{
			const auto [px, py] = frontier.front();
			frontier.pop();
			const Uint16 nextDepth =
				static_cast<Uint16>(*distance[map.coordToIndex(px, py)] + 1);

			for (const auto [dx, dy] : kBfsNeighbors)
			{
				const int nx = map.normalizeX(px + dx);
				const int ny = map.normalizeY(py + dy);
				auto& cell = distance[map.coordToIndex(nx, ny)];
				if (!cell.has_value() && map.isGrass(nx, ny))
				{
					cell = nextDepth;
					frontier.emplace(nx, ny);
				}
			}
		}
		return distance;
	}
}

namespace FertilityCalculator
{
	void compute(Map& map, const ProgressCallback& progress)
	{
		// BFS is fast relative to the kernel pass, so it isn't progress-reported.
		const DistanceMap reachable = computeResourceDistance(map);
		const auto& kernel = fertilityKernel();

		std::vector<Uint16> fertility(
			static_cast<size_t>(map.getW()) * map.getH(), 0);
		Uint16 fertilityMax = 0;

		for (int x = 0; x < map.getW(); ++x)
		{
			if (progress)
				progress(static_cast<float>(x) / static_cast<float>(map.getW()));

			for (int y = 0; y < map.getH(); ++y)
			{
				if (!map.isGrass(x, y))
					continue;
				if (!reachable[map.coordToIndex(x, y)].has_value())
					continue;

				Uint16 total = 0;
				for (int ny = -kFertilityRadius; ny <= kFertilityRadius; ++ny)
				{
					for (int nx = -kFertilityRadius; nx <= kFertilityRadius; ++nx)
					{
						// Map::isWater wraps coords via coordToIndex; no normalize needed.
						if (map.isWater(x + nx, y + ny))
						{
							const int kIdx = (ny + kFertilityRadius) * kKernelSide
							                 + (nx + kFertilityRadius);
							total += kernel[kIdx];
						}
					}
				}
				fertilityMax = std::max(fertilityMax, total);
				fertility[map.coordToIndex(x, y)] = total;
			}
		}

		for (int x = 0; x < map.getW(); ++x)
			for (int y = 0; y < map.getH(); ++y)
				map.getCase(x, y).fertility = fertility[map.coordToIndex(x, y)];
		map.fertilityMaximum = fertilityMax;
	}
}
