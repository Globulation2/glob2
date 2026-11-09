// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerrainCatalogIO.h"
#include "TerrainBuiltins.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <memory>
#include <stdexcept>
namespace TerrainVisual
{
namespace
{
nlohmann::json readJson(const std::string &path)
{
	std::unique_ptr<std::ifstream> input(GAGCore::Toolkit::getFileManager()->openIFStream(path));
	if (!input || !*input)
		throw std::runtime_error("Cannot read " + path);
	return nlohmann::json::parse(*input);
}
} // namespace
nlohmann::json assembleCatalog(nlohmann::json index,
							   const std::function<nlohmann::json(const std::string &key)> &readMaterial)
{
	for (auto &entry : index.at("materials"))
	{
		if (!entry.is_string())
			continue;
		const auto key = entry.get<std::string>();
		auto block = readMaterial(key);
		if (block.value("key", std::string()) != key)
			throw std::runtime_error("data/terrain/materials/" + key + ".json must have key " + key);
		entry = std::move(block);
	}
	return index;
}
nlohmann::json loadCatalogDocument()
{
	return assembleCatalog(readJson("data/terrain/tileset.json"), [](const std::string &key)
						   { return readJson("data/terrain/materials/" + key + ".json"); });
}
Catalog loadCatalog(std::shared_ptr<const MapAssetBundle> assets)
{
	auto document = loadCatalogDocument();
    if (assets && !assets->terrains.empty()) {
        document.erase("compiled_pack");
        // Map files keep the profile keys of the catalog they were made with.
        // bundle_profiles renames retired keys and gives each its border class;
        // any other key is a solid material unless the bundle names a class.
        const auto aliases = document.value("bundle_profiles", nlohmann::json::object());
        for (const auto& [key, value] : assets->terrains.items()) {
            auto material = value; material["key"] = key;
            if (document.value("version", 1) >= 4) {
                const auto alias = aliases.find(material.at("profile").get<std::string>());
                if (alias != aliases.end()) material["profile"] = alias->at("profile");
                if (!material.contains("class"))
                    material["class"] = alias != aliases.end() ? alias->at("class") : nlohmann::json("solid");
            }
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
