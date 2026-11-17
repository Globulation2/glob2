// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include "RenderResources.h"
#include <memory>
#include <span>
#include <cstdint>

namespace GAGCore
{
// Geometry is expressed in logical screen coordinates; texture keys are owned
// by DrawableSurface and must be forgotten before that surface is destroyed.
struct RenderOperations
{
	std::uint64_t blits = 0, fills = 0, triangles = 0;
    std::uint64_t rasterBatches = 0, rasterTiles = 0, rasterReferences = 0, rasterSerialBatches = 0;
    std::uint64_t rasterJobs = 0;
    std::uint64_t rasterSourceReuses = 0, rasterSourceRetainedBytes = 0;
    std::uint64_t rasterSnapshotBytes = 0, rasterPeakRetainedBytes = 0, rasterFallbacks = 0;
    // SDL_Renderer (portable) submission counters; cumulative like the above.
    std::uint64_t vertices = 0, textureSwitches = 0;
    // SDL_RenderGeometry calls after merging compatible consecutive submissions.
    std::uint64_t geometryBatches = 0;
    std::uint64_t textureUploads = 0, textureUploadBytes = 0, gpuPreparedUploads = 0;
    std::uint64_t textureCreates = 0, textureDestroys = 0, cpuPremultipliedBytes = 0;
    // SDL_Renderer texture-cache gauges: current values, not deltas. Bytes are
    // width*height*bytes-per-pixel estimates of driver storage; source bytes are
    // the owners' CPU surfaces that stay alive beside each cached texture.
    std::uint64_t textureCount = 0, textureBytes = 0, peakTextureBytes = 0;
    std::uint64_t sourceSurfaceBytes = 0, sharedSourceTextures = 0, gpuPreparedTextures = 0;
    // Vertex and index storage the batcher keeps allocated between frames.
    std::uint64_t stagingTextureBytes = 0, batchCapacityBytes = 0;
};

class RenderBackend
{
  public:
	virtual void blit(const void *key, SDL_Surface *source, std::uint64_t revision, bool opaque,
					  const SDL_Rect &sourceRect, const SDL_FRect &destination, Uint8 alpha) = 0;
    virtual void blitLinear(const void *key, SDL_Surface *source, std::uint64_t revision,
                           const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) {
        blit(key,source,revision,false,src,dst,alpha);
    }
    virtual void blitMipmapped(const void *key, SDL_Surface *source, std::uint64_t revision,
                              std::span<const RenderMipLevel>, const SDL_Rect &src,
                              const SDL_FRect &dst, Uint8 alpha) {
        blit(key, source, revision, false, src, dst, alpha);
    }
    // Adds the source's light (colour times alpha) to the target instead of
    // blending over it, linearly filtered. Returns false when the backend cannot,
    // and the caller then draws normally.
    virtual bool blitAdditive(const void *, SDL_Surface *, std::uint64_t, const SDL_Rect &,
                              const SDL_FRect &, Uint8) { return false; }
    // Draws a sprite's team layer recoloured on the GPU by `hue` degrees,
    // exactly as DrawableSurface::shiftHSV would recolour a copy; mipLevels as
    // blitMipmapped, empty for nearest sampling. Returns false, drawing
    // nothing, when the backend has no team-colour shader: the caller then
    // draws a CPU-recoloured copy instead.
    // `linear` samples a texture without mip levels linearly (an HD sprite frame).
    virtual bool blitTeamColor(const void *, SDL_Surface *, std::uint64_t, std::span<const RenderMipLevel>,
                               const SDL_Rect &, const SDL_FRect &, Uint8, float, bool /*linear*/ = false) { return false; }
    // Textured triangles (a radial fade's grid) whose texels take the same
    // recolour; vertex colours carry tint and opacity as in triangles().
    virtual bool trianglesTeamColor(std::span<const SDL_Vertex>, const void *, SDL_Surface *, std::uint64_t, float)
    { return false; }
    // The device budget class of textures the following draws admit, until set
    // again. Backends without per-class device budgets ignore it.
    virtual void textureBudgetClass(TextureDescriptor::BudgetClass) {}
	virtual void fill(const SDL_FRect &rect, SDL_Color color) = 0;
    virtual void prepareTexture(const void*, SDL_Surface*, std::uint64_t, bool linear = false) {}
    // Like prepareTexture, after writes confined to `dirty` (pixel rectangles of
    // the surface). A backend whose cached texture holds exactly `previous` may
    // update only those rectangles; every other state prepares the whole surface.
    virtual void prepareTextureRegions(const void *key, SDL_Surface *pixels, std::uint64_t revision,
                                       std::uint64_t /*previous*/, std::span<const SDL_Rect> /*dirty*/,
                                       bool linear = false) {
        prepareTexture(key, pixels, revision, linear);
    }
	virtual RenderOperations operations() const { return {}; }
	// Resident and peak bytes of the textures this backend caches, in constant
	// time (operations() walks every texture); zero where it keeps none.
	struct TextureMemory { std::uint64_t bytes = 0, peak = 0; };
	virtual TextureMemory textureMemory() const { return {}; }
	// Zero means the backend does not impose or report a texture limit.
	virtual int maximumTextureSize() const { return 0; }
	// Only software backends borrow a target; binding flushes old queued work.
	virtual void bindTarget(SDL_Surface *) {}
	virtual ~RenderBackend() = default;
	virtual void clip(const SDL_Rect *rect) = 0;
	// UI-only transform and output clipping; simulation/world coordinates are unchanged.
	virtual void transform(float scale, float x, float y, const SDL_Rect *bounds) = 0;
	virtual void triangles(std::span<const SDL_Vertex> vertices, const void *key = nullptr,
						   SDL_Surface *pixels = nullptr, std::uint64_t revision = 0) = 0;
	// Untextured screen-space geometry, retaining the current clip. This avoids
	// changing/flushing the map transform for pixel-aligned overlays.
	virtual void screenTriangles(std::span<const SDL_Vertex> vertices) = 0;
	virtual void forget(const void *key) = 0;
	virtual void reset() = 0;
	virtual void present() = 0;
    // While held, present() completes and submits the frame as usual but leaves
    // the window showing what it showed (GraphicContext::holdPresentation).
    virtual void setPresentationHeld(bool) {}
    // Complete deferred drawing before accessing borrowed target pixels outside
    // this backend. Capture, presentation and target binding fence themselves.
	virtual void flush() = 0;
    // Abandon queued drawing after a failed CPU batch. No output is committed;
    // the owner restores the completed image before beginning another frame.
    virtual void discardFrame() noexcept {}
	virtual void logicalSize(int width, int height) = 0;
	// Map logical geometry across the CPU target without letterboxing.
	virtual void nativeLogicalSize(int width, int height) { logicalSize(width, height); }
	virtual SDL_Surface *capture() = 0;
	virtual void outputSize(int &width, int &height) = 0;
    // Optional CPU path for tile edges already snapped to backing pixels.
    // Retains the current clip and ordering without a logical-coordinate round trip.
    virtual bool blitPixels(SDL_Surface*, const SDL_Rect&, const SDL_Rect&, Uint8) { return false; }
    // A non-null key/revision follows the same mutation/forget contract as blit.
    // The default preserves backends that do not cache backing-pixel sources.
    virtual bool blitPixels(const void *, SDL_Surface *source, std::uint64_t,
                            const SDL_Rect &src, const SDL_Rect &dst, Uint8 alpha) {
        return blitPixels(source, src, dst, alpha);
    }
    virtual bool fillPixels(const SDL_Rect&, SDL_Color) { return false; }
    // Whether `key` has a texture holding `revision`, so its CPU pixels can go.
    virtual bool hasTexture(const void * /*key*/, std::uint64_t /*revision*/) const { return false; }
    // Draws the texture `key` already holds, whose CPU pixels were released, in
    // the sampling it was uploaded with. False when there is none (a reset).
    virtual bool blitCached(const void * /*key*/, const SDL_Rect & /*src*/, const SDL_FRect & /*dst*/,
                            Uint8 /*alpha*/) { return false; }
};
std::unique_ptr<RenderBackend> makeSoftwareRenderBackend(SDL_Surface *surface);
std::unique_ptr<RenderBackend> makeSDLRenderBackend(SDL_Window *window, int width, int height);
} // namespace GAGCore
