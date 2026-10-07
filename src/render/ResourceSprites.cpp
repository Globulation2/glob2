// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceSprites.h"
#include "GlobalContainer.h"
#include <Toolkit.h>
#include <GraphicContext.h>

const ResourceSprites& ResourceSprites::resolve(std::shared_ptr<const ResourceRegistry> definitions)
{
    static ResourceSprites cache;
    static GAGCore::Sprite* legacy = nullptr;
    if (cache.definitions != definitions || legacy != globalContainer->resources)
    {
        cache.definitions = std::move(definitions);
        legacy = globalContainer->resources;
        cache.sprites.assign(cache.definitions->size(), nullptr);
        cache.legacyOnly = true;
        for (unsigned id = 0; id < cache.sprites.size(); ++id)
        {
            const auto& presentation = cache.definitions->presentation(static_cast<ResourceId>(id));
            auto* sprite = presentation.sprite == "data/gfx/ressource" ? legacy
                : GAGCore::Toolkit::getSprite(presentation.sprite);
            cache.sprites[id] = sprite;
            if (sprite != legacy || presentation.animationFrames != 1) cache.legacyOnly = false;
        }
    }
    return cache;
}
