// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainCompatibility.h"
#include "TerrainTypeTable.h"
#include <array>
#include <cstdint>
// Semantic editor/interchange metadata. Detailed visual materials are defined by
// data/terrain/tileset.json, independently of gameplay identities and save frames.
struct TerrainPresentation
{
	const char *name, *label;
	TerrainColor minimap, overview, image;
	TerrainColor preview = minimap;
	bool editorSelectable = true;
};
struct TerrainSharedBackdrop
{
	const char *sprite;
	int firstFrame, frames, ticksPerFrame, scrollDivisorX, scrollDivisorY;
};
inline constexpr TerrainSharedBackdrop TerrainOceanBackdrop{"data/gfx/water", 0, 1, 1, 2, 0};
inline constexpr auto TerrainPresentations = []
{
	std::array<TerrainPresentation, TERRAIN_COUNT> definitions{};
	for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
	{
		const auto &row = TERRAIN_TYPES[i];
		definitions[i] = {row.name, row.label, row.minimap, row.overview, row.image, row.preview,
						  terrainPaintable(TerrainType(i))};
	}
	return definitions;
}();
inline constexpr const TerrainPresentation &terrainPresentation(TerrainType type)
{
	return TerrainPresentations[unsigned(type)];
}
inline constexpr int terrainScrollOffset(int time, int divisor)
{
	return divisor ? time / divisor : 0;
}
inline constexpr int terrainAnimatedFrame(int first, int phases, int ticks, int time)
{
	return first + (unsigned(time) / unsigned(ticks)) % unsigned(phases);
}
