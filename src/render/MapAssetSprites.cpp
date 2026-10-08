// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapAssetSprites.h"
#include <Toolkit.h>
#include <SDL3_image/SDL_image.h>
#include <stdexcept>

namespace {
class SheetSprite : public GAGCore::Sprite {
public:
    explicit SheetSprite(const MapAssetBundle::Sheet& sheet) {
        auto* io = SDL_IOFromConstMem(sheet.png.data(), sheet.png.size());
        if (!io) throw std::runtime_error("Cannot open map spritesheet");
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(IMG_LoadPNG_IO(io), SDL_DestroySurface);
        SDL_CloseIO(io);
        if (!pixels) throw std::runtime_error("Cannot decode map spritesheet");
        images.reserve(sheet.frames()); rotated.reserve(sheet.frames());
        experimentImages.assign(sheet.frames(), nullptr);
        experimentRotated.assign(sheet.frames(), nullptr);
        for (unsigned y = 0; y < sheet.height; y += sheet.frameHeight)
            for (unsigned x = 0; x < sheet.width; x += sheet.frameWidth) {
                auto frame = std::make_unique<GAGCore::DrawableSurface>(sheet.frameWidth, sheet.frameHeight);
                SDL_Rect source{int(x), int(y), int(sheet.frameWidth), int(sheet.frameHeight)};
                SDL_Rect target{0, 0, int(sheet.frameWidth), int(sheet.frameHeight)};
                SDL_SetSurfaceBlendMode(pixels.get(), SDL_BLENDMODE_NONE);
                if (!SDL_BlitSurface(pixels.get(), &source, frame->getSDLSurface(), &target))
                    throw std::runtime_error("Cannot slice map spritesheet");
                frame->markPixelsChanged(); images.push_back(frame.release()); rotated.push_back(nullptr);
            }
    }
};
}
GAGCore::Sprite* MapAssetSprites::resolve(const std::string& path) {
    if (!path.starts_with("data/sets/")) return GAGCore::Toolkit::getSprite(path);
    if (auto it = sprites.find(path); it != sprites.end()) return it->second.get();
    // Before format 144, all sprite paths referred to installed files. Keep
    // their ordinary missing-art fallback when loading those legacy snapshots.
    if (!bundle || bundle->isEmpty()) return GAGCore::Toolkit::getSprite(path);
    auto found = bundle->sheets.find(path);
    if (found == bundle->sheets.end()) return GAGCore::Toolkit::getSprite(path);
    auto sprite = std::make_unique<SheetSprite>(found->second);
    auto* result = sprite.get(); sprites.emplace(path, std::move(sprite)); return result;
}
