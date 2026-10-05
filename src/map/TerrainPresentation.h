// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainCompatibility.h"
#include <array>
#include <cstdint>
// Semantic editor/interchange metadata. Detailed visual materials are defined by
// data/terrain/tileset.json, independently of gameplay identities and save frames.
struct TerrainColor
{
	std::uint8_t r, g, b;
};
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
	std::array<TerrainPresentation, TERRAIN_COUNT> definitions{{
		{"water", "[water]", {0, 40, 120}, {70, 50, 191}, {0, 64, 255}},
		{"sand", "[sand]", {170, 170, 0}, {182, 168, 48}, {240, 220, 140}},
		{"grass", "[grass]", {0, 90, 0}, {30, 113, 30}, {0, 128, 0}},
		{"ice", "[ice]", {190, 225, 240}, {190, 225, 240}, {190, 225, 240}},
		// Trail retains its legacy external name/key for scripts and files.
		{"road", "[road]", {176, 138, 98}, {176, 138, 98}, {176, 138, 98}},
		{"grass_sand_border", "[sand]", {85, 130, 0}, {106, 140, 39}, {240, 220, 140}},
		{"sand_water_border", "[sand]", {85, 105, 60}, {126, 109, 119}, {240, 220, 140}},
	}};
	definitions[GRASS_SAND_SHORE].editorSelectable = false;
	definitions[SAND_WATER_SHORE].editorSelectable = false;
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
