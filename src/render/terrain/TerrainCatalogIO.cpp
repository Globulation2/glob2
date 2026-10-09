// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerrainCatalogIO.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <memory>
#include <stdexcept>
namespace TerrainVisual
{
Catalog loadCatalog(std::shared_ptr<const MapAssetBundle> assets)
{
	std::unique_ptr<std::ifstream> input(
		GAGCore::Toolkit::getFileManager()->openIFStream("data/terrain/tileset.json"));
	if (!input || !*input)
		throw std::runtime_error("Cannot read data/terrain/tileset.json");
	auto document = nlohmann::json::parse(*input);
    if (assets && !assets->terrains.empty()) {
        document.erase("compiled_pack");
        for (const auto& [key, value] : assets->terrains.items()) {
            auto material = value; material["key"] = key;
            document["materials"].push_back(material);
            document["bindings"][key] = key;
        }
    }
    return Catalog::parse(document);
}
namespace
{
// Legacy shoreline identities need not have catalog bindings. Keep their distinct
// semantic colors instead of treating both shore profiles as grass/sand.
TerrainPalette palette(const Catalog &catalog, bool overview)
{
	TerrainPalette result{};
	for (unsigned type = 0; type < TERRAIN_COUNT; ++type)
	{
		const auto &semantic = terrainPresentation(TerrainType(type));
		result[type] = overview ? semantic.overview : semantic.minimap;
		const auto binding = catalog.bindings.find(semantic.name);
		if (binding != catalog.bindings.end())
		{
			const auto &material = catalog.materials[binding->second];
			const auto &color = overview ? material.preview : material.minimap;
			result[type] = {color[0], color[1], color[2]};
		}
	}
	return result;
}
} // namespace

TerrainPalette minimapPalette(const Catalog &catalog)
{
	return palette(catalog, false);
}

TerrainPalette overviewPalette(const Catalog &catalog)
{
	return palette(catalog, true);
}
} // namespace TerrainVisual
