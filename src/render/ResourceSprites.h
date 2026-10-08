// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourceRegistry.h"
#include "MapAssetSprites.h"
#include <memory>
#include <vector>
namespace GAGCore { class Sprite; }

// Render-thread cache retaining the exact immutable catalog used by the scene.
// Missing presentation assets never change simulation definitions.
struct ResourceSprites
{
    std::shared_ptr<const ResourceRegistry> definitions;
    std::vector<GAGCore::Sprite*> sprites;
    std::unique_ptr<MapAssetSprites> customSprites;
    std::shared_ptr<const MapAssetBundle> assets;
    bool legacyOnly = true;
    static void clear();
    static const ResourceSprites& resolve(std::shared_ptr<const ResourceRegistry> definitions,
        std::shared_ptr<const MapAssetBundle> assets = MapAssetBundle::empty());
};
