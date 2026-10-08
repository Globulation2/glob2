// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainRegistry.h"
#include <algorithm>
#include <array>

// Presentation of a cell from its four corner terrains (top-left, top-right,
// bottom-left, bottom-right), for drawing code that still thinks per cell.

constexpr bool classicTerrain(TerrainType type) { return type == WATER || type == SAND || type == GRASS; }

// The corner terrain a whole-cell view of the cell shows (preview hue, appearance).
// Catalogue terrain wins over classic so small features stay visible in those
// coarse views; then the most frequent corner, then corner order.
inline TerrainType dominantCornerTerrain(const std::array<TerrainType, 4> &corners)
{
	TerrainType best = corners[0];
	int bestScore = -1;
	for (const auto type : corners)
	{
		const int score = int(std::count(corners.begin(), corners.end(), type)) + (classicTerrain(type) ? 0 : 8);
		if (score > bestScore)
		{
			best = type;
			bestScore = score;
		}
	}
	return best;
}
