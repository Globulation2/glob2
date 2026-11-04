// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainMaterials.h"
#include "TerrainPresentation.h"
#include "map/MapAssetBundle.h"
#include <functional>
#include <nlohmann/json_fwd.hpp>
namespace TerrainVisual
{
// The catalog is stored one file per terrain: data/terrain/tileset.json holds
// the shared tables and lists material keys in catalog order, and
// data/terrain/materials/<key>.json holds each block. Inline blocks in the
// list are kept as they are. Returns the single document Catalog::parse reads.
nlohmann::json assembleCatalog(nlohmann::json index,
							   const std::function<nlohmann::json(const std::string &key)> &readMaterial);
// The shipped catalog through the file manager, assembled.
nlohmann::json loadCatalogDocument();
// Catalog and palette metadata load without creating sprites or a graphics context.
Catalog loadCatalog(std::shared_ptr<const MapAssetBundle> assets = MapAssetBundle::empty());
using TerrainPalette = std::array<TerrainColor, TERRAIN_COUNT>;
TerrainPalette minimapPalette(const Catalog &);
TerrainPalette overviewPalette(const Catalog &);
} // namespace TerrainVisual
