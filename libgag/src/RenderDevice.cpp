// SPDX-License-Identifier: GPL-3.0-or-later
#include <RenderDevice.h>
#include <deque>
#include <stdexcept>
#include <unordered_map>

namespace GAGCore
{
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
            if (packet.kind == DrawPacket::Kind::MeshTriangles || packet.kind == DrawPacket::Kind::WeightedCompose || packet.kind == DrawPacket::Kind::Clear)
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
    struct TextureEntry { TextureHandle handle; std::uint64_t lastUse; };
    std::unordered_map<const void *, TextureEntry> textures;
    std::uint64_t useClock = 0;
    bool aborted=false,lastPresented=true;
    std::string fatalReason;
    void abort(){aborted=true;recorder.cancel();}
    void append(DrawPacket value){if(!aborted){recorder.append(std::move(value));if(aborted)recorder.cancel();}}
    bool evictOldest(const void *except)
    {
        auto oldest = textures.end();
        for (auto it = textures.begin(); it != textures.end(); ++it)
            if (it->first != except && (oldest == textures.end() || it->second.lastUse < oldest->second.lastUse)) oldest = it;
        if (oldest == textures.end()) return false;
        resources.erase(oldest->second.handle); textures.erase(oldest); return true;
    }
    RenderTransform currentTransform;
    std::optional<SDL_Rect> currentClip;
    TextureSnapshot currentTarget;
    float targetWidth = 0, targetHeight = 0;
    struct TargetState { TextureSnapshot target; float width, height; RenderTransform transform; std::optional<SDL_Rect> clip; };
    std::vector<TargetState> targetStack;
    RenderOperations counts;
    std::uint64_t frameIdentity = 1, captureIdentity = 0, submittedFrameIdentity = 0, presentedFrameIdentity = 0;
    int width, height;
    TextureSnapshot texture(const void *key, SDL_Surface *source, std::uint64_t revision, bool opaque, std::span<const RenderMipLevel> mipLevels = {})
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
        }
        descriptor.alpha = opaque ? TextureDescriptor::Alpha::Opaque : TextureDescriptor::Alpha::Straight;
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
            if (previous && previous->descriptor.mips != descriptor.mips) {
                // The source content revision may be unchanged when an authored
                // padded chain replaces an ordinary upload. A new handle keeps
                // old queued pixels and device upload identities independent.
                resources.erase(found->second.handle); textures.erase(found); found = textures.end();
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
            found = textures.emplace(key, TextureEntry{handle, ++useClock}).first;
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
                    if (!replacement) { textures.erase(found); throw RenderAdmissionDeferred("Texture resource budget exhausted by in-flight revisions"); }
                    found->second.handle = replacement;
                }
            }
            found->second.lastUse = ++useClock;
        }
        return resources.snapshot(found->second.handle);
    }
    DrawPacket packet(DrawPacket::Kind kind) const
    {
        DrawPacket value; value.kind = kind; value.transform = currentTransform; value.clip = currentClip;
        value.target = currentTarget; value.targetLogicalWidth = targetWidth; value.targetLogicalHeight = targetHeight; return value;
    }
  public:
    DeviceRenderBackend(RenderDevice &device, RenderResources &resources, int w, int h)
        : device(device), resources(resources), recorder([this](RenderFrame frame) {
            if(aborted) return;
            try { const auto identity=frame.identity; if(!this->device.submit(std::move(frame))) abort(); else submittedFrameIdentity=identity; }
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
        targetStack.push_back({currentTarget, targetWidth, targetHeight, currentTransform, currentClip});
        currentTarget = std::move(target); targetWidth = w; targetHeight = h; currentTransform = {}; currentClip.reset(); return true;
    }
    void popTarget()
    {
        if (targetStack.empty()) throw std::logic_error("Unbalanced device target scope");
        auto state = std::move(targetStack.back()); targetStack.pop_back();
        currentTarget = std::move(state.target); targetWidth = state.width; targetHeight = state.height;
        currentTransform = state.transform; currentClip = state.clip;
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
        append(std::move(packet));
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
        auto value = packet(DrawPacket::Kind::Blit); try{value.texture = texture(key, source, revision, false);}catch(const RenderAdmissionDeferred &){abort();return;}
        value.source = src; value.destination = dst; value.alpha = alpha; value.linear = true;
        append(std::move(value)); ++counts.blits;
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
        if (found != textures.end()) { resources.erase(found->second.handle); textures.erase(found); }
    }
    void reset() override { recorder.cancel(); device.reset(); submittedFrameIdentity=presentedFrameIdentity=0;aborted=false;lastPresented=false;fatalReason.clear(); }
    void present() override { flush();lastPresented=false;
        if(!fatalReason.empty())throw std::runtime_error(fatalReason);
        if(aborted){device.discardFrame();submittedFrameIdentity=presentedFrameIdentity;}else{device.present();presentedFrameIdentity=submittedFrameIdentity;lastPresented=true;}
        aborted=false;recorder.begin(++frameIdentity,width,height); }
    std::uint64_t submittedIdentity() const {return submittedFrameIdentity;}
    bool recorded() const {return !aborted;}
    bool presented() const {return lastPresented;}
    void flush() override { if(aborted)recorder.cancel();else recorder.flush(); }
    void logicalSize(int w, int h) override { flush(); width = w; height = h; recorder.begin(frameIdentity, width, height); }
    void nativeLogicalSize(int w, int h) override { logicalSize(w, h); }
    SDL_Surface *capture() override
    {
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
bool didRecordDeviceFrame(const RenderBackend &backend){auto *device=dynamic_cast<const DeviceRenderBackend *>(&backend);return !device || device->recorded();}
bool didPresentDeviceFrame(const RenderBackend &backend){auto *device=dynamic_cast<const DeviceRenderBackend *>(&backend);return !device || device->presented();}
std::uint64_t submittedDeviceFrameIdentity(const RenderBackend &backend){auto *device=dynamic_cast<const DeviceRenderBackend *>(&backend);return device ? device->submittedIdentity() : 0;}
} // namespace GAGCore
