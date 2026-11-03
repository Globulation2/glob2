// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace GAGCore {
struct SpriteAtlasStamp {
    std::uint64_t identity = 0, revision = 0;
    int width = 0, height = 0;
    bool operator==(const SpriteAtlasStamp &) const = default;
};
struct SpriteAtlasSource { SDL_Surface *pixels; SpriteAtlasStamp stamp; };
struct SpriteAtlasMemory;
struct SpriteAtlasPage {
    std::shared_ptr<const SpriteAtlasMemory> memory;
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels{nullptr, SDL_DestroySurface};
    std::vector<SDL_Rect> regions;
    std::vector<SpriteAtlasStamp> stamps;
};
// Presentation-owned CPU pages have an independent shared 64 MiB bound. Failed
// admission keeps standalone images usable and permits promotion on a later draw.
std::shared_ptr<const SpriteAtlasMemory> reserveSpriteAtlasMemory(std::size_t bytes);
std::size_t spriteAtlasMemoryBytes();
std::unique_ptr<SpriteAtlasPage> prepareSpriteRenderAtlas(std::span<const SpriteAtlasSource>,
                                                              int maximumTextureSize, bool variableSizes);
} // namespace GAGCore
