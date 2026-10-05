// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "GradientConstants.h"
#include "map/TerrainProperties.h"

// One cost layer of the gradient bucket queue. Only cells[0..size) are live;
// cells.size() is allocated storage, not the number of queued entries. The
// kernel reserves enough storage for a chunk, writes through raw end cursors,
// then publishes their final offsets back to size. Rejected writes beyond size
// may remain in storage and must never be read as queue entries.
struct GradientBucket
{
    // Compiler-selected power of two above every registered terrain edge.
    // Current definitions retain the 64-bucket fast path; a future quarter-speed
    // swimming terrain automatically enlarges it rather than aliasing a cursor.
    static constexpr unsigned COUNT = [] {
        unsigned largest=GRADIENT_SLOWEST_SWIM_STEP*GRADIENT_DIAGONAL_STEP/GRADIENT_STEP;
        for(const auto& p:TERRAIN_PROPERTIES)
        {
            const unsigned base=p.swimmable?GRADIENT_SLOWEST_SWIM_STEP:GRADIENT_STEP;
            const unsigned cardinal=std::max(1u,(base*256+p.groundSpeedQ8/2)/p.groundSpeedQ8);
            largest=std::max(largest,cardinal*GRADIENT_DIAGONAL_STEP/GRADIENT_STEP);
        }
        unsigned count=1;
        while(count<=largest) count*=2;
        return count;
    }();

	std::vector<std::uint32_t> cells;
	std::size_t size = 0;

	void reserveExtra(std::size_t extra)
	{
		if (size + extra > cells.size())
			grow(size + extra);
	}
	void push(std::uint32_t cell)
	{
		reserveExtra(1);
		cells[size++] = cell;
	}
	void clear() { size = 0; }

private:
	[[gnu::noinline, gnu::cold]] void grow(std::size_t needed)
	{
		cells.resize(std::max(needed, std::max<std::size_t>(256, cells.size() * 2)));
	}
};
