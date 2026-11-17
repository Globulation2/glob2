// SPDX-License-Identifier: GPL-3.0-or-later
#include <SDLGPUDevice.h>
#include <VulkanInstanceLock.h>
#include <chrono>
#ifndef __EMSCRIPTEN__
#include <RendererShaderLayouts.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <stdexcept>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <functional>
#include <nlohmann/json.hpp>
#include <filesystem>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace GAGCore
{
namespace
{
namespace Layout = RendererShaderLayouts;
// Recorder vertices exclude the six vertices generated for each rectangle,
// clipped clear or terrain-composition packet.
constexpr std::size_t ExpandedVertexCapacity = RenderFrame::VertexCapacity + RenderFrame::CommandCapacity * 6;
const char *platformDriver()
{
#if defined(_WIN32)
    return "direct3d12";
#elif defined(__APPLE__)
    return "metal";
#else
    return "vulkan";
#endif
}
static_assert(sizeof(SDL_Vertex) == Layout::primitive_stride);
static_assert(Layout::terrain_compose_stride == Layout::primitive_stride && Layout::terrain_reduce_stride == Layout::primitive_stride);
static_assert(Layout::terrain_compose_fragment_size == 368 && Layout::terrain_compose_fragment_params_offset == 0 &&
    Layout::terrain_compose_fragment_seamGeometry_offset == 16 && Layout::terrain_compose_fragment_seamFringe_offset == 80 &&
    Layout::terrain_compose_fragment_surfPlace_offset == 144 && Layout::terrain_compose_fragment_surfColor_offset == 288 &&
    Layout::terrain_compose_fragment_surfLand_offset == 352);
static_assert(Layout::terrain_reduce_fragment_size == 16 && Layout::terrain_reduce_fragment_params_offset == 0);
// Team colour shares the primitive vertex stream and uniform block sizes.
static_assert(Layout::team_color_stride == Layout::primitive_stride && Layout::team_color_size == Layout::primitive_size &&
    Layout::team_color_fragment_size == Layout::primitive_fragment_size);
// Team colour samples straight uploads and premultiplies in the shader, after
// recolouring, wherever a primitive blit would sample a premultiplied upload.
bool teamPremultiplied(const DrawPacket &packet, bool texturePremultiplied)
{
    return texturePremultiplied || packet.linear;
}
static_assert(offsetof(SDL_Vertex, position) == Layout::primitive_attributes[0].offset);
static_assert(offsetof(SDL_Vertex, color) == Layout::primitive_attributes[1].offset);
static_assert(offsetof(SDL_Vertex, tex_coord) == Layout::primitive_attributes[2].offset);

[[noreturn]] void fail(const char *operation) { throw std::runtime_error(std::string(operation) + ": " + SDL_GetError()); }
SDL_GPUTextureFormat gpuFormat(TextureCompression compression)
{
    switch (compression) {
    case TextureCompression::BC3: return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    case TextureCompression::ASTC4x4: return SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM;
    case TextureCompression::Uncompressed: break;
    }
    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}
//! Device bytes of a revision's whole chain: its blocks, or RGBA8 levels.
std::size_t revisionBytes(const TextureRevision &source)
{
    std::size_t bytes = 0;
    if (source.descriptor.compression != TextureCompression::Uncompressed) {
        for (const auto &level : source.blocks) bytes += level.size();
        return bytes;
    }
    bytes = std::size_t(source.descriptor.width) * source.descriptor.height * 4;
    for (const auto &level : source.mips) bytes += level.rgba.size();
    return bytes;
}
// One level of a revision as upload rows: texel rows, or rows of 4x4 blocks.
struct UploadLevel
{
    int width = 0, height = 0, rowTexels = 1;
    std::size_t rowBytes = 0, rows = 0, pitch = 0;
    const unsigned char *data = nullptr;
    std::size_t bytes() const { return rowBytes * rows; }
};
UploadLevel uploadLevel(const TextureRevision &source, std::size_t index)
{
    UploadLevel level;
    if (source.descriptor.compression != TextureCompression::Uncompressed) {
        level.width = std::max(1, source.descriptor.width >> index); level.height = std::max(1, source.descriptor.height >> index);
        level.rowTexels = 4; level.rowBytes = std::size_t((level.width + 3) / 4) * 16; level.rows = std::size_t((level.height + 3) / 4);
        level.pitch = level.rowBytes; level.data = reinterpret_cast<const unsigned char *>(source.blocks.at(index).data());
    } else if (index == 0) {
        level.width = source.descriptor.width; level.height = source.descriptor.height;
        level.rowBytes = std::size_t(level.width) * 4; level.rows = level.height; level.pitch = source.pixels->pitch;
        level.data = static_cast<const unsigned char *>(source.pixels->pixels);
    } else {
        const auto &mip = source.mips.at(index - 1);
        level.width = mip.width; level.height = mip.height; level.rowBytes = std::size_t(mip.width) * 4;
        level.rows = mip.height; level.pitch = level.rowBytes; level.data = reinterpret_cast<const unsigned char *>(mip.rgba.data());
    }
    return level;
}
std::size_t uploadLevelCount(const TextureRevision &source)
{
    return source.descriptor.compression != TextureCompression::Uncompressed ? source.blocks.size() : source.mips.size() + 1;
}
std::vector<unsigned char> readShader(const std::string &file)
{
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Missing shader: " + file);
    auto size = stream.tellg();
    if (size <= 0 || size > 1024 * 1024) throw std::runtime_error("Invalid offline shader size: " + file);
    std::vector<unsigned char> code(static_cast<std::size_t>(size));
    stream.seekg(0); stream.read(reinterpret_cast<char *>(code.data()), size);
    if (!stream) throw std::runtime_error("Unreadable shader: " + file);
    return code;
}
// Released colour textures, kept for reuse by an allocation of the same shape.
// SDL's Vulkan backend submits a layout transition for every texture it
// creates, so revisions that churn at one size (overview and terrain pages,
// streamed snapshots) would otherwise pay a queue submission each.
struct TexturePool
{
    struct Shape
    {
        int width, height; SDL_GPUTextureFormat format; Uint32 levels; SDL_GPUTextureUsageFlags usage;
        auto operator<=>(const Shape &) const = default;
    };
    struct Idle { SDL_GPUTexture *texture; std::size_t bytes; };
    std::map<Shape, std::vector<Idle>> idle;
    std::size_t bytes = 0, limit = 0;
    bool open = false;
    std::function<bool(std::size_t)> fits; // Whether the device's total budget has room.
    static constexpr std::size_t ShapeDepth = 8;
    bool keep(const Shape &shape, SDL_GPUTexture *texture, std::size_t size)
    {
        if (!open || bytes + size > limit || (fits && !fits(size))) return false;
        auto &list = idle[shape];
        if (list.size() >= ShapeDepth) return false;
        list.push_back({texture, size}); bytes += size; return true;
    }
    SDL_GPUTexture *take(const Shape &shape)
    {
        const auto found = idle.find(shape);
        if (found == idle.end() || found->second.empty()) return nullptr;
        const auto entry = found->second.back(); found->second.pop_back();
        if (found->second.empty()) idle.erase(found);
        bytes -= entry.bytes; return entry.texture;
    }
    void release(SDL_GPUDevice *device)
    {
        for (auto &[shape, list] : idle) for (const auto &entry : list) SDL_ReleaseGPUTexture(device, entry.texture);
        idle.clear(); bytes = 0;
    }
};
struct Texture
{
    SDL_GPUDevice *device;
    SDL_GPUTexture *gpu = nullptr;
    TextureSnapshot source;
    int width=0, height=0;
    std::size_t byteSize=0;
    std::size_t *residentBytes = nullptr;
    // Its budget class's running total while the texture cache has admitted it.
    std::size_t *classBytes = nullptr;
    bool submitted = false, premultiplied = false, renderTarget = false;
    bool progressiveUpload = false;
    // Block-compressed textures are sampled only, never rendered to.
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    bool compressed = false;
    std::size_t uploadOffset = 0;
    std::uint64_t uploadSubmission = 0;
    std::shared_ptr<Texture> depth;
    TexturePool *pool = nullptr;
    TexturePool::Shape shape{};
    ~Texture() {
        if (gpu && !(pool && pool->keep(shape, gpu, byteSize))) SDL_ReleaseGPUTexture(device, gpu);
        if (residentBytes) *residentBytes -= byteSize;
        if (classBytes) *classBytes -= byteSize;
    }
};
using TexturePtr = std::shared_ptr<Texture>;
struct Geometry
{
    SDL_GPUDevice *device;
    GeometrySnapshot source;
    SDL_GPUBuffer *vertices = nullptr, *indices = nullptr;
    std::size_t *residentBytes = nullptr; // Running total, charged source->bytes.
    bool submitted = false;
    ~Geometry() {
        if (vertices) SDL_ReleaseGPUBuffer(device, vertices);
        if (indices) SDL_ReleaseGPUBuffer(device, indices);
        if (residentBytes) *residentBytes -= source->bytes;
    }
};
using GeometryPtr = std::shared_ptr<Geometry>;
struct CoverageGPU
{
    SDL_GPUDevice *device = nullptr;
    SDL_GPUTexture *weights = nullptr, *selectors = nullptr;
    std::shared_ptr<const CoverageData> source;
    std::size_t bytes = 0;
    std::size_t *residentBytes = nullptr; // Running total, charged bytes.
    bool submitted = false;
    ~CoverageGPU() {
        if (weights) SDL_ReleaseGPUTexture(device, weights);
        if (selectors) SDL_ReleaseGPUTexture(device, selectors);
        if (residentBytes) *residentBytes -= bytes;
    }
};
using CoveragePtr = std::shared_ptr<CoverageGPU>;
struct Work
{
    RenderSubmission submission;
    SDL_GPUFence *fence = nullptr;
    std::vector<TexturePtr> textures;
    std::vector<GeometryPtr> geometry;
    std::vector<CoveragePtr> coverage;
    std::vector<std::pair<SDL_GPUTransferBuffer *, std::size_t>> transfers;
    SDL_GPUBuffer *vertices = nullptr;
    std::size_t vertexBytes = 0;
    // Transient bytes this work holds until it retires: vertices and staging.
    std::size_t chargedBytes() const
    {
        std::size_t total = vertexBytes;
        for (const auto &transfer : transfers) total += transfer.second;
        return total;
    }
};
struct Readback
{
    CaptureRequest request;
    SDL_GPUFence *fence = nullptr;
    SDL_GPUTransferBuffer *transfer = nullptr;
    bool cancelled = false;
    std::chrono::steady_clock::time_point admitted = std::chrono::steady_clock::now();
    TexturePtr source;
};
struct Draw
{
    const DrawPacket *packet;
    TexturePtr texture, target;
    Uint32 first = 0, count = 0;
    GeometryPtr geometry;
    TexturePtr secondTexture;
    std::array<TexturePtr,4> materials{};
    CoveragePtr coverage;
    TexturePtr scratch;
    std::array<TexturePtr, DirectTerrainDraw::Count> directTextures{};
    std::array<TexturePtr, DirectTerrainDraw::StreamedCount> streamedTextures{};
};
// A device texture written in place for its StreamedTexture owner.
struct StreamedEntry
{
    std::weak_ptr<StreamedTexture> owner;
    TexturePtr texture;
    bool committed = false;   // Created by a submitted frame.
    std::size_t recorded = 0; // Queued writes recorded into the open submission.
    std::size_t recordedBytes = 0;
};
struct PrimitiveState
{
    SDL_GPUTexture *target = nullptr, *image = nullptr;
    SDL_GPUSampler *sampler = nullptr;
    SDL_GPUGraphicsPipeline *pipeline = nullptr;
    SDL_Rect clip{}, viewport{};
    std::array<float, Layout::primitive_size / sizeof(float)> vertex{};
    std::array<float, Layout::primitive_fragment_size / sizeof(float)> fragment{};
    bool operator==(const PrimitiveState &other) const
    {
        return target == other.target && image == other.image && sampler == other.sampler && pipeline == other.pipeline &&
            SDL_RectsEqual(&clip, &other.clip) && SDL_RectsEqual(&viewport, &other.viewport) &&
            vertex == other.vertex && fragment == other.fragment;
    }
};
struct PrimitivePass
{
    SDL_GPURenderPass *pass = nullptr;
    SDL_GPUTexture *target = nullptr;
    ~PrimitivePass() { end(); }
    void end() { if (pass) { SDL_EndGPURenderPass(pass); pass = nullptr; } target = nullptr; }
};
class SDLGPUDevice final : public RenderDevice
{
    SDL_GPUDevice *device = nullptr;
    SDL_GPUShaderFormat shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
    std::string shaderFamily = "vulkan", shaderRoot;
    nlohmann::json shaderArtifacts;
    SDL_Window *window;
    bool claimed = false;
    SDL_GPUShader *vertexShader = nullptr, *fragmentShader = nullptr;
    SDL_GPUSampler *nearest = nullptr, *linear = nullptr;
    std::map<std::tuple<bool, bool, bool, bool>, SDL_GPUSampler *> samplers;
    std::map<DrawPacket::Material, std::pair<SDL_GPUShader *, SDL_GPUShader *>> worldShaders;
    using WorldPipelineKey = std::tuple<DrawPacket::Material, DrawPacket::Blend, DrawPacket::Depth, bool, DrawPacket::Cull>;
    std::map<WorldPipelineKey, SDL_GPUGraphicsPipeline *> worldPipelines;
    // Declared before texture owners so it outlives every allocation lease.
    // A texture is charged once, including while only pending work owns it.
    std::size_t residentTextureBytes = 0;
    TexturePool texturePool;
    // Running totals, so admission and telemetry never walk the caches: live
    // geometry and coverage, cached textures by budget class, retiring work's
    // transient bytes and pending readbacks.
    std::size_t residentGeometryBytes = 0, residentCoverageBytes = 0, workBytes = 0, captureBytes = 0;
    std::array<std::size_t, std::tuple_size_v<decltype(RenderBudget::classes)>> classBytes{};
    std::map<const GeometryRevision *, GeometryPtr> geometryCache;
    bool worldAvailable = false, skinAvailable = false, terrainAvailable = false, teamColorAvailable = false;
    bool bc3Supported = false, astcSupported = false;
    std::array<SDL_GPUShader *,2> terrainShaders{};
    SDL_GPUShader *directTerrainVertex = nullptr, *directTerrainFragment = nullptr;
    SDL_GPUGraphicsPipeline *directTerrainPipeline = nullptr;
    SDL_GPUShader *directTerrainHdFragment = nullptr;
    SDL_GPUGraphicsPipeline *directTerrainHdPipeline = nullptr;
    std::map<const StreamedTexture *, StreamedEntry> streamed;
    unsigned streamedDeferrals = 0; // Consecutive frames deferred for streamed staging.
    // New cache textures this submission created for screen packets (see resolve).
    static constexpr unsigned FrameTextureLimit = 96;
    unsigned frameTextures = 0;
    bool capCreations = false;
    std::array<SDL_GPUGraphicsPipeline *,2> terrainPipelines{};
    using CoverageKey = std::tuple<std::uint64_t,std::uint64_t,std::uint32_t,std::uint32_t>;
    std::map<CoverageKey,CoveragePtr> coverageCache;
    std::map<int,TexturePtr> terrainScratch;
    using PrimitivePipelineKey = std::tuple<SDL_GPUTextureFormat, bool, DrawPacket::Material, DrawPacket::Blend>;
    std::map<PrimitivePipelineKey, SDL_GPUGraphicsPipeline *> pipelines;
    using TextureKey = std::tuple<const TextureRevision *, bool, bool>;
    std::map<TextureKey, TexturePtr> cache;
    std::map<const TextureRevision *, std::weak_ptr<Texture>> targets;
    // Cache entries admitted since the last submission: marked submitted, or
    // dropped when the frame fails, without walking the caches.
    std::vector<TextureKey> freshTextures;
    std::vector<const GeometryRevision *> freshGeometry;
    std::vector<CoverageKey> freshCoverage;
    // reap() retires dead CPU revisions a bounded slice at a time, resuming here.
    TextureKey textureSweep{};
    const TextureRevision *targetSweep = nullptr;
    const GeometryRevision *geometrySweep = nullptr;
    CoverageKey coverageSweep{};
    static constexpr std::size_t SweepSlice = 128;
    TexturePtr screen, committedScreen, white;
    std::shared_ptr<const CoverageData> zeroCoverage;
    bool frameOpen = false;
    std::deque<Work> work;
    std::deque<Readback> captures;
    std::uint64_t epoch = 1, serial = 0, completed = 0, lastFrame = 0, committedFrame = 0;
    int width, height;
    RenderTelemetry counts;
    static constexpr int MaximumTextureSize = 4096;
    static constexpr std::size_t SubmissionLimit = 8;
    // Platform limits (smaller on phones); see RenderBudget::mobile.
    const RenderBudget budgetLimits = platformRenderBudget();
    const std::size_t TotalBudget = budgetLimits.total, GeometryBudget = budgetLimits.geometry;

    TexturePtr allocate(int w, int h, TextureSnapshot source = {}, const Work *pending = nullptr)
    {
        if (w <= 0 || h <= 0 || w > MaximumTextureSize || h > MaximumTextureSize)
            throw std::invalid_argument("SDL GPU texture dimensions exceed supported limit");
        updateMemory(pending);
        const std::size_t bytes = source ? revisionBytes(*source) : std::size_t(w) * h * 4;
        if (bytes > TotalBudget - std::min(TotalBudget, counts.resourceBytes) && (pooledUploadBytes || texturePool.bytes)) { releaseUploadPool(); updateMemory(pending); }
        if (bytes > TotalBudget - std::min(TotalBudget, counts.resourceBytes)) throw RenderAdmissionDeferred("SDL GPU combined budget exhausted");
        const auto compression = source ? source->descriptor.compression : TextureCompression::Uncompressed;
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D; info.format = gpuFormat(compression);
        info.usage = compression != TextureCompression::Uncompressed ? SDL_GPU_TEXTUREUSAGE_SAMPLER
            : SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        info.width = w; info.height = h; info.layer_count_or_depth = 1; info.num_levels = source ? Uint32(uploadLevelCount(*source)) : 1;
        auto texture = std::make_shared<Texture>();texture->device=device;texture->source=std::move(source);
        texture->width=w;texture->height=h;texture->byteSize=bytes;
        texture->format=info.format;texture->compressed=compression!=TextureCompression::Uncompressed;
        // Every caller uploads, clears or blits all of a new allocation before
        // sampling it, so a pooled texture's old contents never show.
        texture->shape = {w, h, info.format, info.num_levels, info.usage};
        texture->gpu = texturePool.take(texture->shape);
        texture->pool = &texturePool;
        if (texture->gpu) { texture->residentBytes = &residentTextureBytes; residentTextureBytes += bytes; return texture; }
        texture->gpu = SDL_CreateGPUTexture(device, &info);
        if (!texture->gpu) fail("create texture");
        texture->residentBytes = &residentTextureBytes; residentTextureBytes += bytes;
        return texture;
    }
    void release(Work &entry)
    {
        if (entry.fence) {
            // Exception cleanup may receive submitted work that has not completed.
            // Normal retirement only calls this after its fence has signalled.
            if (!SDL_QueryGPUFence(device, entry.fence)) SDL_WaitForGPUFences(device, true, &entry.fence, 1);
            SDL_ReleaseGPUFence(device, entry.fence);
        }
        for (auto [transfer, bytes] : entry.transfers) recycleTransfer(transfer);
        if (entry.vertices) recycleVertices(entry.vertices);
        entry.textures.clear(); entry.geometry.clear(); entry.coverage.clear();
    }
    // Visits at most `slice` entries from `cursor`, wrapping, erasing those
    // `retire` accepts, and remembers where to resume: per-call work stays
    // bounded however many entries the cache holds.
    template <class Map, class Retire>
    static void sweep(Map &map, typename Map::key_type &cursor, std::size_t slice, Retire retire)
    {
        if (map.empty()) return;
        auto it = map.lower_bound(cursor);
        const std::size_t limit = std::min(slice, map.size());
        for (std::size_t visited = 0; visited < limit && !map.empty(); ++visited) {
            if (it == map.end()) it = map.begin();
            if (retire(it->second)) it = map.erase(it); else ++it;
        }
        if (it == map.end()) it = map.begin();
        cursor = it == map.end() ? typename Map::key_type{} : it->first;
    }
    void reap()
    {
        while (!work.empty() && SDL_QueryGPUFence(device, work.front().fence)) {
            completed = std::max(completed, work.front().submission.identity);
            workBytes -= std::min(workBytes, work.front().chargedBytes());
            release(work.front()); work.pop_front();
        }
        // CPU revisions referenced only by cache and completed GPU work can retire.
        sweep(cache, textureSweep, SweepSlice, [](const TexturePtr &texture) {
            return texture.use_count() == 1 && texture->source.use_count() == 1;
        });
        sweep(targets, targetSweep, SweepSlice, [](const std::weak_ptr<Texture> &target) { return target.expired(); });
        sweep(geometryCache, geometrySweep, SweepSlice, [](const GeometryPtr &geometry) {
            return geometry.use_count() == 1 && geometry->source.use_count() == 1;
        });
        sweep(coverageCache, coverageSweep, SweepSlice, [](const CoveragePtr &coverage) {
            return coverage.use_count() == 1 && coverage->source.use_count() == 1;
        });
        updateMemory();
    }
    void updateMemory(const Work *pending = nullptr)
    {
        counts.resourceBytes = residentTextureBytes + residentCoverageBytes + residentGeometryBytes + workBytes + captureBytes +
            (pending ? pending->chargedBytes() : 0);
        counts.idleBytes = idleBytes();
    }
    // Cache admissions since the last submission, now owned by submitted work.
    void markFreshSubmitted()
    {
        for (const auto &key : freshTextures) if (auto found = cache.find(key); found != cache.end()) found->second->submitted = true;
        for (auto *key : freshGeometry) if (auto found = geometryCache.find(key); found != geometryCache.end()) found->second->submitted = true;
        for (const auto &key : freshCoverage) if (auto found = coverageCache.find(key); found != coverageCache.end()) found->second->submitted = true;
        freshTextures.clear(); freshGeometry.clear(); freshCoverage.clear();
    }
    // A failed frame: forget admissions no submitted work uploaded.
    void dropFreshUnsubmitted()
    {
        for (const auto &key : freshTextures) if (auto found = cache.find(key); found != cache.end() && !found->second->submitted) cache.erase(found);
        for (auto *key : freshGeometry) if (auto found = geometryCache.find(key); found != geometryCache.end() && !found->second->submitted) geometryCache.erase(found);
        for (const auto &key : freshCoverage) if (auto found = coverageCache.find(key); found != coverageCache.end() && !found->second->submitted) coverageCache.erase(found);
        freshTextures.clear(); freshGeometry.clear(); freshCoverage.clear();
    }
    void admitTexture(const TextureKey &key, const TexturePtr &texture)
    {
        if (!cache.emplace(key, texture).second) return;
        freshTextures.push_back(key);
        if (!texture->classBytes) {
            texture->classBytes = &classBytes[std::size_t(texture->source->descriptor.budgetClass)];
            *texture->classBytes += texture->byteSize;
        }
    }
    void clearCaches()
    {
        cache.clear(); targets.clear(); geometryCache.clear(); coverageCache.clear(); terrainScratch.clear();
        freshTextures.clear(); freshGeometry.clear(); freshCoverage.clear();
    }
    void reserveMemory(std::size_t bytes, const Work *pending = nullptr)
    {
        updateMemory(pending);
        if (bytes > TotalBudget) throw std::length_error("SDL GPU single resource exceeds combined budget");
        if (counts.resourceBytes > TotalBudget - bytes && (pooledUploadBytes || texturePool.bytes)) { releaseUploadPool(); updateMemory(pending); }
        if (counts.resourceBytes > TotalBudget - bytes) throw RenderAdmissionDeferred("SDL GPU combined resource budget exhausted");
    }
    // Upload staging buffers are recycled once their work completes: creating
    // and binding a Vulkan buffer for every frame's vertices and uploads cost
    // more CPU than filling them. Power-of-two classes from 64 KiB to 16 MiB,
    // a few of each, at most UploadPoolBudget idle (24 MiB, a tenth of the
    // total budget on phones); larger buffers are released. Idle buffers and
    // textures (idleBytes, reported apart from resourceBytes) are kept only
    // while they fit beside the resources within the total budget, and an
    // admission that would fail and an explicit trim release them first.
    static constexpr unsigned UploadClasses = 9;
    static constexpr std::size_t UploadClassDepth = 4;
    const std::size_t UploadPoolBudget = std::min<std::size_t>(24 * 1024 * 1024, TotalBudget / 10);
    // Upload and download (capture readback) buffers pool separately. Every
    // pooled-size buffer is in transferClass while it exists; release one only
    // through recycleTransfer or dropTransfer, or a later buffer SDL places at
    // the same address would inherit its class.
    std::array<std::array<std::vector<SDL_GPUTransferBuffer *>, UploadClasses>, 2> uploadPool;
    std::unordered_map<SDL_GPUTransferBuffer *, std::pair<bool, unsigned>> transferClass; // download?, class
    static unsigned transferKind(SDL_GPUTransferBufferUsage usage) { return usage == SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD ? 1 : 0; }
    std::size_t pooledUploadBytes = 0;
    static std::size_t uploadClassBytes(unsigned index) { return std::size_t(64 * 1024) << index; }
    std::size_t idleBytes() const { return pooledUploadBytes + texturePool.bytes; }
    bool idleFits(std::size_t bytes) const { return counts.resourceBytes + idleBytes() + bytes <= TotalBudget; }
    SDL_GPUTransferBuffer *transfer(SDL_GPUTransferBufferUsage usage, std::size_t size)
    {
        if (size > 0xffffffffu) throw std::length_error("GPU transfer exceeds 32-bit capacity");
        unsigned index = 0;
        while (index < UploadClasses && uploadClassBytes(index) < size) ++index;
        const bool pooled = index < UploadClasses;
        auto &pool = uploadPool[transferKind(usage)];
        if (pooled && !pool[index].empty()) {
            auto *result = pool[index].back(); pool[index].pop_back();
            pooledUploadBytes -= uploadClassBytes(index);
            return result;
        }
        SDL_GPUTransferBufferCreateInfo info{}; info.usage = usage;
        info.size = static_cast<Uint32>(pooled ? uploadClassBytes(index) : size);
        auto *result = SDL_CreateGPUTransferBuffer(device, &info);
        if (!result) fail("create transfer buffer");
        if (pooled) transferClass.emplace(result, std::make_pair(transferKind(usage) == 1, index));
        return result;
    }
    // Work holding the buffer has completed (or was never submitted).
    void recycleTransfer(SDL_GPUTransferBuffer *buffer)
    {
        const auto found = transferClass.find(buffer);
        if (found != transferClass.end()) {
            const auto [download, index] = found->second;
            auto &pool = uploadPool[download ? 1 : 0];
            if (pool[index].size() < UploadClassDepth && pooledUploadBytes + uploadClassBytes(index) <= UploadPoolBudget &&
                idleFits(uploadClassBytes(index))) {
                pool[index].push_back(buffer); pooledUploadBytes += uploadClassBytes(index); return;
            }
            transferClass.erase(found);
        }
        SDL_ReleaseGPUTransferBuffer(device, buffer);
    }
    // The buffer may still be in use (failure cleanup): never reuse it.
    void dropTransfer(SDL_GPUTransferBuffer *buffer)
    {
        transferClass.erase(buffer);
        SDL_ReleaseGPUTransferBuffer(device, buffer);
    }
    void releaseUploadPool()
    {
        for (auto &pool : uploadPool) for (auto &buffers : pool) {
            for (auto *buffer : buffers) { transferClass.erase(buffer); SDL_ReleaseGPUTransferBuffer(device, buffer); }
            buffers.clear();
        }
        for (auto &buffers : vertexPool) {
            for (auto *buffer : buffers) { vertexClass.erase(buffer); SDL_ReleaseGPUBuffer(device, buffer); }
            buffers.clear();
        }
        pooledUploadBytes = 0;
        texturePool.release(device);
    }
    // Each frame's vertex buffer comes from the same size classes, at most
    // VertexClassDepth idle per class (one per frame in flight would do; the
    // frame's size wanders between neighbouring classes).
    static constexpr std::size_t VertexClassDepth = 2;
    std::array<std::vector<SDL_GPUBuffer *>, UploadClasses> vertexPool;
    std::unordered_map<SDL_GPUBuffer *, unsigned> vertexClass;
    SDL_GPUBuffer *vertexBuffer(std::size_t size, std::size_t &capacity)
    {
        unsigned index = 0;
        while (index < UploadClasses && uploadClassBytes(index) < size) ++index;
        const bool pooled = index < UploadClasses;
        capacity = pooled ? uploadClassBytes(index) : size;
        if (pooled && !vertexPool[index].empty()) {
            auto *result = vertexPool[index].back(); vertexPool[index].pop_back();
            pooledUploadBytes -= capacity;
            return result;
        }
        SDL_GPUBufferCreateInfo info{}; info.usage = SDL_GPU_BUFFERUSAGE_VERTEX; info.size = static_cast<Uint32>(capacity);
        auto *result = SDL_CreateGPUBuffer(device, &info); if (!result) fail("create vertex buffer");
        if (pooled) vertexClass.emplace(result, index);
        return result;
    }
    void recycleVertices(SDL_GPUBuffer *buffer)
    {
        const auto found = vertexClass.find(buffer);
        if (found != vertexClass.end()) {
            const auto index = found->second;
            if (vertexPool[index].size() < VertexClassDepth && pooledUploadBytes + uploadClassBytes(index) <= UploadPoolBudget &&
                idleFits(uploadClassBytes(index))) {
                vertexPool[index].push_back(buffer); pooledUploadBytes += uploadClassBytes(index); return;
            }
            vertexClass.erase(found);
        }
        SDL_ReleaseGPUBuffer(device, buffer);
    }
    void uploadBlocks(SDL_GPUCommandBuffer *commands, Work &entry, const TexturePtr &texture)
    {
        // Prepared premultiplied blocks: copied as they are, every level at once.
        const std::size_t bytes = texture->byteSize;
        reserveMemory(bytes, &entry);
        auto *buffer = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes); entry.transfers.emplace_back(buffer, bytes);
        auto *mapped = static_cast<unsigned char *>(SDL_MapGPUTransferBuffer(device, buffer, false));
        if (!mapped) fail("map compressed texture upload");
        const auto &source = *texture->source;
        std::size_t offset = 0;
        for (const auto &level : source.blocks) { std::memcpy(mapped + offset, level.data(), level.size()); offset += level.size(); }
        SDL_UnmapGPUTransferBuffer(device, buffer);
        auto *copy = SDL_BeginGPUCopyPass(commands); if (!copy) fail("begin compressed upload pass");
        offset = 0;
        for (std::size_t index = 0; index < source.blocks.size(); ++index) {
            const auto level = uploadLevel(source, index);
            SDL_GPUTextureTransferInfo from{}; from.transfer_buffer = buffer; from.offset = Uint32(offset);
            from.pixels_per_row = Uint32(level.width); from.rows_per_layer = Uint32(level.height);
            SDL_GPUTextureRegion region{}; region.texture = texture->gpu; region.mip_level = Uint32(index);
            region.w = Uint32(level.width); region.h = Uint32(level.height); region.d = 1;
            SDL_UploadToGPUTexture(copy, &from, &region, false); offset += level.bytes();
        }
        SDL_EndGPUCopyPass(copy);
        counts.uploadBytes += bytes;
    }
    void upload(SDL_GPUCommandBuffer *commands, Work &entry, TexturePtr texture, SDL_Surface *source, bool opaque, bool premultiplied = false)
    {
        if (texture->compressed) { uploadBlocks(commands, entry, texture); return; }
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> converted(
            SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        if (!converted) fail("convert texture pixels");
        const std::size_t bytes = texture->byteSize;
        // The destination texture is already charged by allocate().
        reserveMemory(bytes, &entry);
        auto *buffer = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes); entry.transfers.emplace_back(buffer, bytes);
        auto *pixels = static_cast<unsigned char *>(SDL_MapGPUTransferBuffer(device, buffer, false));
        if (!pixels) fail("map texture upload");
        struct Level {int width,height;std::size_t offset;};
        std::vector<Level> levels;
        auto write=[&](int w,int h,const void *sourcePixels,std::size_t pitch,std::size_t offset) {
            for(int y=0;y<h;++y) {
                auto *row=pixels+offset+std::size_t(y)*w*4;
                std::memcpy(row,static_cast<const unsigned char *>(sourcePixels)+y*pitch,w*4);
                if(opaque)for(int x=0;x<w;++x)row[x*4+3]=255;
                if(premultiplied)for(int x=0;x<w;++x)for(int channel=0;channel<3;++channel)
                    row[x*4+channel]=(unsigned(row[x*4+channel])*row[x*4+3]+127)/255;
            }
            levels.push_back({w,h,offset});
        };
        write(texture->width,texture->height,converted->pixels,converted->pitch,0);
        std::size_t offset=std::size_t(texture->width)*texture->height*4;
        if(texture->source)for(const auto &level:texture->source->mips) {
            write(level.width,level.height,level.rgba.data(),std::size_t(level.width)*4,offset);offset+=level.rgba.size();
        }
        SDL_UnmapGPUTransferBuffer(device, buffer);
        auto *copy=SDL_BeginGPUCopyPass(commands);if(!copy)fail("begin upload pass");
        for(std::size_t index=0;index<levels.size();++index) {
            const auto &level=levels[index];SDL_GPUTextureTransferInfo sourceInfo{};sourceInfo.transfer_buffer=buffer;
            sourceInfo.offset=Uint32(level.offset);sourceInfo.pixels_per_row=level.width;sourceInfo.rows_per_layer=level.height;
            SDL_GPUTextureRegion region{};region.texture=texture->gpu;region.mip_level=Uint32(index);region.w=level.width;region.h=level.height;region.d=1;
            SDL_UploadToGPUTexture(copy,&sourceInfo,&region,false);
        }
        SDL_EndGPUCopyPass(copy);
        counts.uploadBytes += bytes;
    }
    void progressUpload(TexturePtr texture, const Work &pending, bool opaque, bool premultiplied)
    {
        // A partial immutable upload survives an aborted presentation frame.
        // Only one bounded staging slice is outstanding per texture, and its
        // fence must signal before staging another slice or sampling any level.
        if(texture->uploadSubmission>completed)throw RenderAdmissionDeferred("SDL GPU texture upload pending");
        if(texture->uploadOffset==texture->byteSize) {texture->progressiveUpload=false;return;}
        if(work.size()>=SubmissionLimit)throw RenderAdmissionDeferred("SDL GPU upload submission queue full");
        constexpr std::size_t SliceBytes=8*1024*1024;
        const auto &revision=*texture->source;
        if(!texture->compressed && revision.pixels->format!=SDL_PIXELFORMAT_RGBA32)throw std::invalid_argument("Progressive upload requires normalized RGBA32 restoration pixels");
        std::size_t offset=texture->uploadOffset;unsigned levelIndex=0;
        auto level=uploadLevel(revision,0);
        while(offset>=level.bytes()) {offset-=level.bytes();level=uploadLevel(revision,++levelIndex);}
        // Rows are texel rows, or rows of 4x4 blocks in a compressed level.
        const auto rowBytes=level.rowBytes;const unsigned firstRow=unsigned(offset/rowBytes);
        const unsigned rows=unsigned(std::min<std::size_t>(level.rows-firstRow,std::max<std::size_t>(1,SliceBytes/rowBytes)));
        const auto bytes=rowBytes*rows;reserveMemory(bytes,&pending);
        const bool alterTexels=!texture->compressed && (opaque || premultiplied);
        const int width=level.width;
        Work entry;entry.textures.push_back(texture);entry.transfers.reserve(1);
        auto *commands=SDL_AcquireGPUCommandBuffer(device);if(!commands)fail("acquire incremental upload commands");
        try {
            auto *buffer=transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,bytes);entry.transfers.emplace_back(buffer,bytes);
            auto *pixels=static_cast<unsigned char *>(SDL_MapGPUTransferBuffer(device,buffer,false));if(!pixels)fail("map incremental texture upload");
            for(unsigned y=0;y<rows;++y) {
                auto *row=pixels+y*rowBytes;std::memcpy(row,level.data+(firstRow+y)*level.pitch,rowBytes);
                if(!alterTexels)continue;
                if(opaque)for(int x=0;x<width;++x)row[x*4+3]=255;
                if(premultiplied)for(int x=0;x<width;++x)for(int channel=0;channel<3;++channel)
                    row[x*4+channel]=(unsigned(row[x*4+channel])*row[x*4+3]+127)/255;
            }
            SDL_UnmapGPUTransferBuffer(device,buffer);
            auto *copy=SDL_BeginGPUCopyPass(commands);if(!copy)fail("begin incremental upload pass");
            const unsigned texelRow=firstRow*level.rowTexels,texelRows=std::min<unsigned>(unsigned(level.height)-texelRow,rows*level.rowTexels);
            SDL_GPUTextureTransferInfo sourceInfo{};sourceInfo.transfer_buffer=buffer;sourceInfo.pixels_per_row=width;sourceInfo.rows_per_layer=rows*level.rowTexels;
            SDL_GPUTextureRegion region{};region.texture=texture->gpu;region.mip_level=levelIndex;region.y=texelRow;region.w=width;region.h=texelRows;region.d=1;
            SDL_UploadToGPUTexture(copy,&sourceInfo,&region,false);SDL_EndGPUCopyPass(copy);
            submitWork(commands,entry);texture->submitted=true;texture->uploadSubmission=serial;texture->uploadOffset+=bytes;counts.uploadBytes+=bytes;
        } catch(...) {if(commands)SDL_CancelGPUCommandBuffer(commands);release(entry);throw;}
        throw RenderAdmissionDeferred("SDL GPU texture upload progressing");
    }
    void preservePreparedUploads(SDL_GPUCommandBuffer *&commands, Work &entry)
    {
        if(entry.transfers.empty())return;
        // resolve() runs before ordered drawing begins. Commit immutable upload
        // preparation under pressure so retries do not discard the same assets
        // forever while their staging buffers occupy the admission budget.
        for(const auto &key:freshTextures)if(auto found=cache.find(key);found!=cache.end() && !found->second->submitted)entry.textures.push_back(found->second);
        for(auto *key:freshGeometry)if(auto found=geometryCache.find(key);found!=geometryCache.end() && !found->second->submitted)entry.geometry.push_back(found->second);
        for(const auto &key:freshCoverage)if(auto found=coverageCache.find(key);found!=coverageCache.end() && !found->second->submitted)entry.coverage.push_back(found->second);
        submitWork(commands,entry);
        markFreshSubmitted();
    }
    void validateBlocks(const TextureRevision &source, bool asTarget) const
    {
        const auto format = source.descriptor.compression;
        if ((format == TextureCompression::BC3 && !bc3Supported) || (format == TextureCompression::ASTC4x4 && !astcSupported))
            throw std::invalid_argument("SDL GPU device cannot sample this block format");
        if (asTarget) throw std::invalid_argument("Compressed textures cannot be render targets");
        if (source.descriptor.alpha != TextureDescriptor::Alpha::Premultiplied || source.descriptor.mips != TextureDescriptor::Mips::Generated ||
            source.blocks.empty() || source.blocks.size() > 16 || source.descriptor.width > MaximumTextureSize || source.descriptor.height > MaximumTextureSize)
            throw std::invalid_argument("Invalid compressed texture descriptor");
        for (std::size_t index = 0; index < source.blocks.size(); ++index) {
            const int w = source.descriptor.width >> index, h = source.descriptor.height >> index;
            if (w < 4 || h < 4 || source.blocks[index].size() != compressedLevelBytes(w, h))
                throw std::invalid_argument("Invalid compressed level blocks");
        }
    }
    TexturePtr resolve(SDL_GPUCommandBuffer *&commands, Work &entry, TextureSnapshot source, bool opaque = false, bool premultiplied = false, bool asTarget = false)
    {
        if (!source) return white;
        if (source->descriptor.compression != TextureCompression::Uncompressed) validateBlocks(*source, asTarget);
        else {
        if (!source->pixels || source->pixels->w != source->descriptor.width || source->pixels->h != source->descriptor.height)
            throw std::invalid_argument("SDL GPU requires CPU texture pixels");
        if(asTarget && source->descriptor.mips!=TextureDescriptor::Mips::Disabled)throw std::invalid_argument("Authored mip textures cannot be render targets");
        int mipWidth=source->descriptor.width,mipHeight=source->descriptor.height;
        for(const auto &level:source->mips) {
            if(mipWidth==1 && mipHeight==1)throw std::invalid_argument("Excess authored mip levels");
            mipWidth=std::max(1,mipWidth/2);mipHeight=std::max(1,mipHeight/2);
            if(level.width!=mipWidth || level.height!=mipHeight || level.rgba.size()!=std::size_t(mipWidth)*mipHeight*4)
                throw std::invalid_argument("Invalid authored mip dimensions or owned bytes");
        }
        if((source->descriptor.mips==TextureDescriptor::Mips::Generated && (mipWidth!=1 || mipHeight!=1)) ||
            (source->descriptor.mips==TextureDescriptor::Mips::Disabled && !source->mips.empty()))
            throw std::invalid_argument("Incomplete authored mip chain");
        }
        // GPU render targets store premultiplied source-over results. Later
        // sampling must use those results rather than immutable restoration pixels.
        if (auto target = targets.find(source.get()); target != targets.end()) {
            if (auto retained = target->second.lock()) {
                if(retained->progressiveUpload) {
                    try {progressUpload(retained,entry,opaque || source->descriptor.alpha==TextureDescriptor::Alpha::Opaque,false);}
                    catch(const RenderAdmissionDeferred &){preservePreparedUploads(commands,entry);throw;}
                }
                return retained;
            }
            targets.erase(target);
        }
        premultiplied |= source->descriptor.alpha == TextureDescriptor::Alpha::Premultiplied;
        if (asTarget) premultiplied = source->descriptor.alpha == TextureDescriptor::Alpha::Premultiplied;
        opaque |= source->descriptor.alpha == TextureDescriptor::Alpha::Opaque;
        const auto key = std::make_tuple(source.get(), opaque, premultiplied);
        auto found = cache.find(key);
        if (found != cache.end()) {
            if(found->second->progressiveUpload) {
                try {progressUpload(found->second,entry,opaque,premultiplied && source->descriptor.alpha!=TextureDescriptor::Alpha::Premultiplied);}
                catch(const RenderAdmissionDeferred &){preservePreparedUploads(commands,entry);throw;}
            }
            if (asTarget) { found->second->renderTarget = true; targets[source.get()] = found->second; }
            return found->second;
        }
        const std::size_t bytes = revisionBytes(*source);
        const auto &budgets = budgetLimits.classes;
        const auto budget = static_cast<std::size_t>(source->descriptor.budgetClass);
        if (budget >= budgets.size()) throw std::invalid_argument("Invalid texture budget class");
        std::size_t resident = classBytes[budget];
        if (source->descriptor.budgetClass==TextureDescriptor::BudgetClass::TerrainMask) resident+=residentCoverageBytes;
        if (bytes > budgets[budget]) throw std::length_error("SDL GPU single texture exceeds class budget");
        // Immutable uploaded content can be restored from its snapshot even
        // while the logical asset remains alive. Keep rendered targets and any
        // allocation referenced by this frame or in-flight work intact.
        // Under pressure, first retire every dead revision reap() has not
        // reached yet (it visits a bounded slice per call).
        if (resident > budgets[budget] - bytes) {
            sweep(cache, textureSweep, cache.size(), [](const TexturePtr &texture) {
                return texture.use_count() == 1 && texture->source.use_count() == 1;
            });
            resident = classBytes[budget];
            if (source->descriptor.budgetClass==TextureDescriptor::BudgetClass::TerrainMask) resident+=residentCoverageBytes;
        }
        for (auto it=cache.begin(); resident>budgets[budget]-bytes && it!=cache.end();) {
            const auto &candidate=it->second;
            if(candidate.use_count()==1 && !candidate->renderTarget && candidate->source->descriptor.budgetClass==source->descriptor.budgetClass) {
                resident-=candidate->byteSize; it=cache.erase(it);
            } else ++it;
        }
        if (resident > budgets[budget] - bytes) throw RenderAdmissionDeferred("SDL GPU texture class budget exhausted");
        try {
        // SDL's Vulkan backend submits a command buffer for every texture it
        // creates. A deferrable submission creates at most FrameTextureLimit
        // new textures for packets drawn to the screen; past that the uploads so far are
        // kept and the frame defers, so the previous frame shows while later
        // frames create the rest (a resume after a background trim, a first
        // zoom out). Offscreen composition (terrain pages) is exempt: a page
        // half composed would have to start again.
        if (capCreations && !asTarget) {
            if (frameTextures >= FrameTextureLimit) {
                ++counts.deferredFrames;
                throw RenderAdmissionDeferred("SDL GPU per-frame texture creations reached");
            }
            ++frameTextures;
        }
        auto texture = allocate(source->descriptor.width, source->descriptor.height, source, &entry);
        texture->premultiplied = premultiplied; texture->renderTarget = asTarget;
        if (asTarget) targets[source.get()] = texture;
        // Large authored chains may fit as resident resources but not with a
        // second full-chain transfer allocation beside retained depth atlases.
        // In that case admit bounded asynchronous slices across frame retries.
        updateMemory(&entry);
        if(bytes>TotalBudget-std::min(TotalBudget,counts.resourceBytes)) {
            texture->progressiveUpload=true;admitTexture(key,texture);
            progressUpload(texture,entry,opaque,premultiplied && source->descriptor.alpha!=TextureDescriptor::Alpha::Premultiplied);
        }
        upload(commands, entry, texture, source->pixels.get(), opaque, premultiplied && source->descriptor.alpha != TextureDescriptor::Alpha::Premultiplied);
        admitTexture(key, texture); return texture;
        } catch(const RenderAdmissionDeferred &) {preservePreparedUploads(commands,entry);throw;}
    }
    TexturePtr captureSource(CaptureRequest &request, SDL_Rect &rect)
    {
        TexturePtr source = screen;
        if (request.target) {
            auto found = targets.find(request.target.get());
            if (found == targets.end() || !(source = found->second.lock())) throw std::invalid_argument("Capture target has no submitted allocation");
        }
        if(source->progressiveUpload)throw RenderAdmissionDeferred("Capture target restoration is incomplete");
        rect = request.pixelRegion.value_or(SDL_Rect{0,0,source->width,source->height});
        if (rect.x < 0 || rect.y < 0 || rect.w <= 0 || rect.h <= 0 || std::int64_t(rect.x) + rect.w > source->width || std::int64_t(rect.y) + rect.h > source->height ||
            (request.width && request.width != rect.w) || (request.height && request.height != rect.h) || (request.frameIdentity && request.frameIdentity != lastFrame))
            throw std::invalid_argument("Capture dimensions, region or frame do not match allocation");
        request.width = rect.w; request.height = rect.h; return source;
    }
    SDL_GPUGraphicsPipeline *pipeline(SDL_GPUTextureFormat format, bool premultiplied = false, DrawPacket::Material material = DrawPacket::Material::Primitive, DrawPacket::Blend mode = DrawPacket::Blend::Automatic, int direct = 0)
    {
        // direct: 1 native direct terrain, 2 streamed (HD) direct terrain.
        if (direct == 1 && directTerrainPipeline) return directTerrainPipeline;
        if (direct == 2 && directTerrainHdPipeline) return directTerrainHdPipeline;
        const PrimitivePipelineKey key{format, premultiplied, material, mode};
        if (auto found = pipelines.find(key); !direct && found != pipelines.end()) return found->second;
        SDL_GPUVertexBufferDescription buffer{}; buffer.slot = 0; buffer.pitch = Layout::primitive_stride;
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        std::array<SDL_GPUVertexAttribute, 3> attributes{};
        for (std::size_t i = 0; i < attributes.size(); ++i) {
            attributes[i].location = Layout::primitive_attributes[i].location;
            attributes[i].buffer_slot = 0; attributes[i].offset = Layout::primitive_attributes[i].offset;
            attributes[i].format = Layout::primitive_attributes[i].components == 4 ? SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4 : SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        }
        SDL_GPUColorTargetDescription color{}; color.format = format;
        auto &blend = color.blend_state;
        blend.enable_blend = mode != DrawPacket::Blend::Opaque; blend.src_color_blendfactor = (mode == DrawPacket::Blend::Premultiplied || (mode == DrawPacket::Blend::Automatic && premultiplied)) ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor = mode == DrawPacket::Blend::Additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = mode == DrawPacket::Blend::Additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertexShader; info.fragment_shader = fragmentShader;
        if (direct) {
            info.vertex_shader = directTerrainVertex;
            info.fragment_shader = direct == 2 ? directTerrainHdFragment : directTerrainFragment;
        } else if (material == DrawPacket::Material::SmoothPoint) {
            const auto stages = worldShaders.find(material);
            if (stages == worldShaders.end()) throw std::runtime_error("Particle shader binaries unavailable");
            info.vertex_shader = stages->second.first; info.fragment_shader = stages->second.second;
        } else if (material == DrawPacket::Material::TeamColor) {
            const auto stages = worldShaders.find(material);
            if (stages == worldShaders.end()) throw std::runtime_error("Team colour shader binaries unavailable");
            info.vertex_shader = stages->second.first; info.fragment_shader = stages->second.second;
        } else if (material != DrawPacket::Material::Primitive) throw std::invalid_argument("Unsupported primitive material");
        info.vertex_input_state.vertex_buffer_descriptions = &buffer; info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes.data(); info.vertex_input_state.num_vertex_attributes = attributes.size();
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL; info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.target_info.color_target_descriptions = &color; info.target_info.num_color_targets = 1;
        auto *result = SDL_CreateGPUGraphicsPipeline(device, &info); if (!result) fail("create primitive pipeline");
        if (direct == 1) directTerrainPipeline = result;
        else if (direct == 2) directTerrainHdPipeline = result;
        else pipelines.emplace(key, result);
        return result;
    }
    void uploadVertices(SDL_GPUCommandBuffer *commands, Work &entry, const std::vector<SDL_Vertex> &vertices)
    {
        if (vertices.empty()) return;
        const auto size = vertices.size() * sizeof(SDL_Vertex);
        reserveMemory(size * 2, &entry);
        auto *buffer = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size); entry.transfers.emplace_back(buffer, size);
        void *mapped = SDL_MapGPUTransferBuffer(device, buffer, false); if (!mapped) fail("map vertices");
        std::memcpy(mapped, vertices.data(), size); SDL_UnmapGPUTransferBuffer(device, buffer);
        entry.vertices = vertexBuffer(size, entry.vertexBytes);
        SDL_GPUTransferBufferLocation from{}; from.transfer_buffer = buffer;
        SDL_GPUBufferRegion to{}; to.buffer = entry.vertices; to.size = static_cast<Uint32>(size);
        auto *copy = SDL_BeginGPUCopyPass(commands); if (!copy) fail("begin geometry upload");
        SDL_UploadToGPUBuffer(copy, &from, &to, false); SDL_EndGPUCopyPass(copy); counts.uploadBytes += size;
    }
    static SDL_Rect scissor(const DrawPacket &packet, int tw, int th, float lw, float lh)
    {
        SDL_Rect result{0, 0, tw, th};
        auto intersect = [&](const SDL_Rect &logical) {
            // Clip rectangles are logical screen coordinates, independent of world transform.
            SDL_Rect physical{int(std::floor(double(logical.x) * tw / lw)), int(std::floor(double(logical.y) * th / lh)), 0, 0};
            physical.w = int(std::ceil(double(logical.x + logical.w) * tw / lw)) - physical.x;
            physical.h = int(std::ceil(double(logical.y + logical.h) * th / lh)) - physical.y;
            SDL_Rect intersection{}; SDL_GetRectIntersection(&result, &physical, &intersection); result = intersection;
        };
        if (packet.transform.bounds) intersect(*packet.transform.bounds);
        if (packet.clip) {
            const auto &clip = *packet.clip;
            const auto &transform = packet.transform;
            const int x = int(std::floor(clip.x * transform.scale + transform.x));
            const int y = int(std::floor(clip.y * transform.scale + transform.y));
            SDL_Rect mapped{x, y, int(std::ceil((clip.x + clip.w) * transform.scale + transform.x)) - x,
                            int(std::ceil((clip.y + clip.h) * transform.scale + transform.y)) - y};
            intersect(mapped);
        }
        return detail::intersectTargetScissor(result, packet.targetScissor);
    }
    PrimitiveState primitiveState(const Draw &draw, float lw, float lh)
    {
        const auto &packet = *draw.packet;
        if (packet.targetLogicalWidth > 0) lw = packet.targetLogicalWidth;
        if (packet.targetLogicalHeight > 0) lh = packet.targetLogicalHeight;
        PrimitiveState state;
        state.target = draw.target->gpu;
        state.clip = scissor(packet, draw.target->width, draw.target->height, lw, lh);
        state.viewport = packet.viewport.value_or(SDL_Rect{0, 0, draw.target->width, draw.target->height});
        const bool team = packet.material == DrawPacket::Material::TeamColor;
        state.pipeline = pipeline(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
            team ? teamPremultiplied(packet, draw.texture->premultiplied) : bool(packet.texture) && draw.texture->premultiplied,
            packet.material, packet.kind == DrawPacket::Kind::Clear ? DrawPacket::Blend::Opaque : packet.blend);
        if (packet.material == DrawPacket::Material::Primitive) {
            state.image = draw.texture->gpu; state.sampler = samplerFor(packet);
        }
        state.vertex[0] = lw; state.vertex[1] = lh;
        auto &fragment = state.fragment;
        if (packet.material == DrawPacket::Material::TeamColor) {
            // The shader fetches and filters texels itself, after recolouring.
            state.image = draw.texture->gpu; state.sampler = nearest;
            const bool filtering = packet.linear || packet.texture->descriptor.sampling == TextureDescriptor::Sampling::Linear;
            const bool mipmapped = filtering && packet.texture->descriptor.mips == TextureDescriptor::Mips::Generated;
            fragment[Layout::team_color_fragment_alpha_offset / sizeof(float)] = packet.alpha / 255.f;
            fragment[Layout::team_color_fragment_linear_offset / sizeof(float)] = filtering ? 1.f : 0.f;
            // 0: straight in and out; 1: straight in, premultiplied out; 2: premultiplied in and out.
            fragment[Layout::team_color_fragment_premultiplied_offset / sizeof(float)] =
                draw.texture->premultiplied ? 2.f : teamPremultiplied(packet, false) ? 1.f : 0.f;
            fragment[Layout::team_color_fragment_levels_offset / sizeof(float)] = mipmapped ? float(uploadLevelCount(*packet.texture)) : 1.f;
            const auto bounds = Layout::team_color_fragment_uvBounds_offset / sizeof(float);
            if (packet.kind != DrawPacket::Kind::Blit) {
                fragment[bounds] = fragment[bounds + 1] = 0; fragment[bounds + 2] = fragment[bounds + 3] = 1;
                return state;
            }
            fragment[bounds] = (packet.source.x + .5f) / draw.texture->width;
            fragment[bounds + 1] = (packet.source.y + .5f) / draw.texture->height;
            fragment[bounds + 2] = (packet.source.x + packet.source.w - .5f) / draw.texture->width;
            fragment[bounds + 3] = (packet.source.y + packet.source.h - .5f) / draw.texture->height;
            return state;
        }
        fragment[Layout::primitive_fragment_textured_offset / sizeof(float)] = packet.texture ? 1.f : 0.f;
        fragment[Layout::primitive_fragment_alpha_offset / sizeof(float)] = packet.kind == DrawPacket::Kind::Blit ? packet.alpha / 255.f : 1.f;
        const auto bounds = Layout::primitive_fragment_uvBounds_offset / sizeof(float);
        fragment[bounds + 2] = fragment[bounds + 3] = 1.f;
        if (packet.kind == DrawPacket::Kind::Blit) {
            fragment[bounds] = (packet.source.x + .5f) / draw.texture->width;
            fragment[bounds + 1] = (packet.source.y + .5f) / draw.texture->height;
            fragment[bounds + 2] = (packet.source.x + packet.source.w - .5f) / draw.texture->width;
            fragment[bounds + 3] = (packet.source.y + packet.source.h - .5f) / draw.texture->height;
        }
        return state;
    }
    void clear(SDL_GPUCommandBuffer *commands, TexturePtr texture, bool opaque = false)
    {
        SDL_GPUColorTargetInfo color{}; color.texture = texture->gpu; color.load_op = SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE; color.clear_color = {0, 0, 0, opaque ? 1.f : 0.f};
        auto *pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr); if (!pass) fail("clear target"); SDL_EndGPURenderPass(pass);
    }
    void submitWork(SDL_GPUCommandBuffer *&commands, Work &entry)
    {
        // Allocate the queue node while commands can still be cancelled.
        // Nothing may allocate between GPU submission and ownership publication.
        work.emplace_back();
        auto *submitted = commands; commands = nullptr;
        entry.fence = SDL_SubmitGPUCommandBufferAndAcquireFence(submitted);
        if (!entry.fence) {
            work.pop_back();SDL_WaitForGPUIdle(device);fail("submit GPU work");
        }
        entry.submission = {++serial, epoch}; workBytes += entry.chargedBytes(); work.back()=std::move(entry);
        entry=Work{}; // Clear copied raw handles before callers' failure cleanup.
        updateMemory();
    }
    GeometryPtr resolveGeometry(SDL_GPUCommandBuffer *, Work &, GeometrySnapshot);
    TexturePtr depthFor(TexturePtr, const Work * = nullptr);
    SDL_GPUSampler *samplerFor(const DrawPacket &);
    SDL_GPUGraphicsPipeline *worldPipeline(const DrawPacket &);
    void drawWorld(SDL_GPUCommandBuffer *, const Draw &, float, float);
    void drawPackets(SDL_GPUCommandBuffer *, Work &, const std::vector<Draw> &, float, float);
    void clearPacket(SDL_GPUCommandBuffer *, const Draw &);
    bool hasShader(const std::string &variant, const char *stage) const
    {
        return shaderArtifacts.contains(variant) && shaderArtifacts.at(variant).contains(stage);
    }
    SDL_GPUShader *loadShader(const std::string &variant, const char *stage, Uint32 uniforms, Uint32 samplers)
    {
        if (!hasShader(variant, stage)) throw std::runtime_error("Missing offline shader receipt: " + shaderFamily + "/" + variant + "/" + stage);
        const auto &receipt = shaderArtifacts.at(variant).at(stage);
        const std::string format = shaderFormat == SDL_GPU_SHADERFORMAT_SPIRV ? "spirv" : shaderFormat == SDL_GPU_SHADERFORMAT_DXIL ? "dxil" : "metallib";
        const auto path = std::filesystem::path(receipt.at("path").get<std::string>());
        const auto entrypoint = receipt.at("entrypoint").get<std::string>();
        if (receipt.at("format") != format || receipt.at("stage") != stage || path.empty() || path.is_absolute() || entrypoint.empty() || entrypoint.size() > 256)
            throw std::runtime_error("Invalid offline shader receipt");
        for (const auto &component : path) if (component == "..") throw std::runtime_error("Offline shader path escapes asset directory");
        auto code = readShader((std::filesystem::path(shaderRoot) / path).string());
        if (shaderFormat == SDL_GPU_SHADERFORMAT_SPIRV && code.size() % 4) throw std::runtime_error("Misaligned SPIR-V code");
        SDL_GPUShaderCreateInfo info{}; info.format = shaderFormat; info.entrypoint = entrypoint.c_str();
        info.stage = std::strcmp(stage, "vertex") == 0 ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        info.code = code.data(); info.code_size = code.size(); info.num_uniform_buffers = uniforms; info.num_samplers = samplers;
        auto *result = SDL_CreateGPUShader(device, &info); if (!result) fail("create offline GPU shader");
        return result;
    }
    void loadWorldShaders(const std::string &);
    void loadSkinShaders(const std::string &);
    void loadTerrainShaders();
    void loadDirectTerrainShaders();
    void loadTeamColorShaders();
    void drawDirectTerrain(SDL_GPUCommandBuffer *, Work &, const Draw &, float logicalWidth, float logicalHeight);
    TexturePtr resolveStreamed(SDL_GPUCommandBuffer *, Work &, const std::shared_ptr<StreamedTexture> &);
    void settleStreamed(bool submitted);
    CoveragePtr resolveCoverage(SDL_GPUCommandBuffer *, Work &, std::shared_ptr<const CoverageData>);
    void prepareTerrain(SDL_GPUCommandBuffer *&, Work &, Draw &, std::vector<SDL_Vertex> &);
    void drawTerrain(SDL_GPUCommandBuffer *, Work &, const Draw &, PrimitivePass &);
    void teardown()
    {
        if (!device) return;
        SDL_WaitForGPUIdle(device);
        texturePool.open = false;
        for (auto &entry : work) release(entry);
        work.clear();
        for (auto &capture : captures) {
            SDL_ReleaseGPUFence(device, capture.fence); dropTransfer(capture.transfer);
        }
        captures.clear(); captureBytes = 0; workBytes = 0; clearCaches(); screen.reset(); committedScreen.reset(); white.reset();
        if (directTerrainPipeline) SDL_ReleaseGPUGraphicsPipeline(device, directTerrainPipeline);
        if (directTerrainVertex) SDL_ReleaseGPUShader(device, directTerrainVertex);
        if (directTerrainFragment) SDL_ReleaseGPUShader(device, directTerrainFragment);
        if (directTerrainHdPipeline) SDL_ReleaseGPUGraphicsPipeline(device, directTerrainHdPipeline);
        if (directTerrainHdFragment) SDL_ReleaseGPUShader(device, directTerrainHdFragment);
        streamed.clear();
        releaseUploadPool();
        for (auto *pipeline : terrainPipelines) if (pipeline) SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
        terrainPipelines.fill(nullptr);
        for (auto *shader : terrainShaders) if (shader) SDL_ReleaseGPUShader(device,shader);
        terrainShaders.fill(nullptr);
        for (const auto &[key, object] : worldPipelines) SDL_ReleaseGPUGraphicsPipeline(device, object);
        worldPipelines.clear();
        for (const auto &[key, stages] : worldShaders) { if (stages.first) SDL_ReleaseGPUShader(device, stages.first); if (stages.second) SDL_ReleaseGPUShader(device, stages.second); }
        worldShaders.clear();
        for (const auto &[key, sampler] : samplers) SDL_ReleaseGPUSampler(device, sampler);
        samplers.clear();
        for (auto &[format, object] : pipelines) SDL_ReleaseGPUGraphicsPipeline(device, object);
        pipelines.clear();
        if (nearest) SDL_ReleaseGPUSampler(device, nearest);
        if (linear) SDL_ReleaseGPUSampler(device, linear);
        if (vertexShader) SDL_ReleaseGPUShader(device, vertexShader);
        if (fragmentShader) SDL_ReleaseGPUShader(device, fragmentShader);
        if (claimed) SDL_ReleaseWindowFromGPUDevice(device, window);
        {
            std::lock_guard instanceLock(vulkanInstanceMutex()); // see VulkanInstanceLock.h
            SDL_DestroyGPUDevice(device);
        }
        device = nullptr;
    }
  public:
    SDLGPUDevice(SDL_Window *window, int w, int h, const std::string &shaders, const char *driver)
        : window(window), width(w), height(h)
    {
        if (!driver) driver=platformDriver();
        if (!std::strcmp(driver, "direct3d12")) { shaderFamily = "direct3d12"; shaderFormat = SDL_GPU_SHADERFORMAT_DXIL; }
        else if (!std::strcmp(driver, "metal")) { shaderFamily = "metal"; shaderFormat = SDL_GPU_SHADERFORMAT_METALLIB; }
        else if (std::strcmp(driver, "vulkan")) throw std::invalid_argument("Unsupported SDL GPU driver family");
        shaderRoot = shaders;
        const auto unavailable=sdlGPUUnavailableReason(shaders,driver);
        if(!unavailable.empty())throw std::runtime_error(unavailable);
        try {
            std::ifstream manifest(shaders + "/metadata.json", std::ios::binary | std::ios::ate);
            if (!manifest || manifest.tellg() <= 0 || manifest.tellg() > 4 * 1024 * 1024)
                throw std::runtime_error("Missing or invalid offline shader metadata");
            manifest.seekg(0);
            const auto metadata = nlohmann::json::parse(manifest);
            if (!metadata.contains("shaderArtifacts") || !metadata.at("shaderArtifacts").contains(shaderFamily))
                throw std::runtime_error("Offline shader assets unavailable for " + shaderFamily);
            shaderArtifacts = metadata.at("shaderArtifacts").at(shaderFamily);
            if (!hasShader("primitive", "vertex") || !hasShader("primitive", "fragment"))
                throw std::runtime_error("Incomplete primitive shader assets for " + shaderFamily);
            {
                // Never alongside another Vulkan instance being created (the
                // device profile's VRAM probe thread): see VulkanInstanceLock.h.
                std::lock_guard instanceLock(vulkanInstanceMutex());
                device = SDL_CreateGPUDevice(shaderFormat, false, driver);
            }
            if (!device) fail("create SDL GPU device");
            texturePool.limit = std::min<std::size_t>(32 * 1024 * 1024, TotalBudget / 8); texturePool.open = true;
            texturePool.fits = [this](std::size_t bytes) { return idleFits(bytes); };
            if (window) { claimed = SDL_ClaimWindowForGPUDevice(device, window); if (!claimed) fail("claim GPU window"); }
            // Block formats for prepared artwork. Desktop GPUs decode BC3 and most
            // mobile GPUs ASTC; GLOB2_TEXTURE_COMPRESSION=none|bc3|astc restricts it.
            const char *wanted = SDL_getenv_unsafe("GLOB2_TEXTURE_COMPRESSION");
            const std::string choice = wanted ? wanted : "";
            const auto sampled = [&](SDL_GPUTextureFormat format) {
                return SDL_GPUTextureSupportsFormat(device, format, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER);
            };
            bc3Supported = (choice.empty() || choice == "bc3") && sampled(SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM);
            astcSupported = (choice.empty() || choice == "astc") && sampled(SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM);
            vertexShader = loadShader("primitive", "vertex", 1, 0);
            fragmentShader = loadShader("primitive", "fragment", 1, 1);
            SDL_GPUSamplerCreateInfo sampler{}; sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
            sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            nearest = SDL_CreateGPUSampler(device, &sampler); if (!nearest) fail("create nearest sampler");
            sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
            linear = SDL_CreateGPUSampler(device, &sampler); if (!linear) fail("create linear sampler");
            screen = allocate(w, h); white = allocate(1, 1);
            pipeline(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            loadWorldShaders(shaders);
            loadSkinShaders(shaders);
            loadTerrainShaders();
            loadDirectTerrainShaders();
            loadTeamColorShaders();
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixel(SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
            if (!pixel) fail("create white pixel");
            SDL_FillSurfaceRect(pixel.get(), nullptr, SDL_MapSurfaceRGBA(pixel.get(), 255, 255, 255, 255));
            Work entry; auto *commands = SDL_AcquireGPUCommandBuffer(device); if (!commands) fail("acquire initial commands");
            try {
                upload(commands, entry, white, pixel.get(), true); clear(commands, screen, true); submitWork(commands, entry);
            } catch (...) {
                if (commands) SDL_CancelGPUCommandBuffer(commands);
                release(entry); throw;
            }
        } catch (...) { teardown(); throw; }
    }
    ~SDLGPUDevice() override { teardown(); }
    RenderDeviceCapabilities capabilities() const override
    {
        RenderDeviceCapabilities result{true, true, false, MaximumTextureSize, worldAvailable, worldAvailable, skinAvailable, terrainAvailable, directTerrainPipeline != nullptr, directTerrainHdPipeline != nullptr};
        result.bc3Textures = bc3Supported; result.astc4x4Textures = astcSupported;
        result.teamColor = teamColorAvailable;
        return result;
    }
    RenderSubmission submit(RenderFrame frame) override;
    RenderCompletion completion(RenderSubmission submission) override
    {
        if (!submission || submission.deviceGeneration != epoch || submission.identity > serial) return RenderCompletion::Cancelled;
        reap(); return submission.identity <= completed ? RenderCompletion::Complete : RenderCompletion::Pending;
    }
    void present() override;
    void setPresentationHeld(bool held) override { presentationHeld = held; }
    bool presentationHeld = false;
    void discardFrame() override;
    void reset() override
    {
        SDL_WaitForGPUIdle(device); for (auto &entry : work) release(entry);
        work.clear(); workBytes = 0; clearCaches();
        streamed.clear(); // Owners that had writes applied see their textures lost.
        texturePool.release(device);
        cancelCaptures(); frameOpen = false; ++epoch; completed = serial; lastFrame = 0; reap();
    }
    std::uint64_t generation() const override { return epoch; }
    RenderTelemetry telemetry() const override { return counts; }
    void trimRetiredResources() override {
        releaseUploadPool();
        reap();
        for(auto it=cache.begin();it!=cache.end();) {
            if(it->second.use_count()==1 && !it->second->renderTarget && !it->second->progressiveUpload) it=cache.erase(it); else ++it;
        }
        for(auto it=geometryCache.begin();it!=geometryCache.end();) {
            if(it->second.use_count()==1) it=geometryCache.erase(it); else ++it;
        }
        for(auto it=coverageCache.begin();it!=coverageCache.end();) {
            if(it->second.use_count()==1) it=coverageCache.erase(it); else ++it;
        }
        for(auto it=terrainScratch.begin();it!=terrainScratch.end();) {
            if(it->second.use_count()==1) it=terrainScratch.erase(it); else ++it;
        }
        updateMemory();
    }
    bool capture(CaptureRequest request) override;
    bool pollCapture(CaptureResult &result) override;
    SDL_Surface *captureSynchronously(CaptureRequest request) override;
    void cancelCaptures() override
    {
        for (auto &capture : captures) if (!capture.cancelled) { capture.cancelled = true; ++counts.captureDrops; }
    }
    void outputSize(int &w, int &h) override { w = width; h = height; }
};
void SDLGPUDevice::loadWorldShaders(const std::string &directory)
{
    const std::array<const char *, 3> names{"world", "cloud", "point"};
    (void)directory;
    for (auto name : names) for (auto stage : {"vertex", "fragment"}) if (!hasShader(name, stage)) return;
    const std::array materials{DrawPacket::Material::WorldLit, DrawPacket::Material::Cloud, DrawPacket::Material::SmoothPoint};
    for (std::size_t i = 0; i < names.size(); ++i) {
        auto &stages = worldShaders[materials[i]];
        stages.first = loadShader(names[i], "vertex", 1, 0);
        stages.second = loadShader(names[i], "fragment", i != 1 ? 1 : 0, i == 2 ? 0 : 1);
    }
    for (auto material : {DrawPacket::Material::WorldLit, DrawPacket::Material::Cloud}) {
        DrawPacket packet; packet.material = material; packet.depth = DrawPacket::Depth::LessEqual;
        packet.depthWrite = material == DrawPacket::Material::WorldLit;
        packet.blend = packet.depthWrite ? DrawPacket::Blend::Opaque : DrawPacket::Blend::SourceOver;
        worldPipeline(packet);
    }
    pipeline(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false, DrawPacket::Material::SmoothPoint, DrawPacket::Blend::Additive);
    worldAvailable = true;
}
void SDLGPUDevice::loadTeamColorShaders()
{
    // Shaders and both blend pipelines are made with the device, never during
    // a frame. Without them, team layers keep their CPU recolour.
    if (!hasShader("team-color", "vertex") || !hasShader("team-color", "fragment")) return;
    try {
        auto &stages = worldShaders[DrawPacket::Material::TeamColor];
        stages.first = loadShader("team-color", "vertex", 1, 0);
        stages.second = loadShader("team-color", "fragment", 1, 1);
        for (bool premultiplied : {false, true})
            pipeline(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, premultiplied, DrawPacket::Material::TeamColor);
        teamColorAvailable = true;
    } catch (const std::runtime_error &error) {
        // Optional: a driver that rejects it keeps the device and the CPU path.
        for (auto it = pipelines.begin(); it != pipelines.end();)
            if (std::get<2>(it->first) == DrawPacket::Material::TeamColor) { SDL_ReleaseGPUGraphicsPipeline(device, it->second); it = pipelines.erase(it); }
            else ++it;
        if (auto found = worldShaders.find(DrawPacket::Material::TeamColor); found != worldShaders.end()) {
            if (found->second.first) SDL_ReleaseGPUShader(device, found->second.first);
            if (found->second.second) SDL_ReleaseGPUShader(device, found->second.second);
            worldShaders.erase(found);
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "SDL GPU team colour unavailable: %s", error.what());
    }
}
void SDLGPUDevice::loadSkinShaders(const std::string &directory)
{
    (void)directory;
    for (auto name : {"skin", "skin-rig"}) for (auto stage : {"vertex", "fragment"}) if (!hasShader(name, stage)) return;
    for (auto material : {DrawPacket::Material::Skin, DrawPacket::Material::SkinRig}) {
        const std::string name = material == DrawPacket::Material::Skin ? "skin" : "skin-rig";
        auto &stages = worldShaders[material];
        stages.first = loadShader(name, "vertex", 1, 0);
        stages.second = loadShader(name, "fragment", 1, 2);
        DrawPacket packet; packet.material = material; packet.depth = DrawPacket::Depth::LessEqual; packet.depthWrite = true; packet.blend = DrawPacket::Blend::Opaque;
        worldPipeline(packet);
    }
    skinAvailable = true;
}
void SDLGPUDevice::loadTerrainShaders()
{
    if (!hasShader("terrain-compose","fragment") || !hasShader("terrain-reduce","fragment")) return;
    if (!SDL_GPUTextureSupportsFormat(device,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_SAMPLER) ||
        !SDL_GPUTextureSupportsFormat(device,SDL_GPU_TEXTUREFORMAT_R8_UINT,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_SAMPLER)) return;
    const std::array<const char *,2> names{"terrain-compose","terrain-reduce"};
    for (std::size_t i=0;i<names.size();++i) {
        terrainShaders[i]=loadShader(names[i],"fragment",1,i==0 ? 6 : 1);
        SDL_GPUVertexBufferDescription buffer{}; buffer.slot=0; buffer.pitch=Layout::primitive_stride;
        buffer.input_rate=SDL_GPU_VERTEXINPUTRATE_VERTEX;
        std::array<SDL_GPUVertexAttribute,3> attributes{};
        for (std::size_t j=0;j<attributes.size();++j) {
            attributes[j].location=Layout::primitive_attributes[j].location; attributes[j].buffer_slot=0;
            attributes[j].offset=Layout::primitive_attributes[j].offset;
            attributes[j].format=Layout::primitive_attributes[j].components==4 ? SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4 : SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        }
        SDL_GPUColorTargetDescription color{}; color.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUGraphicsPipelineCreateInfo info{}; info.vertex_shader=vertexShader; info.fragment_shader=terrainShaders[i];
        info.vertex_input_state.vertex_buffer_descriptions=&buffer; info.vertex_input_state.num_vertex_buffers=1;
        info.vertex_input_state.vertex_attributes=attributes.data(); info.vertex_input_state.num_vertex_attributes=attributes.size();
        info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE; info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
        info.target_info.color_target_descriptions=&color; info.target_info.num_color_targets=1;
        terrainPipelines[i]=SDL_CreateGPUGraphicsPipeline(device,&info); if(!terrainPipelines[i]) fail("create terrain pipeline");
    }
    auto zero=std::make_shared<CoverageData>();zero->identity=UINT64_MAX;zero->revision=1;zero->width=zero->height=1;zero->samples.resize(1);
    zeroCoverage=std::move(zero);
    terrainAvailable=true;
}
void SDLGPUDevice::loadDirectTerrainShaders()
{
    if (!hasShader("terrain-direct", "vertex") || !hasShader("terrain-direct", "fragment")) return;
    try {
        directTerrainVertex = loadShader("terrain-direct", "vertex", 1, 0);
        directTerrainFragment = loadShader("terrain-direct", "fragment", 1, DirectTerrainDraw::Count);
        pipeline(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false, DrawPacket::Material::Primitive,
                 DrawPacket::Blend::Premultiplied, 1);
    } catch (const std::exception &error) {
        if (directTerrainVertex) SDL_ReleaseGPUShader(device, directTerrainVertex);
        if (directTerrainFragment) SDL_ReleaseGPUShader(device, directTerrainFragment);
        directTerrainVertex = directTerrainFragment = nullptr;
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Direct terrain unavailable: %s", error.what());
        return;
    }
    // Streamed HD direct terrain shares the vertex stage.
    if (!hasShader("terrain-direct-hd", "fragment")) return;
    try {
        directTerrainHdFragment = loadShader("terrain-direct-hd", "fragment", 1, DirectTerrainDraw::StreamedCount);
        pipeline(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false, DrawPacket::Material::Primitive,
                 DrawPacket::Blend::Premultiplied, 2);
    } catch (const std::exception &error) {
        if (directTerrainHdFragment) SDL_ReleaseGPUShader(device, directTerrainHdFragment);
        directTerrainHdFragment = nullptr;
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Streamed direct terrain unavailable: %s", error.what());
    }
}
TexturePtr SDLGPUDevice::resolveStreamed(SDL_GPUCommandBuffer *commands, Work &entry, const std::shared_ptr<StreamedTexture> &source)
{
    if (source->lost) return nullptr;
    auto found = streamed.find(source.get());
    if (found != streamed.end() && found->second.owner.lock() != source) { streamed.erase(found); found = streamed.end(); }
    try {
        if (found == streamed.end()) {
            // Writes already applied went to a texture this device no longer holds.
            if (source->applied) { source->lost = true; return nullptr; }
            if (source->width <= 0 || source->height <= 0 || source->width > MaximumTextureSize || source->height > MaximumTextureSize)
                throw std::invalid_argument("Invalid streamed texture dimensions");
            StreamedEntry created; created.owner = source;
            created.texture = allocate(source->width, source->height, {}, &entry);
            clear(commands, created.texture);
            found = streamed.emplace(source.get(), std::move(created)).first;
        }
        auto &state = found->second;
        // Every queued write is applied before this frame draws: the owner
        // bounds what it queues per frame, and a prefix could pair a new index
        // with a tile not yet written.
        if (state.recorded < source->writes.size()) {
            std::size_t bytes = 0;
            for (std::size_t i = state.recorded; i < source->writes.size(); ++i) {
                const auto &write = source->writes[i];
                const auto &rect = write.rect;
                if (rect.x < 0 || rect.y < 0 || rect.w <= 0 || rect.h <= 0 || rect.x + rect.w > source->width ||
                    rect.y + rect.h > source->height || write.rgba.size() != std::size_t(rect.w) * rect.h * 4)
                    throw std::invalid_argument("Streamed texture write outside its texture");
                bytes += write.rgba.size();
            }
            reserveMemory(bytes, &entry);
            auto *buffer = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes); entry.transfers.emplace_back(buffer, bytes);
            auto *mapped = static_cast<unsigned char *>(SDL_MapGPUTransferBuffer(device, buffer, false));
            if (!mapped) fail("map streamed texture writes");
            std::size_t offset = 0;
            for (std::size_t i = state.recorded; i < source->writes.size(); ++i) {
                std::memcpy(mapped + offset, source->writes[i].rgba.data(), source->writes[i].rgba.size());
                offset += source->writes[i].rgba.size();
            }
            SDL_UnmapGPUTransferBuffer(device, buffer);
            auto *copy = SDL_BeginGPUCopyPass(commands); if (!copy) fail("begin streamed texture pass");
            offset = 0;
            for (std::size_t i = state.recorded; i < source->writes.size(); ++i) {
                const auto &write = source->writes[i];
                SDL_GPUTextureTransferInfo from{}; from.transfer_buffer = buffer; from.offset = Uint32(offset);
                from.pixels_per_row = Uint32(write.rect.w); from.rows_per_layer = Uint32(write.rect.h);
                SDL_GPUTextureRegion region{}; region.texture = state.texture->gpu;
                region.x = Uint32(write.rect.x); region.y = Uint32(write.rect.y);
                region.w = Uint32(write.rect.w); region.h = Uint32(write.rect.h); region.d = 1;
                SDL_UploadToGPUTexture(copy, &from, &region, false);
                offset += write.rgba.size();
            }
            SDL_EndGPUCopyPass(copy);
            counts.uploadBytes += bytes;
            state.recordedBytes += bytes;
            state.recorded = source->writes.size();
        }
        entry.textures.push_back(state.texture);
        return state.texture;
    } catch (const RenderAdmissionDeferred &) {
        // Staging for this frame's writes did not fit: usually in-flight work
        // that retires within a frame or two. A texture the owner already
        // draws defers the frame (its writes stay queued) a bounded number of
        // times before the owner is told to start its textures again.
        constexpr unsigned StreamedDeferrals = 30;
        if (found != streamed.end() && found->second.committed && ++streamedDeferrals <= StreamedDeferrals) throw;
        if (found != streamed.end() && !found->second.committed) streamed.erase(found);
        streamedDeferrals = 0;
        source->lost = true;
        return nullptr;
    }
}
void SDLGPUDevice::settleStreamed(bool submitted)
{
    for (auto it = streamed.begin(); it != streamed.end();) {
        auto &state = it->second;
        auto owner = state.owner.lock();
        if (!owner) { it = streamed.erase(it); continue; }
        if (submitted) {
            // The recorded writes are on the GPU: drop them from the queue.
            for (std::size_t i = 0; i < state.recorded; ++i) owner->writes.pop_front();
            owner->applied += state.recorded;
            owner->queuedBytes -= std::min(owner->queuedBytes, state.recordedBytes);
            state.committed = true;
        } else if (!state.committed) { it = streamed.erase(it); continue; }
        state.recorded = state.recordedBytes = 0;
        ++it;
    }
}
void SDLGPUDevice::drawDirectTerrain(SDL_GPUCommandBuffer *commands, Work &entry, const Draw &draw,
                                    float logicalWidth, float logicalHeight)
{
    const auto &packet = *draw.packet;
    if (packet.targetLogicalWidth > 0) logicalWidth = packet.targetLogicalWidth;
    if (packet.targetLogicalHeight > 0) logicalHeight = packet.targetLogicalHeight;
    const auto clip = scissor(packet, draw.target->width, draw.target->height, logicalWidth, logicalHeight);
    if (clip.w <= 0 || clip.h <= 0) return;

    SDL_GPUColorTargetInfo color{};
    color.texture = draw.target->gpu;
    color.load_op = SDL_GPU_LOADOP_LOAD;
    color.store_op = SDL_GPU_STOREOP_STORE;
    auto *pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
    if (!pass) fail("begin direct terrain pass");
    const bool streamedDraw = packet.directTerrain->isStreamed();
    SDL_BindGPUGraphicsPipeline(pass, streamedDraw ? directTerrainHdPipeline : directTerrainPipeline);
    const SDL_GPUBufferBinding vertices{entry.vertices, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
    const auto rect = packet.viewport.value_or(SDL_Rect{0, 0, draw.target->width, draw.target->height});
    const SDL_GPUViewport viewport{float(rect.x), float(rect.y), float(rect.w), float(rect.h), 0, 1};
    SDL_SetGPUViewport(pass, &viewport);
    SDL_SetGPUScissor(pass, &clip);
    namespace Shader = RendererShaderLayouts;
    const std::array<float, Shader::terrain_direct_size / sizeof(float)> vertex{logicalWidth, logicalHeight};
    if (streamedDraw) {
        std::array<SDL_GPUTextureSamplerBinding, DirectTerrainDraw::StreamedCount> images{};
        for (unsigned i = 0; i < images.size(); ++i) {
            const auto &source = packet.directTerrain->streamed[i];
            images[i] = {draw.streamedTextures[i]->gpu, source && source->linear ? linear : nearest};
        }
        SDL_BindGPUFragmentSamplers(pass, 0, images.data(), images.size());
        const auto &hd = packet.directTerrain->hd;
        std::array<float, Shader::terrain_direct_hd_fragment_size / sizeof(float)> fragment{};
        const auto pair = [&](std::size_t offset, const float (&value)[2]) {
            fragment[offset / sizeof(float)] = value[0]; fragment[offset / sizeof(float) + 1] = value[1];
        };
        pair(Shader::terrain_direct_hd_fragment_mapSize_offset, hd.mapSize);
        pair(Shader::terrain_direct_hd_fragment_artSize_offset, hd.artSize);
        pair(Shader::terrain_direct_hd_fragment_coverageSize_offset, hd.lowArtSize);
        pair(Shader::terrain_direct_hd_fragment_variantSize_offset, hd.variantSize);
        pair(Shader::terrain_direct_hd_fragment_hdCoverageSize_offset, hd.hdCoverageSize);
        pair(Shader::terrain_direct_hd_fragment_hdPreviousSize_offset, hd.hdPreviousSize);
        pair(Shader::terrain_direct_hd_fragment_hdBaseSize_offset, hd.hdBaseSize);
        fragment[Shader::terrain_direct_hd_fragment_animationTime_offset / sizeof(float)] = packet.directTerrain->animationTime;
        fragment[Shader::terrain_direct_hd_fragment_wholeMapValue_offset / sizeof(float)] = packet.directTerrain->wholeMap;
        fragment[Shader::terrain_direct_hd_fragment_artTilePixels_offset / sizeof(float)] = hd.artTilePixels;
        fragment[Shader::terrain_direct_hd_fragment_hdTilePixels_offset / sizeof(float)] = hd.hdTilePixels;
        fragment[Shader::terrain_direct_hd_fragment_hdPreviousTile_offset / sizeof(float)] = hd.hdPreviousTile;
        fragment[Shader::terrain_direct_hd_fragment_hdBaseTile_offset / sizeof(float)] = hd.hdBaseTile;
        SDL_PushGPUVertexUniformData(commands, Shader::terrain_direct_binding, vertex.data(), sizeof(vertex));
        SDL_PushGPUFragmentUniformData(commands, Shader::terrain_direct_hd_fragment_binding, fragment.data(), sizeof(fragment));
        SDL_DrawGPUPrimitives(pass, draw.count, 1, draw.first, 0);
        SDL_EndGPURenderPass(pass);
        ++counts.batches;
        return;
    }
    std::array<SDL_GPUTextureSamplerBinding, DirectTerrainDraw::Count> images{};
    for (unsigned i = 0; i < images.size(); ++i) {
        const auto &source = packet.directTerrain->textures[i];
        images[i] = {draw.directTextures[i]->gpu,
                     source->descriptor.sampling == TextureDescriptor::Sampling::Linear ? linear : nearest};
    }
    SDL_BindGPUFragmentSamplers(pass, 0, images.data(), images.size());

    std::array<float, Shader::terrain_direct_fragment_size / sizeof(float)> fragment{};
    const auto size = [&](std::size_t offset, DirectTerrainDraw::Texture slot, int widthDivisor = 1) {
        fragment[offset / sizeof(float)] = draw.directTextures[slot]->width / widthDivisor;
        fragment[offset / sizeof(float) + 1] = draw.directTextures[slot]->height;
    };
    size(Shader::terrain_direct_fragment_mapSize_offset, DirectTerrainDraw::Cells, 2);
    size(Shader::terrain_direct_fragment_artSize_offset, DirectTerrainDraw::Art);
    size(Shader::terrain_direct_fragment_coverageSize_offset, DirectTerrainDraw::Coverage);
    size(Shader::terrain_direct_fragment_variantSize_offset, DirectTerrainDraw::Variants, 2);
    fragment[Shader::terrain_direct_fragment_animationTime_offset / sizeof(float)] = packet.directTerrain->animationTime;
    fragment[Shader::terrain_direct_fragment_wholeMapValue_offset / sizeof(float)] = packet.directTerrain->wholeMap;
    SDL_PushGPUVertexUniformData(commands, Shader::terrain_direct_binding, vertex.data(), sizeof(vertex));
    SDL_PushGPUFragmentUniformData(commands, Shader::terrain_direct_fragment_binding, fragment.data(), sizeof(fragment));
    SDL_DrawGPUPrimitives(pass, draw.count, 1, draw.first, 0);
    SDL_EndGPURenderPass(pass);
    ++counts.batches;
}
CoveragePtr SDLGPUDevice::resolveCoverage(SDL_GPUCommandBuffer *commands, Work &entry, std::shared_ptr<const CoverageData> source)
{
    if (!source || !source->identity || !source->revision || !source->width || !source->height || source->width>MaximumTextureSize || source->height>MaximumTextureSize ||
        source->samples.size()!=std::size_t(source->width)*source->height) throw std::invalid_argument("Invalid immutable terrain coverage");
    const CoverageKey key{source->identity,source->revision,source->width,source->height};
    if(auto found=coverageCache.find(key);found!=coverageCache.end()) { entry.coverage.push_back(found->second); return found->second; }
    const std::size_t bytes=source->samples.size()*9, limit=budgetLimits.classLimit(TextureDescriptor::BudgetClass::TerrainMask);
    if(bytes>limit) throw std::length_error("Terrain coverage exceeds mask budget");
    std::size_t used=residentCoverageBytes+classBytes[std::size_t(TextureDescriptor::BudgetClass::TerrainMask)];
    for(auto it=coverageCache.begin();used>limit-bytes && it!=coverageCache.end();) {
        if(it->second.use_count()==1) {used-=it->second->bytes;it=coverageCache.erase(it);} else++it;
    }
    if(used>limit-bytes)throw RenderAdmissionDeferred("Terrain mask budget exhausted");
    reserveMemory(bytes*2,&entry);
    auto value=std::make_shared<CoverageGPU>();value->device=device;value->source=std::move(source);value->bytes=bytes;
    value->residentBytes=&residentCoverageBytes;residentCoverageBytes+=bytes;
    SDL_GPUTextureCreateInfo info{};info.type=SDL_GPU_TEXTURETYPE_2D;info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width=value->source->width;info.height=value->source->height;info.layer_count_or_depth=1;info.num_levels=1;
    info.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT;value->weights=SDL_CreateGPUTexture(device,&info);if(!value->weights)fail("create terrain weights");
    info.format=SDL_GPU_TEXTUREFORMAT_R8_UINT;value->selectors=SDL_CreateGPUTexture(device,&info);if(!value->selectors)fail("create terrain selectors");
    auto *buffer=transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,bytes);entry.transfers.emplace_back(buffer,bytes);
    auto *mapped=static_cast<unsigned char *>(SDL_MapGPUTransferBuffer(device,buffer,false));if(!mapped)fail("map terrain mask");
    auto *weights=reinterpret_cast<std::uint16_t *>(mapped);auto *slots=mapped+value->source->samples.size()*8;
    for(std::size_t i=0;i<value->source->samples.size();++i) {
        const auto &sample=value->source->samples[i];
        std::copy(sample.others.begin(),sample.others.end(),weights+i*4);weights[i*4+3]=sample.margin;slots[i]=sample.slots;
    }
    SDL_UnmapGPUTransferBuffer(device,buffer);
    auto *copy=SDL_BeginGPUCopyPass(commands);if(!copy)fail("begin terrain mask upload");
    SDL_GPUTextureTransferInfo from{};from.transfer_buffer=buffer;from.pixels_per_row=info.width;from.rows_per_layer=info.height;
    SDL_GPUTextureRegion destination{};destination.texture=value->weights;destination.w=info.width;destination.h=info.height;destination.d=1;
    SDL_UploadToGPUTexture(copy,&from,&destination,false);
    from.offset=static_cast<Uint32>(value->source->samples.size()*8);destination.texture=value->selectors;
    SDL_UploadToGPUTexture(copy,&from,&destination,false);SDL_EndGPUCopyPass(copy);
    counts.uploadBytes+=bytes;coverageCache.emplace(key,value);freshCoverage.push_back(key);entry.coverage.push_back(value);return value;
}
void SDLGPUDevice::prepareTerrain(SDL_GPUCommandBuffer *&commands, Work &entry, Draw &draw, std::vector<SDL_Vertex> &vertices)
{
    const auto &packet=*draw.packet;
    if(!terrainAvailable || !packet.target || !packet.weightedCompose)throw std::invalid_argument("Terrain composition requires shaders, target and data");
    const auto &data=*packet.weightedCompose;const auto &rect=data.targetRect;
    const auto size=data.compositionSize,divisor=data.reductionDivisor;
    if(!size || size>MaximumTextureSize || !divisor || divisor>32 || (divisor&(divisor-1)) || size%divisor ||
        !data.paletteCount || data.paletteCount>4 || rect.x<0 || rect.y<0 || rect.w!=int(size/divisor) || rect.h!=rect.w ||
        std::int64_t(rect.x)+rect.w>draw.target->width || std::int64_t(rect.y)+rect.h>draw.target->height ||
        packet.texture || packet.secondTexture || packet.clip || packet.targetScissor || packet.viewport || packet.transform.bounds || packet.depthWrite || packet.depth!=DrawPacket::Depth::Disabled ||
        packet.target->descriptor.alpha==TextureDescriptor::Alpha::Opaque)
        throw std::invalid_argument("Invalid terrain composition dimensions or pass state");
    if(!data.coverage && data.paletteCount!=1)throw std::invalid_argument("Mixed terrain requires coverage");
    if(data.coverage) {
        if(data.coverage->width!=size || data.coverage->height!=size)throw std::invalid_argument("Terrain coverage dimensions disagree");
        for(const auto &sample:data.coverage->samples) {
            const unsigned sum=unsigned(sample.others[0])+sample.others[1]+sample.others[2];
            if(sum>65536 || (sample.slots&3)>=data.paletteCount || ((sample.slots>>2)&3)>=data.paletteCount || ((sample.slots>>4)&3)>=data.paletteCount)
                throw std::invalid_argument("Invalid terrain coverage weights or selectors");
        }
        draw.coverage=resolveCoverage(commands,entry,data.coverage);
    } else draw.coverage=resolveCoverage(commands,entry,zeroCoverage);
    for(unsigned i=0;i<data.paletteCount;++i) {
        const auto &seam=data.seams[i];
        if(seam.cast<0 || seam.cast>256 || seam.fringe<0 || seam.fringe>256 || seam.castWidth<0 || seam.fringeWidth<0 ||
            seam.castWidth>65535 || seam.fringeWidth>65535)
            throw std::invalid_argument("Invalid terrain seam constants");
        const auto &surf=data.surfs[i];
        if(surf.period<0 || surf.period>65535 || surf.ticks<1 || surf.ticks>std::max(surf.period,1) || surf.reach<0 || surf.foamWidth<0 || surf.reach+surf.foamWidth>2048 ||
            surf.shallowWidth<0 || surf.shallowWidth>2048 || surf.wetWidth<0 || surf.wetWidth>2048 ||
            surf.shallow<0 || surf.shallow>256 || surf.wet<0 || surf.wet>256 || surf.foam<0 || surf.foam>256 ||
            surf.land<0 || surf.land>256)
            throw std::invalid_argument("Invalid terrain surf constants");
        if(data.materials[i] && (data.materials[i]==packet.target || data.materials[i]->descriptor.alpha==TextureDescriptor::Alpha::Premultiplied))
            throw std::invalid_argument("Terrain material must provide canonical straight pixels");
        draw.materials[i]=resolve(commands,entry,data.materials[i]);entry.textures.push_back(draw.materials[i]);
    }
    if(divisor>1) {
        auto found=terrainScratch.find(int(size));
        if(found==terrainScratch.end()) {
            for(auto it=terrainScratch.begin();it!=terrainScratch.end();) {if(it->second.use_count()==1)it=terrainScratch.erase(it);else++it;}
            if(std::size_t(size)*size*4>32*1024*1024)throw std::length_error("Terrain scratch exceeds budget");
            std::size_t scratchBytes=0;for(const auto &[key,value]:terrainScratch)scratchBytes+=std::size_t(value->width)*value->height*4;
            if(scratchBytes+std::size_t(size)*size*4>32*1024*1024)throw RenderAdmissionDeferred("Terrain scratch budget exhausted");
            reserveMemory(std::size_t(size)*size*4,&entry);
            found=terrainScratch.emplace(int(size),allocate(size,size,{},&entry)).first;
        }
        draw.scratch=found->second;entry.textures.push_back(draw.scratch);
    }
    if(vertices.size()+6>ExpandedVertexCapacity)throw std::length_error("Terrain vertices exceed frame capacity");
    const float extent=float(size);const SDL_FColor tint{1,1,1,1};
    for(const SDL_Vertex vertex:{SDL_Vertex{{0,0},tint,{0,0}},SDL_Vertex{{extent,0},tint,{1,0}},SDL_Vertex{{extent,extent},tint,{1,1}},
                               SDL_Vertex{{0,0},tint,{0,0}},SDL_Vertex{{extent,extent},tint,{1,1}},SDL_Vertex{{0,extent},tint,{0,1}}})vertices.push_back(vertex);
    draw.count=6;
}
void SDLGPUDevice::drawTerrain(SDL_GPUCommandBuffer *commands, Work &entry, const Draw &draw, PrimitivePass &active)
{
    const auto &data=*draw.packet->weightedCompose;const bool reduced=data.reductionDivisor>1;
    const bool pma=draw.packet->target->descriptor.alpha==TextureDescriptor::Alpha::Premultiplied;
    std::array<std::uint32_t,Layout::terrain_compose_fragment_size/sizeof(std::uint32_t)> uniforms{};uniforms[0]=data.compositionSize;uniforms[1]=data.paletteCount;
    uniforms[3]=(data.coverage?1u:0u)|(!reduced && pma?2u:0u);
    std::array<SDL_GPUTextureSamplerBinding,6> images{};
    for(unsigned i=0;i<4;++i) {
        if(data.materials[i])uniforms[2]|=1u<<i;
        images[i]={draw.materials[i]?draw.materials[i]->gpu:white->gpu,nearest};
        const auto &seam=data.seams[i];
        uniforms[Layout::terrain_compose_fragment_seamGeometry_offset/4+i*4]=std::uint32_t(seam.height);uniforms[Layout::terrain_compose_fragment_seamGeometry_offset/4+1+i*4]=seam.cast;uniforms[Layout::terrain_compose_fragment_seamGeometry_offset/4+2+i*4]=seam.castWidth;uniforms[Layout::terrain_compose_fragment_seamGeometry_offset/4+3+i*4]=seam.fringe;
        uniforms[Layout::terrain_compose_fragment_seamFringe_offset/4+i*4]=seam.fringeWidth;for(unsigned k=0;k<3;++k)uniforms[Layout::terrain_compose_fragment_seamFringe_offset/4+1+i*4+k]=seam.fringeColor[k];
    }
    const auto surf=surfUniforms(data);
    std::memcpy(uniforms.data()+Layout::terrain_compose_fragment_surfPlace_offset/4,surf.data(),sizeof surf);
    // Integer images must remain valid bindings even when the uniform path does
    // not load coverage. The factory creates a one-pixel immutable zero mask.
    const auto &mask=draw.coverage;
    images[4]={mask->weights,nearest};images[5]={mask->selectors,nearest};
    auto render=[&](unsigned index,TexturePtr target,SDL_Rect rect,const void *data,Uint32 bytes,const SDL_GPUTextureSamplerBinding *bindings,Uint32 count) {
        // Adjacent cells on one page have independent uniforms and scissors,
        // but share the attachment. Avoid a load/store render pass per cell.
        if (!active.pass || active.target != target->gpu) {
            active.end();
            SDL_GPUColorTargetInfo color{};color.texture=target->gpu;color.load_op=SDL_GPU_LOADOP_LOAD;color.store_op=SDL_GPU_STOREOP_STORE;
            active.pass=SDL_BeginGPURenderPass(commands,&color,1,nullptr);if(!active.pass)fail("begin terrain pass");
            active.target=target->gpu;
        }
        auto *pass=active.pass;
        SDL_BindGPUGraphicsPipeline(pass,terrainPipelines[index]);SDL_GPUBufferBinding vertices{entry.vertices,0};SDL_BindGPUVertexBuffers(pass,0,&vertices,1);
        SDL_GPUViewport viewport{float(rect.x),float(rect.y),float(rect.w),float(rect.h),0,1};SDL_SetGPUViewport(pass,&viewport);SDL_SetGPUScissor(pass,&rect);
        SDL_BindGPUFragmentSamplers(pass,0,bindings,count);
        const std::array<float,4> vertex{float(draw.packet->weightedCompose->compositionSize),float(draw.packet->weightedCompose->compositionSize),0,0};
        SDL_PushGPUVertexUniformData(commands,0,vertex.data(),sizeof(vertex));SDL_PushGPUFragmentUniformData(commands,0,data,bytes);
        SDL_DrawGPUPrimitives(pass,6,1,draw.first,0);++counts.batches;
        // Reduction samples the just-composed scratch attachment; close it
        // before sampling and close the destination before scratch reuse.
        if (reduced) active.end();
    };
    render(0,reduced?draw.scratch:draw.target,reduced?SDL_Rect{0,0,int(data.compositionSize),int(data.compositionSize)}:data.targetRect,uniforms.data(),sizeof(uniforms),images.data(),images.size());
    if(reduced) {
        const std::array<std::uint32_t,4> reduce{data.reductionDivisor|(pma?65536u:0u),0,0,0};
        const SDL_GPUTextureSamplerBinding raw{draw.scratch->gpu,nearest};render(1,draw.target,data.targetRect,reduce.data(),sizeof(reduce),&raw,1);
    }
}
GeometryPtr SDLGPUDevice::resolveGeometry(SDL_GPUCommandBuffer *commands, Work &entry, GeometrySnapshot source)
{
    if (!source || source->vertices.empty() || !source->stride || source->vertices.size() % source->stride)
        throw std::invalid_argument("SDL GPU mesh requires immutable vertices");
    const auto stride = source->layout == GeometryLayout::World48 ? Layout::world_stride :
                        source->layout == GeometryLayout::Skin40 ? Layout::skin_stride :
                        source->layout == GeometryLayout::SkinRig72 ? Layout::skin_rig_stride : 0;
    if (!stride || source->stride != stride) throw std::invalid_argument("Unsupported mesh vertex layout");
    if (auto found = geometryCache.find(source.get()); found != geometryCache.end()) return found->second;
    std::size_t resident = residentGeometryBytes;
    const auto bytes = source->vertices.size() + source->indices.size() * sizeof(std::uint32_t);
    if (bytes > GeometryBudget) throw std::length_error("SDL GPU single geometry exceeds budget");
    for(auto it=geometryCache.begin();resident>GeometryBudget-bytes && it!=geometryCache.end();) {
        if(it->second.use_count()==1) { resident-=it->second->source->bytes;it=geometryCache.erase(it); }
        else ++it;
    }
    if (resident > GeometryBudget - bytes) throw RenderAdmissionDeferred("SDL GPU geometry budget exhausted");
    reserveMemory(bytes * 2, &entry);
    for (std::size_t offset = 0; offset < source->vertices.size(); offset += sizeof(float)) {
        float value; std::memcpy(&value, source->vertices.data() + offset, sizeof(value));
        if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite world geometry");
    }
    if (source->layout == GeometryLayout::SkinRig72) {
        for (std::size_t base = 0; base < source->vertices.size(); base += source->stride)
            for (std::size_t joint = 0; joint < 4; ++joint) {
                float index; std::memcpy(&index, source->vertices.data() + base + Layout::skin_rig_attributes[3].offset + joint * sizeof(float), sizeof(index));
                if (index < 0 || index >= 32 || std::floor(index) != index) throw std::invalid_argument("Rig joint index outside bone palette");
            }
    }
    for (auto index : source->indices) if (index >= source->vertices.size() / source->stride) throw std::invalid_argument("Mesh index outside vertex range");
    auto geometry = std::make_shared<Geometry>(); geometry->device = device; geometry->source = source;
    geometry->residentBytes = &residentGeometryBytes; residentGeometryBytes += source->bytes;
    auto uploadBuffer = [&](const void *pixels, std::size_t size, SDL_GPUBufferUsageFlags usage) {
        SDL_GPUBufferCreateInfo info{}; info.usage = usage; info.size = Uint32(size);
        auto *buffer = SDL_CreateGPUBuffer(device, &info); if (!buffer) fail("create persistent geometry buffer");
        try {
            auto *staging = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size); entry.transfers.emplace_back(staging, size);
            void *mapped = SDL_MapGPUTransferBuffer(device, staging, false); if (!mapped) fail("map persistent geometry");
            std::memcpy(mapped, pixels, size); SDL_UnmapGPUTransferBuffer(device, staging);
            SDL_GPUTransferBufferLocation from{}; from.transfer_buffer = staging;
            SDL_GPUBufferRegion to{}; to.buffer = buffer; to.size = Uint32(size);
            auto *copy = SDL_BeginGPUCopyPass(commands); if (!copy) fail("begin geometry copy");
            SDL_UploadToGPUBuffer(copy, &from, &to, false); SDL_EndGPUCopyPass(copy); counts.uploadBytes += size;
            return buffer;
        } catch (...) { SDL_ReleaseGPUBuffer(device, buffer); throw; }
    };
    geometry->vertices = uploadBuffer(source->vertices.data(), source->vertices.size(), SDL_GPU_BUFFERUSAGE_VERTEX);
    if (!source->indices.empty()) geometry->indices = uploadBuffer(source->indices.data(), source->indices.size() * sizeof(std::uint32_t), SDL_GPU_BUFFERUSAGE_INDEX);
    geometryCache.emplace(source.get(), geometry); freshGeometry.push_back(source.get()); return geometry;
}
TexturePtr SDLGPUDevice::depthFor(TexturePtr color, const Work *pending)
{
    if (color->depth) return color->depth;
    reserveMemory(std::size_t(color->width) * color->height * 4, pending);
    if (std::size_t(color->width) * color->height * 4 > TotalBudget - std::min(TotalBudget,counts.resourceBytes)) throw RenderAdmissionDeferred("SDL GPU depth exceeds combined budget");
    SDL_GPUTextureCreateInfo info{}; info.type = SDL_GPU_TEXTURETYPE_2D; info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET; info.width = color->width; info.height = color->height;
    info.layer_count_or_depth = info.num_levels = 1;
    auto depth = std::make_shared<Texture>(); depth->device = device; depth->width = color->width; depth->height = color->height;depth->byteSize=std::size_t(depth->width)*depth->height*4;
    depth->gpu = SDL_CreateGPUTexture(device, &info); if (!depth->gpu) fail("create depth attachment");
    depth->residentBytes = &residentTextureBytes; residentTextureBytes += depth->byteSize;
    color->depth = depth; return depth;
}
SDL_GPUSampler *SDLGPUDevice::samplerFor(const DrawPacket &packet)
{
    const bool filtering = packet.linear || (packet.texture && packet.texture->descriptor.sampling == TextureDescriptor::Sampling::Linear);
    const bool u = packet.texture && packet.texture->descriptor.addressU == TextureDescriptor::Address::Repeat;
    const bool v = packet.texture && packet.texture->descriptor.addressV == TextureDescriptor::Address::Repeat;
    const bool mipmapped=packet.texture && packet.texture->descriptor.mips==TextureDescriptor::Mips::Generated;
    if (!u && !v && !mipmapped) return filtering ? linear : nearest;
    const auto key = std::make_tuple(filtering, u, v, mipmapped);
    if (auto found = samplers.find(key); found != samplers.end()) return found->second;
    SDL_GPUSamplerCreateInfo info{}; info.min_filter = info.mag_filter = filtering ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    info.mipmap_mode = filtering && mipmapped ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.max_lod=mipmapped?32.f:0.f;
    info.address_mode_u = u ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = v ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    auto *sampler = SDL_CreateGPUSampler(device, &info); if (!sampler) fail("create world sampler");
    samplers.emplace(key, sampler); return sampler;
}
SDL_GPUGraphicsPipeline *SDLGPUDevice::worldPipeline(const DrawPacket &packet)
{
    const auto key = WorldPipelineKey{packet.material, packet.blend, packet.depth, packet.depthWrite, packet.cull};
    if (auto found = worldPipelines.find(key); found != worldPipelines.end()) return found->second;
    const auto stages = worldShaders.find(packet.material);
    if (stages == worldShaders.end()) throw std::invalid_argument("Unsupported world material");
    SDL_GPUVertexBufferDescription buffer{}; buffer.pitch = Layout::world_stride; buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    const bool skin = packet.material == DrawPacket::Material::Skin || packet.material == DrawPacket::Material::SkinRig;
    const bool rig = packet.material == DrawPacket::Material::SkinRig;
    buffer.pitch = skin ? (rig ? Layout::skin_rig_stride : Layout::skin_stride) : Layout::world_stride;
    const auto *layout = skin ? (rig ? Layout::skin_rig_attributes : Layout::skin_attributes) : Layout::world_attributes;
    std::vector<SDL_GPUVertexAttribute> attributes(skin ? (rig ? 5 : 3) : 4);
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        const auto &source = layout[i];
        attributes[i].location = source.location; attributes[i].offset = source.offset;
        attributes[i].format = source.components == 4 ? SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4 : source.components == 3 ? SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3 : SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    }
    SDL_GPUColorTargetDescription color{}; color.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    auto &blend = color.blend_state;
    const auto mode = packet.blend == DrawPacket::Blend::Automatic ? (packet.material == DrawPacket::Material::WorldLit ? DrawPacket::Blend::Opaque : DrawPacket::Blend::SourceOver) : packet.blend;
    blend.enable_blend = mode != DrawPacket::Blend::Opaque;
    blend.src_color_blendfactor = mode == DrawPacket::Blend::Premultiplied ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blend.dst_color_blendfactor = mode == DrawPacket::Blend::Additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; blend.dst_alpha_blendfactor = mode == DrawPacket::Blend::Additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo info{}; info.vertex_shader = stages->second.first; info.fragment_shader = stages->second.second;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer; info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes.data(); info.vertex_input_state.num_vertex_attributes = attributes.size();
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL; info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.cull_mode = packet.cull == DrawPacket::Cull::Back ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.enable_depth_test = packet.depth != DrawPacket::Depth::Disabled;
    info.depth_stencil_state.enable_depth_write = packet.depthWrite;
    info.depth_stencil_state.compare_op = packet.depth == DrawPacket::Depth::LessEqual ? SDL_GPU_COMPAREOP_LESS_OR_EQUAL : SDL_GPU_COMPAREOP_LESS;
    info.target_info.color_target_descriptions = &color; info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = packet.depth != DrawPacket::Depth::Disabled || packet.depthWrite;
    info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    auto *result = SDL_CreateGPUGraphicsPipeline(device, &info); if (!result) fail("create world pipeline");
    worldPipelines.emplace(key, result); return result;
}
void SDLGPUDevice::clearPacket(SDL_GPUCommandBuffer *commands, const Draw &draw)
{
    const auto &packet = *draw.packet;
    if (!packet.clearColor && !packet.clearDepth) return;
    SDL_GPUColorTargetInfo color{}; color.texture = draw.target->gpu;
    color.load_op = packet.clearColor ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD; color.store_op = SDL_GPU_STOREOP_STORE; color.clear_color = packet.clearValue;
    SDL_GPUDepthStencilTargetInfo depth{};
    if (packet.clearDepth) {
        depth.texture = depthFor(draw.target)->gpu; depth.clear_depth = packet.clearDepthValue;
        depth.load_op = SDL_GPU_LOADOP_CLEAR; depth.store_op = SDL_GPU_STOREOP_STORE;
    }
    auto *pass = SDL_BeginGPURenderPass(commands, &color, 1, packet.clearDepth ? &depth : nullptr); if (!pass) fail("clear world targets");
    SDL_EndGPURenderPass(pass);
}
void SDLGPUDevice::drawWorld(SDL_GPUCommandBuffer *commands, const Draw &draw, float lw, float lh)
{
    const auto &packet = *draw.packet;
    if (packet.targetLogicalWidth > 0) lw = packet.targetLogicalWidth;
    if (packet.targetLogicalHeight > 0) lh = packet.targetLogicalHeight;
    auto clip = scissor(packet, draw.target->width, draw.target->height, lw, lh); if (clip.w <= 0 || clip.h <= 0 || !draw.count) return;
    auto *pipeline = worldPipeline(packet);
    SDL_GPUColorTargetInfo color{}; color.texture = draw.target->gpu; color.load_op = SDL_GPU_LOADOP_LOAD; color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};
    const bool needsDepth = packet.depth != DrawPacket::Depth::Disabled || packet.depthWrite;
    if (needsDepth) { depth.texture = depthFor(draw.target)->gpu; depth.load_op = SDL_GPU_LOADOP_LOAD; depth.store_op = SDL_GPU_STOREOP_STORE; }
    auto *pass = SDL_BeginGPURenderPass(commands, &color, 1, needsDepth ? &depth : nullptr); if (!pass) fail("begin world pass");
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_GPUBufferBinding buffer{}; buffer.buffer = draw.geometry->vertices; SDL_BindGPUVertexBuffers(pass, 0, &buffer, 1);
    if (packet.indexed) { buffer.buffer = draw.geometry->indices; SDL_BindGPUIndexBuffer(pass, &buffer, SDL_GPU_INDEXELEMENTSIZE_32BIT); }
    if (packet.material != DrawPacket::Material::SmoothPoint) {
        const bool skin = packet.material == DrawPacket::Material::Skin || packet.material == DrawPacket::Material::SkinRig;
        const SDL_GPUTextureSamplerBinding images[]{{draw.texture->gpu, samplerFor(packet)}, {skin ? draw.secondTexture->gpu : draw.texture->gpu, nearest}};
        SDL_BindGPUFragmentSamplers(pass, 0, images, skin ? 2 : 1);
    }
    SDL_SetGPUScissor(pass, &clip);
    if (packet.viewport) {
        const auto &rect = *packet.viewport;
        SDL_GPUViewport viewport{float(rect.x), float(rect.y), float(rect.w), float(rect.h), 0, 1}; SDL_SetGPUViewport(pass, &viewport);
    }
    if (packet.material == DrawPacket::Material::Skin || packet.material == DrawPacket::Material::SkinRig) {
        const auto &skin = *packet.skin;
        const bool rig = packet.material == DrawPacket::Material::SkinRig;
        std::vector<float> vertex((rig ? Layout::skin_rig_size : Layout::skin_size) / sizeof(float));
        if (rig) {
            const auto &pose = *skin.pose;
            std::copy(pose.bones.begin(), pose.bones.end(), vertex.begin() + Layout::skin_rig_bones_offset / sizeof(float));
            std::copy(pose.view.begin(), pose.view.end(), vertex.begin() + Layout::skin_rig_view_offset / sizeof(float));
            for (int column = 0; column < 3; ++column) for (int row = 0; row < 3; ++row)
                vertex[Layout::skin_rig_normalView_offset / sizeof(float) + column * 4 + row] = pose.normalView[column * 3 + row];
        }
        const auto shell = (rig ? Layout::skin_rig_shell_offset : Layout::skin_shell_offset) / sizeof(float);
        vertex[shell] = skin.shell; vertex[shell + 1] = skin.furLength; vertex[shell + 2] = skin.shellDepth;
        const std::array<float,4> fragment{skin.region[0],skin.region[1],skin.shell,0};
        SDL_PushGPUVertexUniformData(commands, 0, vertex.data(), Uint32(vertex.size() * sizeof(float)));
        SDL_PushGPUFragmentUniformData(commands, 0, fragment.data(), sizeof(fragment));
    } else {
    std::array<float, Layout::world_size / sizeof(float)> vertex{};
    std::copy(packet.world.projection.begin(), packet.world.projection.end(), vertex.begin() + Layout::world_projection_offset / sizeof(float));
    std::copy(packet.world.uvTransform.begin(), packet.world.uvTransform.end(), vertex.begin() + Layout::world_uvTransform_offset / sizeof(float));
    SDL_PushGPUVertexUniformData(commands, Layout::world_binding, vertex.data(), sizeof(vertex));
    if (packet.material == DrawPacket::Material::WorldLit)
        SDL_PushGPUFragmentUniformData(commands, Layout::world_fragment_binding, packet.world.sunSpecular.data(), Layout::world_fragment_size);
    }
    if (packet.indexed) SDL_DrawGPUIndexedPrimitives(pass, draw.count, 1, draw.first, 0, 0);
    else SDL_DrawGPUPrimitives(pass, draw.count, 1, draw.first, 0);
    SDL_EndGPURenderPass(pass); ++counts.batches;
}
void SDLGPUDevice::drawPackets(SDL_GPUCommandBuffer *commands, Work &entry, const std::vector<Draw> &draws, float lw, float lh)
{
    PrimitivePass active, terrain;
    std::optional<PrimitiveState> bound, pending;
    Uint32 first = 0, count = 0;
    auto flush = [&] {
        if (!pending) return;
        const auto &state = *pending;
        if (!active.pass || !bound || bound->target != state.target) {
            active.end(); bound.reset();
            SDL_GPUColorTargetInfo color{}; color.texture = state.target;
            color.load_op = SDL_GPU_LOADOP_LOAD; color.store_op = SDL_GPU_STOREOP_STORE;
            active.pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
            if (!active.pass) fail("begin primitive pass");
        }
        const bool pipelineChanged = !bound || bound->pipeline != state.pipeline;
        if (pipelineChanged) {
            SDL_BindGPUGraphicsPipeline(active.pass, state.pipeline);
            SDL_GPUBufferBinding vertices{entry.vertices, 0};
            SDL_BindGPUVertexBuffers(active.pass, 0, &vertices, 1);
        }
        if (state.image && (pipelineChanged || bound->image != state.image || bound->sampler != state.sampler)) {
            const SDL_GPUTextureSamplerBinding image{state.image, state.sampler};
            SDL_BindGPUFragmentSamplers(active.pass, Layout::image_binding, &image, 1);
        }
        if (!bound || !SDL_RectsEqual(&bound->clip, &state.clip)) SDL_SetGPUScissor(active.pass, &state.clip);
        if (!bound || !SDL_RectsEqual(&bound->viewport, &state.viewport)) {
            const auto &rect = state.viewport;
            const SDL_GPUViewport viewport{float(rect.x), float(rect.y), float(rect.w), float(rect.h), 0, 1};
            SDL_SetGPUViewport(active.pass, &viewport);
        }
        // Uniform bindings are conservatively repushed when shaders change.
        // Otherwise both immutable blocks can be shared by adjacent draws.
        if (pipelineChanged || bound->vertex != state.vertex)
            SDL_PushGPUVertexUniformData(commands, Layout::primitive_binding, state.vertex.data(), sizeof(state.vertex));
        if (pipelineChanged || bound->fragment != state.fragment)
            SDL_PushGPUFragmentUniformData(commands, Layout::primitive_fragment_binding, state.fragment.data(), sizeof(state.fragment));
        SDL_DrawGPUPrimitives(active.pass, count, 1, first, 0); ++counts.batches;
        bound = state; pending.reset();
    };
    auto endPass = [&] { flush(); active.end(); bound.reset(); };
    for (const auto &draw : draws) {
        const auto &packet = *draw.packet;
        if (packet.kind != DrawPacket::Kind::WeightedCompose) terrain.end();
        // These boundaries may change attachments, depth, or sampled targets.
        // They also prohibit adjacent geometry merging even when state matches.
        if (packet.kind == DrawPacket::Kind::Barrier) { endPass(); continue; }
        if (packet.kind == DrawPacket::Kind::Clear) {
            endPass();
            if (!draw.count) { clearPacket(commands, draw); continue; }
        } else if (packet.kind == DrawPacket::Kind::DirectTerrain) {
            endPass(); drawDirectTerrain(commands, entry, draw, lw, lh); continue;
        } else if (packet.kind == DrawPacket::Kind::WeightedCompose) {
            endPass(); drawTerrain(commands, entry, draw, terrain); continue;
        } else if (packet.kind == DrawPacket::Kind::MeshTriangles) {
            endPass(); drawWorld(commands, draw, lw, lh); continue;
        }
        if (!draw.count) continue;
        auto state = primitiveState(draw, lw, lh);
        if (state.clip.w <= 0 || state.clip.h <= 0) continue;
        // All positions and tint are already in the bounded, ordered vertex
        // stream. Only consecutive ranges with identical effective state merge.
        if (pending && *pending == state && first + count == draw.first) count += draw.count;
        else { flush(); pending = state; first = draw.first; count = draw.count; }
        if (packet.kind == DrawPacket::Kind::Clear) endPass();
    }
    endPass(); terrain.end();
}
RenderSubmission SDLGPUDevice::submit(RenderFrame frame)
{
    detail::RenderCpuTimer timing(counts.cpuSubmitNanoseconds);
    reap(); if (work.size() >= SubmissionLimit) return {};
    frameTextures = 0;
    if (frame.logicalWidth <= 0 || frame.logicalHeight <= 0 || frame.packets.size() > RenderFrame::CommandCapacity || frame.vertexCount > RenderFrame::VertexCapacity)
        throw std::invalid_argument("Invalid or unbounded render frame");
    std::size_t recordedVertices = 0;
    for (const auto &packet : frame.packets) {
        if (packet.vertices.size() > RenderFrame::VertexCapacity - recordedVertices)
            throw std::length_error("GPU recorded vertex capacity exhausted");
        recordedVertices += packet.vertices.size();
    }
    Work entry;
    const auto previousScreen = screen;
    const int previousWidth = width, previousHeight = height;
    auto *commands = SDL_AcquireGPUCommandBuffer(device); if (!commands) fail("acquire frame commands");
    try {
        if (!frameOpen) {
            committedFrame=lastFrame;
            if (!committedScreen || committedScreen->width != screen->width || committedScreen->height != screen->height)
                committedScreen = allocate(screen->width, screen->height, {}, &entry);
            SDL_GPUBlitInfo copy{}; copy.source.texture=screen->gpu; copy.source.w=screen->width; copy.source.h=screen->height;
            copy.destination.texture=committedScreen->gpu; copy.destination.w=committedScreen->width; copy.destination.h=committedScreen->height;
            copy.load_op=SDL_GPU_LOADOP_DONT_CARE; copy.filter=SDL_GPU_FILTER_NEAREST;
            SDL_BlitGPUTexture(commands,&copy); entry.textures.push_back(screen); entry.textures.push_back(committedScreen);
        }
        if (window) {
            int w, h; if (!SDL_GetWindowSizeInPixels(window, &w, &h)) fail("get output dimensions");
            if (w > 0 && h > 0 && (w != width || h != height)) {
                auto replacement = allocate(w, h, {}, &entry); clear(commands, replacement, true);
                entry.textures.push_back(screen); entry.textures.push_back(replacement); screen = replacement; width = w; height = h;
            }
        }
        std::vector<SDL_Vertex> vertices;
        std::vector<Draw> draws;
        draws.reserve(frame.packets.size());
        std::size_t vertexReserve = 0;
        for (const auto &packet : frame.packets) {
            const auto count = packet.kind == DrawPacket::Kind::Triangles || packet.kind == DrawPacket::Kind::ScreenTriangles ||
                packet.kind == DrawPacket::Kind::DirectTerrain ? packet.vertices.size() : packet.kind == DrawPacket::Kind::Fill || packet.kind == DrawPacket::Kind::Blit ||
                    packet.kind == DrawPacket::Kind::Clear || packet.kind == DrawPacket::Kind::WeightedCompose ? 6u : 0u;
            vertexReserve += count;
        }
        vertices.reserve(vertexReserve);
        for (const auto &packet : frame.packets) {
            if (!std::isfinite(packet.transform.scale) || packet.transform.scale <= 0 || !std::isfinite(packet.transform.x) || !std::isfinite(packet.transform.y))
                throw std::invalid_argument("Invalid packet transform");
            if (packet.kind == DrawPacket::Kind::Barrier) {
                Draw barrier{}; barrier.packet = &packet; draws.push_back(std::move(barrier));
                ++counts.packets; continue;
            }
            if (packet.texture && packet.target && packet.texture.get() == packet.target.get())
                throw std::invalid_argument("Render target cannot sample itself");
            if (packet.material == DrawPacket::Material::TeamColor &&
                (!teamColorAvailable || (packet.kind != DrawPacket::Kind::Blit && packet.kind != DrawPacket::Kind::Triangles) ||
                 !packet.texture || !std::isfinite(packet.teamHue) || packet.blend != DrawPacket::Blend::Automatic))
                throw std::invalid_argument("Invalid team colour draw");
            if (packet.kind == DrawPacket::Kind::Blit) {
                if (!packet.texture) throw std::invalid_argument("Blit requires texture snapshot");
                if (packet.source.w <= 0 || packet.source.h <= 0 || packet.destination.w == 0 || packet.destination.h == 0) continue;
                if (packet.source.x < 0 || packet.source.y < 0 ||
                    std::int64_t(packet.source.x) + packet.source.w > packet.texture->descriptor.width ||
                    std::int64_t(packet.source.y) + packet.source.h > packet.texture->descriptor.height)
                    throw std::invalid_argument("Blit source is outside texture bounds");
            }
            capCreations = frame.deferrable && !packet.target;
            auto sampled = resolve(commands, entry, packet.texture, packet.opaque,
                packet.linear && packet.kind == DrawPacket::Kind::Blit && packet.material != DrawPacket::Material::TeamColor);
            capCreations = false;
            Draw draw{&packet, std::move(sampled),
                      packet.target ? resolve(commands, entry, packet.target, false, true, true) : screen, static_cast<Uint32>(vertices.size()), 0, {}, {}, {}, {}, {}};
            if (packet.kind == DrawPacket::Kind::WeightedCompose) {
                prepareTerrain(commands,entry,draw,vertices);
                entry.textures.push_back(draw.target); draws.push_back(std::move(draw)); ++counts.packets; continue;
            }
            if (packet.viewport && (packet.viewport->w <= 0 || packet.viewport->h <= 0)) throw std::invalid_argument("Invalid physical viewport");
            if (packet.kind == DrawPacket::Kind::DirectTerrain) {
                if (!directTerrainPipeline || !packet.directTerrain || packet.vertices.size() != 6 ||
                    packet.texture || packet.blend != DrawPacket::Blend::Premultiplied ||
                    packet.depth != DrawPacket::Depth::Disabled || packet.depthWrite)
                    throw std::invalid_argument("Invalid direct terrain draw");
                if (packet.directTerrain->isStreamed()) {
                    if (!directTerrainHdPipeline) throw std::invalid_argument("Streamed direct terrain unavailable");
                    bool lost = false;
                    for (unsigned i = 0; i < draw.streamedTextures.size(); ++i) {
                        const auto &source = packet.directTerrain->streamed[i];
                        if (!source) { draw.streamedTextures[i] = white; continue; }
                        draw.streamedTextures[i] = resolveStreamed(commands, entry, source);
                        lost |= !draw.streamedTextures[i];
                    }
                    // The owner sees the loss and draws its fallback from the next frame.
                    if (lost) continue;
                }
                else for (unsigned i = 0; i < draw.directTextures.size(); ++i) {
                    const auto &source = packet.directTerrain->textures[i];
                    if (!source || !source->pixels || source == packet.target || targets.contains(source.get()))
                        throw std::invalid_argument("Invalid direct terrain texture");
                    draw.directTextures[i] = resolve(commands, entry, source);
                    entry.textures.push_back(draw.directTextures[i]);
                }
            }
            if (packet.kind == DrawPacket::Kind::MeshTriangles) {
                for (float value : packet.world.projection) if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite world projection");
                for (float value : packet.world.uvTransform) if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite UV transform");
                for (float value : packet.world.sunSpecular) if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite world lighting");
                const bool skin = packet.material == DrawPacket::Material::Skin || packet.material == DrawPacket::Material::SkinRig;
                if (skin) {
                    if (!skinAvailable || !packet.skin || !packet.texture || !packet.secondTexture) throw std::invalid_argument("Skin shaders, uniforms and two textures required");
                    const auto &uniforms = *packet.skin;
                    for (float value : {uniforms.shell,uniforms.furLength,uniforms.shellDepth,uniforms.region[0],uniforms.region[1]}) if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite skin uniforms");
                    if (packet.material == DrawPacket::Material::SkinRig) {
                        if (!uniforms.pose) throw std::invalid_argument("Rig pose required");
                        for (float value : uniforms.pose->bones) if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite rig bones");
                        for (float value : uniforms.pose->view) if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite rig view");
                        for (float value : uniforms.pose->normalView) if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite rig normals");
                    }
                    draw.secondTexture = resolve(commands, entry, packet.secondTexture);
                    entry.textures.push_back(draw.secondTexture);
                } else if (!worldAvailable) throw std::runtime_error("World shader binaries unavailable");
                draw.geometry = resolveGeometry(commands, entry, packet.geometry);
                const auto layout = packet.material == DrawPacket::Material::Skin ? GeometryLayout::Skin40 : packet.material == DrawPacket::Material::SkinRig ? GeometryLayout::SkinRig72 : GeometryLayout::World48;
                if (packet.geometry->layout != layout) throw std::invalid_argument("Material and vertex layout disagree");
                const std::size_t available = packet.indexed ? packet.geometry->indices.size() : packet.geometry->vertices.size() / packet.geometry->stride;
                if (packet.first > available || packet.count > available - packet.first) throw std::invalid_argument("Mesh range exceeds geometry");
                if (!skin && packet.material != DrawPacket::Material::WorldLit && packet.material != DrawPacket::Material::Cloud) throw std::invalid_argument("Mesh requires world material");
                if (packet.count % 3) throw std::invalid_argument("Mesh triangles require triples");
                draw.first = packet.first; draw.count = packet.count;
                if (packet.depth != DrawPacket::Depth::Disabled || packet.depthWrite) depthFor(draw.target, &entry);
                entry.geometry.push_back(draw.geometry); entry.textures.push_back(draw.texture); entry.textures.push_back(draw.target);
                draws.push_back(std::move(draw)); ++counts.packets; continue;
            }
            if (packet.kind == DrawPacket::Kind::Clear) {
                if (!std::isfinite(packet.clearDepthValue) || packet.clearDepthValue < 0 || packet.clearDepthValue > 1) throw std::invalid_argument("Depth clear outside canonical range");
                const bool partial = packet.clip || packet.targetScissor || packet.transform.bounds || packet.viewport;
                if (partial && packet.clearDepth) throw std::invalid_argument("Clipped depth clear is unsupported; issue full depth clear separately");
                if (partial && packet.clearColor) {
                    const float w = packet.targetLogicalWidth > 0 ? packet.targetLogicalWidth : frame.logicalWidth;
                    const float h = packet.targetLogicalHeight > 0 ? packet.targetLogicalHeight : frame.logicalHeight;
                    const auto c = packet.clearValue;
                    if (vertices.size() + 6 > ExpandedVertexCapacity) throw std::length_error("GPU vertex capacity exhausted");
                    for (const SDL_Vertex v : {SDL_Vertex{{0,0},c,{0,0}},SDL_Vertex{{w,0},c,{0,0}},SDL_Vertex{{w,h},c,{0,0}},
                                              SDL_Vertex{{0,0},c,{0,0}},SDL_Vertex{{w,h},c,{0,0}},SDL_Vertex{{0,h},c,{0,0}}}) vertices.push_back(v);
                    draw.count = 6;
                }
                if (packet.clearDepth) depthFor(draw.target, &entry);
                entry.textures.push_back(draw.target); draws.push_back(std::move(draw)); ++counts.packets; continue;
            }
            auto append = [&](SDL_Vertex vertex) {
                if (vertices.size() >= ExpandedVertexCapacity) throw std::length_error("GPU vertex capacity exhausted");
                if (packet.kind != DrawPacket::Kind::ScreenTriangles) {
                    vertex.position.x = vertex.position.x * packet.transform.scale + packet.transform.x;
                    vertex.position.y = vertex.position.y * packet.transform.scale + packet.transform.y;
                }
                if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y)) throw std::invalid_argument("Nonfinite geometry");
                if (packet.texture && draw.texture->premultiplied && packet.kind != DrawPacket::Kind::Blit) {
                    vertex.color.r *= vertex.color.a; vertex.color.g *= vertex.color.a; vertex.color.b *= vertex.color.a;
                }
                // Team colour triangles: hue shift in y (team-color.vert); blits set it below.
                if (packet.material == DrawPacket::Material::TeamColor && packet.kind != DrawPacket::Kind::Blit)
                    vertex.color.g = packet.teamHue;
                vertices.push_back(vertex);
            };
            if (packet.kind == DrawPacket::Kind::Fill || packet.kind == DrawPacket::Kind::Blit) {
                const auto &rect = packet.destination;
                if (rect.w < 0 || rect.h < 0) throw std::invalid_argument("Negative rectangle extent");
                SDL_FColor color{packet.color.r / 255.f, packet.color.g / 255.f, packet.color.b / 255.f, packet.color.a / 255.f};
                if (packet.kind == DrawPacket::Kind::Blit) {
                    const bool premultiplied = packet.material == DrawPacket::Material::TeamColor
                        ? teamPremultiplied(packet, draw.texture->premultiplied) : draw.texture->premultiplied;
                    const float rgb = premultiplied ? packet.alpha / 255.f : 1.f;
                    color = {rgb, rgb, rgb, 1};
                    // Team colour: grey tint in x, hue shift in y (team-color.vert).
                    if (packet.material == DrawPacket::Material::TeamColor) color = {rgb, packet.teamHue, 0, 1};
                }
                float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
                if (packet.texture) {
                    u0 = float(packet.source.x) / draw.texture->width; v0 = float(packet.source.y) / draw.texture->height;
                    u1 = float(packet.source.x + packet.source.w) / draw.texture->width;
                    v1 = float(packet.source.y + packet.source.h) / draw.texture->height;
                }
                const SDL_Vertex a{{rect.x, rect.y}, color, {u0, v0}}, b{{rect.x + rect.w, rect.y}, color, {u1, v0}},
                                 c{{rect.x + rect.w, rect.y + rect.h}, color, {u1, v1}}, d{{rect.x, rect.y + rect.h}, color, {u0, v1}};
                for (auto vertex : {a, b, c, a, c, d}) append(vertex);
            } else {
                if (packet.vertices.size() % 3) throw std::invalid_argument("Triangle count must be a multiple of three");
                for (auto vertex : packet.vertices) append(vertex);
            }
            draw.count = static_cast<Uint32>(vertices.size()) - draw.first;
            entry.textures.push_back(draw.texture); entry.textures.push_back(draw.target);
            draws.push_back(std::move(draw)); ++counts.packets;
        }
        uploadVertices(commands, entry, vertices);
        drawPackets(commands, entry, draws, frame.logicalWidth, frame.logicalHeight);
        submitWork(commands, entry);
        settleStreamed(true);
        streamedDeferrals = 0;
        markFreshSubmitted();
        frameOpen = true; lastFrame = frame.identity; return work.back().submission;
    } catch (...) {
        if (commands) SDL_CancelGPUCommandBuffer(commands);
        release(entry);
        dropFreshUnsubmitted();
        settleStreamed(false);
        screen = previousScreen; width = previousWidth; height = previousHeight; throw;
    }
}
void SDLGPUDevice::present()
{
    detail::RenderCpuTimer timing(counts.cpuPresentNanoseconds);
    if (!window) { frameOpen=false; return; }
    reap();
    // Presentation must preserve recorder order. This bound is backpressure,
    // not a synchronous wait on every submitted/captured frame.
    // One reserved completion slot permits presentation or discard after eight draw segments.
    if (work.size() > SubmissionLimit) throw RenderAdmissionDeferred("SDL GPU completion slot busy");
    Work entry; auto *commands = SDL_AcquireGPUCommandBuffer(device); if (!commands) fail("acquire presentation commands");
    SDL_GPUTexture *swapchain = nullptr; Uint32 w = 0, h = 0;
    // A held frame completes like any other but leaves the window's image.
    if (!presentationHeld && !SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &swapchain, &w, &h)) {
        SDL_CancelGPUCommandBuffer(commands); fail("acquire swapchain");
    }
    if (swapchain) {
        // SDL's GPU blit performs the format conversion into the platform
        // swapchain without shader-layout duplication or presentation geometry.
        SDL_GPUBlitInfo blit{};
        blit.source.texture = screen->gpu; blit.source.w = width; blit.source.h = height;
        blit.destination.texture = swapchain; blit.destination.w = w; blit.destination.h = h;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE; blit.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(commands, &blit); entry.textures.push_back(screen);
    }
    try { submitWork(commands, entry); frameOpen=false; }
    catch (...) { if (commands) SDL_CancelGPUCommandBuffer(commands); release(entry); throw; }
    // Without the swapchain wait nothing paces held frames: each waits for its
    // work, so its uploads have landed and the next frame finds room to submit.
    if (presentationHeld) { if (!SDL_WaitForGPUIdle(device)) fail("wait for held frame"); reap(); }
}
void SDLGPUDevice::discardFrame()
{
    if (!frameOpen || !committedScreen) return;
    // Ordered after earlier frame segments; no CPU wait and no presentation.
    reap();
    Work entry; auto *commands=SDL_AcquireGPUCommandBuffer(device); if (!commands) fail("acquire discard commands");
    const auto previous=screen;
    try {
        if (screen->width!=committedScreen->width || screen->height!=committedScreen->height)
            screen=allocate(committedScreen->width,committedScreen->height,{},&entry);
        SDL_GPUBlitInfo copy{}; copy.source.texture=committedScreen->gpu; copy.source.w=committedScreen->width; copy.source.h=committedScreen->height;
        copy.destination.texture=screen->gpu; copy.destination.w=screen->width; copy.destination.h=screen->height;
        copy.load_op=SDL_GPU_LOADOP_DONT_CARE; copy.filter=SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(commands,&copy); entry.textures={screen,committedScreen,previous};
        submitWork(commands,entry); width=screen->width; height=screen->height; lastFrame=committedFrame; frameOpen=false;
    } catch (...) { if(commands) SDL_CancelGPUCommandBuffer(commands); release(entry); screen=previous; throw; }
}
bool SDLGPUDevice::capture(CaptureRequest request)
{
    detail::RenderCpuTimer timing(counts.cpuCaptureNanoseconds);
    if (captures.size() >= 3) { ++counts.captureDrops; return false; }
    SDL_Rect pixels{}; TexturePtr source;
    try { source = captureSource(request, pixels); } catch (...) { return false; }
    Readback readback; readback.request = std::move(request);
    auto *commands = SDL_AcquireGPUCommandBuffer(device); if (!commands) return false;
    bool queued=false;
    try {
        reserveMemory(std::size_t(pixels.w) * pixels.h * 4);
        readback.transfer = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, std::size_t(pixels.w) * pixels.h * 4);
        SDL_GPUTextureRegion region{}; region.texture = source->gpu; region.x = pixels.x; region.y = pixels.y; region.w = pixels.w; region.h = pixels.h; region.d = 1;
        SDL_GPUTextureTransferInfo target{}; target.transfer_buffer = readback.transfer;
        target.pixels_per_row = pixels.w; target.rows_per_layer = pixels.h;
        auto *copy = SDL_BeginGPUCopyPass(commands); if (!copy) fail("begin capture copy");
        SDL_DownloadFromGPUTexture(copy, &region, &target); SDL_EndGPUCopyPass(copy);
        readback.source = source;
        // Copy metadata and allocate its queue node before irreversible submission.
        captures.push_back(readback);queued=true;
        auto *submitted = commands; commands = nullptr;
        captures.back().fence = SDL_SubmitGPUCommandBufferAndAcquireFence(submitted);
        if (!captures.back().fence) {SDL_WaitForGPUIdle(device);fail("submit capture");}
        readback.transfer=nullptr;queued=false;
        captureBytes+=std::size_t(pixels.w)*pixels.h*4; updateMemory();
        return true;
    } catch (...) {
        if(queued)captures.pop_back();
        if (readback.transfer) dropTransfer(readback.transfer);
        if (commands) SDL_CancelGPUCommandBuffer(commands);
        ++counts.captureDrops; return false;
    }
}
bool SDLGPUDevice::pollCapture(CaptureResult &result)
{
    detail::RenderCpuTimer timing(counts.cpuCaptureNanoseconds);
    if (captures.empty() || !SDL_QueryGPUFence(device, captures.front().fence)) return false;
    auto &entry = captures.front();
    CaptureResult next; next.request = entry.request; next.cancelled = entry.cancelled;
    if (!next.cancelled) {
        next.pixels.reset(SDL_CreateSurface(entry.request.width, entry.request.height, SDL_PIXELFORMAT_RGBA32));
        if (!next.pixels) fail("allocate capture pixels");
        const void *pixels = SDL_MapGPUTransferBuffer(device, entry.transfer, false); if (!pixels) fail("map completed capture");
        for (int y = 0; y < entry.request.height; ++y)
            std::memcpy(static_cast<unsigned char *>(next.pixels->pixels) + y * next.pixels->pitch,
                        static_cast<const unsigned char *>(pixels) + std::size_t(y) * entry.request.width * 4, entry.request.width * 4);
        SDL_UnmapGPUTransferBuffer(device, entry.transfer);
        ++counts.captureCompletions;
        counts.captureLatencyNanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-entry.admitted).count();
    }
    SDL_ReleaseGPUFence(device, entry.fence); recycleTransfer(entry.transfer);
    captureBytes -= std::min(captureBytes, std::size_t(entry.request.width) * entry.request.height * 4);
    captures.pop_front(); updateMemory(); result = std::move(next); return true;
}
SDL_Surface *SDLGPUDevice::captureSynchronously(CaptureRequest request)
{
    detail::RenderCpuTimer timing(counts.cpuCaptureNanoseconds);
    SDL_Rect rect{}; auto source = captureSource(request, rect);
    const int width = rect.w, height = rect.h;
    CaptureResult result; result.request = std::move(request);
    result.pixels.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
    if (!result.pixels) fail("allocate screenshot surface");
    reserveMemory(std::size_t(width) * height * 4);
    auto *buffer = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, std::size_t(width) * height * 4);
    SDL_GPUFence *fence = nullptr;
    auto *commands = SDL_AcquireGPUCommandBuffer(device);
    try {
        if (!commands) fail("acquire screenshot commands");
        SDL_GPUTextureRegion region{}; region.texture = source->gpu; region.x = rect.x; region.y = rect.y; region.w = width; region.h = height; region.d = 1;
        SDL_GPUTextureTransferInfo to{}; to.transfer_buffer = buffer; to.pixels_per_row = width; to.rows_per_layer = height;
        auto *copy = SDL_BeginGPUCopyPass(commands); if (!copy) fail("begin screenshot copy");
        SDL_DownloadFromGPUTexture(copy, &region, &to); SDL_EndGPUCopyPass(copy);
        auto *submitted = commands; commands = nullptr;
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence(submitted); if (!fence) fail("submit screenshot");
        if (!SDL_WaitForGPUFences(device, true, &fence, 1)) fail("wait screenshot fence");
        const void *pixels = SDL_MapGPUTransferBuffer(device, buffer, false); if (!pixels) fail("map screenshot");
        for (int y = 0; y < height; ++y)
            std::memcpy(static_cast<unsigned char *>(result.pixels->pixels) + y * result.pixels->pitch,
                        static_cast<const unsigned char *>(pixels) + std::size_t(y) * width * 4, width * 4);
        SDL_UnmapGPUTransferBuffer(device, buffer);
        SDL_ReleaseGPUFence(device, fence); recycleTransfer(buffer);
        return result.pixels.release();
    } catch (...) {
        if (commands) SDL_CancelGPUCommandBuffer(commands);
        if (fence) SDL_ReleaseGPUFence(device, fence);
        dropTransfer(buffer); throw;
    }
}
} // namespace
std::unique_ptr<RenderDevice> makeSDLGPUDevice(SDL_Window *window, int width, int height, const std::string &shaderDirectory, const char *driver)
{
    return std::make_unique<SDLGPUDevice>(window, width, height, shaderDirectory, driver);
}
std::string sdlGPUUnavailableReason(const std::string &directory, const char *driver)
{
#if defined(__APPLE__) && TARGET_OS_SIMULATOR
    return "Pinned SDL GPU does not support the iOS Simulator";
#endif
    if(!driver)driver=platformDriver();
    const std::string family=driver;
    const std::string format=family=="vulkan" ? "spirv" : family=="direct3d12" ? "dxil" : family=="metal" ? "metallib" : "";
    if(format.empty())return "Unsupported SDL GPU driver family";
    try {
        std::ifstream manifest(directory+"/metadata.json",std::ios::binary|std::ios::ate);
        if(!manifest || manifest.tellg()<=0 || manifest.tellg()>4*1024*1024)return "Offline shader metadata is unavailable";
        manifest.seekg(0);const auto metadata=nlohmann::json::parse(manifest);
        if(!metadata.contains("shaderArtifacts") || !metadata.at("shaderArtifacts").contains(family))return "Offline shader assets unavailable for "+family;
        const auto &variants=metadata.at("shaderArtifacts").at(family);
        if(!variants.contains("primitive"))return "Offline primitive shader receipts are unavailable for "+family;
        for(const char *stage:{"vertex","fragment"}) {
            if(!variants.at("primitive").contains(stage))return "Incomplete offline primitive shader receipts for "+family;
            const auto &receipt=variants.at("primitive").at(stage);
            const auto path=std::filesystem::path(receipt.at("path").get<std::string>());
            const auto entry=receipt.at("entrypoint").get<std::string>();
            if(receipt.at("format")!=format || receipt.at("stage")!=stage || path.empty() || path.is_absolute() || entry.empty() || entry.size()>256)
                return "Invalid offline primitive shader receipt for "+family;
            for(const auto &component:path)if(component=="..")return "Offline shader path escapes asset directory";
            std::ifstream binary(std::filesystem::path(directory)/path,std::ios::binary|std::ios::ate);
            if(!binary || binary.tellg()<=0 || binary.tellg()>1024*1024 || (format=="spirv" && binary.tellg()%4))return "Offline primitive shader binary unavailable for "+family;
        }
        return {};
    } catch(const std::exception &error) {return std::string("Invalid offline shader metadata: ")+error.what();}
}
} // namespace GAGCore

#else
#include <stdexcept>
namespace GAGCore {
std::string sdlGPUUnavailableReason(const std::string &, const char *) {return "SDL GPU is unavailable in browser builds";}
std::unique_ptr<RenderDevice> makeSDLGPUDevice(SDL_Window *, int, int, const std::string &, const char *)
{ throw std::runtime_error("SDL GPU is unavailable in browser builds"); }
}
#endif
