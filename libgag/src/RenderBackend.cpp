// SPDX-License-Identifier: GPL-3.0-or-later
#include <RenderBackend.h>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <optional>
#include <vector>
#include <stdexcept>
#include <unordered_map>

namespace GAGCore
{
namespace
{
bool update(SDL_Texture *texture, SDL_Surface *pixels)
{
	if (!SDL_LockSurface(pixels))
		return false;
	const bool result = SDL_UpdateTexture(texture, nullptr, pixels->pixels, pixels->pitch);
	SDL_UnlockSurface(pixels);
	return result;
}
void check(bool result)
{
	if (!result)
		throw std::runtime_error(SDL_GetError());
}
bool gpuPreparationEnabled(SDL_Renderer *renderer)
{
	const char *disabled = std::getenv("GLOB2_DISABLE_GPU_TEXTURE_PREPARATION");
	return std::strcmp(SDL_GetRendererName(renderer), "software") != 0 && (!disabled || !*disabled);
}
class SDLRenderBackend final : public RenderBackend
{
	SDL_Renderer *renderer;
	float scale = 1, offsetX = 0, offsetY = 0;
	std::optional<SDL_Rect> bounds;
	std::vector<SDL_Vertex> transformed;
	struct Texture
	{
		SDL_Texture *handle;
		std::uint64_t revision;
		bool linear, gpu;
		int width, height;
		SDL_PixelFormat format;
	};
	std::unordered_map<const void *, Texture> textures;
	SDL_Texture *uploadTexture = nullptr;
	bool gpuPreparation;

	// A single straight-alpha staging texture is reused; retained linear
	// textures contain premultiplied pixels. No CPU copy or pixel loop is needed.
	bool premultiply(SDL_Texture *target, SDL_Surface *pixels)
	{
		if (uploadTexture && (uploadTexture->w != pixels->w || uploadTexture->h != pixels->h))
		{
			SDL_DestroyTexture(uploadTexture);
			uploadTexture = nullptr;
		}
		if (!uploadTexture)
		{
			uploadTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
											  SDL_TEXTUREACCESS_STATIC, pixels->w, pixels->h);
			if (!uploadTexture || !SDL_SetTextureBlendMode(uploadTexture, SDL_BLENDMODE_BLEND) ||
				!SDL_SetTextureScaleMode(uploadTexture, SDL_SCALEMODE_NEAREST))
				return false;
		}
		if (!update(uploadTexture, pixels))
			return false;
		auto *previous = SDL_GetRenderTarget(renderer);
		SDL_FColor color;
		if (!SDL_GetRenderDrawColorFloat(renderer, &color.r, &color.g, &color.b, &color.a))
			return false;
		if (!SDL_SetRenderTarget(renderer, target))
			return false;
		// Viewport, clip, scale and logical presentation belong to each target
		// in SDL. Switching back restores the caller's state, including clips.
		const bool result = SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0) &&
							SDL_RenderClear(renderer) &&
							SDL_RenderTexture(renderer, uploadTexture, nullptr, nullptr);
		const bool restored = SDL_SetRenderTarget(renderer, previous);
		const bool colorRestored =
			SDL_SetRenderDrawColorFloat(renderer, color.r, color.g, color.b, color.a);
		check(restored && colorRestored);
		return result;
	}
	RenderOperations counts;

  public:
	explicit SDLRenderBackend(SDL_Renderer *renderer)
		: renderer(renderer), gpuPreparation(gpuPreparationEnabled(renderer))
	{
	}
	~SDLRenderBackend() override
	{
		reset();
		SDL_DestroyRenderer(renderer);
	}
	int maximumTextureSize() const override
	{
		return int(SDL_GetNumberProperty(SDL_GetRendererProperties(renderer),
										 SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0));
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
			if (bounds && !SDL_GetRectIntersection(&mapped, &*bounds, &mapped))
				mapped = {}; // SDL treats negative clip extents as clipping disabled.
			result = mapped;
		}
		if (result && (result->w <= 0 || result->h <= 0))
			result = SDL_Rect{};
		check(SDL_SetRenderClipRect(renderer, result ? &*result : nullptr));
	}
	SDL_Texture *textureFor(const void *key, SDL_Surface *pixels, std::uint64_t revision,
							bool linear = false)
	{
		if (!pixels)
			throw std::invalid_argument("Texture has no CPU pixels");
		auto found = textures.find(key);
		if (found != textures.end() &&
			(found->second.linear != linear || found->second.width != pixels->w ||
			 found->second.height != pixels->h ||
			 (found->second.gpu && SDL_SurfaceHasColorKey(pixels)) ||
			 found->second.format != pixels->format))
		{
			forget(key);
			found = textures.end();
		}
		if (found != textures.end() && found->second.revision == revision)
			return found->second.handle;
		SDL_Texture *texture = found == textures.end() ? nullptr : found->second.handle;
		bool gpu = found != textures.end() && found->second.gpu;
		if (linear && (gpuPreparation || gpu) && pixels->format == SDL_PIXELFORMAT_ARGB8888 &&
			!SDL_SurfaceHasColorKey(pixels) && (!texture || gpu))
		{
			if (!texture)
				texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
											SDL_TEXTUREACCESS_TARGET, pixels->w, pixels->h);
			if (texture && SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED) &&
				SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR) &&
				premultiply(texture, pixels))
				gpu = true;
			else
			{
				if (found != textures.end())
					textures.erase(found);
				SDL_DestroyTexture(texture);
				texture = nullptr;
				// Unsupported target/blend combinations retain the CPU path.
				gpuPreparation = false;
				gpu = false;
			}
		}
		if (!gpu)
		{
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> converted(
				nullptr, SDL_DestroySurface);
			if (linear)
			{
				converted.reset(SDL_ConvertSurface(pixels, SDL_PIXELFORMAT_ARGB8888));
				if (!converted || !SDL_LockSurface(converted.get()))
					throw std::runtime_error(SDL_GetError());
				for (int y = 0; y < converted->h; ++y)
					for (int x = 0; x < converted->w; ++x)
					{
						auto &p = reinterpret_cast<Uint32 *>(
							static_cast<Uint8 *>(converted->pixels) + y * converted->pitch)[x];
						const Uint32 a = p >> 24;
						p = (a << 24) | (((((p >> 16) & 255) * a + 127) / 255) << 16) |
							(((((p >> 8) & 255) * a + 127) / 255) << 8) |
							(((p & 255) * a + 127) / 255);
					}
				SDL_UnlockSurface(converted.get());
			}
			auto *source = linear ? converted.get() : pixels;
			if (texture &&
				(SDL_SurfaceHasColorKey(source) ||
				 SDL_GetNumberProperty(SDL_GetTextureProperties(texture),
									   SDL_PROP_TEXTURE_FORMAT_NUMBER, 0) != source->format))
			{
				SDL_DestroyTexture(texture);
				texture = nullptr;
			}
			if (texture)
				check(update(texture, source));
			else
				texture = SDL_CreateTextureFromSurface(renderer, source);
			if (!texture)
				throw std::runtime_error(SDL_GetError());
			check(SDL_SetTextureBlendMode(texture, linear ? SDL_BLENDMODE_BLEND_PREMULTIPLIED
														  : SDL_BLENDMODE_BLEND));
			check(SDL_SetTextureScaleMode(texture,
										  linear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST));
		}
		textures[key] = {texture, revision, linear, gpu, pixels->w, pixels->h, pixels->format};
		return texture;
	}
	void prepareTexture(const void *key, SDL_Surface *pixels, std::uint64_t revision,
						bool linear = false) override
	{
		textureFor(key, pixels, revision, linear);
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
				std::uint64_t revision, bool applyTransform, bool linear = false)
	{
		if (vertices.empty())
			return;
		++counts.triangles;
		if (vertices.size() % 3 ||
			vertices.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
			throw std::invalid_argument("Invalid triangle batch size");
		SDL_Texture *texture = nullptr;
		if (key)
			texture = textureFor(key, pixels, revision, linear);
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
	void blitLinear(const void *key, SDL_Surface *pixels, std::uint64_t revision,
					const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) override
	{
		++counts.blits;
		const float opacity = alpha / 255.f;
		const SDL_FColor c{opacity, opacity, opacity, opacity};
		const float u0 = float(src.x) / pixels->w, v0 = float(src.y) / pixels->h;
		const float u1 = float(src.x + src.w) / pixels->w, v1 = float(src.y + src.h) / pixels->h;
		const SDL_Vertex a{{dst.x, dst.y}, c, {u0, v0}}, b{{dst.x + dst.w, dst.y}, c, {u1, v0}},
			d{{dst.x, dst.y + dst.h}, c, {u0, v1}}, e{{dst.x + dst.w, dst.y + dst.h}, c, {u1, v1}};
		const SDL_Vertex vertices[] = {a, b, e, a, e, d};
		submit(vertices, key, pixels, revision, true, true);
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
			SDL_DestroyTexture(found->second.handle);
			textures.erase(found);
		}
	}
	void reset() override
	{
		for (auto [key, texture] : textures)
			SDL_DestroyTexture(texture.handle);
		textures.clear();
		SDL_DestroyTexture(uploadTexture);
		uploadTexture = nullptr;
		gpuPreparation = gpuPreparationEnabled(renderer);
	}
	void clear()
	{
		check(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
		check(SDL_RenderClear(renderer));
	}
	void logicalSize(int width, int height) override
	{
		check(SDL_SetRenderLogicalPresentation(renderer, width, height,
											   SDL_LOGICAL_PRESENTATION_LETTERBOX));
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
		if (!pixels)
			throw std::runtime_error(SDL_GetError());
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
