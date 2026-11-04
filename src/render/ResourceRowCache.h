// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

class ResourceRegistry;
class TerrainRegistry;
class MapAssetBundle;
namespace GAGCore { class Sprite; }

class ResourceRowCache
{
public:
    struct Stamp
    {
        std::uint64_t map, terrain, artwork;
        std::uint32_t seed;
        std::shared_ptr<const ResourceRegistry> resourceRegistry;
        std::shared_ptr<const TerrainRegistry> terrainRegistry;
        std::shared_ptr<const MapAssetBundle> assets;
        const GAGCore::Sprite *sprite, *decor, *overlay;
        int frameCount;
        bool operator==(const Stamp &) const = default;
    };
    struct Layer
    {
        std::vector<int> frames;
        std::vector<int> animated;
        std::uint64_t revision = 0;
        unsigned visibleFrames = 0;
    };
    struct Row
    {
        Layer resources, decor, overlay;
        std::uint64_t resourceRevision = 0;
        std::uint32_t tick = 0;
        unsigned invalidFrames = 0;
        bool built = false, customSprites = false, refreshGeometry = true;
    };
    Stamp stamp{};
    std::vector<Row> rows;
    std::uint64_t resourceGeneration = 0;
    std::vector<std::uint64_t> resourceRevisions;
};
