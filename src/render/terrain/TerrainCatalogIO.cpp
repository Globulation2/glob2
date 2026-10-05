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
Catalog loadCatalog()
{
	std::unique_ptr<std::ifstream> input(
		GAGCore::Toolkit::getFileManager()->openIFStream("data/terrain/tileset.json"));
	if (!input || !*input)
		throw std::runtime_error("Cannot read data/terrain/tileset.json");
	return Catalog::parse(nlohmann::json::parse(*input));
}
std::array<TerrainColor, TERRAIN_COUNT> previewPalette(const Catalog &catalog)
{
	std::array<TerrainColor, TERRAIN_COUNT> result{};
	for (unsigned type = 0; type < TERRAIN_COUNT; ++type)
	{
		const auto &semantic = terrainPresentation(TerrainType(type));
		result[type] = semantic.preview;
		const auto binding = catalog.bindings.find(semantic.name);
		if (binding != catalog.bindings.end())
		{
			const auto &color = catalog.materials[binding->second].minimap;
			result[type] = {color[0], color[1], color[2]};
		}
	}
	return result;
}
} // namespace TerrainVisual
