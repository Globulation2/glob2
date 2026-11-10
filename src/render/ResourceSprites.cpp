// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceSprites.h"
#include "map/MapAssetBundle.h"
#include "GlobalContainer.h"
#include <Toolkit.h>
#include <GraphicContext.h>

namespace { ResourceSprites cache; }
void ResourceSprites::clear() { cache = ResourceSprites{}; }
ResourceSprites& ResourceSprites::resolve(std::shared_ptr<const ResourceRegistry> definitions, std::shared_ptr<const MapAssetBundle> assets)
{
    if (cache.definitions != definitions || cache.assets != assets || cache.legacy != globalContainer->resources)
    {
        ResourceSprites next;
        next.definitions = std::move(definitions);
        next.assets = std::move(assets);
        next.customSprites = std::make_unique<MapAssetSprites>(next.assets);
        next.legacy = globalContainer->resources;
        next.sprites.resize(next.definitions->size());
        next.loadStates.resize(next.sprites.size(), LoadState::Unrequested);
        cache = std::move(next);
    }
    return cache;
}

GAGCore::Sprite* ResourceSprites::sprite(ResourceId id, bool waitForLoad)
{
    const auto index = resourceIndex(id);
    if (index >= sprites.size()) return nullptr;
    auto *&resolved = sprites[index];
    auto &state = loadStates[index];
    if (state == LoadState::Resolved) return resolved;
    const auto &path = definitions->presentation(id).sprite;
    if (path == LegacyArtwork) return legacy;
    if (waitForLoad || (assets && assets->sheets.contains(path)))
    {
        resolved = customSprites->resolve(path);
        state = LoadState::Resolved;
        return resolved;
    }
    if (state == LoadState::Unrequested)
    {
        GAGCore::Toolkit::requestSprite(path);
        state = LoadState::Requested;
    }
    resolved = GAGCore::Toolkit::findSprite(path);
    if (resolved) state = LoadState::Resolved;
    return resolved;
}
