// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <memory>
#include <map>
#include <string>
#include <string_view>
class TerrainRegistry;
class ResourceRegistry;

// CPU-only, immutable map content. Graphics handles belong to the renderer.
// Built-in artwork is never stored here.
class MapAssetBundle
{
public:
    static constexpr std::size_t MaximumBytes = 16 * 1024 * 1024;
    static constexpr std::size_t MaximumDecodedBytes = 64 * 1024 * 1024;
    struct Sheet {
        std::string png;
        unsigned width, height, frameWidth, frameHeight;
        unsigned frames() const { return (width / frameWidth) * (height / frameHeight); }
    };
    static nlohmann::json parseDocument(std::string_view bytes);
    static std::shared_ptr<const MapAssetBundle> empty();
    static std::shared_ptr<const MapAssetBundle> deserialize(std::string_view bytes);
    std::shared_ptr<const MapAssetBundle> merge(const MapAssetBundle& other) const;
    std::string serialize() const;
    void validate(const TerrainRegistry&, const ResourceRegistry&) const;
    bool isEmpty() const { return sheets.empty() && terrains.empty() && credits.empty(); }
    std::map<std::string, Sheet> sheets;
    // Terrain stable key -> TerrainVisual material declaration.
    nlohmann::json terrains = nlohmann::json::object();
    nlohmann::json credits = nlohmann::json::array();
private:
    nlohmann::json document = {{"schemaVersion", 1}, {"sheets", nlohmann::json::array()},
        {"terrains", nlohmann::json::object()}, {"credits", nlohmann::json::array()}};
};
