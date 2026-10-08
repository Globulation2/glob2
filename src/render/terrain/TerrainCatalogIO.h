// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainMaterials.h"
#include "TerrainPresentation.h"
#include "map/MapAssetBundle.h"
namespace TerrainVisual
{
// Catalog and palette metadata load without creating sprites or a graphics context.
Catalog loadCatalog(std::shared_ptr<const MapAssetBundle> assets = MapAssetBundle::empty());
using TerrainPalette = std::array<TerrainColor, TERRAIN_COUNT>;
TerrainPalette minimapPalette(const Catalog &);
TerrainPalette overviewPalette(const Catalog &);
} // namespace TerrainVisual
