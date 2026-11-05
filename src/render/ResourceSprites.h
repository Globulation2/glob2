// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourceRegistry.h"
#include "MapAssetSprites.h"
#include <memory>
#include <vector>
namespace GAGCore { class Sprite; }

// Render-thread cache retaining the exact immutable catalog used by the scene.
// Missing presentation assets never change simulation definitions.
class ResourceSprites
{
public:
    static constexpr const char *LegacyArtwork = "data/gfx/ressource";
    static void clear();
    static ResourceSprites& resolve(std::shared_ptr<const ResourceRegistry>,
                                    std::shared_ptr<const MapAssetBundle>);

    // Interactive draws request installed artwork without waiting for it.
    // Captures and cached swatches need a complete image on the first draw.
    GAGCore::Sprite* sprite(ResourceId id, bool waitForLoad = false);

private:
    enum class LoadState : unsigned char { Unrequested, Requested, Resolved };
    std::shared_ptr<const ResourceRegistry> definitions;
    std::vector<GAGCore::Sprite*> sprites;
    std::unique_ptr<MapAssetSprites> customSprites;
    std::shared_ptr<const MapAssetBundle> assets;
    std::vector<LoadState> loadStates;
    GAGCore::Sprite* legacy = nullptr;
};
