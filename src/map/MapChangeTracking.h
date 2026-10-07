// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapState.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace MapState
{
// Dirty tracking of a cell array at sector granularity (16x16 cells), the unit
// a snapshot capture copies. Every mark stamps its chunk with the next value of
// one increasing sequence, so a captured chunk whose stamp equals the live
// stamp is byte-identical to the live array.
struct ChunkGeometry
{
	static constexpr unsigned Shift = 4;
	static constexpr std::size_t Side = std::size_t(1) << Shift;
	int width = 0, height = 0;
	unsigned wDec = 0;
	Uint32 wMask = 0;
	int chunksWide = 0, chunksHigh = 0;
	void reset(int w, int h, unsigned widthShift, Uint32 widthMask)
	{
		width = w; height = h; wDec = widthShift; wMask = widthMask;
		chunksWide = int((std::size_t(std::max(w, 0)) + Side - 1) / Side);
		chunksHigh = int((std::size_t(std::max(h, 0)) + Side - 1) / Side);
	}
	std::size_t count() const { return std::size_t(chunksWide) * chunksHigh; }
	std::size_t chunkOf(std::size_t index) const
	{ return ((index >> wDec) >> Shift) * chunksWide + ((index & wMask) >> Shift); }
	//! The contiguous cell ranges (start index, length) of one chunk, one per row.
	template<class Visit> void forEachRow(std::size_t chunk, Visit&& visit) const
	{
		const std::size_t cx = chunk % chunksWide, cy = chunk / chunksWide;
		const std::size_t x0 = cx * Side, length = std::min(Side, std::size_t(width) - x0);
		const std::size_t rows = std::min(Side, std::size_t(height) - cy * Side);
		for (std::size_t r = 0; r < rows; ++r) visit((cy * Side + r) * std::size_t(width) + x0, length);
	}
};
struct ChangeTracker
{
	Uint64 generation = 1;
	std::vector<Uint64> chunks;
	void mark(std::size_t chunk) { chunks[chunk] = ++generation; }
	void markAll() { ++generation; std::fill(chunks.begin(), chunks.end(), generation); }
	void reset(std::size_t count) { ++generation; chunks.assign(count, generation); }
};
enum class TrackedArray { Terrain, Resources, Occupancy, Areas, Visibility };
} // namespace MapState
