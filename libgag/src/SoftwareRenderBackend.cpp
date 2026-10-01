// SPDX-License-Identifier: GPL-3.0-or-later
#include <RenderBackend.h>
#include <SurfaceRaster.h>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace GAGCore
{
// Private factory: arbitrary geometry retains SDL's rasterizer. Sprites and
// rectangles do not create textures or enter its command queue.
std::unique_ptr<RenderBackend> makeSDLSoftwareGeometryBackend(SDL_Surface *surface);
namespace
{
class TargetClip
{
	SDL_Surface *surface;
	SDL_Rect previous;

  public:
	TargetClip(SDL_Surface *value, const SDL_Rect &rect)
		: surface(value), previous(value->clip_rect)
	{
		SDL_SetClipRect(surface, &rect);
	}
	~TargetClip() { SDL_SetClipRect(surface, &previous); }
};
class SoftwareRenderBackend final : public RenderBackend
{
	SDL_Surface *target; // Borrowed; the context/presenter outlives this backend.
	float scale = 1, offsetX = 0, offsetY = 0;
	std::optional<SDL_Rect> bounds, localClip;
	std::unique_ptr<RenderBackend> geometry;
	RenderOperations counts;

	SDL_Rect outputClip() const
	{
		SDL_Rect result{0, 0, target->w, target->h};
		if (bounds)
			SDL_IntersectRect(&result, &*bounds, &result);
		if (localClip)
		{
			const int x = int(std::floor(localClip->x * scale + offsetX));
			const int y = int(std::floor(localClip->y * scale + offsetY));
			const SDL_Rect transformed{
				x, y, int(std::ceil((localClip->x + localClip->w) * scale + offsetX)) - x,
				int(std::ceil((localClip->y + localClip->h) * scale + offsetY)) - y};
			SDL_IntersectRect(&result, &transformed, &result);
		}
		return result;
	}
	SDL_Rect pixels(const SDL_FRect &rect) const
	{
		// Round shared endpoints, never origin and width independently. Adjacent
		// tiles share exactly one edge even at 33% zoom and negative offsets.
		const auto edge = [](double value) { return int(std::floor(value + 0.5)); };
		const int x = edge(rect.x * scale + offsetX), y = edge(rect.y * scale + offsetY);
		return {x, y, edge((rect.x + rect.w) * scale + offsetX) - x,
				edge((rect.y + rect.h) * scale + offsetY) - y};
	}
	RenderBackend &fallback()
	{
		if (!geometry)
		{
			geometry = makeSDLSoftwareGeometryBackend(target);
			geometry->transform(scale, offsetX, offsetY, bounds ? &*bounds : nullptr);
			geometry->clip(localClip ? &*localClip : nullptr);
		}
		return *geometry;
	}

  public:
	explicit SoftwareRenderBackend(SDL_Surface *value) : target(value) {}
	void bindTarget(SDL_Surface *value) override
	{
		if (target == value)
			return;
		flush();
		geometry.reset(); // SDL's software renderer borrows its creation surface.
		target = value;
	}
	void transform(float value, float x, float y, const SDL_Rect *clipBounds) override
	{
		if (!std::isfinite(value) || value <= 0 || !std::isfinite(x) || !std::isfinite(y))
			throw std::invalid_argument("Invalid software transform");
		scale = value;
		offsetX = x;
		offsetY = y;
		bounds = clipBounds ? std::optional<SDL_Rect>(*clipBounds) : std::nullopt;
		localClip.reset();
		if (geometry)
			geometry->transform(value, x, y, clipBounds);
	}
	void clip(const SDL_Rect *rect) override
	{
		localClip = rect ? std::optional<SDL_Rect>(*rect) : std::nullopt;
		if (geometry)
			geometry->clip(rect);
	}
	void blit(const void *key, SDL_Surface *source, std::uint64_t revision, bool opaque,
			  const SDL_Rect &sourceRect, const SDL_FRect &destination, Uint8 alpha) override
	{
		++counts.blits;
		// SDL's large textured triangles have observable fixed-point overflow
		// behavior (notably the 512-pixel water asset). Preserve that behavior
		// for existing images; changing it needs separate visual acceptance.
		// PREALLOC surfaces are borrowed terrain-run views: their pixels replace
		// many small tile blits and must not acquire large-triangle artifacts.
		const SDL_Rect mapped = pixels(destination);
		if (sourceRect.w >= 512 && sourceRect.h >= 512 &&
			(mapped.w > 512 || mapped.h > 512) && !(source->flags & SDL_PREALLOC))
		{
			fallback().blit(key, source, revision, opaque, sourceRect, destination, alpha);
			return;
		}
		// Direct writes must follow earlier queued triangles, not overtake them.
		flush();
		TargetClip clip(target, outputClip());
		SurfaceRaster::blit(target, source, sourceRect, mapped, alpha, opaque,
							SurfaceRaster::BlitBlend::Triangle);
	}
	void fill(const SDL_FRect &rect, SDL_Color color) override
	{
		++counts.fills;
		flush();
		TargetClip clip(target, outputClip());
		SurfaceRaster::fill(target, pixels(rect),
							SDL_MapRGBA(target->format, color.r, color.g, color.b, 255), color.a,
							SurfaceRaster::FillBlend::SourceOver);
	}
	void triangles(std::span<const SDL_Vertex> vertices, const void *key, SDL_Surface *source,
				   std::uint64_t revision) override
	{
		++counts.triangles;
		fallback().triangles(vertices, key, source, revision);
	}
	void screenTriangles(std::span<const SDL_Vertex> vertices) override
	{
		++counts.triangles;
		fallback().screenTriangles(vertices);
	}
	RenderOperations operations() const override { return counts; }
	void forget(const void *key) override
	{
		if (geometry)
			geometry->forget(key);
	}
	void reset() override
	{
		flush();
		geometry.reset();
	}
	void flush() override
	{
		if (geometry)
			geometry->flush();
	}
	void present() override { flush(); }
	void logicalSize(int, int) override { reset(); }
	void outputSize(int &w, int &h) override
	{
		w = target->w;
		h = target->h;
	}
	SDL_Surface *capture() override
	{
		flush();
		auto *copy = SDL_ConvertSurfaceFormat(target, SDL_PIXELFORMAT_RGBA32, 0);
		if (!copy)
			throw std::runtime_error(SDL_GetError());
		return copy;
	}
};
} // namespace
std::unique_ptr<RenderBackend> makeSoftwareRenderBackend(SDL_Surface *surface)
{
	if (!surface || surface->format->BytesPerPixel != 4)
		throw std::invalid_argument("Software rendering requires a 32-bit target");
	return std::make_unique<SoftwareRenderBackend>(surface);
}
} // namespace GAGCore
