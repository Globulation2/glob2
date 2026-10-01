// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include <RenderBackend.h>
#include <SurfaceRaster.h>
#include <FileManager.h>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace GAGCore
{
// The retained SDL triangle implementation is the pixel reference for integer
// transforms; it also remains the production fallback for arbitrary geometry.
std::unique_ptr<RenderBackend> makeSDLSoftwareGeometryBackend(SDL_Surface *surface);
} // namespace GAGCore
using namespace GAGCore;
namespace
{
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)>;
Surface pixels(int width, int height)
{
	Surface result(SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888),
				   SDL_FreeSurface);
	REQUIRE(result);
	return result;
}
Uint32 pixel(SDL_Surface *surface, int x, int y)
{
	Uint32 value;
	std::memcpy(&value, static_cast<char *>(surface->pixels) + y * surface->pitch + x * 4, 4);
	return value;
}
std::vector<Uint32> snapshot(SDL_Surface *surface)
{
	std::vector<Uint32> result;
	for (int y = 0; y < surface->h; ++y)
		for (int x = 0; x < surface->w; ++x)
			result.push_back(pixel(surface, x, y));
	return result;
}
} // namespace
TEST_SUITE("SoftwareRenderer")
{
	TEST_CASE("nearest blits retain sampling under destination clipping and restore source state")
	{
		auto source = pixels(8, 8), full = pixels(32, 32), clipped = pixels(32, 32);
		for (int y = 0; y < 8; ++y)
			for (int x = 0; x < 8; ++x)
			{
				Uint32 value =
					SDL_MapRGBA(source->format, x * 31, y * 31, 17, (x + y) % 3 ? 255 : 0);
				std::memcpy(static_cast<char *>(source->pixels) + y * source->pitch + x * 4, &value,
							4);
			}
		SDL_SetSurfaceBlendMode(source.get(), SDL_BLENDMODE_ADD);
		SDL_SetSurfaceAlphaMod(source.get(), 79);
		for (Uint8 opacity : {0, 127, 255})
		{
			SDL_FillRect(full.get(), nullptr, 0xff123456);
			SDL_SetClipRect(clipped.get(), nullptr);
			SDL_FillRect(clipped.get(), nullptr, 0xff123456);
			SDL_Rect clip{3, 4, 13, 14};
			SDL_SetClipRect(clipped.get(), &clip);
			for (auto *target : {full.get(), clipped.get()})
				SurfaceRaster::blit(target, source.get(), SDL_Rect{1, 1, 6, 6},
									SDL_Rect{-5, -3, 27, 29}, opacity, false,
									SurfaceRaster::BlitBlend::Surface);
			for (int y = 4; y < 18; ++y)
				for (int x = 3; x < 16; ++x)
					CHECK(pixel(full.get(), x, y) == pixel(clipped.get(), x, y));
			SDL_BlendMode blend;
			Uint8 alpha;
			SDL_GetSurfaceBlendMode(source.get(), &blend);
			SDL_GetSurfaceAlphaMod(source.get(), &alpha);
			CHECK(blend == SDL_BLENDMODE_ADD);
			CHECK(alpha == 79);
		}
	}
	TEST_CASE("fractional shared endpoints leave no seams; queued triangles obey direct writes")
	{
		auto target = pixels(64, 48);
		auto backend = makeSoftwareRenderBackend(target.get());
		for (float zoom : {0.33f, 0.5f, 1.0f, 1.3f, 2.0f})
		{
			SDL_FillRect(target.get(), nullptr, 0xff000000);
			backend->transform(zoom, -0.5f, 0.5f, nullptr);
			backend->fill(SDL_FRect{0, 0, 16, 16}, SDL_Color{255, 0, 0, 255});
			backend->fill(SDL_FRect{16, 0, 16, 16}, SDL_Color{0, 255, 0, 255});
			const int end = int(std::floor(32 * zoom));
			for (int x = 0; x < end; ++x)
				CHECK(pixel(target.get(), x, 1) != 0xff000000);
		}
		backend->transform(1, 0, 0, nullptr);
		const SDL_Color blue{0, 0, 255, 255};
		const SDL_Vertex triangle[] = {
			{{0, 0}, blue, {}}, {{40, 0}, blue, {}}, {{0, 40}, blue, {}}};
		backend->triangles(triangle);
		backend->fill(SDL_FRect{0, 0, 10, 10}, SDL_Color{255, 0, 0, 255});
		CHECK(pixel(target.get(), 2, 2) == 0xffff0000);
		CHECK(pixel(target.get(), 12, 2) == 0xff0000ff);
		auto replacement = pixels(64, 48);
		backend->triangles(triangle);
		backend->bindTarget(replacement.get());
		backend->fill(SDL_FRect{0, 0, 10, 10}, SDL_Color{0, 255, 0, 255});
		CHECK(pixel(target.get(), 2, 2) == 0xff0000ff);
		CHECK(pixel(replacement.get(), 2, 2) == 0xff00ff00);
	}
	TEST_CASE(
		"opacity classification follows content revisions; opaque copy matches alpha blending")
	{
		GraphicContext context(64, 48, 0, "Software opacity test");
		DrawableSurface source(16, 16);
		source.drawFilledRect(0, 0, 16, 16, Color(51, 92, 143));
		CHECK(source.hasOpaquePixels());
		const auto revision = source.contentRevision();
		source.drawPixel(2, 2, Color(51, 92, 143, 127));
		CHECK(source.contentRevision() > revision);
		CHECK_FALSE(source.hasOpaquePixels());
		source.drawFilledRect(0, 0, 16, 16, Color(51, 92, 143));
		auto copy = pixels(16, 16), blend = pixels(16, 16);
		SDL_FillRect(copy.get(), nullptr, 0xff1b2c3d);
		SDL_FillRect(blend.get(), nullptr, 0xff1b2c3d);
		SurfaceRaster::blit(copy.get(), source.getSDLSurface(), SDL_Rect{0, 0, 16, 16},
							SDL_Rect{0, 0, 16, 16}, 255, source.hasOpaquePixels(),
							SurfaceRaster::BlitBlend::Surface);
		SurfaceRaster::blit(blend.get(), source.getSDLSurface(), SDL_Rect{0, 0, 16, 16},
							SDL_Rect{0, 0, 16, 16}, 255, false, SurfaceRaster::BlitBlend::Surface);
		CHECK(snapshot(copy.get()) == snapshot(blend.get()));
	}
	TEST_CASE("opaque row copies retain SDL clipping and color modulation")
	{
		auto source = pixels(8, 8), reference = pixels(20, 20), actual = pixels(20, 20);
		for (int y = 0; y < 8; ++y)
			for (int x = 0; x < 8; ++x)
			{
				const Uint32 value = SDL_MapRGBA(source->format, 31 * x, 29 * y, 137, 255);
				std::memcpy(static_cast<char *>(source->pixels) + y * source->pitch + x * 4, &value,
							4);
			}
		SDL_SetSurfaceBlendMode(source.get(), SDL_BLENDMODE_BLEND);
		for (Uint8 modulation : {127, 255})
			for (int sx : {-2, 0, 3})
				for (int dx : {-5, 0, 7})
				{
					SDL_SetSurfaceColorMod(source.get(), modulation, 255, modulation);
					SDL_Rect src{sx, -1, 9, 8}, destination{dx, 3, 9, 8};
					for (auto *target : {reference.get(), actual.get()})
					{
						SDL_SetClipRect(target, nullptr);
						SDL_FillRect(target, nullptr, 0xff376b91);
						SDL_Rect clip{2, 2, 12, 13};
						SDL_SetClipRect(target, &clip);
					}
					SurfaceRaster::blit(actual.get(), source.get(), src, destination, 255, true,
										SurfaceRaster::BlitBlend::Native);
					SDL_BlitSurface(source.get(), &src, reference.get(), &destination);
					CHECK(snapshot(actual.get()) == snapshot(reference.get()));
				}
	}
	TEST_CASE("integer transformed primitives retain SDL triangle blending exactly")
	{
		auto source = pixels(8, 8), expected = pixels(32, 32), actual = pixels(32, 32);
		for (int y = 0; y < 8; ++y)
			for (int x = 0; x < 8; ++x)
			{
				const Uint32 value = SDL_MapRGBA(source->format, 31 * x, 29 * y, 137,
												 (x + y) % 4 == 0   ? 0
												 : (x + y) % 4 == 1 ? 63
												 : (x + y) % 4 == 2 ? 127
																	: 255);
				std::memcpy(static_cast<char *>(source->pixels) + y * source->pitch + x * 4, &value,
							4);
			}
		auto reference = makeSDLSoftwareGeometryBackend(expected.get());
		auto optimized = makeSoftwareRenderBackend(actual.get());
		// Geometry ignores source SDL modulation; explicit vertex opacity wins.
		SDL_SetSurfaceColorMod(source.get(), 127, 79, 191);
		SDL_SetSurfaceAlphaMod(source.get(), 79);
		for (Uint8 alpha : {0, 63, 127, 128, 255})
		{
			for (auto *target : {expected.get(), actual.get()})
				SDL_FillRect(target, nullptr, 0xff376b91);
			for (auto *backend : {reference.get(), optimized.get()})
			{
				backend->transform(1, 3, 2, nullptr);
				backend->blit(source.get(), source.get(), 1, false, SDL_Rect{1, 1, 6, 6},
							  SDL_FRect{2, 2, 6, 6}, alpha);
				backend->fill(SDL_FRect{11, 3, 9, 11}, SDL_Color{151, 89, 43, alpha});
				backend->flush();
			}
			CHECK(snapshot(actual.get()) == snapshot(expected.get()));
		}
	}
	TEST_CASE("geometry-reference blits preserve mixed pixel formats and source modulation")
	{
		Surface source(SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_RGBA32),
					   SDL_FreeSurface);
		REQUIRE(source);
		auto expected = pixels(20, 20), actual = pixels(20, 20);
		auto reference = makeSDLSoftwareGeometryBackend(expected.get());
		auto optimized = makeSoftwareRenderBackend(actual.get());
		SDL_SetSurfaceColorMod(source.get(), 127, 79, 191);
		SDL_SetSurfaceAlphaMod(source.get(), 79);
		for (Uint8 sourceAlpha : {127, 255})
			for (Uint8 drawAlpha : {127, 255})
			{
				SDL_FillRect(source.get(), nullptr,
							 SDL_MapRGBA(source->format, 151, 89, 43, sourceAlpha));
				for (auto *target : {expected.get(), actual.get()})
					SDL_FillRect(target, nullptr, 0xff376b91);
				for (auto *backend : {reference.get(), optimized.get()})
				{
					backend->blit(source.get(), source.get(), sourceAlpha, sourceAlpha == 255,
								  SDL_Rect{0, 0, 8, 8}, SDL_FRect{2, 2, 8, 8}, drawAlpha);
					backend->flush();
				}
				CHECK(snapshot(actual.get()) == snapshot(expected.get()));
			}
	}
	TEST_CASE("large owned images retain SDL geometry sampling and overflow behavior")
	{
		auto source = pixels(512, 512);
		auto expected = pixels(1000, 700), actual = pixels(1000, 700);
		SDL_FillRect(source.get(), nullptr, 0xff4939d5);
		auto reference = makeSDLSoftwareGeometryBackend(expected.get());
		auto optimized = makeSoftwareRenderBackend(actual.get());
		for (float scale : {0.5f, 1.f, 2.f})
		{
			for (auto *target : {expected.get(), actual.get()})
				SDL_FillRect(target, nullptr, 0xff123456);
			for (auto *backend : {reference.get(), optimized.get()})
			{
				backend->transform(scale, -44, -44, nullptr);
				backend->blit(source.get(), source.get(), 1, true, SDL_Rect{0, 0, 512, 512},
					SDL_FRect{-96, -288, 512, 512}, 255);
				backend->flush();
			}
			CHECK(snapshot(actual.get()) == snapshot(expected.get()));
		}
	}
	TEST_CASE("translucent fills preserve colors on 32-bit targets without alpha")
	{
		Surface expected(SDL_CreateRGBSurfaceWithFormat(0, 16, 16, 32, SDL_PIXELFORMAT_RGB888),
						 SDL_FreeSurface);
		Surface actual(SDL_CreateRGBSurfaceWithFormat(0, 16, 16, 32, SDL_PIXELFORMAT_RGB888),
					   SDL_FreeSurface);
		REQUIRE(expected);
		REQUIRE(actual);
		auto reference = makeSDLSoftwareGeometryBackend(expected.get());
		auto optimized = makeSoftwareRenderBackend(actual.get());
		for (Uint8 alpha : {0, 63, 127, 255})
		{
			for (auto *target : {expected.get(), actual.get()})
				SDL_FillRect(target, nullptr, SDL_MapRGB(target->format, 71, 83, 29));
			for (auto *backend : {reference.get(), optimized.get()})
			{
				backend->fill(SDL_FRect{2, 2, 8, 8}, SDL_Color{151, 89, 43, alpha});
				backend->flush();
			}
			Uint8 er, eg, eb, ar, ag, ab;
			SDL_GetRGB(pixel(expected.get(), 3, 3), expected->format, &er, &eg, &eb);
			SDL_GetRGB(pixel(actual.get(), 3, 3), actual->format, &ar, &ag, &ab);
			CHECK(er == ar);
			CHECK(eg == ag);
			CHECK(eb == ab);
		}
	}
}
