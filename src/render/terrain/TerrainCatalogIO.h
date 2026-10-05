// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainMaterials.h"
#include "TerrainPresentation.h"
namespace TerrainVisual
{
// Catalog and preview metadata load without creating sprites or a graphics context.
Catalog loadCatalog();
std::array<TerrainColor, TERRAIN_COUNT> previewPalette(const Catalog &);
} // namespace TerrainVisual
