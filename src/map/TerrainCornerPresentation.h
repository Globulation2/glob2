// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainRegistry.h"
#include "LegacyTerrainFrames.h"
#include <algorithm>
#include <array>

// Presentation of a cell from its four corner terrains (top-left, top-right,
// bottom-left, bottom-right), for drawing code that still thinks per cell.

inline bool classicTerrain(TerrainType type) { return type == WATER || type == SAND || type == GRASS; }

// The corner terrain a whole-cell view of the cell shows: authored (non-classic)
// terrain first, then the most frequent corner, then corner order.
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

// The sprite frame the classic renderer draws the cell with: a classic corner
// pattern's frame range, else the dominant terrain's range, varied by position.
inline std::uint16_t legacyCellFrame(const TerrainRegistry &registry, const std::array<TerrainType, 4> &corners,
									  int x, int y)
{
	const auto hash = terrainVisualHash(x, y);
	if (std::all_of(corners.begin(), corners.end(), classicTerrain))
	{
		const auto &range = legacyClassicFrames(corners[0], corners[1], corners[2], corners[3]);
		return std::uint16_t(range[0] + hash % range[1]);
	}
	const auto &frames = registry.compatibility(dominantCornerTerrain(corners));
	return std::uint16_t(frames.firstFrame + hash % frames.variants);
}
