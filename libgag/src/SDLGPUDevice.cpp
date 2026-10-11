// SPDX-License-Identifier: GPL-3.0-or-later
#include <SDLGPUDevice.h>
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
#include <set>
#include <unordered_map>
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
static_assert(Layout::terrain_compose_fragment_size == 144 && Layout::terrain_compose_fragment_params_offset == 0 &&
    Layout::terrain_compose_fragment_seamGeometry_offset == 16 && Layout::terrain_compose_fragment_seamFringe_offset == 80);
static_assert(Layout::terrain_reduce_fragment_size == 16 && Layout::terrain_reduce_fragment_params_offset == 0);
static_assert(offsetof(SDL_Vertex, position) == Layout::primitive_attributes[0].offset);
static_assert(offsetof(SDL_Vertex, color) == Layout::primitive_attributes[1].offset);
static_assert(offsetof(SDL_Vertex, tex_coord) == Layout::primitive_attributes[2].offset);

[[noreturn]] void fail(const char *operation) { throw std::runtime_error(std::string(operation) + ": " + SDL_GetError()); }
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
struct Texture
{
    SDL_GPUDevice *device;
    SDL_GPUTexture *gpu = nullptr;
    TextureSnapshot source;
    int width=0, height=0;
    std::size_t byteSize=0;
    bool submitted = false, premultiplied = false, renderTarget = false;
    bool progressiveUpload = false;
    std::size_t uploadOffset = 0;
    std::uint64_t uploadSubmission = 0;
    std::shared_ptr<Texture> depth;
    ~Texture() { if (gpu) SDL_ReleaseGPUTexture(device, gpu); }
};
using TexturePtr = std::shared_ptr<Texture>;
struct Geometry
{
    SDL_GPUDevice *device;
    GeometrySnapshot source;
    SDL_GPUBuffer *vertices = nullptr, *indices = nullptr;
    bool submitted = false;
    ~Geometry() {
        if (vertices) SDL_ReleaseGPUBuffer(device, vertices);
        if (indices) SDL_ReleaseGPUBuffer(device, indices);
    }
};
using GeometryPtr = std::shared_ptr<Geometry>;
struct CoverageGPU
{
    SDL_GPUDevice *device = nullptr;
    SDL_GPUTexture *weights = nullptr, *selectors = nullptr;
    std::shared_ptr<const CoverageData> source;
    std::size_t bytes = 0;
    bool submitted = false;
    ~CoverageGPU() {
        if (weights) SDL_ReleaseGPUTexture(device, weights);
        if (selectors) SDL_ReleaseGPUTexture(device, selectors);
    }
};
using CoveragePtr = std::shared_ptr<CoverageGPU>;
struct Work
{
    RenderSubmission submission;
    SDL_GPUFence *fence = nullptr;
    // One lease per physical allocation, even when thousands of ordered draws
    // reuse it. Ownership still lasts until this submission's fence completes.
    std::unordered_map<const Texture *, TexturePtr> textures;
    void retain(const TexturePtr &texture)
    {
        if (texture) textures.try_emplace(texture.get(), texture);
    }
    std::vector<GeometryPtr> geometry;
    std::vector<CoveragePtr> coverage;
    std::vector<std::pair<SDL_GPUTransferBuffer *, std::size_t>> transfers;
    SDL_GPUBuffer *vertices = nullptr;
    std::size_t vertexBytes = 0;
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
    ~PrimitivePass() { end(); }
    void end() { if (pass) { SDL_EndGPURenderPass(pass); pass = nullptr; } }
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
    std::map<const GeometryRevision *, GeometryPtr> geometryCache;
    bool worldAvailable = false, skinAvailable = false, terrainAvailable = false;
    std::array<SDL_GPUShader *,2> terrainShaders{};
    std::array<SDL_GPUGraphicsPipeline *,2> terrainPipelines{};
    using CoverageKey = std::tuple<std::uint64_t,std::uint64_t,std::uint32_t,std::uint32_t>;
    std::map<CoverageKey,CoveragePtr> coverageCache;
    std::map<int,TexturePtr> terrainScratch;
    using PrimitivePipelineKey = std::tuple<SDL_GPUTextureFormat, bool, DrawPacket::Material, DrawPacket::Blend>;
    std::map<PrimitivePipelineKey, SDL_GPUGraphicsPipeline *> pipelines;
    std::map<std::tuple<const TextureRevision *, bool, bool>, TexturePtr> cache;
    std::map<const TextureRevision *, std::weak_ptr<Texture>> targets;
    TexturePtr screen, committedScreen, white;
    std::shared_ptr<const CoverageData> zeroCoverage;
    bool frameOpen = false;
    std::deque<Work> work;
    std::deque<Readback> captures;
    std::uint64_t epoch = 1, serial = 0, completed = 0, lastFrame = 0, committedFrame = 0;
    int width, height;
    RenderTelemetry counts;
    // Submission preparation storage is bounded by RenderFrame's existing
    // capacities. Packet pointers and resource leases are cleared on every exit.
    std::vector<SDL_Vertex> vertexScratch;
    std::vector<Draw> drawScratch;
    static constexpr int MaximumTextureSize = 4096;
    static constexpr std::size_t TextureBudget = 64 * 1024 * 1024, TotalBudget = 256 * 1024 * 1024, SubmissionLimit = 8;

    TexturePtr allocate(int w, int h, TextureSnapshot source = {}, const Work *pending = nullptr)
    {
        if (w <= 0 || h <= 0 || w > MaximumTextureSize || h > MaximumTextureSize)
            throw std::invalid_argument("SDL GPU texture dimensions exceed supported limit");
        updateMemory(pending);
        std::size_t bytes=std::size_t(w)*h*4;
        if(source)for(const auto &level:source->mips)bytes+=level.rgba.size();
        if (bytes > TotalBudget - std::min(TotalBudget, counts.resourceBytes)) throw RenderAdmissionDeferred("SDL GPU combined budget exhausted");
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D; info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        info.width = w; info.height = h; info.layer_count_or_depth = 1; info.num_levels = source ? Uint32(source->mips.size()+1) : 1;
        auto texture = std::make_shared<Texture>();texture->device=device;texture->source=std::move(source);
        texture->width=w;texture->height=h;texture->byteSize=bytes;
        texture->gpu = SDL_CreateGPUTexture(device, &info);
        if (!texture->gpu) fail("create texture");
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
        for (auto [transfer, bytes] : entry.transfers) SDL_ReleaseGPUTransferBuffer(device, transfer);
        if (entry.vertices) SDL_ReleaseGPUBuffer(device, entry.vertices);
        entry.textures.clear(); entry.geometry.clear(); entry.coverage.clear();
    }
    void reap()
    {
        while (!work.empty() && SDL_QueryGPUFence(device, work.front().fence)) {
            completed = std::max(completed, work.front().submission.identity);
            release(work.front()); work.pop_front();
        }
        // CPU revisions referenced only by cache and completed GPU work can retire.
        for (auto it = cache.begin(); it != cache.end();) {
            if (it->second.use_count() == 1 && it->second->source.use_count() == 1) it = cache.erase(it);
            else ++it;
        }
        for (auto it = targets.begin(); it != targets.end();) {
            if (it->second.expired()) it = targets.erase(it); else ++it;
        }
        for (auto it = geometryCache.begin(); it != geometryCache.end();) {
            if (it->second.use_count() == 1 && it->second->source.use_count() == 1) it = geometryCache.erase(it); else ++it;
        }
        for (auto it=coverageCache.begin(); it!=coverageCache.end();) {
            if (it->second.use_count()==1 && it->second->source.use_count()==1) it=coverageCache.erase(it); else ++it;
        }
        updateMemory();
    }
    void updateMemory(const Work *pending = nullptr)
    {
        std::set<const Texture *> textures;
        if (screen) textures.insert(screen.get());
        if (committedScreen) textures.insert(committedScreen.get());
        if (white) textures.insert(white.get());
        for (const auto &[size,texture] : terrainScratch) textures.insert(texture.get());
        counts.resourceBytes = 0;
        for (const auto &[key,coverage] : coverageCache) counts.resourceBytes += coverage->bytes;
        for (const auto &[key, texture] : cache) textures.insert(texture.get());
        const auto addWork=[&](const Work &entry) {
            for (const auto &[identity, texture] : entry.textures) textures.insert(identity);
            counts.resourceBytes += entry.vertexBytes;
            for (auto [buffer, bytes] : entry.transfers) counts.resourceBytes += bytes;
        };
        for (const auto &entry : work) addWork(entry);
        // Resizing can remove a retiring screen from the member/cache maps
        // before this segment is submitted; its queued lease still consumes VRAM.
        if(pending)addWork(*pending);
        for (const auto &capture : captures) {
            textures.insert(capture.source.get());
            counts.resourceBytes += std::size_t(capture.request.width) * capture.request.height * 4;
        }
        for (const auto *texture : textures) if (texture)
            counts.resourceBytes += texture->byteSize + (texture->depth ? texture->depth->byteSize : 0);
        for (const auto &[key, geometry] : geometryCache) counts.resourceBytes += geometry->source->bytes;
    }
    void reserveMemory(std::size_t bytes, const Work *pending = nullptr)
    {
        updateMemory(pending);
        const std::size_t used = counts.resourceBytes;
        if (bytes > TotalBudget) throw std::length_error("SDL GPU single resource exceeds combined budget");
        if (used > TotalBudget - bytes) throw RenderAdmissionDeferred("SDL GPU combined resource budget exhausted");
    }
    SDL_GPUTransferBuffer *transfer(SDL_GPUTransferBufferUsage usage, std::size_t size)
    {
        if (size > 0xffffffffu) throw std::length_error("GPU transfer exceeds 32-bit capacity");
        SDL_GPUTransferBufferCreateInfo info{}; info.usage = usage; info.size = static_cast<Uint32>(size);
        auto *result = SDL_CreateGPUTransferBuffer(device, &info);
        if (!result) fail("create transfer buffer");
        return result;
    }
    void upload(SDL_GPUCommandBuffer *commands, Work &entry, TexturePtr texture, SDL_Surface *source, bool opaque, bool premultiplied = false)
    {
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> converted(
            SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        if (!converted) fail("convert texture pixels");
        const std::size_t bytes = texture->byteSize;
        reserveMemory(bytes * 2, &entry);
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
        std::size_t offset=texture->uploadOffset;unsigned levelIndex=0;
        int width=texture->width,height=texture->height;
        const unsigned char *source=static_cast<const unsigned char *>(texture->source->pixels->pixels);
        std::size_t pitch=texture->source->pixels->pitch;
        while(offset>=std::size_t(width)*height*4) {
            offset-=std::size_t(width)*height*4;
            const auto &level=texture->source->mips.at(levelIndex++);
            width=level.width;height=level.height;source=reinterpret_cast<const unsigned char *>(level.rgba.data());pitch=std::size_t(width)*4;
        }
        const auto rowBytes=std::size_t(width)*4;const unsigned firstRow=unsigned(offset/rowBytes);
        const unsigned rows=unsigned(std::min<std::size_t>(height-firstRow,SliceBytes/rowBytes));
        const auto bytes=rowBytes*rows;reserveMemory(bytes,&pending);
        if(texture->source->pixels->format!=SDL_PIXELFORMAT_RGBA32)throw std::invalid_argument("Progressive upload requires normalized RGBA32 restoration pixels");
        Work entry;entry.retain(texture);entry.transfers.reserve(1);
        auto *commands=SDL_AcquireGPUCommandBuffer(device);if(!commands)fail("acquire incremental upload commands");
        try {
            auto *buffer=transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,bytes);entry.transfers.emplace_back(buffer,bytes);
            auto *pixels=static_cast<unsigned char *>(SDL_MapGPUTransferBuffer(device,buffer,false));if(!pixels)fail("map incremental texture upload");
            for(unsigned y=0;y<rows;++y) {
                auto *row=pixels+y*rowBytes;std::memcpy(row,source+(firstRow+y)*pitch,rowBytes);
                if(opaque)for(int x=0;x<width;++x)row[x*4+3]=255;
                if(premultiplied)for(int x=0;x<width;++x)for(int channel=0;channel<3;++channel)
                    row[x*4+channel]=(unsigned(row[x*4+channel])*row[x*4+3]+127)/255;
            }
            SDL_UnmapGPUTransferBuffer(device,buffer);
            auto *copy=SDL_BeginGPUCopyPass(commands);if(!copy)fail("begin incremental upload pass");
            SDL_GPUTextureTransferInfo sourceInfo{};sourceInfo.transfer_buffer=buffer;sourceInfo.pixels_per_row=width;sourceInfo.rows_per_layer=rows;
            SDL_GPUTextureRegion region{};region.texture=texture->gpu;region.mip_level=levelIndex;region.y=firstRow;region.w=width;region.h=rows;region.d=1;
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
        for(const auto &[key,value]:cache)if(!value->submitted)entry.retain(value);
        for(const auto &[key,value]:geometryCache)if(!value->submitted)entry.geometry.push_back(value);
        for(const auto &[key,value]:coverageCache)if(!value->submitted)entry.coverage.push_back(value);
        submitWork(commands,entry);
        for(auto &[key,value]:cache)value->submitted=true;
        for(auto &[key,value]:geometryCache)value->submitted=true;
        for(auto &[key,value]:coverageCache)value->submitted=true;
    }
    TexturePtr resolve(SDL_GPUCommandBuffer *&commands, Work &entry, TextureSnapshot source, bool opaque = false, bool premultiplied = false, bool asTarget = false)
    {
        if (!source) return white;
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
        std::size_t bytes = std::size_t(source->descriptor.width) * source->descriptor.height * 4;
        for(const auto &level:source->mips)bytes+=level.rgba.size();
        std::array<std::size_t,4> budgets{TextureBudget,TextureBudget,128 * 1024 * 1024,32 * 1024 * 1024};
        const auto budget = static_cast<std::size_t>(source->descriptor.budgetClass);
        if (budget >= budgets.size()) throw std::invalid_argument("Invalid texture budget class");
        std::size_t resident = 0;
        for (const auto &[key, texture] : cache) if (texture->source->descriptor.budgetClass == source->descriptor.budgetClass)
            resident += texture->byteSize;
        if (source->descriptor.budgetClass==TextureDescriptor::BudgetClass::TerrainMask)
            for(const auto &[key,value]:coverageCache)resident+=value->bytes;
        if (bytes > budgets[budget]) throw std::length_error("SDL GPU single texture exceeds class budget");
        // Immutable uploaded content can be restored from its snapshot even
        // while the logical asset remains alive. Keep rendered targets and any
        // allocation referenced by this frame or in-flight work intact.
        for (auto it=cache.begin(); resident>budgets[budget]-bytes && it!=cache.end();) {
            const auto &candidate=it->second;
            if(candidate.use_count()==1 && !candidate->renderTarget && candidate->source->descriptor.budgetClass==source->descriptor.budgetClass) {
                resident-=candidate->byteSize; it=cache.erase(it);
            } else ++it;
        }
        if (resident > budgets[budget] - bytes) throw RenderAdmissionDeferred("SDL GPU texture class budget exhausted");
        try {
        auto texture = allocate(source->descriptor.width, source->descriptor.height, source, &entry);
        texture->premultiplied = premultiplied; texture->renderTarget = asTarget;
        if (asTarget) targets[source.get()] = texture;
        // Large authored chains may fit as resident resources but not with a
        // second full-chain transfer allocation beside retained depth atlases.
        // In that case admit bounded asynchronous slices across frame retries.
        if(bytes>TotalBudget-std::min(TotalBudget,counts.resourceBytes+bytes)) {
            texture->progressiveUpload=true;cache.emplace(key,texture);
            progressUpload(texture,entry,opaque,premultiplied && source->descriptor.alpha!=TextureDescriptor::Alpha::Premultiplied);
        }
        upload(commands, entry, texture, source->pixels.get(), opaque, premultiplied && source->descriptor.alpha != TextureDescriptor::Alpha::Premultiplied);
        cache.emplace(key, texture); counts.resourceBytes += bytes; return texture;
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
    SDL_GPUGraphicsPipeline *pipeline(SDL_GPUTextureFormat format, bool premultiplied = false, DrawPacket::Material material = DrawPacket::Material::Primitive, DrawPacket::Blend mode = DrawPacket::Blend::Automatic)
    {
        const PrimitivePipelineKey key{format, premultiplied, material, mode};
        if (auto found = pipelines.find(key); found != pipelines.end()) return found->second;
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
        if (material == DrawPacket::Material::SmoothPoint) {
            const auto stages = worldShaders.find(material);
            if (stages == worldShaders.end()) throw std::runtime_error("Particle shader binaries unavailable");
            info.vertex_shader = stages->second.first; info.fragment_shader = stages->second.second;
        } else if (material != DrawPacket::Material::Primitive) throw std::invalid_argument("Unsupported primitive material");
        info.vertex_input_state.vertex_buffer_descriptions = &buffer; info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes.data(); info.vertex_input_state.num_vertex_attributes = attributes.size();
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL; info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.target_info.color_target_descriptions = &color; info.target_info.num_color_targets = 1;
        auto *result = SDL_CreateGPUGraphicsPipeline(device, &info); if (!result) fail("create primitive pipeline");
        pipelines.emplace(key, result); return result;
    }
    void uploadVertices(SDL_GPUCommandBuffer *commands, Work &entry, const std::vector<SDL_Vertex> &vertices)
    {
        if (vertices.empty()) return;
        const auto size = vertices.size() * sizeof(SDL_Vertex);
        reserveMemory(size * 2, &entry);
        auto *buffer = transfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size); entry.transfers.emplace_back(buffer, size);
        void *mapped = SDL_MapGPUTransferBuffer(device, buffer, false); if (!mapped) fail("map vertices");
        std::memcpy(mapped, vertices.data(), size); SDL_UnmapGPUTransferBuffer(device, buffer);
        SDL_GPUBufferCreateInfo info{}; info.usage = SDL_GPU_BUFFERUSAGE_VERTEX; info.size = static_cast<Uint32>(size);
        entry.vertexBytes = size;
        entry.vertices = SDL_CreateGPUBuffer(device, &info); if (!entry.vertices) fail("create vertex buffer");
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
        state.pipeline = pipeline(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, bool(packet.texture) && draw.texture->premultiplied,
            packet.material, packet.kind == DrawPacket::Kind::Clear ? DrawPacket::Blend::Opaque : packet.blend);
        if (packet.material == DrawPacket::Material::Primitive) {
            state.image = draw.texture->gpu; state.sampler = samplerFor(packet);
        }
        state.vertex[0] = lw; state.vertex[1] = lh;
        auto &fragment = state.fragment;
        fragment[Layout::primitive_fragment_textured_offset / sizeof(float)] = packet.texture ? 1.f : 0.f;
        fragment[Layout::primitive_fragment_alpha_offset / sizeof(float)] = packet.kind == DrawPacket::Kind::Blit ? packet.alpha / 255.f : 1.f;
        const auto bounds = Layout::primitive_fragment_uvBounds_offset / sizeof(float);
        fragment[bounds + 2] = fragment[bounds + 3] = 1.f;
        // Nearest samples remain inside the source rectangle via vertex UVs.
        // Linear and authored-mip sampling retain per-region half-texel clamps.
        // Unmipped nearest draws can share bounds without splitting atlas batches.
        if (packet.kind == DrawPacket::Kind::Blit &&
            (packet.linear || packet.texture->descriptor.sampling == TextureDescriptor::Sampling::Linear ||
             packet.texture->descriptor.mips == TextureDescriptor::Mips::Generated)) {
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
        entry.submission = {++serial, epoch}; work.back()=std::move(entry);
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
    CoveragePtr resolveCoverage(SDL_GPUCommandBuffer *, Work &, std::shared_ptr<const CoverageData>);
    void prepareTerrain(SDL_GPUCommandBuffer *&, Work &, Draw &, std::vector<SDL_Vertex> &);
    void drawTerrain(SDL_GPUCommandBuffer *, Work &, const Draw &);
    void teardown()
    {
        if (!device) return;
        SDL_WaitForGPUIdle(device);
        for (auto &entry : work) release(entry);
        work.clear();
        for (auto &capture : captures) {
            SDL_ReleaseGPUFence(device, capture.fence); SDL_ReleaseGPUTransferBuffer(device, capture.transfer);
        }
        captures.clear(); cache.clear(); targets.clear(); geometryCache.clear(); coverageCache.clear(); terrainScratch.clear(); screen.reset(); committedScreen.reset(); white.reset();
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
        SDL_DestroyGPUDevice(device); device = nullptr;
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
            device = SDL_CreateGPUDevice(shaderFormat, false, driver); if (!device) fail("create SDL GPU device");
            if (window) { claimed = SDL_ClaimWindowForGPUDevice(device, window); if (!claimed) fail("claim GPU window"); }
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
    RenderDeviceCapabilities capabilities() const override { return {true, true, false, MaximumTextureSize, worldAvailable, worldAvailable, skinAvailable, terrainAvailable}; }
    RenderSubmission submit(RenderFrame frame) override;
    RenderCompletion completion(RenderSubmission submission) override
    {
        if (!submission || submission.deviceGeneration != epoch || submission.identity > serial) return RenderCompletion::Cancelled;
        reap(); return submission.identity <= completed ? RenderCompletion::Complete : RenderCompletion::Pending;
    }
    void present() override;
    void discardFrame() override;
    void reset() override
    {
        SDL_WaitForGPUIdle(device); for (auto &entry : work) release(entry);
        work.clear(); cache.clear(); targets.clear(); geometryCache.clear(); coverageCache.clear(); terrainScratch.clear();
        cancelCaptures(); frameOpen = false; ++epoch; completed = serial; lastFrame = 0; reap();
    }
    std::uint64_t generation() const override { return epoch; }
    RenderTelemetry telemetry() const override { return counts; }
    void trimRetiredResources() override {
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
CoveragePtr SDLGPUDevice::resolveCoverage(SDL_GPUCommandBuffer *commands, Work &entry, std::shared_ptr<const CoverageData> source)
{
    if (!source || !source->identity || !source->revision || !source->width || !source->height || source->width>MaximumTextureSize || source->height>MaximumTextureSize ||
        source->samples.size()!=std::size_t(source->width)*source->height) throw std::invalid_argument("Invalid immutable terrain coverage");
    const CoverageKey key{source->identity,source->revision,source->width,source->height};
    if(auto found=coverageCache.find(key);found!=coverageCache.end()) { entry.coverage.push_back(found->second); return found->second; }
    const std::size_t bytes=source->samples.size()*9, limit=32*1024*1024;
    if(bytes>limit) throw std::length_error("Terrain coverage exceeds mask budget");
    std::size_t used=0; for(const auto &[key,value]:coverageCache)used+=value->bytes;
    for(const auto &[key,value]:cache)if(value->source->descriptor.budgetClass==TextureDescriptor::BudgetClass::TerrainMask)used+=value->byteSize;
    for(auto it=coverageCache.begin();used>limit-bytes && it!=coverageCache.end();) {
        if(it->second.use_count()==1) {used-=it->second->bytes;it=coverageCache.erase(it);} else++it;
    }
    if(used>limit-bytes)throw RenderAdmissionDeferred("Terrain mask budget exhausted");
    reserveMemory(bytes*2,&entry);
    auto value=std::make_shared<CoverageGPU>();value->device=device;value->source=std::move(source);value->bytes=bytes;
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
    counts.uploadBytes+=bytes;coverageCache.emplace(key,value);entry.coverage.push_back(value);return value;
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
        if(data.materials[i] && (data.materials[i]==packet.target || data.materials[i]->descriptor.alpha==TextureDescriptor::Alpha::Premultiplied))
            throw std::invalid_argument("Terrain material must provide canonical straight pixels");
        draw.materials[i]=resolve(commands,entry,data.materials[i]);entry.retain(draw.materials[i]);
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
        draw.scratch=found->second;entry.retain(draw.scratch);
    }
    if(vertices.size()+6>RenderFrame::VertexCapacity)throw std::length_error("Terrain vertices exceed frame capacity");
    const float extent=float(size);const SDL_FColor tint{1,1,1,1};
    for(const SDL_Vertex vertex:{SDL_Vertex{{0,0},tint,{0,0}},SDL_Vertex{{extent,0},tint,{1,0}},SDL_Vertex{{extent,extent},tint,{1,1}},
                               SDL_Vertex{{0,0},tint,{0,0}},SDL_Vertex{{extent,extent},tint,{1,1}},SDL_Vertex{{0,extent},tint,{0,1}}})vertices.push_back(vertex);
    draw.count=6;
}
void SDLGPUDevice::drawTerrain(SDL_GPUCommandBuffer *commands, Work &entry, const Draw &draw)
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
    // Integer images must remain valid bindings even when the uniform path does
    // not load coverage. The factory creates a one-pixel immutable zero mask.
    const auto &mask=draw.coverage;
    images[4]={mask->weights,nearest};images[5]={mask->selectors,nearest};
    auto render=[&](unsigned index,TexturePtr target,SDL_Rect rect,const void *data,Uint32 bytes,const SDL_GPUTextureSamplerBinding *bindings,Uint32 count) {
        SDL_GPUColorTargetInfo color{};color.texture=target->gpu;color.load_op=SDL_GPU_LOADOP_LOAD;color.store_op=SDL_GPU_STOREOP_STORE;
        auto *pass=SDL_BeginGPURenderPass(commands,&color,1,nullptr);if(!pass)fail("begin terrain pass");
        SDL_BindGPUGraphicsPipeline(pass,terrainPipelines[index]);SDL_GPUBufferBinding vertices{entry.vertices,0};SDL_BindGPUVertexBuffers(pass,0,&vertices,1);
        SDL_GPUViewport viewport{float(rect.x),float(rect.y),float(rect.w),float(rect.h),0,1};SDL_SetGPUViewport(pass,&viewport);SDL_SetGPUScissor(pass,&rect);
        SDL_BindGPUFragmentSamplers(pass,0,bindings,count);
        const std::array<float,4> vertex{float(draw.packet->weightedCompose->compositionSize),float(draw.packet->weightedCompose->compositionSize),0,0};
        SDL_PushGPUVertexUniformData(commands,0,vertex.data(),sizeof(vertex));SDL_PushGPUFragmentUniformData(commands,0,data,bytes);
        SDL_DrawGPUPrimitives(pass,6,1,draw.first,0);SDL_EndGPURenderPass(pass);++counts.batches;
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
    std::size_t resident = 0; for (const auto &[key, geometry] : geometryCache) resident += geometry->source->bytes;
    const auto bytes = source->vertices.size() + source->indices.size() * sizeof(std::uint32_t);
    if (bytes > TextureBudget) throw std::length_error("SDL GPU single geometry exceeds budget");
    for(auto it=geometryCache.begin();resident>TextureBudget-bytes && it!=geometryCache.end();) {
        if(it->second.use_count()==1) { resident-=it->second->source->bytes;it=geometryCache.erase(it); }
        else ++it;
    }
    if (resident > TextureBudget - bytes) throw RenderAdmissionDeferred("SDL GPU geometry budget exhausted");
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
    geometryCache.emplace(source.get(), geometry); return geometry;
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
    PrimitivePass active;
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
        // These boundaries may change attachments, depth, or sampled targets.
        // They also prohibit adjacent geometry merging even when state matches.
        if (packet.kind == DrawPacket::Kind::Barrier) { endPass(); continue; }
        if (packet.kind == DrawPacket::Kind::Clear) {
            endPass();
            if (!draw.count) { clearPacket(commands, draw); continue; }
        } else if (packet.kind == DrawPacket::Kind::WeightedCompose) {
            endPass(); drawTerrain(commands, entry, draw); continue;
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
    endPass();
}
RenderSubmission SDLGPUDevice::submit(RenderFrame frame)
{
    detail::RenderCpuTimer timing(counts.cpuSubmitNanoseconds);
    reap(); if (work.size() >= SubmissionLimit) return {};
    if (frame.logicalWidth <= 0 || frame.logicalHeight <= 0 || frame.packets.size() > RenderFrame::CommandCapacity || frame.vertexCount > RenderFrame::VertexCapacity)
        throw std::invalid_argument("Invalid or unbounded render frame");
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
            SDL_BlitGPUTexture(commands,&copy); entry.retain(screen); entry.retain(committedScreen);
        }
        if (window) {
            int w, h; if (!SDL_GetWindowSizeInPixels(window, &w, &h)) fail("get output dimensions");
            if (w > 0 && h > 0 && (w != width || h != height)) {
                auto replacement = allocate(w, h, {}, &entry); clear(commands, replacement, true);
                entry.retain(screen); entry.retain(replacement); screen = replacement; width = w; height = h;
            }
        }
        auto &vertices = vertexScratch;
        auto &draws = drawScratch;
        vertices.clear(); draws.clear();
        struct ClearPreparedDraws {
            std::vector<Draw> &draws;
            ~ClearPreparedDraws() { draws.clear(); }
        } clearPreparedDraws{draws};
        draws.reserve(frame.packets.size());
        std::size_t vertexReserve = 0;
        for (const auto &packet : frame.packets) {
            const auto count = packet.kind == DrawPacket::Kind::Triangles || packet.kind == DrawPacket::Kind::ScreenTriangles
                ? packet.vertices.size() : packet.kind == DrawPacket::Kind::Fill || packet.kind == DrawPacket::Kind::Blit ||
                    packet.kind == DrawPacket::Kind::Clear || packet.kind == DrawPacket::Kind::WeightedCompose ? 6u : 0u;
            vertexReserve += std::min(RenderFrame::VertexCapacity - vertexReserve, count);
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
            if (packet.kind == DrawPacket::Kind::Blit) {
                if (!packet.texture) throw std::invalid_argument("Blit requires texture snapshot");
                if (packet.source.w <= 0 || packet.source.h <= 0 || packet.destination.w == 0 || packet.destination.h == 0) continue;
                if (packet.source.x < 0 || packet.source.y < 0 ||
                    std::int64_t(packet.source.x) + packet.source.w > packet.texture->descriptor.width ||
                    std::int64_t(packet.source.y) + packet.source.h > packet.texture->descriptor.height)
                    throw std::invalid_argument("Blit source is outside texture bounds");
            }
            Draw draw{&packet, resolve(commands, entry, packet.texture, packet.opaque, packet.linear && packet.kind == DrawPacket::Kind::Blit),
                      packet.target ? resolve(commands, entry, packet.target, false, true, true) : screen, static_cast<Uint32>(vertices.size()), 0, {}, {}, {}, {}, {}};
            if (packet.kind == DrawPacket::Kind::WeightedCompose) {
                prepareTerrain(commands,entry,draw,vertices);
                entry.retain(draw.target); draws.push_back(std::move(draw)); ++counts.packets; continue;
            }
            if (packet.viewport && (packet.viewport->w <= 0 || packet.viewport->h <= 0)) throw std::invalid_argument("Invalid physical viewport");
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
                    entry.retain(draw.secondTexture);
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
                entry.geometry.push_back(draw.geometry); entry.retain(draw.texture); entry.retain(draw.target);
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
                    if (vertices.size() + 6 > RenderFrame::VertexCapacity) throw std::length_error("GPU vertex capacity exhausted");
                    for (const SDL_Vertex v : {SDL_Vertex{{0,0},c,{0,0}},SDL_Vertex{{w,0},c,{0,0}},SDL_Vertex{{w,h},c,{0,0}},
                                              SDL_Vertex{{0,0},c,{0,0}},SDL_Vertex{{w,h},c,{0,0}},SDL_Vertex{{0,h},c,{0,0}}}) vertices.push_back(v);
                    draw.count = 6;
                }
                if (packet.clearDepth) depthFor(draw.target, &entry);
                entry.retain(draw.target); draws.push_back(std::move(draw)); ++counts.packets; continue;
            }
            auto append = [&](SDL_Vertex vertex) {
                if (vertices.size() >= RenderFrame::VertexCapacity) throw std::length_error("GPU vertex capacity exhausted");
                if (packet.kind != DrawPacket::Kind::ScreenTriangles) {
                    vertex.position.x = vertex.position.x * packet.transform.scale + packet.transform.x;
                    vertex.position.y = vertex.position.y * packet.transform.scale + packet.transform.y;
                }
                if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y)) throw std::invalid_argument("Nonfinite geometry");
                if (packet.texture && draw.texture->premultiplied && packet.kind != DrawPacket::Kind::Blit) {
                    vertex.color.r *= vertex.color.a; vertex.color.g *= vertex.color.a; vertex.color.b *= vertex.color.a;
                }
                vertices.push_back(vertex);
            };
            if (packet.kind == DrawPacket::Kind::Fill || packet.kind == DrawPacket::Kind::Blit) {
                const auto &rect = packet.destination;
                if (rect.w < 0 || rect.h < 0) throw std::invalid_argument("Negative rectangle extent");
                SDL_FColor color{packet.color.r / 255.f, packet.color.g / 255.f, packet.color.b / 255.f, packet.color.a / 255.f};
                if (packet.kind == DrawPacket::Kind::Blit) {
                    const float rgb = draw.texture->premultiplied ? packet.alpha / 255.f : 1.f;
                    color = {rgb, rgb, rgb, 1};
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
            entry.retain(draw.texture); entry.retain(draw.target);
            draws.push_back(std::move(draw)); ++counts.packets;
        }
        uploadVertices(commands, entry, vertices);
        drawPackets(commands, entry, draws, frame.logicalWidth, frame.logicalHeight);
        submitWork(commands, entry);
        for (auto &[key, texture] : cache) texture->submitted = true;
        for (auto &[key, geometry] : geometryCache) geometry->submitted = true;
        for (auto &[key,coverage] : coverageCache) coverage->submitted=true;
        frameOpen = true; lastFrame = frame.identity; return work.back().submission;
    } catch (...) {
        if (commands) SDL_CancelGPUCommandBuffer(commands);
        release(entry);
        for (auto it = cache.begin(); it != cache.end();) {
            if (!it->second->submitted) it = cache.erase(it); else ++it;
        }
        for (auto it = geometryCache.begin(); it != geometryCache.end();) {
            if (!it->second->submitted) it = geometryCache.erase(it); else ++it;
        }
        for (auto it=coverageCache.begin(); it!=coverageCache.end();) { if(!it->second->submitted)it=coverageCache.erase(it);else++it; }
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
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &swapchain, &w, &h)) {
        SDL_CancelGPUCommandBuffer(commands); fail("acquire swapchain");
    }
    if (swapchain) {
        // SDL's GPU blit performs the format conversion into the platform
        // swapchain without shader-layout duplication or presentation geometry.
        SDL_GPUBlitInfo blit{};
        blit.source.texture = screen->gpu; blit.source.w = width; blit.source.h = height;
        blit.destination.texture = swapchain; blit.destination.w = w; blit.destination.h = h;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE; blit.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(commands, &blit); entry.retain(screen);
    }
    try { submitWork(commands, entry); frameOpen=false; }
    catch (...) { if (commands) SDL_CancelGPUCommandBuffer(commands); release(entry); throw; }
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
        SDL_BlitGPUTexture(commands,&copy); entry.retain(screen); entry.retain(committedScreen); entry.retain(previous);
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
        counts.resourceBytes+=std::size_t(pixels.w)*pixels.h*4;
        return true;
    } catch (...) {
        if(queued)captures.pop_back();
        if (readback.transfer) SDL_ReleaseGPUTransferBuffer(device, readback.transfer);
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
    SDL_ReleaseGPUFence(device, entry.fence); SDL_ReleaseGPUTransferBuffer(device, entry.transfer);
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
        SDL_ReleaseGPUFence(device, fence); SDL_ReleaseGPUTransferBuffer(device, buffer);
        return result.pixels.release();
    } catch (...) {
        if (commands) SDL_CancelGPUCommandBuffer(commands);
        if (fence) SDL_ReleaseGPUFence(device, fence);
        SDL_ReleaseGPUTransferBuffer(device, buffer); throw;
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
