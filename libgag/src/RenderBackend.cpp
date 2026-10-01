// SPDX-License-Identifier: GPL-3.0-or-later
#include <RenderBackend.h>
#include <limits>
#include <cmath>
#include <optional>
#include <vector>
#include <stdexcept>
#include <unordered_map>

namespace GAGCore
{
namespace
{
void check(int result)
{
	if (result < 0)
		throw std::runtime_error(SDL_GetError());
}
class SDLRenderBackend final : public RenderBackend
{
	SDL_Renderer *renderer;
	float scale = 1, offsetX = 0, offsetY = 0;
	std::optional<SDL_Rect> bounds;
	std::vector<SDL_Vertex> transformed;
	std::unordered_map<const void *, SDL_Texture *> textures;
	std::unordered_map<const void *, std::uint64_t> revisions;
	RenderOperations counts;

  public:
	explicit SDLRenderBackend(SDL_Renderer *renderer) : renderer(renderer) {}
	~SDLRenderBackend() override
	{
		reset();
		SDL_DestroyRenderer(renderer);
	}
	void transform(float factor, float x, float y, const SDL_Rect *output) override
	{
		if (!std::isfinite(factor) || factor <= 0 || !std::isfinite(x) || !std::isfinite(y))
			throw std::invalid_argument("Invalid UI transform");
		scale = factor;
		offsetX = x;
		offsetY = y;
		bounds = output ? std::optional<SDL_Rect>(*output) : std::nullopt;
		clip(nullptr);
	}
	void clip(const SDL_Rect *rect) override
	{
		std::optional<SDL_Rect> result = bounds;
		if (rect)
		{
			const int x = int(std::floor(rect->x * scale + offsetX)),
					  y = int(std::floor(rect->y * scale + offsetY));
			SDL_Rect mapped{x, y, int(std::ceil((rect->x + rect->w) * scale + offsetX)) - x,
							int(std::ceil((rect->y + rect->h) * scale + offsetY)) - y};
			if (bounds)
				SDL_IntersectRect(&mapped, &*bounds, &mapped);
			result = mapped;
		}
		check(SDL_RenderSetClipRect(renderer, result ? &*result : nullptr));
	}
	void triangles(std::span<const SDL_Vertex> vertices, const void *key, SDL_Surface *pixels,
				   std::uint64_t revision) override
	{
		submit(vertices, key, pixels, revision, true);
	}
	void screenTriangles(std::span<const SDL_Vertex> vertices) override
	{
		submit(vertices, nullptr, nullptr, false, false);
	}
	void submit(std::span<const SDL_Vertex> vertices, const void *key, SDL_Surface *pixels,
				std::uint64_t revision, bool applyTransform)
	{
		if (vertices.empty())
			return;
		++counts.triangles;
		if (vertices.size() % 3 ||
			vertices.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
			throw std::invalid_argument("Invalid triangle batch size");
		SDL_Texture *texture = nullptr;
		if (key)
		{
			if (!pixels)
				throw std::invalid_argument("Texture has no CPU pixels");
			auto version = revisions.find(key);
			if (version == revisions.end() || version->second != revision)
				forget(key);
			auto found = textures.find(key);
			if (found == textures.end())
			{
				texture = SDL_CreateTextureFromSurface(renderer, pixels);
				if (!texture)
					throw std::runtime_error(SDL_GetError());
				textures.emplace(key, texture);
				revisions[key] = revision;
				check(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND));
				check(SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest));
			}
			else
				texture = found->second;
		}
		if (applyTransform && (scale != 1 || offsetX != 0 || offsetY != 0))
		{
			transformed.assign(vertices.begin(), vertices.end());
			for (auto &vertex : transformed)
			{
				vertex.position.x = vertex.position.x * scale + offsetX;
				vertex.position.y = vertex.position.y * scale + offsetY;
			}
			vertices = transformed;
		}
		check(SDL_RenderGeometry(renderer, texture, vertices.data(),
								 static_cast<int>(vertices.size()), nullptr, 0));
	}
	void blit(const void *key, SDL_Surface *pixels, std::uint64_t revision, bool,
			  const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) override
	{
		++counts.blits;
		const SDL_Color c{255, 255, 255, alpha};
		const float u0 = float(src.x) / pixels->w, v0 = float(src.y) / pixels->h;
		const float u1 = float(src.x + src.w) / pixels->w, v1 = float(src.y + src.h) / pixels->h;
		const SDL_Vertex a{{dst.x, dst.y}, c, {u0, v0}}, b{{dst.x + dst.w, dst.y}, c, {u1, v0}},
			d{{dst.x, dst.y + dst.h}, c, {u0, v1}}, e{{dst.x + dst.w, dst.y + dst.h}, c, {u1, v1}};
		const SDL_Vertex vertices[] = {a, b, e, a, e, d};
		submit(vertices, key, pixels, revision, true);
	}
	void fill(const SDL_FRect &rect, SDL_Color c) override
	{
		++counts.fills;
		const SDL_Vertex a{{rect.x, rect.y}, c, {0, 0}}, b{{rect.x + rect.w, rect.y}, c, {0, 0}},
			d{{rect.x, rect.y + rect.h}, c, {0, 0}},
			e{{rect.x + rect.w, rect.y + rect.h}, c, {0, 0}};
		const SDL_Vertex vertices[] = {a, b, e, a, e, d};
		submit(vertices, nullptr, nullptr, 0, true);
	}
	RenderOperations operations() const override { return counts; }
	void forget(const void *key) override
	{
		auto found = textures.find(key);
		if (found != textures.end())
		{
			SDL_DestroyTexture(found->second);
			textures.erase(found);
		}
		revisions.erase(key);
	}
	void reset() override
	{
		for (auto [key, texture] : textures)
			SDL_DestroyTexture(texture);
		textures.clear();
		revisions.clear();
	}
	void clear()
	{
		check(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
		check(SDL_RenderClear(renderer));
	}
	void logicalSize(int width, int height) override
	{
		check(SDL_RenderSetLogicalSize(renderer, width, height));
		clear();
	}
	void flush() override { check(SDL_RenderFlush(renderer)); }
	void present() override
	{
		SDL_RenderPresent(renderer);
		clear();
	}
	void outputSize(int &width, int &height) override
	{
		check(SDL_GetRendererOutputSize(renderer, &width, &height));
	}
	SDL_Surface *capture() override
	{
		int width, height;
		outputSize(width, height);
		SDL_Surface *pixels =
			SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
		if (!pixels)
			throw std::runtime_error(SDL_GetError());
		if (SDL_RenderReadPixels(renderer, nullptr, pixels->format->format, pixels->pixels,
								 pixels->pitch) < 0)
		{
			SDL_FreeSurface(pixels);
			throw std::runtime_error(SDL_GetError());
		}
		return pixels;
	}
};
} // namespace
std::unique_ptr<RenderBackend> makeSDLRenderBackend(SDL_Window *window, int width, int height)
{
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (!renderer)
		return {};
	auto backend = std::make_unique<SDLRenderBackend>(renderer);
	backend->logicalSize(width, height);
	check(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND));
	return backend;
}

std::unique_ptr<RenderBackend> makeSDLSoftwareGeometryBackend(SDL_Surface *surface)
{
	auto *renderer = SDL_CreateSoftwareRenderer(surface);
	if (!renderer)
		throw std::runtime_error(SDL_GetError());
	auto backend = std::make_unique<SDLRenderBackend>(renderer);
	check(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND));
	return backend;
}

} // namespace GAGCore
