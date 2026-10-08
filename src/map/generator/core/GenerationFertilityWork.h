// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationWork.h"
#include "Map.h"

namespace MapGeneration
{
// Fertility kernels also serve simulation, so generator-only callers reserve
// their external work explicitly rather than changing simulation's inner loops.
inline void generationFertilityRebuildWork(int width, int height)
{
	if (!generationWork)
		return;
	const auto tiles = std::uint64_t(width) * height;
	// 31*31 offsets, four box passes and up to 61 padded samples per tile on
	// narrow maps, plus conversions/counts/output. 2048 safely covers both paths.
	generationCheckpoint(tiles * 2048);
	generationAllocation(tiles * 32 + std::uint64_t(height) * (width + 60) * 2);
}

inline void generationFertilityMapWork(const Map &map)
{
	if (!generationWork)
		return;
	const auto width = map.getW(), height = map.getH();
	const auto tiles = std::uint64_t(width) * height;
	bool rebuild;
	{
		std::lock_guard lock(map.growthCacheMutex);
		rebuild = !map.growthCache.validFor(map);
	}
	if (rebuild)
	{
		// A fresh cache computes land and aquatic kernels, local modifiers and
		// two padded views. Include retained output and scratch at their peak.
		generationCheckpoint(tiles * 4096);
		generationAllocation(tiles * 128 + std::uint64_t(width + height) * 180);
	}
	else
	{
		// forMap copies the cached land field and may flood/gate crop deposits.
		generationCheckpoint(tiles * 32);
		generationAllocation(tiles * 16);
	}
}
} // namespace MapGeneration
