// SPDX-License-Identifier: GPL-3.0-or-later
#include <RenderBackend.h>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace GAGCore
{
namespace
{
void check(int result)
{
    if (result < 0) throw std::runtime_error(SDL_GetError());
}
class SDLRenderBackend final : public RenderBackend
{
    SDL_Renderer* renderer;
    std::unordered_map<const void*, SDL_Texture*> textures;
public:
    explicit SDLRenderBackend(SDL_Renderer* renderer) : renderer(renderer) {}
    ~SDLRenderBackend() override { reset(); SDL_DestroyRenderer(renderer); }
    void clip(const SDL_Rect* rect) override { check(SDL_RenderSetClipRect(renderer, rect)); }
    void triangles(std::span<const SDL_Vertex> vertices, const void* key,
                   SDL_Surface* pixels, bool changed) override
    {
        if (vertices.empty()) return;
        if (vertices.size() % 3 || vertices.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
            throw std::invalid_argument("Invalid triangle batch size");
        SDL_Texture* texture = nullptr;
        if (key) {
            if (!pixels) throw std::invalid_argument("Texture has no CPU pixels");
            if (changed) forget(key);
            auto found = textures.find(key);
            if (found == textures.end()) {
                texture = SDL_CreateTextureFromSurface(renderer, pixels);
                if (!texture) throw std::runtime_error(SDL_GetError());
                textures.emplace(key, texture);
                check(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND));
                check(SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest));
            } else texture = found->second;
        }
        check(SDL_RenderGeometry(renderer, texture, vertices.data(), static_cast<int>(vertices.size()), nullptr, 0));
    }
    void forget(const void* key) override
    {
        auto found = textures.find(key);
        if (found != textures.end()) { SDL_DestroyTexture(found->second); textures.erase(found); }
    }
    void reset() override
    {
        for (auto [key, texture] : textures) SDL_DestroyTexture(texture);
        textures.clear();
    }
    void present() override { SDL_RenderPresent(renderer); }
    void outputSize(int& width, int& height) override { check(SDL_GetRendererOutputSize(renderer, &width, &height)); }
    SDL_Surface* capture() override
    {
        int width, height;
        outputSize(width, height);
        SDL_Surface* pixels = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
        if (!pixels) throw std::runtime_error(SDL_GetError());
        if (SDL_RenderReadPixels(renderer, nullptr, pixels->format->format, pixels->pixels, pixels->pitch) < 0) {
            SDL_FreeSurface(pixels);
            throw std::runtime_error(SDL_GetError());
        }
        return pixels;
    }
};
}
std::unique_ptr<RenderBackend> makeSDLRenderBackend(SDL_Window* window, int width, int height)
{
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) return {};
    auto backend = std::make_unique<SDLRenderBackend>(renderer);
    check(SDL_RenderSetLogicalSize(renderer, width, height));
    check(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND));
    return backend;
}
}
