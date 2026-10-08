// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "map/MapAssetBundle.h"
#include <SDLGraphicContext.h>

// Owned by a renderer cache, never shared with the simulation thread.
class MapAssetSprites
{
public:
    explicit MapAssetSprites(std::shared_ptr<const MapAssetBundle> bundle) : bundle(std::move(bundle)) {}
    GAGCore::Sprite* resolve(const std::string& path);
private:
    std::shared_ptr<const MapAssetBundle> bundle;
    std::map<std::string, std::unique_ptr<GAGCore::Sprite>> sprites;
};
