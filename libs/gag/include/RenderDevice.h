// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <RenderBackend.h>
#include <RenderFrame.h>
#include <string>
#include <stdexcept>
#include <SteadyClock.h>

namespace GAGCore
{
struct RenderDeviceCapabilities
{
    bool offscreenTargets = false, asynchronousCapture = false, gpuTiming = false;
    int maximumTextureSize = 0;
    bool mesh3D = false, torus = false, skinMaterials = false, terrainComposition = false;
    bool directTerrain = false;
    // DirectTerrainDraw::streamed: streamed HD direct terrain.
    bool directTerrainStreaming = false;
    // Block formats the device samples directly (see TextureCompression).
    bool bc3Textures = false, astc4x4Textures = false, bc7Textures = false;
    bool supports(TextureCompression format) const
    {
        return format == TextureCompression::Uncompressed || (format == TextureCompression::BC3 && bc3Textures) ||
            (format == TextureCompression::ASTC4x4 && astc4x4Textures) || (format == TextureCompression::BC7 && bc7Textures);
    }
    // DrawPacket::Material::TeamColor blits, so team layers need no CPU recolour.
    bool teamColor = false;
};
struct RenderSubmission
{
    std::uint64_t identity = 0, deviceGeneration = 0;
    explicit operator bool() const { return identity != 0; }
};
class RenderAdmissionDeferred : public std::runtime_error { public: using std::runtime_error::runtime_error; };
enum class RenderCompletion { Pending, Complete, Cancelled };
struct RenderTelemetry
{
    std::uint64_t packets = 0, batches = 0, uploadBytes = 0, captureDrops = 0;
    std::size_t resourceBytes = 0;
    // Released buffers and textures kept for reuse, apart from resourceBytes
    // and within the total budget beside it.
    std::size_t idleBytes = 0;
    std::uint64_t cpuSubmitNanoseconds=0,cpuPresentNanoseconds=0,cpuCaptureNanoseconds=0;
    std::uint64_t captureLatencyNanoseconds=0,captureCompletions=0;
    std::uint64_t deferredFrames=0,reusedFrames=0,gpuSkipped=0,gpuInvalid=0;
};
namespace detail
{
// Cumulative timings: consumers subtract snapshots to measure a frame/workload.
class RenderCpuTimer
{
    std::uint64_t &counter;
    std::int64_t started=GAGCore::steadyClockNanoseconds();
  public:
    explicit RenderCpuTimer(std::uint64_t &counter):counter(counter){}
    RenderCpuTimer(const RenderCpuTimer &)=delete;
    RenderCpuTimer &operator=(const RenderCpuTimer &)=delete;
    ~RenderCpuTimer(){counter+=std::uint64_t(GAGCore::steadyClockNanoseconds()-started);}
};
}
struct CaptureRequest
{
    std::uint64_t identity = 0, frameIdentity = 0, recordingGeneration = 0, timestamp = 0;
    int width = 0, height = 0;
    std::string context;
    TextureSnapshot target;
    std::optional<SDL_Rect> pixelRegion; // Target pixels, top-left origin.
};
struct CaptureResult
{
    CaptureRequest request;
    // CPU capture is top-left origin. Ownership passes to the consumer.
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels{nullptr, SDL_DestroySurface};
    bool cancelled = false;
};
// One presentation thread owns the device. submit takes ownership of packets;
// implementations retain their texture snapshots until completion/cancellation.
// A zero submission means bounded admission rejected the frame.
struct GpuTimingResult { std::uint64_t frame = 0, startedNs = 0, durationNs = 0; bool renderPasses = false; };
class RenderDevice
{
  public:
    virtual ~RenderDevice() = default;
    virtual RenderDeviceCapabilities capabilities() const = 0;
    // Check once before recording a presentation frame. False means retain the
    // previous completed output and skip drawing this entire frame.
    virtual bool canBeginFrame() { return true; }
    virtual RenderSubmission submit(RenderFrame) = 0;
    virtual RenderCompletion completion(RenderSubmission) = 0;
    virtual void present() = 0;
    // While held, present() finishes and submits the frame but does not show
    // it: the window keeps its last presented image.
    virtual void setPresentationHeld(bool) {}
    // Abandon current chunks without displaying partial output. Hardware
    // implementations fence their leases and retain the previous completed
    // output and earlier captures; the fallback resets a simple device.
    virtual void discardFrame() { reset(); }
    virtual void reset() = 0;
    virtual std::uint64_t generation() const = 0;
    virtual RenderTelemetry telemetry() const = 0;
    // Nonblocking; never wait for a query result.
    virtual bool pollTiming(GpuTimingResult &) { return false; }
    virtual unsigned pendingTimings() const { return 0; }
    // Poll completion and release retired allocations before a large preflight.
    // Never wait for GPU work. Completed reconstructible GPU copies may be
    // forgotten while logical CPU snapshots remain owned by callers; preserve
    // in-flight leases and externally owned rendered target contents.
    virtual void trimRetiredResources() {}
    virtual bool capture(CaptureRequest) = 0;
    virtual bool pollCapture(CaptureResult &) = 0;
    // Explicit screenshot/export wait, independent of the recording queue.
    virtual SDL_Surface *captureSynchronously(CaptureRequest)
    { throw std::runtime_error("Synchronous capture unsupported by this device"); }
    virtual void cancelCaptures() = 0;
    virtual void outputSize(int &, int &) = 0;
};
// Adapter retains the existing software rasterizer as the pixel reference.
std::unique_ptr<RenderDevice> makeSoftwareRenderDevice(SDL_Surface *borrowedTarget);
// Records existing drawing calls as neutral packets. Device and registry must
// outlive the adapter. Caller flushes before destroying/replacing either.
std::unique_ptr<RenderBackend> makeDeviceRenderBackend(RenderDevice &, RenderResources &, int width, int height);
// Keeps the adapter's restoration snapshot of `key` at `revision` until the key
// is forgotten, so its owner may free its CPU pixels and draw with blitCached.
// Retained snapshots never take part in eviction and are capped at half of each
// budget class. False, changing nothing, without that snapshot or the room.
bool retainDeviceTexture(RenderBackend &, const void *key, std::uint64_t revision);
std::size_t retainedDeviceTextureBytes(const RenderBackend &);
//! The retained bytes of one budget class, and that class's limit (0 for other backends).
std::size_t retainedDeviceTextureBytes(const RenderBackend &, TextureDescriptor::BudgetClass);
std::size_t deviceTextureClassLimit(const RenderBackend &, TextureDescriptor::BudgetClass);
// Moves every cached snapshot to the adapter replacing it (context recovery);
// both must share one registry. The previous adapter keeps none.
void adoptDeviceTextures(RenderBackend &previous, RenderBackend &replacement);
// Erases up to `limit` of the adapter's cached restoration snapshots that were
// not drawn in the last `idleFrames` presentations (zero: any not drawn since
// the last presentation); their owners
// re-create them on the next draw. Returns how many went (0 for other backends).
std::size_t trimDeviceTextures(RenderBackend &, std::size_t limit, std::uint64_t idleFrames);
bool pushDeviceTarget(RenderBackend &, TextureSnapshot, float logicalWidth, float logicalHeight);
void popDeviceTarget(RenderBackend &);
bool recordDevicePacket(RenderBackend &, DrawPacket);
// Query after flush and before capture admission, then after present respectively.
bool didRecordDeviceFrame(const RenderBackend &);
// Fence healthy offscreen work while retaining the previous completed main
// presentation. Failure remains ordinary abort; inspect intention after present.
// Set exact backing-pixel clipping for this target scope; nullptr resets it.
bool setDeviceTargetScissor(RenderBackend &, const SDL_Rect *);
bool deferDeviceFrame(RenderBackend &);
// Lets the device defer the rest of this frame to spread work over later
// frames (RenderFrame::deferrable); present and a synchronous capture end it.
// Only owners that show the previous frame on deferral and capture
// asynchronously call it.
void allowDeviceFrameDeferral(RenderBackend &);
bool didIntentionallyDeferDeviceFrame(const RenderBackend &);
bool didPresentDeviceFrame(const RenderBackend &);
std::uint64_t submittedDeviceFrameIdentity(const RenderBackend &);
} // namespace GAGCore
