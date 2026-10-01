// SPDX-License-Identifier: GPL-3.0-or-later
#include <RenderBackend.h>
#include <limits>
#include <cmath>
#include <optional>
#include <vector>
#include <stdexcept>
#include <unordered_map>

namespace GAGCore
{
namespace
{
void check(bool result)
{
    if (!result) throw std::runtime_error(SDL_GetError());
}
class SDLRenderBackend final : public RenderBackend
{
    SDL_Renderer* renderer;
    float scale=1, offsetX=0, offsetY=0;
    std::optional<SDL_Rect> bounds;
    std::vector<SDL_Vertex> transformed;
    std::unordered_map<const void*, SDL_Texture*> textures;
public:
    explicit SDLRenderBackend(SDL_Renderer* renderer) : renderer(renderer) {}
    ~SDLRenderBackend() override { reset(); SDL_DestroyRenderer(renderer); }
    void transform(float factor, float x, float y, const SDL_Rect* output) override
    {
        if (!std::isfinite(factor) || factor <= 0 || !std::isfinite(x) || !std::isfinite(y))
            throw std::invalid_argument("Invalid UI transform");
        scale=factor; offsetX=x; offsetY=y;
        bounds=output ? std::optional<SDL_Rect>(*output) : std::nullopt;
        clip(nullptr);
    }
    void clip(const SDL_Rect* rect) override
    {
        std::optional<SDL_Rect> result=bounds;
        if (rect) {
            const int x=int(std::floor(rect->x*scale+offsetX)), y=int(std::floor(rect->y*scale+offsetY));
            SDL_Rect mapped{x,y,int(std::ceil((rect->x+rect->w)*scale+offsetX))-x,
                                  int(std::ceil((rect->y+rect->h)*scale+offsetY))-y};
            if (bounds) SDL_GetRectIntersection(&mapped,&*bounds,&mapped);
            result=mapped;
        }
        check(SDL_SetRenderClipRect(renderer,result ? &*result : nullptr));
    }
    void triangles(std::span<const SDL_Vertex> vertices, const void* key,
                   SDL_Surface* pixels, bool changed) override
    {
        submit(vertices, key, pixels, changed, true);
    }
    void screenTriangles(std::span<const SDL_Vertex> vertices) override
    {
        submit(vertices, nullptr, nullptr, false, false);
    }
    void submit(std::span<const SDL_Vertex> vertices, const void* key,
                SDL_Surface* pixels, bool changed, bool applyTransform)
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
                check(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST));
            } else texture = found->second;
        }
        if (applyTransform && (scale!=1 || offsetX!=0 || offsetY!=0)) {
            transformed.assign(vertices.begin(),vertices.end());
            for (auto& vertex : transformed) {
                vertex.position.x=vertex.position.x*scale+offsetX;
                vertex.position.y=vertex.position.y*scale+offsetY;
            }
            vertices=transformed;
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
    void clear() { check(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255)); check(SDL_RenderClear(renderer)); }
    void logicalSize(int width, int height) override { check(SDL_SetRenderLogicalPresentation(renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX)); clear(); }
    void flush() override { check(SDL_FlushRenderer(renderer)); }
    void present() override { SDL_RenderPresent(renderer); clear(); }
    void outputSize(int& width, int& height) override { check(SDL_GetRenderOutputSize(renderer, &width, &height)); }
    SDL_Surface* capture() override
    {
        SDL_Surface *pixels = SDL_RenderReadPixels(renderer, nullptr);
        if (!pixels) throw std::runtime_error(SDL_GetError());
        return pixels;
    }
};
}
std::unique_ptr<RenderBackend> makeSDLRenderBackend(SDL_Window* window, int width, int height)
{
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) return {};
    auto backend = std::make_unique<SDLRenderBackend>(renderer);
    backend->logicalSize(width, height);
    check(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND));
    return backend;
}
std::unique_ptr<RenderBackend> makeSoftwareRenderBackend(SDL_Surface* surface)
{
    auto* renderer=SDL_CreateSoftwareRenderer(surface);
    if (!renderer) throw std::runtime_error(SDL_GetError());
    check(SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND));
    return std::make_unique<SDLRenderBackend>(renderer);
}

}
