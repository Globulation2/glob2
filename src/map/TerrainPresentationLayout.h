// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
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
