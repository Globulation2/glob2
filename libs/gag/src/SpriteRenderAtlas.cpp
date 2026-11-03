// SPDX-License-Identifier: GPL-3.0-or-later
#include <SpriteRenderAtlas.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace GAGCore {
namespace {
constexpr std::size_t AtlasBudget = 64 * 1024 * 1024;
auto &accounting() {
    static auto bytes = std::make_shared<std::atomic<std::size_t>>(0);
    return bytes;
}
}
struct SpriteAtlasMemory {
    std::shared_ptr<std::atomic<std::size_t>> accounting;
    std::size_t bytes;
    SpriteAtlasMemory(std::shared_ptr<std::atomic<std::size_t>> state,std::size_t size):accounting(std::move(state)),bytes(size) {}
    ~SpriteAtlasMemory() { accounting->fetch_sub(bytes, std::memory_order_relaxed); }
};
std::shared_ptr<const SpriteAtlasMemory> reserveSpriteAtlasMemory(std::size_t bytes) {
    if (!bytes || bytes > AtlasBudget) return {};
    auto state = accounting(); auto used = state->load(std::memory_order_relaxed);
    do { if (bytes > AtlasBudget - used) return {}; }
    while (!state->compare_exchange_weak(used, used + bytes, std::memory_order_relaxed));
    try { return std::make_shared<const SpriteAtlasMemory>(state, bytes); }
    catch (...) { state->fetch_sub(bytes, std::memory_order_relaxed); throw; }
}
std::size_t spriteAtlasMemoryBytes() { return accounting()->load(std::memory_order_relaxed); }
std::unique_ptr<SpriteAtlasPage> prepareSpriteRenderAtlas(std::span<const SpriteAtlasSource> sources,
                                                              int maximumTextureSize, bool variableSizes) {
    maximumTextureSize = std::min(maximumTextureSize, 2048);
    if (sources.empty() || sources.size() > 16384 || maximumTextureSize <= 0) return {};
    int width = 0, height = 0;
    for (const auto &source : sources) {
        if (!source.pixels || source.pixels->w <= 0 || source.pixels->h <= 0 || !source.stamp.identity) return {};
        if (width && !variableSizes && (width != source.pixels->w || height != source.pixels->h)) return {};
        width = std::max(width, source.pixels->w); height = std::max(height, source.pixels->h);
    }
    const unsigned columns = unsigned(std::ceil(std::sqrt(sources.size())));
    const unsigned rows = unsigned((sources.size() + columns - 1) / columns);
    const auto pageWidth = (std::uint64_t(width) + 2) * columns;
    const auto pageHeight = (std::uint64_t(height) + 2) * rows;
    if (pageWidth > unsigned(maximumTextureSize) || pageHeight > unsigned(maximumTextureSize) || pageWidth * pageHeight > AtlasBudget / 4) return {};
    auto lease = reserveSpriteAtlasMemory(std::size_t(pageWidth * pageHeight * 4));
    if (!lease) return {};
    auto page = std::make_unique<SpriteAtlasPage>(); page->memory = std::move(lease);
    page->pixels.reset(SDL_CreateSurface(int(pageWidth), int(pageHeight), SDL_PIXELFORMAT_ARGB8888));
    if (!page->pixels) throw std::runtime_error(SDL_GetError());
    if (!SDL_ClearSurface(page->pixels.get(), 0, 0, 0, 0)) throw std::runtime_error(SDL_GetError());
    page->regions.reserve(sources.size()); page->stamps.reserve(sources.size());
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto &source = sources[index];
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> converted(
            SDL_ConvertSurface(source.pixels, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        if (!converted) throw std::runtime_error(SDL_GetError());
        const int x = int(index % columns) * (width + 2) + 1;
        const int y = int(index / columns) * (height + 2) + 1;
        for (int row = -1; row <= converted->h; ++row) {
            const auto *input = static_cast<const unsigned char *>(converted->pixels) + std::clamp(row, 0, converted->h - 1) * converted->pitch;
            auto *output = static_cast<unsigned char *>(page->pixels->pixels) + (y + row) * page->pixels->pitch + (x - 1) * 4;
            std::memcpy(output, input, 4); std::memcpy(output + 4, input, std::size_t(converted->w) * 4);
            std::memcpy(output + (converted->w + 1) * 4, input + (converted->w - 1) * 4, 4);
        }
        page->regions.push_back({x, y, source.pixels->w, source.pixels->h});
        page->stamps.push_back({source.stamp.identity, source.stamp.revision, source.pixels->w, source.pixels->h});
    }
    return page;
}
} // namespace GAGCore
