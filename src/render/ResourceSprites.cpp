// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceSprites.h"
#include "GlobalContainer.h"
#include <Toolkit.h>
#include <GraphicContext.h>

namespace { ResourceSprites cache; GAGCore::Sprite* legacy = nullptr; }
void ResourceSprites::clear() { cache=ResourceSprites{};legacy=nullptr; }
const ResourceSprites& ResourceSprites::resolve(std::shared_ptr<const ResourceRegistry> definitions, std::shared_ptr<const MapAssetBundle> assets)
{
    if (cache.definitions != definitions || cache.assets != assets || legacy != globalContainer->resources)
    {
        cache.definitions = std::move(definitions);
        cache.assets = std::move(assets);
        cache.customSprites = std::make_unique<MapAssetSprites>(cache.assets);
        legacy = globalContainer->resources;
        cache.sprites.assign(cache.definitions->size(), nullptr);
        cache.legacyOnly = true;
        for (unsigned id = 0; id < cache.sprites.size(); ++id)
        {
            const auto& presentation = cache.definitions->presentation(static_cast<ResourceId>(id));
            auto* sprite = presentation.sprite == "data/gfx/ressource" ? legacy
                : cache.customSprites->resolve(presentation.sprite);
            cache.sprites[id] = sprite;
            if (sprite != legacy || presentation.animationFrames != 1) cache.legacyOnly = false;
        }
    }
    return cache;
}
