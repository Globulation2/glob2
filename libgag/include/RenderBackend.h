// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
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
};

class RenderBackend
{
  public:
	virtual void blit(const void *key, SDL_Surface *source, std::uint64_t revision, bool opaque,
					  const SDL_Rect &sourceRect, const SDL_FRect &destination, Uint8 alpha) = 0;
	virtual void fill(const SDL_FRect &rect, SDL_Color color) = 0;
    virtual void prepareTexture(const void*, SDL_Surface*, std::uint64_t) {}
	virtual RenderOperations operations() const { return {}; }
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
	virtual void flush() = 0;
	virtual void logicalSize(int width, int height) = 0;
	// Map logical geometry across the CPU target without letterboxing.
	virtual void nativeLogicalSize(int width, int height) { logicalSize(width, height); }
	virtual SDL_Surface *capture() = 0;
	virtual void outputSize(int &width, int &height) = 0;
};
std::unique_ptr<RenderBackend> makeSoftwareRenderBackend(SDL_Surface *surface);
std::unique_ptr<RenderBackend> makeSDLRenderBackend(SDL_Window *window, int width, int height);
} // namespace GAGCore
