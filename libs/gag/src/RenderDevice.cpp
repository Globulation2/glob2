// SPDX-License-Identifier: GPL-3.0-or-later
#include <RenderDevice.h>
#include <chrono>
#include <cmath>
#include <deque>
#include <list>
#include <stdexcept>
#include <unordered_map>

namespace GAGCore
{
void setSoftwareTargetScissor(RenderBackend &, const SDL_Rect *);
namespace
{
class SoftwareRenderDevice final : public RenderDevice
{
    std::unique_ptr<RenderBackend> backend;
    std::uint64_t epoch = 1, lastSubmission = 0;
    SDL_Surface *target;
    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> backup{nullptr,SDL_DestroySurface};
    bool frameOpen=false;
    std::deque<CaptureResult> captures;
    RenderTelemetry counts;
  public:
    explicit SoftwareRenderDevice(SDL_Surface *target) : backend(makeSoftwareRenderBackend(target)),target(target) {}
    RenderDeviceCapabilities capabilities() const override { return {false, false, false, backend->maximumTextureSize()}; }
    RenderSubmission submit(RenderFrame frame) override
    {
        detail::RenderCpuTimer timing(counts.cpuSubmitNanoseconds);
        std::size_t vertexCount = 0;
        for (const auto &packet : frame.packets) {
            if (packet.vertices.size() > RenderFrame::VertexCapacity - vertexCount)
                throw std::length_error("Unbounded render geometry");
            vertexCount += packet.vertices.size();
            detail::intersectTargetScissor({0,0,target->w,target->h}, packet.targetScissor);
            if (packet.kind == DrawPacket::Kind::MeshTriangles || packet.kind == DrawPacket::Kind::WeightedCompose || packet.kind == DrawPacket::Kind::DirectTerrain || packet.kind == DrawPacket::Kind::Clear)
                throw std::invalid_argument("Software device does not support depth/mesh passes");
            if (packet.target) throw std::invalid_argument("Software offscreen targets require a separate device");
            if (packet.kind == DrawPacket::Kind::Blit && !packet.texture)
                throw std::invalid_argument("Blit requires a texture revision");
        }
        if (frame.packets.size() > RenderFrame::CommandCapacity)
            throw std::length_error("Unbounded render submission");
        if (frame.logicalWidth <= 0 || frame.logicalHeight <= 0) throw std::invalid_argument("Invalid frame dimensions");
        if(!frameOpen){ backup.reset(backend->capture()); frameOpen=true; }
        // nativeLogicalSize clears the CPU target, so only apply it on a change.
        if (logicalWidth != frame.logicalWidth || logicalHeight != frame.logicalHeight) {
            backend->nativeLogicalSize(frame.logicalWidth, frame.logicalHeight);
            logicalWidth = frame.logicalWidth; logicalHeight = frame.logicalHeight;
        }
        for (std::size_t i = 0; i < frame.packets.size(); i = frame.batchEnd(i)) ++counts.batches;
        try {
        for (const auto &packet : frame.packets) {
            backend->transform(packet.transform.scale, packet.transform.x, packet.transform.y,
                packet.transform.bounds ? &*packet.transform.bounds : nullptr);
            backend->clip(packet.clip ? &*packet.clip : nullptr);
            setSoftwareTargetScissor(*backend, packet.targetScissor ? &*packet.targetScissor : nullptr);
            auto *pixels = packet.texture ? packet.texture->pixels.get() : nullptr;
            const void *key = packet.texture.get();
            const auto revision = packet.texture ? packet.texture->contentRevision : 0;
            switch (packet.kind) {
                case DrawPacket::Kind::Fill: backend->fill(packet.destination, packet.color); break;
                case DrawPacket::Kind::Blit:
                    if (!pixels) throw std::invalid_argument("Blit requires a texture revision");
                    // Preserve the CPU renderer's existing nearest HD policy. Authored
                    // chains use owned RGBA pixels; skin linear rasterization requires ARGB.
                    if (packet.linear && packet.texture->descriptor.mips != TextureDescriptor::Mips::Generated)
                        backend->blitLinear(key, pixels, revision, packet.source, packet.destination, packet.alpha);
                    else backend->blit(key, pixels, revision, packet.opaque, packet.source, packet.destination, packet.alpha);
                    break;
                case DrawPacket::Kind::Triangles: backend->triangles(packet.vertices, key, pixels, revision); break;
                case DrawPacket::Kind::ScreenTriangles: backend->screenTriangles(packet.vertices); break;
                case DrawPacket::Kind::Barrier: backend->flush(); break;
                case DrawPacket::Kind::DirectTerrain:
                case DrawPacket::Kind::WeightedCompose:
                case DrawPacket::Kind::Clear:
                case DrawPacket::Kind::MeshTriangles: throw std::invalid_argument("Unsupported software mesh pass");
            }
            ++counts.packets;
        }
        // The reference geometry backend borrows source pixels. Flush before
        // releasing revisions, then forget cache keys before addresses can recur.
        backend->flush();
        for (const auto &packet : frame.packets)
            if (packet.texture) backend->forget(packet.texture.get());
        } catch (...) { backend->reset(); throw; }
        return {++lastSubmission, epoch};
    }
    RenderCompletion completion(RenderSubmission submission) override
    {
        if (!submission || submission.deviceGeneration != epoch || submission.identity > lastSubmission) return RenderCompletion::Cancelled;
        return RenderCompletion::Complete;
    }
    void present() override { detail::RenderCpuTimer timing(counts.cpuPresentNanoseconds); backend->present(); frameOpen=false; }
    void discardFrame() override {
        backend->reset();
        if(frameOpen && backup){SDL_SetSurfaceBlendMode(backup.get(),SDL_BLENDMODE_NONE);
            if(!SDL_BlitSurface(backup.get(),nullptr,target,nullptr)) throw std::runtime_error(SDL_GetError());}
        frameOpen=false;
    }
    void reset() override { backend->reset(); ++epoch; cancelCaptures(); frameOpen=false; backup.reset(); }
    std::uint64_t generation() const override { return epoch; }
    RenderTelemetry telemetry() const override {
        auto result=counts;
        if(backup)result.resourceBytes+=std::size_t(backup->pitch)*backup->h;
        for(const auto &capture:captures)if(capture.pixels)result.resourceBytes+=std::size_t(capture.pixels->pitch)*capture.pixels->h;
        return result;
    }
    bool capture(CaptureRequest request) override
    {
        const auto admitted=std::chrono::steady_clock::now();
        if (captures.size() == 3) { ++counts.captureDrops; return false; }
        CaptureResult result;
        try { result.pixels.reset(captureSynchronously(request)); } catch(const std::invalid_argument &) { return false; }
        request.width=result.pixels->w; request.height=result.pixels->h; result.request=std::move(request);
        captures.push_back(std::move(result)); ++counts.captureCompletions;
        counts.captureLatencyNanoseconds+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-admitted).count(); return true;
    }
    bool pollCapture(CaptureResult &result) override
    {
        if (captures.empty()) return false;
        result = std::move(captures.front()); captures.pop_front(); return true;
    }
    SDL_Surface *captureSynchronously(CaptureRequest request) override
    {
        detail::RenderCpuTimer timing(counts.cpuCaptureNanoseconds);
        int width, height; backend->outputSize(width, height);
        const auto region=request.pixelRegion.value_or(SDL_Rect{0,0,width,height});
        if(request.target || region.x<0 || region.y<0 || region.w<=0 || region.h<=0 || region.w>width || region.h>height ||
           region.x>width-region.w || region.y>height-region.h ||
           (request.width && request.width!=region.w) || (request.height && request.height!=region.h))
            throw std::invalid_argument("Software capture region outside target");
        auto full=std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)>(backend->capture(),SDL_DestroySurface);
        if(!request.pixelRegion) return full.release();
        auto cropped=std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)>(SDL_CreateSurface(region.w,region.h,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
        if(!full || !cropped) throw std::runtime_error(SDL_GetError());
        SDL_SetSurfaceBlendMode(full.get(),SDL_BLENDMODE_NONE);
        if(!SDL_BlitSurface(full.get(),&region,cropped.get(),nullptr)) throw std::runtime_error(SDL_GetError());
        return cropped.release();
    }
    void cancelCaptures() override { counts.captureDrops += captures.size(); captures.clear(); }
    void outputSize(int &width, int &height) override { backend->outputSize(width, height); }
  private:
    int logicalWidth = 0, logicalHeight = 0;
};

class DeviceRenderBackend final : public RenderBackend
{
    RenderDevice &device;
    RenderResources &resources;
    RenderFrameRecorder recorder;
    // Entries whose owner still has CPU pixels are on `recent` (least recently
    // used first) and may be evicted and re-created. A retained entry holds the
    // only copy of a surface whose pixels were released, so it never is; it is
    // capped at half of its budget class so admission always has room.
    struct TextureEntry
    {
        TextureHandle handle;
        std::list<const void *>::iterator recentPosition;
        bool retained = false;
        std::size_t retainedBytes = 0;
        TextureDescriptor::BudgetClass category = TextureDescriptor::BudgetClass::General;
        // Presentations counted when it was last drawn (trim's idle test).
        std::uint64_t lastFrame = 0;
    };
    std::unordered_map<const void *, TextureEntry> textures;
    std::list<const void *> recent;
    std::array<std::size_t, 4> retainedBytes{};
    std::uint64_t presentedFrames = 0;
    using TextureIterator = std::unordered_map<const void *, TextureEntry>::iterator;
    TextureIterator insertEntry(const void *key, TextureHandle handle)
    {
        recent.push_back(key);
        auto found = textures.emplace(key, TextureEntry{handle, std::prev(recent.end())}).first;
        found->second.lastFrame = presentedFrames;
        return found;
    }
    void touch(TextureEntry &entry)
    {
        entry.lastFrame = presentedFrames;
        if (!entry.retained) recent.splice(recent.end(), recent, entry.recentPosition);
    }
    TextureIterator eraseEntry(TextureIterator it)
    {
        resources.erase(it->second.handle);
        if (it->second.retained) retainedBytes[std::size_t(it->second.category)] -= it->second.retainedBytes;
        else recent.erase(it->second.recentPosition);
        return textures.erase(it);
    }
    bool aborted=false,lastPresented=true,intentionalDefer=false,lastIntentionalDefer=false;
    // Set by the owner for this frame only (see RenderFrame::deferrable).
    bool deferrableFrame=false;
    std::string fatalReason;
    void abort(){aborted=true;recorder.cancel();}
    void append(DrawPacket value){if(!aborted){recorder.append(std::move(value));if(aborted)recorder.cancel();}}
    bool evictOldest(const void *except)
    {
        // Constant time: at most the first entry is the one being admitted.
        auto candidate = recent.begin();
        if (candidate != recent.end() && *candidate == except) ++candidate;
        if (candidate == recent.end()) return false;
        eraseEntry(textures.find(*candidate)); return true;
    }
    RenderTransform currentTransform;
    TextureDescriptor::BudgetClass budgetClass = TextureDescriptor::BudgetClass::General;
    std::optional<SDL_Rect> currentClip, currentTargetScissor;
    TextureSnapshot currentTarget;
    float targetWidth = 0, targetHeight = 0;
    struct TargetState { TextureSnapshot target; float width, height; RenderTransform transform; std::optional<SDL_Rect> clip, targetScissor; };
    std::vector<TargetState> targetStack;
    RenderOperations counts;
    std::uint64_t frameIdentity = 1, captureIdentity = 0, submittedFrameIdentity = 0, presentedFrameIdentity = 0;
    int width, height;
    // `linear` records linear sampling for a texture without mip levels, so a
    // released owner's cached draw (blitCached) samples it the same way.
    TextureSnapshot texture(const void *key, SDL_Surface *source, std::uint64_t revision, bool opaque, std::span<const RenderMipLevel> mipLevels = {},
                            bool linear = false)
    {
        if(aborted) throw RenderAdmissionDeferred("Presentation frame already deferred");
        if (!source) return {};
        if(std::size_t(source->pitch)*source->h>std::min(resources.byteLimit(),std::size_t(64*1024*1024)))
            throw std::length_error("Texture exceeds logical resource budget");
        TextureDescriptor descriptor;
        if (!mipLevels.empty()) {
            std::uint64_t bytes = 0;
            for (const auto &level : mipLevels) {
                if (level.width <= 0 || level.height <= 0) throw std::invalid_argument("Invalid authored mip dimensions");
                bytes += std::uint64_t(level.width) * level.height * 4;
                if (bytes > std::min(resources.byteLimit(), std::size_t(64*1024*1024))) throw std::length_error("Authored mip chain exceeds logical budget");
            }
            descriptor.mips = TextureDescriptor::Mips::Generated; descriptor.sampling = TextureDescriptor::Sampling::Linear;
            // Prepared blocks the device samples replace the RGBA chain in the
            // snapshot and on the GPU. Without them every level needs pixels.
            const auto compression = mipLevels.front().compression;
            if (compression != TextureCompression::Uncompressed && device.capabilities().supports(compression))
                descriptor.compression = compression;
            else
                for (const auto &level : mipLevels)
                    if (level.pixels.empty()) throw std::invalid_argument("Device cannot sample prepared blocks without pixels");
        }
        descriptor.alpha = opaque ? TextureDescriptor::Alpha::Opaque : TextureDescriptor::Alpha::Straight;
        descriptor.budgetClass = budgetClass;
        if (linear) descriptor.sampling = TextureDescriptor::Sampling::Linear;
        // Borrowed terrain-run views often reuse the same surface address and
        // revision zero with new pixels. A missing logical key never caches them.
        if (!key) {
            auto handle = resources.create(source, revision, descriptor, mipLevels);
            if (!handle) {
                flush();
                if(aborted) throw RenderAdmissionDeferred("Presentation frame deferred during resource admission");
                do { handle = resources.create(source, revision, descriptor, mipLevels); } while (!handle && evictOldest(key));
            }
            if (!handle) throw RenderAdmissionDeferred("Texture resource budget exhausted");
            auto result = resources.snapshot(handle);
            resources.erase(handle);
            return result;
        }
        auto found = textures.find(key);
        if (found != textures.end()) {
            auto previous = resources.snapshot(found->second.handle);
            if (previous && (previous->descriptor.mips != descriptor.mips ||
                             previous->descriptor.compression != descriptor.compression ||
                             previous->descriptor.budgetClass != descriptor.budgetClass)) {
                // The source content revision may be unchanged when an authored
                // padded chain replaces an ordinary upload. A new handle keeps
                // old queued pixels and device upload identities independent.
                eraseEntry(found); found = textures.end();
            }
        }
        if (found == textures.end()) {
            auto handle = resources.create(source, revision, descriptor, mipLevels);
            if (!handle) {
                flush();
                if(aborted) throw RenderAdmissionDeferred("Presentation frame deferred during resource admission");
                do { handle = resources.create(source, revision, descriptor, mipLevels); } while (!handle && evictOldest(key));
            }
            if (!handle) throw RenderAdmissionDeferred("Texture resource budget exhausted");
            found = insertEntry(key, handle);
        } else {
            auto previous = resources.snapshot(found->second.handle);
            const bool changed = !previous || previous->contentRevision != revision || previous->descriptor.mips != descriptor.mips;
            previous.reset();
            if (changed && !resources.update(found->second.handle, source, revision, descriptor, mipLevels)) {
                flush();
                if(aborted) throw RenderAdmissionDeferred("Presentation frame deferred during resource mutation");
                bool updated = false;
                do { updated = resources.update(found->second.handle, source, revision, descriptor, mipLevels); } while (!updated && evictOldest(key));
                if (!updated) {
                    // With submitted pixels independently owned by the device,
                    // retire the cache's old logical revision before admission.
                    resources.erase(found->second.handle);
                    auto replacement = resources.create(source, revision, descriptor, mipLevels);
                    if (!replacement) {
                        found->second.handle = {}; eraseEntry(found);
                        throw RenderAdmissionDeferred("Texture resource budget exhausted by in-flight revisions");
                    }
                    found->second.handle = replacement;
                }
            }
            touch(found->second);
        }
        return resources.snapshot(found->second.handle);
    }
    DrawPacket packet(DrawPacket::Kind kind) const
    {
        DrawPacket value; value.kind = kind; value.transform = currentTransform; value.clip = currentClip; value.targetScissor = currentTargetScissor;
        value.target = currentTarget; value.targetLogicalWidth = targetWidth; value.targetLogicalHeight = targetHeight; return value;
    }
  public:
    DeviceRenderBackend(RenderDevice &device, RenderResources &resources, int w, int h)
        : device(device), resources(resources), recorder([this](RenderFrame frame) {
            if(aborted) return;
            try { const auto identity=frame.identity; frame.deferrable=deferrableFrame; if(!this->device.submit(std::move(frame))) abort(); else submittedFrameIdentity=identity; }
            catch(const RenderAdmissionDeferred &){abort();}
            catch(const std::exception &error){fatalReason=error.what();abort();}
        }), width(w), height(h) { recorder.begin(frameIdentity, width, height); }
    ~DeviceRenderBackend() override
    {
        // Destruction never submits to a potentially lost device.
        recorder.cancel();
        for (const auto &[key, entry] : textures) resources.erase(entry.handle);
    }
    bool pushTarget(TextureSnapshot target, float w, float h)
    {
        if (!device.capabilities().offscreenTargets) return false;
        if (!target || w <= 0 || h <= 0 || targetStack.size() >= 8) throw std::invalid_argument("Invalid device target scope");
        targetStack.push_back({currentTarget, targetWidth, targetHeight, currentTransform, currentClip, currentTargetScissor});
        currentTarget = std::move(target); targetWidth = w; targetHeight = h; currentTransform = {}; currentClip.reset(); currentTargetScissor.reset(); return true;
    }
    void popTarget()
    {
        if (targetStack.empty()) throw std::logic_error("Unbalanced device target scope");
        auto state = std::move(targetStack.back()); targetStack.pop_back();
        currentTarget = std::move(state.target); targetWidth = state.width; targetHeight = state.height;
        currentTransform = state.transform; currentClip = state.clip; currentTargetScissor = state.targetScissor;
    }
    void record(DrawPacket packet)
    {
        if(aborted)return;
        if(packet.material==DrawPacket::Material::Primitive && packet.kind!=DrawPacket::Kind::Clear && packet.kind!=DrawPacket::Kind::WeightedCompose && packet.kind!=DrawPacket::Kind::MeshTriangles) {
            packet.transform=currentTransform;
            if(!packet.clip) packet.clip=currentClip;
        }
        if (!packet.target && currentTarget) {
            packet.target = currentTarget; packet.targetLogicalWidth = targetWidth; packet.targetLogicalHeight = targetHeight;
        }
        if (!packet.targetScissor && packet.target == currentTarget) packet.targetScissor = currentTargetScissor;
        append(std::move(packet));
    }
    void setTargetScissor(const SDL_Rect *rect) {
        if(rect) detail::intersectTargetScissor({0,0,width,height}, *rect);
        currentTargetScissor = rect ? std::optional<SDL_Rect>(*rect) : std::nullopt;
    }
    void blit(const void *key, SDL_Surface *source, std::uint64_t revision, bool opaque,
              const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) override
    {
        if(aborted)return;
        auto value = packet(DrawPacket::Kind::Blit); try{value.texture = texture(key, source, revision, opaque);}catch(const RenderAdmissionDeferred &){abort();return;}
        value.source = src; value.destination = dst; value.alpha = alpha; value.opaque = opaque;
        append(std::move(value)); ++counts.blits;
    }
    void blitLinear(const void *key, SDL_Surface *source, std::uint64_t revision,
                    const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) override
    {
        if(aborted)return;
        auto value = packet(DrawPacket::Kind::Blit); try{value.texture = texture(key, source, revision, false, {}, true);}catch(const RenderAdmissionDeferred &){abort();return;}
        value.source = src; value.destination = dst; value.alpha = alpha; value.linear = true;
        append(std::move(value)); ++counts.blits;
    }
    bool blitAdditive(const void *key, SDL_Surface *source, std::uint64_t revision,
                      const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) override
    {
        if(aborted)return true;
        auto value = packet(DrawPacket::Kind::Blit); try{value.texture = texture(key, source, revision, false);}catch(const RenderAdmissionDeferred &){abort();return true;}
        value.source = src; value.destination = dst; value.alpha = alpha; value.linear = true;
        value.blend = DrawPacket::Blend::Additive;
        append(std::move(value)); ++counts.blits;
        return true;
    }
    void blitMipmapped(const void *key, SDL_Surface *source, std::uint64_t revision,
                      std::span<const RenderMipLevel> mipLevels, const SDL_Rect &src,
                      const SDL_FRect &dst, Uint8 alpha) override
    {
        if (aborted) return;
        auto value = packet(DrawPacket::Kind::Blit);
        try { value.texture = texture(key, source, revision, false, mipLevels); }
        catch (const RenderAdmissionDeferred &) { abort(); return; }
        value.source = src; value.destination = dst; value.alpha = alpha; value.linear = true;
        append(std::move(value)); ++counts.blits;
    }
    bool blitTeamColor(const void *key, SDL_Surface *source, std::uint64_t revision,
                       std::span<const RenderMipLevel> mipLevels, const SDL_Rect &src,
                       const SDL_FRect &dst, Uint8 alpha, float hue, bool linear) override
    {
        if (!device.capabilities().teamColor || !std::isfinite(hue)) return false;
        if (aborted) return true;
        auto value = packet(DrawPacket::Kind::Blit);
        try { value.texture = texture(key, source, revision, false, mipLevels, linear); }
        catch (const RenderAdmissionDeferred &) { abort(); return true; }
        value.material = DrawPacket::Material::TeamColor; value.teamHue = hue;
        value.source = src; value.destination = dst; value.alpha = alpha; value.linear = linear || !mipLevels.empty();
        append(std::move(value)); ++counts.blits;
        return true;
    }
    bool trianglesTeamColor(std::span<const SDL_Vertex> vertices, const void *key, SDL_Surface *pixels,
                            std::uint64_t revision, float hue) override
    {
        if (!device.capabilities().teamColor || !std::isfinite(hue)) return false;
        if (vertices.size() > RenderFrame::VertexCapacity) throw std::length_error("Draw packet exceeds vertex capacity");
        if (aborted) return true;
        auto value = packet(DrawPacket::Kind::Triangles);
        try { value.texture = texture(key, pixels, revision, false); }
        catch (const RenderAdmissionDeferred &) { abort(); return true; }
        value.material = DrawPacket::Material::TeamColor; value.teamHue = hue;
        value.vertices.assign(vertices.begin(), vertices.end());
        append(std::move(value)); ++counts.triangles;
        return true;
    }
    void textureBudgetClass(TextureDescriptor::BudgetClass value) override { budgetClass = value; }
    void prepareTexture(const void *key, SDL_Surface *source, std::uint64_t revision, bool = false) override { if(aborted)return;try{texture(key,source,revision,false);}catch(const RenderAdmissionDeferred &){abort();} }
    void fill(const SDL_FRect &rect, SDL_Color color) override
    {
        auto value = packet(DrawPacket::Kind::Fill); value.destination = rect; value.color = color;
        append(std::move(value)); ++counts.fills;
    }
    void clip(const SDL_Rect *rect) override { currentClip = rect ? std::optional<SDL_Rect>(*rect) : std::nullopt; }
    void transform(float scale, float x, float y, const SDL_Rect *bounds) override
    {
        currentTransform = {scale, x, y, bounds ? std::optional<SDL_Rect>(*bounds) : std::nullopt}; currentClip.reset();
    }
    void triangles(std::span<const SDL_Vertex> vertices, const void *key, SDL_Surface *pixels, std::uint64_t revision) override
    {
        if (vertices.size() > RenderFrame::VertexCapacity) throw std::length_error("Draw packet exceeds vertex capacity");
        if(aborted)return;
        auto value = packet(DrawPacket::Kind::Triangles); try{value.texture = texture(key, pixels, revision, false);}catch(const RenderAdmissionDeferred &){abort();return;}
        value.vertices.assign(vertices.begin(), vertices.end()); append(std::move(value)); ++counts.triangles;
    }
    void screenTriangles(std::span<const SDL_Vertex> vertices) override
    {
        if(aborted)return;
        if (vertices.size() > RenderFrame::VertexCapacity) throw std::length_error("Draw packet exceeds vertex capacity");
        auto value = packet(DrawPacket::Kind::ScreenTriangles); value.vertices.assign(vertices.begin(), vertices.end());
        append(std::move(value)); ++counts.triangles;
    }
    void forget(const void *key) override
    {
        auto found = textures.find(key);
        if (found != textures.end()) eraseEntry(found);
    }
    bool hasTexture(const void *key, std::uint64_t revision) const override
    {
        const auto found = textures.find(key);
        if (found == textures.end()) return false;
        const auto snapshot = resources.snapshot(found->second.handle);
        return snapshot && snapshot->contentRevision == revision;
    }
    // Keeps `key`'s restoration snapshot of `revision` until forgotten, so its
    // owner can release its CPU pixels: device resets and recovery restore the
    // texture from it. False (nothing changes) without that snapshot or room.
    bool retain(const void *key, std::uint64_t revision)
    {
        auto found = textures.find(key);
        if (found == textures.end()) return false;
        auto &entry = found->second;
        const auto snapshot = resources.snapshot(entry.handle);
        if (!snapshot || snapshot->contentRevision != revision) return false;
        if (entry.retained) return true;
        const auto category = snapshot->descriptor.budgetClass;
        auto &held = retainedBytes[std::size_t(category)];
        if (snapshot->bytes > resources.classLimit(category) / 2 - std::min(held, resources.classLimit(category) / 2))
            return false;
        recent.erase(entry.recentPosition);
        entry.retained = true; entry.retainedBytes = snapshot->bytes; entry.category = category;
        held += snapshot->bytes;
        return true;
    }
    std::size_t retainedTextureBytes(TextureDescriptor::BudgetClass category) const
    {
        return retainedBytes[std::size_t(category)];
    }
    std::size_t classLimit(TextureDescriptor::BudgetClass category) const { return resources.classLimit(category); }
    std::size_t retainedTextureBytes() const
    {
        std::size_t total = 0;
        for (auto bytes : retainedBytes) total += bytes;
        return total;
    }
    // Moves every cached snapshot of `previous` (same registry) here, so a
    // replacement device restores textures whose owners released their pixels.
    void adopt(DeviceRenderBackend &previous)
    {
        if (&previous.resources != &resources) throw std::invalid_argument("Device textures belong to another registry");
        for (auto it = textures.begin(); it != textures.end();) it = eraseEntry(it);
        textures = std::move(previous.textures); recent = std::move(previous.recent);
        retainedBytes = previous.retainedBytes;
        // Adopted entries keep their idle age for trim.
        presentedFrames = std::max(presentedFrames, previous.presentedFrames);
        previous.textures.clear(); previous.recent.clear(); previous.retainedBytes = {};
    }
    bool blitCached(const void *key, const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) override
    {
        auto found = textures.find(key);
        if (found == textures.end()) return false;
        auto snapshot = resources.snapshot(found->second.handle);
        if (!snapshot) return false;
        touch(found->second);
        if (aborted) return true;
        // The sampling the texture was recorded with: authored chains are linear.
        auto value = packet(DrawPacket::Kind::Blit);
        value.linear = snapshot->descriptor.mips == TextureDescriptor::Mips::Generated ||
            snapshot->descriptor.sampling == TextureDescriptor::Sampling::Linear;
        value.opaque = snapshot->descriptor.alpha == TextureDescriptor::Alpha::Opaque;
        value.texture = std::move(snapshot);
        value.source = src; value.destination = dst; value.alpha = alpha;
        append(std::move(value)); ++counts.blits;
        return true;
    }
    void reset() override { recorder.cancel(); device.reset(); submittedFrameIdentity=presentedFrameIdentity=0;aborted=false;lastPresented=false;intentionalDefer=lastIntentionalDefer=false;fatalReason.clear();deferrableFrame=false; }
    // Erases up to `limit` cached snapshots not drawn in the last `idleFrames`
    // presentations (zero: any not drawn since the last one); their owners keep
    // the pixels to re-create them on a draw.
    // Queued and submitted work keep the revisions they reference. Returns how
    // many went: fewer than `limit` means none is left to trim.
    // Walks the least-recently-used list from its oldest end, so each step is
    // constant time: the first entry drawn too recently ends the walk. Retained
    // entries are not on it; their owners released the pixels.
    std::size_t trim(std::size_t limit, std::uint64_t idleFrames)
    {
        std::size_t erased = 0;
        while (erased < limit && !recent.empty()) {
            const auto found = textures.find(recent.front());
            if (found->second.lastFrame + idleFrames >= presentedFrames) break;
            eraseEntry(found); ++erased;
        }
        return erased;
    }
    void present() override { ++presentedFrames;flush();lastPresented=false;lastIntentionalDefer=intentionalDefer;
        if(!fatalReason.empty())throw std::runtime_error(fatalReason);
        if(aborted){device.discardFrame();submittedFrameIdentity=presentedFrameIdentity;}else{device.present();presentedFrameIdentity=submittedFrameIdentity;lastPresented=true;}
        aborted=false;intentionalDefer=false;deferrableFrame=false;recorder.begin(++frameIdentity,width,height); }
    void setPresentationHeld(bool held) override { device.setPresentationHeld(held); }
    void allowDeferral() {deferrableFrame=true;}
    std::uint64_t submittedIdentity() const {return submittedFrameIdentity;}
    bool defer() {
        flush();
        if(aborted || !fatalReason.empty())return false;
        intentionalDefer=true;aborted=true;recorder.cancel();return true;
    }
    bool intentionallyDeferred() const {return lastIntentionalDefer;}
    bool recorded() const {return !aborted;}
    bool presented() const {return lastPresented;}
    void flush() override { if(aborted)recorder.cancel();else recorder.flush(); }
    void logicalSize(int w, int h) override { flush(); width = w; height = h; recorder.begin(frameIdentity, width, height); }
    void nativeLogicalSize(int w, int h) override { logicalSize(w, h); }
    SDL_Surface *capture() override
    {
        // A frame read back synchronously cannot defer from here on.
        deferrableFrame=false;
        flush(); if(aborted)throw RenderAdmissionDeferred("Cannot capture deferred presentation frame"); int w, h; device.outputSize(w, h);
        CaptureRequest request; request.identity = ++captureIdentity; request.frameIdentity = submittedFrameIdentity;
        request.width = w; request.height = h;
        return device.captureSynchronously(std::move(request));
    }
    void outputSize(int &w, int &h) override { device.outputSize(w, h); }
    RenderOperations operations() const override { return counts; }
    int maximumTextureSize() const override { return device.capabilities().maximumTextureSize; }
};
} // namespace
std::unique_ptr<RenderDevice> makeSoftwareRenderDevice(SDL_Surface *target)
{
    return std::make_unique<SoftwareRenderDevice>(target);
}
std::unique_ptr<RenderBackend> makeDeviceRenderBackend(RenderDevice &device, RenderResources &resources, int width, int height)
{
    return std::make_unique<DeviceRenderBackend>(device, resources, width, height);
}
bool retainDeviceTexture(RenderBackend &backend, const void *key, std::uint64_t revision)
{
    auto *device = dynamic_cast<DeviceRenderBackend *>(&backend);
    return device && device->retain(key, revision);
}
std::size_t retainedDeviceTextureBytes(const RenderBackend &backend)
{
    auto *device = dynamic_cast<const DeviceRenderBackend *>(&backend);
    return device ? device->retainedTextureBytes() : 0;
}
std::size_t retainedDeviceTextureBytes(const RenderBackend &backend, TextureDescriptor::BudgetClass category)
{
    auto *device = dynamic_cast<const DeviceRenderBackend *>(&backend);
    return device ? device->retainedTextureBytes(category) : 0;
}
std::size_t deviceTextureClassLimit(const RenderBackend &backend, TextureDescriptor::BudgetClass category)
{
    auto *device = dynamic_cast<const DeviceRenderBackend *>(&backend);
    return device ? device->classLimit(category) : 0;
}
void adoptDeviceTextures(RenderBackend &previous, RenderBackend &replacement)
{
    auto *from = dynamic_cast<DeviceRenderBackend *>(&previous);
    auto *to = dynamic_cast<DeviceRenderBackend *>(&replacement);
    if (!from || !to) throw std::invalid_argument("Device textures move between device adapters only");
    to->adopt(*from);
}
std::size_t trimDeviceTextures(RenderBackend &backend, std::size_t limit, std::uint64_t idleFrames)
{
    auto *device = dynamic_cast<DeviceRenderBackend *>(&backend);
    return device ? device->trim(limit, idleFrames) : 0;
}
bool pushDeviceTarget(RenderBackend &backend, TextureSnapshot target, float width, float height)
{
    auto *device = dynamic_cast<DeviceRenderBackend *>(&backend);
    return device && device->pushTarget(std::move(target), width, height);
}
void popDeviceTarget(RenderBackend &backend)
{
    auto *device = dynamic_cast<DeviceRenderBackend *>(&backend);
    if (!device) throw std::invalid_argument("Backend has no device target scopes");
    device->popTarget();
}
bool recordDevicePacket(RenderBackend &backend, DrawPacket packet)
{
    auto *device = dynamic_cast<DeviceRenderBackend *>(&backend);
    if (!device) return false;
    device->record(std::move(packet)); return true;
}
bool setDeviceTargetScissor(RenderBackend &backend, const SDL_Rect *rect){auto *device=dynamic_cast<DeviceRenderBackend *>(&backend);if(!device)return false;device->setTargetScissor(rect);return true;}
void allowDeviceFrameDeferral(RenderBackend &backend){if(auto *device=dynamic_cast<DeviceRenderBackend *>(&backend))device->allowDeferral();}
bool deferDeviceFrame(RenderBackend &backend){auto *device=dynamic_cast<DeviceRenderBackend *>(&backend);return device && device->defer();}
bool didIntentionallyDeferDeviceFrame(const RenderBackend &backend){auto *device=dynamic_cast<const DeviceRenderBackend *>(&backend);return device && device->intentionallyDeferred();}
bool didRecordDeviceFrame(const RenderBackend &backend){auto *device=dynamic_cast<const DeviceRenderBackend *>(&backend);return !device || device->recorded();}
bool didPresentDeviceFrame(const RenderBackend &backend){auto *device=dynamic_cast<const DeviceRenderBackend *>(&backend);return !device || device->presented();}
std::uint64_t submittedDeviceFrameIdentity(const RenderBackend &backend){auto *device=dynamic_cast<const DeviceRenderBackend *>(&backend);return device ? device->submittedIdentity() : 0;}
} // namespace GAGCore
