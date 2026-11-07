// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainType.h"
#include <array>
#include <cstdint>
// Semantic editor/interchange metadata. Detailed visual materials are defined by
// the terrain catalog (data/terrain/), independently of gameplay identities.
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
// The built-ins' presentations, derived from the rows in TerrainCatalog.cpp and
// constant-initialized there, so editing a row recompiles only that file.
extern const std::array<TerrainPresentation, TERRAIN_COUNT> TerrainPresentations;
inline const TerrainPresentation &terrainPresentation(TerrainType type)
{
	return TerrainPresentations[unsigned(type)];
}
