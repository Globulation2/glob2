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
void check(bool result)
{
	if (!result)
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
				SDL_GetRectIntersection(&mapped, &*bounds, &mapped);
			result = mapped;
		}
		check(SDL_SetRenderClipRect(renderer, result ? &*result : nullptr));
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
				check(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST));
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
		const SDL_FColor c{1, 1, 1, alpha / 255.0f};
		const float u0 = float(src.x) / pixels->w, v0 = float(src.y) / pixels->h;
		const float u1 = float(src.x + src.w) / pixels->w, v1 = float(src.y + src.h) / pixels->h;
		const SDL_Vertex a{{dst.x, dst.y}, c, {u0, v0}}, b{{dst.x + dst.w, dst.y}, c, {u1, v0}},
			d{{dst.x, dst.y + dst.h}, c, {u0, v1}}, e{{dst.x + dst.w, dst.y + dst.h}, c, {u1, v1}};
		const SDL_Vertex vertices[] = {a, b, e, a, e, d};
		submit(vertices, key, pixels, revision, true);
	}
	void fill(const SDL_FRect &rect, SDL_Color color) override
	{
		const SDL_FColor c{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
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
		check(SDL_SetRenderLogicalPresentation(renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX));
		clear();
	}
	void nativeLogicalSize(int width, int height) override
	{
		int pixelsW, pixelsH;
		outputSize(pixelsW, pixelsH);
		check(SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED));
		check(SDL_SetRenderScale(renderer, float(pixelsW) / width, float(pixelsH) / height));
		// Lazy CPU geometry may be created after direct writes. Scale setup
		// must not clear the borrowed target or overwrite earlier layers.
	}
	void flush() override { check(SDL_FlushRenderer(renderer)); }
	void present() override
	{
		SDL_RenderPresent(renderer);
		clear();
	}
	void outputSize(int &width, int &height) override
	{
		check(SDL_GetRenderOutputSize(renderer, &width, &height));
	}
	SDL_Surface *capture() override
	{
		SDL_Surface *pixels = SDL_RenderReadPixels(renderer, nullptr);
		if (!pixels) throw std::runtime_error(SDL_GetError());
		return pixels;
	}
};
} // namespace
std::unique_ptr<RenderBackend> makeSDLRenderBackend(SDL_Window *window, int width, int height)
{
	SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
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
