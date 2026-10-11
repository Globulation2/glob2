// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <RenderBackend.h>
#include <RenderFrame.h>
#include <string>
#include <stdexcept>
#include <chrono>

namespace GAGCore
{
struct RenderDeviceCapabilities
{
    bool offscreenTargets = false, asynchronousCapture = false, gpuTiming = false;
    int maximumTextureSize = 0;
    bool mesh3D = false, torus = false, skinMaterials = false, terrainComposition = false;
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
    std::uint64_t cpuSubmitNanoseconds=0,cpuPresentNanoseconds=0,cpuCaptureNanoseconds=0;
    std::uint64_t captureLatencyNanoseconds=0,captureCompletions=0;
};
namespace detail
{
// Cumulative timings: consumers subtract snapshots to measure a frame/workload.
class RenderCpuTimer
{
    std::uint64_t &counter;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
  public:
    explicit RenderCpuTimer(std::uint64_t &counter):counter(counter){}
    RenderCpuTimer(const RenderCpuTimer &)=delete;
    RenderCpuTimer &operator=(const RenderCpuTimer &)=delete;
    ~RenderCpuTimer(){counter+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();}
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
    // Abandon current chunks without displaying partial output. Hardware
    // implementations fence their leases and retain the previous completed
    // output and earlier captures; the fallback resets a simple device.
    virtual void discardFrame() { reset(); }
    virtual void reset() = 0;
    virtual std::uint64_t generation() const = 0;
    virtual RenderTelemetry telemetry() const = 0;
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
bool didIntentionallyDeferDeviceFrame(const RenderBackend &);
bool didPresentDeviceFrame(const RenderBackend &);
std::uint64_t submittedDeviceFrameIdentity(const RenderBackend &);
} // namespace GAGCore
