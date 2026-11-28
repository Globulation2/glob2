// SPDX-License-Identifier: GPL-3.0-or-later
#include <RenderResources.h>
#include <MobilePlatform.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace GAGCore
{
namespace { std::atomic<std::uint64_t> registryIdentity{1}; }
constexpr std::size_t MiB = 1024 * 1024;
// Desktop GPUs hold gigabytes. At a 2x display density a game holds 200 to
// 230 MiB (HD sprites, direct terrain, staging), and the direct pass grows its
// art atlas to 4096 (64 MiB, twice while it fills) only within this total.
// General keeps every HD unit animation resident: on-demand devices retain
// uploaded HD frames within 3/8 of the class (deviceHighResolutionRoom), and
// the 3,680 HD unit frames take about 77 MiB block-compressed with mips.
// The browser keeps CPU copies in a Wasm heap of fixed maximum, so its
// terrain caches stay at the phone sizes. Ground shadow masks (ShadowMask)
// take 2 MiB a chunk in HD, half CPU pixels and half a General texture: 64 MiB
// keeps a view near the HD threshold with room to pan; phones and the browser
// keep a quarter of that, since the textures share General with HD frames.
RenderBudget RenderBudget::desktop()
{
#ifdef __EMSCRIPTEN__
    constexpr Terrain terrain{128 * MiB, 128 * MiB, 64 * MiB, 16 * MiB};
#else
    constexpr Terrain terrain{128 * MiB, 256 * MiB, 256 * MiB, 64 * MiB};
#endif
    return {512 * MiB, {256 * MiB, 64 * MiB, 128 * MiB, 32 * MiB}, 64 * MiB, terrain};
}
// Phones share one memory pool between the CPU and GPU, and Android starts
// killing a backgrounded game long before desktop limits. Each class keeps at
// least twice its largest single texture: the 22.4 MiB authored HD terrain
// block (General), two 16 MiB skin atlas pages, a 16-page window of 1024x1024
// terrain pages (TerrainPage, pages draw downsampled there) and half the
// desktop coverage masks. The total covers those classes plus transient work.
RenderBudget RenderBudget::mobile()
{
    return {160 * MiB, {48 * MiB, 32 * MiB, 64 * MiB, 16 * MiB}, 32 * MiB, {128 * MiB, 128 * MiB, 64 * MiB, 16 * MiB}};
}
// A 16 GB desktop, or Apple silicon, whose GPU shares that memory: a 2x view
// zoomed out over a large map wants more terrain pages and HD frames resident
// than desktop() holds, and evicting them costs a recomposition or an upload
// each time they return. Half the 512 MiB General class (192 MiB) is room for
// every HD unit, building and decor frame at once.
RenderBudget RenderBudget::desktopLarge()
{
    return {1024 * MiB, {512 * MiB, 64 * MiB, 256 * MiB, 64 * MiB}, 96 * MiB, {256 * MiB, 512 * MiB, 384 * MiB, 128 * MiB}};
}
// A phone with about 8 GB (the High tier). mobile()'s 48 MiB General class
// leaves 18 MiB for HD frames, less than a busy view draws, so they evict and
// upload again every few seconds. 128 MiB leaves 48 MiB; terrain pages and
// direct terrain double. The total stays far below what Android lets a
// foreground game on such a phone hold.
RenderBudget RenderBudget::mobileLarge()
{
    return {320 * MiB, {128 * MiB, 32 * MiB, 96 * MiB, 32 * MiB}, 48 * MiB, {128 * MiB, 192 * MiB, 128 * MiB, 32 * MiB}};
}
std::optional<RenderBudget> RenderBudget::named(std::string_view name)
{
    if (name == "desktop") return desktop();
    if (name == "mobile") return mobile();
    if (name == "desktop-large") return desktopLarge();
    if (name == "mobile-large") return mobileLarge();
    return std::nullopt;
}
const char *RenderBudget::name(const RenderBudget &budget)
{
    for (const char *candidate : {"desktop", "mobile", "desktop-large", "mobile-large"})
        if (named(candidate) == budget) return candidate;
    return "custom";
}
namespace {
struct PlatformBudgetChoice {
    std::mutex mutex;
    std::optional<RenderBudget> selected;
    bool used = false;
};
PlatformBudgetChoice &platformBudgetChoice() { static PlatformBudgetChoice choice; return choice; }
}
bool selectPlatformRenderBudget(const RenderBudget &budget)
{
    auto &choice = platformBudgetChoice();
    std::lock_guard lock(choice.mutex);
    if (choice.used || choice.selected) return false;
    choice.selected = budget;
    return true;
}
const RenderBudget &platformRenderBudget()
{
    static const RenderBudget chosen = [] {
        auto &choice = platformBudgetChoice();
        std::lock_guard lock(choice.mutex);
        choice.used = true;
        RenderBudget budget = choice.selected ? *choice.selected
            : platformIsMobile() ? RenderBudget::mobile() : RenderBudget::desktop();
        if (const char *value = std::getenv("GLOB2_RENDER_BUDGET"))
            if (const auto named = RenderBudget::named(value)) budget = *named;
        // GLOB2_RENDER_BUDGET_TOTAL_MIB replaces the total alone, for tests
        // and memory experiments that need the combined limit to bind.
        if (const char *total = std::getenv("GLOB2_RENDER_BUDGET_TOTAL_MIB"))
            if (const long mib = std::strtol(total, nullptr, 10); mib > 0) budget.total = std::size_t(mib) * MiB;
        return budget;
    }();
    return chosen;
}
struct RenderResources::Budget {
    std::size_t bytes = 0, limit;
    std::array<std::size_t,4> classes{}, classLimits{};
    Budget(std::size_t limit, const std::array<std::size_t,4> &limits) : limit(limit)
    {
        for (std::size_t i = 0; i < limits.size(); ++i) classLimits[i] = std::min(limits[i], limit);
    }
};
RenderResources::RenderResources(const RenderBudget &limits, std::size_t count)
    : budget(std::make_shared<Budget>(limits.total, limits.classes)),
      geometryBudget(std::make_shared<Budget>(limits.geometry, std::array<std::size_t,4>{})), maximumSlots(count),
      identity(registryIdentity.fetch_add(1,std::memory_order_relaxed))
{
    if (count > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Too many texture slots");
}
RenderResources::RenderResources(std::size_t bytes, std::size_t count)
    : RenderResources(RenderBudget{bytes, platformRenderBudget().classes, platformRenderBudget().geometry, platformRenderBudget().terrain}, count) {}
TextureSnapshot RenderResources::copy(TextureHandle handle, SDL_Surface *source,
                                      std::uint64_t revision, TextureDescriptor descriptor, std::span<const RenderMipLevel> mipLevels)
{
    if (!source || source->w <= 0 || source->h <= 0 || source->pitch <= 0)
        throw std::invalid_argument("Invalid texture source");
    if (std::size_t(source->h) > std::numeric_limits<std::size_t>::max() / std::size_t(source->pitch))
        throw std::length_error("Texture source size overflow");
    const auto category = std::size_t(descriptor.budgetClass);
    if (category >= budget->classes.size()) throw std::invalid_argument("Invalid texture budget class");
    std::vector<OwnedRenderMipLevel> mips;
    std::shared_ptr<SDL_Surface> authoredBase;
    std::size_t mipBytes = 0;
    if (descriptor.compression != TextureCompression::Uncompressed) {
        // Keep only the blocks: a quarter of the RGBA chain, and the device
        // restores its texture from them after trimming or context loss.
        if (mipLevels.empty() || mipLevels.size() > 32) throw std::invalid_argument("Compressed texture requires authored levels");
        const int baseW = mipLevels.front().width, baseH = mipLevels.front().height;
        if (baseW < source->w || baseH < source->h) throw std::invalid_argument("Authored base does not cover source pixels");
        std::vector<std::vector<std::byte>> blocks;
        std::size_t bytes = 0;
        int expectedW = baseW, expectedH = baseH;
        for (const auto &level : mipLevels) {
            if (level.width < 4 || level.height < 4) break;
            if (level.width != expectedW || level.height != expectedH || level.compression != descriptor.compression ||
                level.blocks.size() != compressedLevelBytes(level.width, level.height))
                throw std::invalid_argument("Invalid compressed level dimensions or blocks");
            bytes += level.blocks.size();
            expectedW = std::max(1, expectedW / 2); expectedH = std::max(1, expectedH / 2);
        }
        if (bytes == 0) throw std::invalid_argument("Compressed texture requires a 4x4 base level");
        if (bytes > budget->classLimits[category] - budget->classes[category] || bytes > budget->limit - budget->bytes) return {};
        for (const auto &level : mipLevels) {
            if (level.width < 4 || level.height < 4) break;
            blocks.emplace_back(level.blocks.begin(), level.blocks.end());
        }
        descriptor.width = baseW; descriptor.height = baseH; descriptor.format = SDL_PIXELFORMAT_RGBA32;
        descriptor.mips = TextureDescriptor::Mips::Generated; descriptor.alpha = TextureDescriptor::Alpha::Premultiplied;
        auto accounting = budget;
        budget->bytes += bytes; budget->classes[category] += bytes;
        // Without pixels, the revision's own lifetime releases its charge.
        return TextureSnapshot(new TextureRevision{handle, descriptor, revision, nullptr, bytes, {}, std::move(blocks)},
            [accounting, bytes, category](const TextureRevision *value) {
                accounting->bytes -= bytes; accounting->classes[category] -= bytes; delete value;
            });
    }
    if (!mipLevels.empty()) {
        if (mipLevels.size() > 32) throw std::invalid_argument("Too many authored mip levels");
        int expectedW = mipLevels.front().width, expectedH = mipLevels.front().height;
        if (expectedW < source->w || expectedH < source->h || expectedW <= 0 || expectedH <= 0)
            throw std::invalid_argument("Authored base does not cover source pixels");
        std::uint64_t authoredBytes = 0;
        int validatedW = expectedW, validatedH = expectedH;
        bool finalSeen = false;
        for (const auto &level : mipLevels) {
            if (finalSeen || level.width != validatedW || level.height != validatedH ||
                level.width <= 0 || level.height <= 0 || level.width > std::numeric_limits<int>::max()/4 ||
                (level.format != SDL_PIXELFORMAT_RGBA32 && level.format != SDL_PIXELFORMAT_ARGB8888))
                throw std::invalid_argument("Invalid authored mip size or format");
            const auto levelBytes = std::uint64_t(level.width) * level.height * 4;
            if (levelBytes > std::numeric_limits<std::size_t>::max() || level.pixels.size() != levelBytes ||
                authoredBytes > std::numeric_limits<std::uint64_t>::max() - levelBytes)
                throw std::invalid_argument("Invalid authored mip byte count");
            authoredBytes += levelBytes; finalSeen = level.width == 1 && level.height == 1;
            validatedW = std::max(1, validatedW/2); validatedH = std::max(1, validatedH/2);
        }
        if (!finalSeen) throw std::invalid_argument("Authored mip chain must reach one texel");
        if (authoredBytes > budget->limit - budget->bytes || authoredBytes > budget->classLimits[category] - budget->classes[category]) return {};
        for (std::size_t index = 0; index < mipLevels.size(); ++index) {
            const auto &level = mipLevels[index];
            if (level.width != expectedW || level.height != expectedH ||
                (level.format != SDL_PIXELFORMAT_RGBA32 && level.format != SDL_PIXELFORMAT_ARGB8888) ||
                std::uint64_t(level.width) * level.height > std::numeric_limits<std::size_t>::max() / 4 ||
                level.pixels.size() != std::size_t(level.width) * level.height * 4)
                throw std::invalid_argument("Invalid authored mip dimensions or pixels");
            if (index && expectedW == 1 && expectedH == 1 && mipLevels[index-1].width == 1 && mipLevels[index-1].height == 1)
                throw std::invalid_argument("Duplicate final mip level");
            SDL_Surface *view = SDL_CreateSurfaceFrom(level.width, level.height, level.format,
                const_cast<std::byte *>(level.pixels.data()), level.width * 4);
            if (!view) throw std::runtime_error(SDL_GetError());
            SDL_Surface *converted = SDL_ConvertSurface(view, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(view);
            if (!converted) throw std::runtime_error(SDL_GetError());
            std::shared_ptr<SDL_Surface> owned(converted, SDL_DestroySurface);
            if (!index) authoredBase = std::move(owned);
            else {
                OwnedRenderMipLevel copied; copied.width = level.width; copied.height = level.height;
                copied.rgba.resize(std::size_t(level.width) * level.height * 4);
                for (int row = 0; row < level.height; ++row)
                    std::memcpy(copied.rgba.data() + std::size_t(row)*level.width*4,
                        static_cast<const std::byte *>(owned->pixels) + std::size_t(row)*owned->pitch, std::size_t(level.width)*4);
                mipBytes += copied.rgba.size(); mips.push_back(std::move(copied));
            }
            expectedW = std::max(1, expectedW / 2); expectedH = std::max(1, expectedH / 2);
        }
        if (mipLevels.back().width != 1 || mipLevels.back().height != 1)
            throw std::invalid_argument("Authored mip chain must reach one texel");
        source = authoredBase.get(); descriptor.mips = TextureDescriptor::Mips::Generated;
    } else if (descriptor.mips == TextureDescriptor::Mips::Generated) {
        throw std::invalid_argument("Authored mip descriptor requires supplied levels");
    }
    const std::size_t bytes = std::size_t(source->pitch) * source->h + mipBytes;
    if (bytes > budget->classLimits[category] - budget->classes[category]) return {};
    if (bytes > budget->limit - budget->bytes) return {};
    SDL_Surface *pixels = authoredBase ? authoredBase.get() : SDL_DuplicateSurface(source);
    if (!pixels) throw std::runtime_error(SDL_GetError());
    const std::size_t allocated = std::size_t(pixels->pitch) * pixels->h + mipBytes;
    if (allocated > budget->limit - budget->bytes || allocated > budget->classLimits[category] - budget->classes[category]) { if (!authoredBase) SDL_DestroySurface(pixels); return {}; }
    descriptor.width = pixels->w;
    descriptor.height = pixels->h;
    descriptor.format = pixels->format;
    auto accounting = budget;
    budget->bytes += allocated; budget->classes[category] += allocated;
    std::shared_ptr<SDL_Surface> owned(pixels, [accounting, allocated, category, base = std::move(authoredBase)](SDL_Surface *surface) mutable {
        if (base) base.reset(); else SDL_DestroySurface(surface);
        accounting->bytes -= allocated; accounting->classes[category] -= allocated;
    });
    return std::make_shared<TextureRevision>(TextureRevision{handle, descriptor, revision, std::move(owned), allocated, std::move(mips)});
}
TextureHandle RenderResources::create(SDL_Surface *source, std::uint64_t revision, TextureDescriptor descriptor, std::span<const RenderMipLevel> mipLevels)
{
    std::size_t index = 0;
    // Generation zero marks exhausted slots, preventing stale handle resurrection.
    while (index < slots.size() && (slots[index].current || !slots[index].generation)) ++index;
    if (index == slots.size()) {
        if (index >= maximumSlots) return {};
        slots.push_back({});
    }
    const TextureHandle handle{std::uint32_t(index), slots[index].generation};
    auto copied = copy(handle, source, revision, descriptor, mipLevels);
    if (!copied) return {};
    slots[index].current = std::move(copied);
    return handle;
}
bool RenderResources::update(TextureHandle handle, SDL_Surface *source, std::uint64_t revision, TextureDescriptor descriptor, std::span<const RenderMipLevel> mipLevels)
{
    if (!snapshot(handle)) return false;
    auto copied = copy(handle, source, revision, descriptor, mipLevels);
    if (!copied) return false;
    slots[handle.slot].current = std::move(copied);
    return true;
}
TextureSnapshot RenderResources::snapshot(TextureHandle handle) const
{
    if (!handle || handle.slot >= slots.size() || slots[handle.slot].generation != handle.generation) return {};
    return slots[handle.slot].current;
}
bool RenderResources::erase(TextureHandle handle)
{
    if (!snapshot(handle)) return false;
    auto &slot = slots[handle.slot];
    slot.current.reset();
    ++slot.generation;
    return true;
}
std::size_t RenderResources::retainedBytes() const { return budget->bytes; }
std::size_t RenderResources::byteLimit() const { return budget->limit; }
std::size_t RenderResources::classLimit(TextureDescriptor::BudgetClass category) const
{
    const auto index = std::size_t(category);
    if (index >= budget->classLimits.size()) throw std::invalid_argument("Invalid texture budget class");
    return budget->classLimits[index];
}
BufferHandle RenderResources::createGeometry(GeometryLayout layout, std::span<const std::byte> vertices,
                                             std::span<const std::uint32_t> indices, std::uint64_t revision)
{
    const std::uint32_t stride = layout == GeometryLayout::Primitive32 ? 32 : layout == GeometryLayout::World48 ? 48 :
        layout == GeometryLayout::Skin40 ? 40 : 72;
    if (layout != GeometryLayout::Primitive32 && layout != GeometryLayout::World48 && layout != GeometryLayout::Skin40 && layout != GeometryLayout::SkinRig72)
        throw std::invalid_argument("Unknown geometry layout");
    if (vertices.empty() || vertices.size() % stride) throw std::invalid_argument("Invalid geometry vertex stride");
    if (indices.size() > geometryBudget->limit / sizeof(std::uint32_t)) return {};
    const std::size_t bytes = vertices.size() + indices.size_bytes();
    if (bytes < vertices.size() || bytes > geometryBudget->limit - geometryBudget->bytes) return {};
    for (std::size_t offset=0; offset<vertices.size(); offset+=sizeof(float)) {
        float value; std::memcpy(&value,vertices.data()+offset,sizeof(value));
        if (!std::isfinite(value)) throw std::invalid_argument("Non-finite geometry vertex");
        if (layout==GeometryLayout::SkinRig72) {
            const auto field=offset%stride;
            if (field>=40 && field<56 && (value<0 || value>31 || value!=std::floor(value)))
                throw std::invalid_argument("Invalid rig joint index");
            if(field>=56 && value<0) throw std::invalid_argument("Negative rig weight");
        }
    }
    if(layout==GeometryLayout::SkinRig72) for(std::size_t offset=0;offset<vertices.size();offset+=stride) {
        float sum=0; for(std::size_t i=56;i<72;i+=4){float value;std::memcpy(&value,vertices.data()+offset+i,4);sum+=value;}
        if(!std::isfinite(sum) || sum<=0) throw std::invalid_argument("Rig vertex requires positive weights");
    }
    for (auto index : indices) if (index >= vertices.size() / stride) throw std::invalid_argument("Geometry index outside vertex range");
    std::size_t slot = 0;
    while (slot < buffers.size() && (buffers[slot].current || !buffers[slot].generation)) ++slot;
    if (slot == buffers.size()) { if (slot >= maximumSlots) return {}; buffers.push_back({}); }
    const BufferHandle handle{std::uint32_t(slot), buffers[slot].generation};
    auto value = std::make_unique<GeometryRevision>();
    value->handle = handle; value->layout = layout; value->stride = stride; value->contentRevision = revision;
    value->vertices.assign(vertices.begin(), vertices.end()); value->indices.assign(indices.begin(), indices.end()); value->bytes = bytes;
    auto accounting = geometryBudget;
    GeometrySnapshot owned(value.release(), [accounting, bytes](const GeometryRevision *geometry) { delete geometry; accounting->bytes -= bytes; });
    geometryBudget->bytes += bytes; buffers[slot].current = std::move(owned); return handle;
}
GeometrySnapshot RenderResources::snapshot(BufferHandle handle) const
{
    if (!handle || handle.slot >= buffers.size() || buffers[handle.slot].generation != handle.generation) return {};
    return buffers[handle.slot].current;
}
bool RenderResources::erase(BufferHandle handle)
{
    if (!snapshot(handle)) return false;
    buffers[handle.slot].current.reset(); ++buffers[handle.slot].generation; return true;
}
std::size_t RenderResources::retainedGeometryBytes() const { return geometryBudget->bytes; }
RenderUploadQueue::RenderUploadQueue(std::size_t byteLimit, std::size_t count)
    : byteLimit(byteLimit), requestLimit(count) {}
bool RenderUploadQueue::enqueue(UploadRequest request)
{
    if (!request.texture || requests.size() >= requestLimit || request.texture->bytes > byteLimit - bytes) return false;
    bytes += request.texture->bytes;
    requests.push_back(std::move(request));
    return true;
}
bool RenderUploadQueue::pop(UploadRequest &request)
{
    if (requests.empty()) return false;
    request = std::move(requests.front());
    bytes -= request.texture->bytes;
    requests.pop_front();
    return true;
}
void RenderUploadQueue::cancel() { requests.clear(); bytes = 0; }
} // namespace GAGCore
