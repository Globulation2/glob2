// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "render/SoftwareTerrainCache.h"
#include "scene/SceneMap.h"
#include <RenderBackend.h>
#include "ScopedEnvironment.h"
#include "ComputeExecutor.h"
#include <PresentationWork.h>
#include <SoftwareFramePresenter.h>
#include <SurfaceRaster.h>
#include <FileManager.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <cmath>
#include <cstring>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
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
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
Surface pixels(int width, int height)
{
	Surface result(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
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
// Deliberately scalar RGBA reference: no production raster helpers or packed
// lane arithmetic, so scheduling and optimized kernels share an external oracle.
void referenceOver(SDL_Surface *target, int x, int y, SDL_Color source, Uint8 opacity)
{
    Uint8 r, g, b, a;
    SDL_GetRGBA(pixel(target, x, y), SDL_GetPixelFormatDetails(target->format), nullptr,
                &r, &g, &b, &a);
    const unsigned alpha = unsigned(source.a) * opacity / 255;
    const unsigned inverse = 255 - alpha;
    const auto channel = [&](unsigned src, unsigned dst)
    { return Uint8((src * alpha + dst * inverse) / 255); };
    const Uint32 result = SDL_MapSurfaceRGBA(target, channel(source.r, r), channel(source.g, g),
        channel(source.b, b), Uint8(alpha + unsigned(a) * inverse / 255));
    std::memcpy(static_cast<Uint8 *>(target->pixels) + y * target->pitch + x * 4, &result, 4);
}
void referenceFill(SDL_Surface *target, SDL_Rect rect, SDL_Color color,
                   SDL_Rect clip = {0, 0, 1000000, 1000000})
{
    const int left = std::max({0, rect.x, clip.x});
    const int top = std::max({0, rect.y, clip.y});
    const int right = std::min({target->w, rect.x + rect.w, clip.x + clip.w});
    const int bottom = std::min({target->h, rect.y + rect.h, clip.y + clip.h});
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            referenceOver(target, x, y, {color.r, color.g, color.b, 255}, color.a);
}
void referenceNearest(SDL_Surface *target, SDL_Surface *source, SDL_Rect src, SDL_Rect dst,
                      Uint8 opacity, bool opaque = false,
                      SDL_Rect clip = {0, 0, 1000000, 1000000}, bool legacySampling = false,
                      SDL_Color tint = {255, 255, 255, 255})
{
    for (int y = 0; y < target->h; ++y)
        for (int x = 0; x < target->w; ++x)
        {
            if (x < dst.x || x >= dst.x + dst.w || y < dst.y || y >= dst.y + dst.h ||
                x < clip.x || x >= clip.x + clip.w || y < clip.y || y >= clip.y + clip.h)
                continue;
            const auto sample = [legacySampling](int position, int sourceExtent, int destinationExtent)
            {
                if (!legacySampling)
                    return int((std::int64_t(2 * position + 1) * sourceExtent) / (2 * destinationExtent));
                const auto step = (std::uint64_t(sourceExtent) << 16) / destinationExtent;
                return int((step / 2 + std::uint64_t(position) * step) >> 16);
            };
            const int sx = src.x + sample(x - dst.x, src.w, dst.w);
            const int sy = src.y + sample(y - dst.y, src.h, dst.h);
            if (sx < 0 || sx >= source->w || sy < 0 || sy >= source->h) continue;
            SDL_Color color;
            REQUIRE(SDL_ReadSurfacePixel(source, sx, sy, &color.r, &color.g, &color.b, &color.a));
            if (opaque) color.a = 255;
            color.r = Uint8(unsigned(color.r) * tint.r / 255);
            color.g = Uint8(unsigned(color.g) * tint.g / 255);
            color.b = Uint8(unsigned(color.b) * tint.b / 255);
            referenceOver(target, x, y, color, opacity);
        }
}
std::array<SDL_Vertex, 6> rectangleVertices(SDL_FRect dst, SDL_FColor color,
                                             SDL_FRect uv = {0, 0, 1, 1})
{
    const SDL_Vertex a{{dst.x, dst.y}, color, {uv.x, uv.y}};
    const SDL_Vertex b{{dst.x + dst.w, dst.y}, color, {uv.x + uv.w, uv.y}};
    const SDL_Vertex c{{dst.x + dst.w, dst.y + dst.h}, color, {uv.x + uv.w, uv.y + uv.h}};
    const SDL_Vertex d{{dst.x, dst.y + dst.h}, color, {uv.x, uv.y + uv.h}};
    return {a, b, c, a, c, d};
}
// SDL's old separate-term and SIMD arithmetic may differ by one channel unit
// for an isolated draw. Keep that allowance local, with exact untouched pixels.
void checkIsolatedSDLChannels(SDL_Surface *actual, SDL_Surface *legacy, Uint32 background)
{
    REQUIRE(actual->w == legacy->w);
    REQUIRE(actual->h == legacy->h);
    for (int y = 0; y < actual->h; ++y)
        for (int x = 0; x < actual->w; ++x)
        {
            if (pixel(legacy, x, y) == background)
            {
                CHECK(pixel(actual, x, y) == background);
                continue;
            }
            Uint8 ar, ag, ab, aa, er, eg, eb, ea;
            REQUIRE(SDL_ReadSurfacePixel(actual, x, y, &ar, &ag, &ab, &aa));
            REQUIRE(SDL_ReadSurfacePixel(legacy, x, y, &er, &eg, &eb, &ea));
            CHECK(std::abs(int(ar) - er) <= 1);
            CHECK(std::abs(int(ag) - eg) <= 1);
            CHECK(std::abs(int(ab) - eb) <= 1);
            CHECK(aa == ea);
        }
}
} // namespace
TEST_SUITE("SoftwareRenderer")
{
	TEST_CASE(
		"native display scaling preserves direct pixels through lazy geometry and target rotation")
	{
		auto first = pixels(64, 72), second = pixels(32, 48);
		auto backend = makeSoftwareRenderBackend(first.get());
		backend->nativeLogicalSize(32, 24);
		for (auto *target : {first.get(), second.get()})
		{
			backend->bindTarget(target);
			SDL_FillSurfaceRect(target, nullptr, SDL_MapSurfaceRGBA(target, 0, 0, 0, 255));
			backend->fill(SDL_FRect{1, 1, 3, 3}, SDL_Color{255, 0, 0, 255});
			const SDL_FColor green{0, 1, 0, 1};
			const SDL_Vertex triangle[] = {
				{{8, 8}, green, {}}, {{12, 8}, green, {}}, {{8, 12}, green, {}}};
			backend->triangles(triangle);
			backend->flush();
			CHECK(pixel(target, target->w * 3 / 64, target->h * 4 / 72) ==
				  SDL_MapSurfaceRGBA(target, 255, 0, 0, 255));
			CHECK(pixel(target, target->w * 18 / 64, target->h * 27 / 72) ==
				  SDL_MapSurfaceRGBA(target, 0, 255, 0, 255));
			CHECK(pixel(target, target->w * 40 / 64, target->h * 40 / 72) ==
				  SDL_MapSurfaceRGBA(target, 0, 0, 0, 255));
		}
	}
	TEST_CASE("nearest blits retain sampling under destination clipping and restore source state")
	{
		auto source = pixels(8, 8), full = pixels(32, 32), clipped = pixels(32, 32);
		for (int y = 0; y < 8; ++y)
			for (int x = 0; x < 8; ++x)
			{
				Uint32 value =
					SDL_MapSurfaceRGBA(source.get(), x * 31, y * 31, 17, (x + y) % 3 ? 255 : 0);
				std::memcpy(static_cast<char *>(source->pixels) + y * source->pitch + x * 4, &value,
							4);
			}
		SDL_SetSurfaceBlendMode(source.get(), SDL_BLENDMODE_ADD);
		SDL_SetSurfaceAlphaMod(source.get(), 79);
		for (Uint8 opacity : {0, 127, 255})
		{
			SDL_FillSurfaceRect(full.get(), nullptr, 0xff123456);
			SDL_SetSurfaceClipRect(clipped.get(), nullptr);
			SDL_FillSurfaceRect(clipped.get(), nullptr, 0xff123456);
			SDL_Rect clip{3, 4, 13, 14};
			SDL_SetSurfaceClipRect(clipped.get(), &clip);
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
			SDL_FillSurfaceRect(target.get(), nullptr, 0xff000000);
			backend->transform(zoom, -0.5f, 0.5f, nullptr);
			backend->fill(SDL_FRect{0, 0, 16, 16}, SDL_Color{255, 0, 0, 255});
			backend->fill(SDL_FRect{16, 0, 16, 16}, SDL_Color{0, 255, 0, 255});
			backend->flush(); // Fence before observing the borrowed target directly.
			const int end = int(std::floor(32 * zoom));
			for (int x = 0; x < end; ++x)
				CHECK(pixel(target.get(), x, 1) != 0xff000000);
		}
		backend->transform(1, 0, 0, nullptr);
		const SDL_FColor blue{0, 0, 1, 1};
		const SDL_Vertex triangle[] = {
			{{0, 0}, blue, {}}, {{40, 0}, blue, {}}, {{0, 40}, blue, {}}};
		backend->triangles(triangle);
		backend->fill(SDL_FRect{0, 0, 10, 10}, SDL_Color{255, 0, 0, 255});
		backend->flush();
		CHECK(pixel(target.get(), 2, 2) == 0xffff0000);
		CHECK(pixel(target.get(), 12, 2) == 0xff0000ff);
		auto replacement = pixels(64, 48);
		backend->triangles(triangle);
		backend->bindTarget(replacement.get());
		backend->fill(SDL_FRect{0, 0, 10, 10}, SDL_Color{0, 255, 0, 255});
		backend->flush();
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
		SDL_FillSurfaceRect(copy.get(), nullptr, 0xff1b2c3d);
		SDL_FillSurfaceRect(blend.get(), nullptr, 0xff1b2c3d);
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
				const Uint32 value = SDL_MapSurfaceRGBA(source.get(), 31 * x, 29 * y, 137, 255);
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
						SDL_SetSurfaceClipRect(target, nullptr);
						SDL_FillSurfaceRect(target, nullptr, 0xff376b91);
						SDL_Rect clip{2, 2, 12, 13};
						SDL_SetSurfaceClipRect(target, &clip);
					}
					SurfaceRaster::blit(actual.get(), source.get(), src, destination, 255, true,
										SurfaceRaster::BlitBlend::Native);
					SDL_BlitSurface(source.get(), &src, reference.get(), &destination);
					CHECK(snapshot(actual.get()) == snapshot(reference.get()));
				}
	}
	TEST_CASE("integer transformed primitives use portable blending and retain SDL coverage")
    {
        auto source = pixels(8, 8);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
            {
                const Uint32 value = SDL_MapSurfaceRGBA(source.get(), 31 * x, 29 * y, 137,
                    std::array<Uint8, 4>{0, 63, 127, 255}[(x + y) % 4]);
                std::memcpy(static_cast<Uint8 *>(source->pixels) + y * source->pitch + x * 4,
                            &value, 4);
            }
        REQUIRE(SDL_SetSurfaceColorMod(source.get(), 127, 79, 191));
        REQUIRE(SDL_SetSurfaceAlphaMod(source.get(), 79));
        for (const char *execution : {"immediate", "serial", "parallel"})
        {
            glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
            for (int scale : {1, 2})
                for (Uint8 alpha : {0, 63, 127, 128, 255})
                {
                    INFO("execution=", std::string(execution), " scale=", scale, " alpha=", int(alpha));
                    auto actual = pixels(48, 48), expected = pixels(48, 48), legacy = pixels(48, 48);
                    for (auto *target : {actual.get(), expected.get(), legacy.get()})
                        REQUIRE(SDL_FillSurfaceRect(target, nullptr, 0xff376b91));
                    auto backend = makeSoftwareRenderBackend(actual.get());
                    auto sdl = makeSDLSoftwareGeometryBackend(legacy.get());
                    for (auto *renderer : {backend.get(), sdl.get()})
                    {
                        renderer->transform(float(scale), 3, 2, nullptr);
                        renderer->blit(source.get(), source.get(), 1, false, {1, 1, 6, 6},
                                       {2, 2, 6, 6}, alpha);
                        renderer->fill({11, 3, 9, 11}, {151, 89, 43, alpha});
                        renderer->flush();
                    }
                    referenceNearest(expected.get(), source.get(), {1, 1, 6, 6},
                        {2 * scale + 3, 2 * scale + 2, 6 * scale, 6 * scale}, alpha, false,
                        {0, 0, 48, 48}, true);
                    referenceFill(expected.get(), {11 * scale + 3, 3 * scale + 2, 9 * scale, 11 * scale},
                                  {151, 89, 43, alpha});
                    CHECK(snapshot(actual.get()) == snapshot(expected.get()));
                    checkIsolatedSDLChannels(actual.get(), legacy.get(), 0xff376b91);
                }
        }
    }
    TEST_CASE("mixed packed pixel formats retain geometry modulation semantics")
    {
        Surface source(SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        REQUIRE(source);
        REQUIRE(SDL_SetSurfaceColorMod(source.get(), 127, 79, 191));
        REQUIRE(SDL_SetSurfaceAlphaMod(source.get(), 79));
        for (const char *execution : {"immediate", "serial", "parallel"})
        {
            glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
            for (Uint8 sourceAlpha : {127, 255})
                for (Uint8 drawAlpha : {127, 255})
                {
                    INFO("execution=", std::string(execution), " source alpha=", int(sourceAlpha),
                         " draw alpha=", int(drawAlpha));
                    REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr,
                        SDL_MapSurfaceRGBA(source.get(), 151, 89, 43, sourceAlpha)));
                    auto expected = pixels(20, 20), actual = pixels(20, 20), legacy = pixels(20, 20);
                    for (auto *target : {expected.get(), actual.get(), legacy.get()})
                        REQUIRE(SDL_FillSurfaceRect(target, nullptr, 0xff376b91));
                    auto backend = makeSoftwareRenderBackend(actual.get());
                    auto sdl = makeSDLSoftwareGeometryBackend(legacy.get());
                    for (auto *renderer : {backend.get(), sdl.get()})
                    {
                        renderer->blit(source.get(), source.get(), sourceAlpha, sourceAlpha == 255,
                                      {0, 0, 8, 8}, {2, 2, 8, 8}, drawAlpha);
                        renderer->flush();
                    }
                    referenceNearest(expected.get(), source.get(), {0, 0, 8, 8}, {2, 2, 8, 8}, drawAlpha, false, {0, 0, 20, 20}, true);
                    CHECK(snapshot(actual.get()) == snapshot(expected.get()));
                    checkIsolatedSDLChannels(actual.get(), legacy.get(), 0xff376b91);
                    CHECK(backend->operations().rasterFallbacks == 0);
                }
        }
    }
	TEST_CASE("large owned images retain SDL geometry sampling and overflow behavior")
	{
		auto source = pixels(512, 512);
		auto expected = pixels(1000, 700), actual = pixels(1000, 700);
		SDL_FillSurfaceRect(source.get(), nullptr, 0xff4939d5);
		auto reference = makeSDLSoftwareGeometryBackend(expected.get());
		auto optimized = makeSoftwareRenderBackend(actual.get());
		for (float scale : {0.5f, 1.f, 2.f})
		{
			for (auto *target : {expected.get(), actual.get()})
				SDL_FillSurfaceRect(target, nullptr, 0xff123456);
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
    TEST_CASE("translucent fills use portable source-over on targets without alpha")
    {
        for (const char *execution : {"immediate", "serial", "parallel"})
        {
            glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
            for (Uint8 alpha : {0, 63, 127, 255})
            {
                Surface expected(SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_XRGB8888), SDL_DestroySurface);
                Surface actual(SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_XRGB8888), SDL_DestroySurface);
                REQUIRE(expected); REQUIRE(actual);
                for (auto *target : {expected.get(), actual.get()})
                    REQUIRE(SDL_FillSurfaceRect(target, nullptr, SDL_MapSurfaceRGB(target, 71, 83, 29)));
                auto backend = makeSoftwareRenderBackend(actual.get());
                backend->fill({2, 2, 8, 8}, {151, 89, 43, alpha});
                backend->flush();
                referenceFill(expected.get(), {2, 2, 8, 8}, {151, 89, 43, alpha});
                for (int y = 0; y < 16; ++y)
                    for (int x = 0; x < 16; ++x)
                    {
                        Uint8 r, g, b, a, er, eg, eb, ea;
                        REQUIRE(SDL_ReadSurfacePixel(actual.get(), x, y, &r, &g, &b, &a));
                        REQUIRE(SDL_ReadSurfacePixel(expected.get(), x, y, &er, &eg, &eb, &ea));
                        CHECK(std::array<Uint8, 3>{r, g, b} == std::array<Uint8, 3>{er, eg, eb});
                    }
            }
        }
    }
	TEST_CASE("presenter rotates full frames and preserves completed pixels during partial updates")
	{
		auto callerOwned = pixels(4, 4);

		const auto failAllocation = [](int, int, SDL_PixelFormat) -> SDL_Surface *
		{
			SDL_SetError("Injected spare-buffer allocation failure");
			return nullptr;
		};
		// Failure leaves the caller's framebuffer available for legacy retention.
		CHECK_THROWS_AS((SoftwareFramePresenter{callerOwned.get(), failAllocation}),
						std::runtime_error);
		SDL_FillSurfaceRect(callerOwned.get(), nullptr, 0xff112233);
		CHECK(pixel(callerOwned.get(), 0, 0) == 0xff112233);

		auto initial = pixels(32, 32);
		SDL_FillSurfaceRect(initial.get(), nullptr, 0xff123456);
		SoftwareFramePresenter presenter(initial.release());
		auto *first = presenter.begin(false);
		presenter.complete();
		auto *second = presenter.begin(false);
		CHECK(first != second);
		CHECK(presenter.completed() == first);
		SDL_FillSurfaceRect(second, nullptr, 0xff654321);
		CHECK(pixel(presenter.completed(), 3, 3) == 0xff123456);
		presenter.complete();
		auto *partial = presenter.begin(true);
		CHECK(partial == first);
		CHECK(pixel(partial, 3, 3) == 0xff654321);
		SDL_Rect area{0, 0, 4, 4};
		SDL_FillSurfaceRect(partial, &area, 0xffabcdef);
		CHECK(pixel(presenter.completed(), 1, 1) == 0xff654321);
		presenter.complete();
		CHECK(pixel(presenter.completed(), 1, 1) == 0xffabcdef);
	}
	TEST_CASE("terrain cache matches integer-transformed coastlines and discovery across wraps and "
			  "mutations "
			  "[display][artifacts]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .teams = 2, .discovered = true});
		auto &game = fixture.game;
		// The view owns the cache and animation phase; the cache follows map replacement.
		Game::ViewState view;
		const auto cache = [&]() -> SoftwareTerrainCache & { return view.render.terrainCache(game.map.identity()); };
		SceneMap extracted;
		const auto sceneOf = [&](const Map &map) -> const SceneMap & { glob2test::observeMap(map,extracted); return extracted; };
		// Every classic corner pattern, on the vertex lattice.
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				game.map.setVertexTerrain(x, y, TerrainType((x * 7 + y * 3 + x * y) % 3));
		auto compare = [&](int vx, int vy, int team)
		{
			const auto checksum = fixture.checksum();
			std::vector<Uint32> expected;
			for (bool enabled : {false, true})
			{
				cache().enabled = enabled;
				globals->gfx->setClipRect();
				globals->gfx->drawFilledRect(0, 0, 640, 480, Color(11, 22, 33));
				globals->gfx->beginMapTransform(1, 1, 1, 0, 0, 640, 480);
				glob2test::drawMap(game,0, 0, 640, 480, 0, 0, vx, vy, team, view, Game::DRAW_NO_CLOUD_LAYER,
							 nullptr, nullptr, true);
				globals->gfx->endMapTransform();
				auto image = snapshot(globals->gfx->getSDLSurface());
				if (!enabled)
					expected = std::move(image);
				else
					CHECK(image == expected);
			}
			CHECK(fixture.checksum() == checksum);
			CHECK(cache().bytes() <= SoftwareTerrainCache::Budget);
		};
		compare(0, 0, 0);
		compare(29, 30, 0);
		compare(29, 30, 1);
		for (int phase : {16, 511, 512, 913})
		{
			view.render.animationTime = phase;
			compare(29, 30, 0);
		}
		// Every water phase: kept coverage of mixed water cells re-blends to the
		// same pixels as uncached composition.
		for (int time = 0; time < 96; time += 6)
		{
			view.render.animationTime = time;
			compare(29, 30, 0);
		}
		CHECK(cache().maskBytes() > 0);
		CHECK(cache().maskBytes() <= SoftwareTerrainCache::MaskBudget);
		game.map.unsetMapDiscovered();
		game.map.setMapDiscovered(0, 0, 16, 32, game.teams[0]->me);
		game.map.setMapDiscovered(16, 0, 16, 32, game.teams[1]->me);
		compare(29, 30, 0);
		compare(29, 30, 1);
		auto *asset = globals->terrain->nativeFrame(0);
		REQUIRE(asset);
		asset->drawPixel(0, 0, Color(17, 33, 51, 127));
		compare(29, 30, 0);
		game.map.paintCell(0, 0, WATER);
		compare(29, 30, 1);
		game.map.setSize(8, 8, GRASS);
		game.map.setGame(&game); // Rebind sectors and animation storage after replacement.
		REQUIRE(cache().bytes() == 0); // a replaced map starts an empty cache
		for (int chunk = 0; chunk < 40; ++chunk)
		{
			REQUIRE(cache().prepare(sceneOf(game.map), *globals->terrain, 0, 0, 15, 15,
													   (chunk % 16) * 16, (chunk / 16) * 16,
													   game.teams[0]->me, true));
			CHECK(cache().bytes() <= SoftwareTerrainCache::Budget);
		}
		CHECK_FALSE(cache().prepare(sceneOf(game.map), *globals->terrain, 0, 0, 127, 127,
													   0, 0, game.teams[0]->me, true));
		game.map.setSize(4, 4, GRASS);
		game.map.setGame(&game);
		compare(0, 0, 0);
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
				game.map.paintCell(x, y, WATER);
		REQUIRE(cache().prepare(sceneOf(game.map), *globals->terrain, 0, 0, 159, 159, 0,
												   0, game.teams[0]->me, true));
		// Water is an ordinary opaque tile material; restore the deliberately
		// translucent legacy pixel before the transformed capture below.
		asset->drawPixel(0, 0, Color(17, 33, 51, 255));

		const auto capture =
			glob2test::artifactDirFromWorkingDirectory() + "/transformed-software.bmp";
		globals->gfx->setUITransform(1.3f, 5, 5, nullptr);
		globals->gfx->drawFilledRect(0, 0, 16, 16, Color(0, 255, 0));
		globals->gfx->printScreen(capture);
		globals->gfx->setUITransform();
		globals->gfx->nextFrame();
		CHECK(std::filesystem::exists(capture));
	}
}

TEST_CASE("recorded software segments freeze borrowed sources and preserve fallback ordering" *
          doctest::test_suite("SoftwareRenderer"))
{
    std::vector<Uint32> expected;
    for (const char *execution : {"immediate", "serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        ComputeExecutor executor;
        executor.configure(4);
        GAGCore::PresentationWorkScope scope(executor);
        auto target = pixels(271, 263), source = pixels(35, 29);
        auto backend = makeSoftwareRenderBackend(target.get());
        REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0xff102030));
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, 0xffcc9966));
        backend->blit(nullptr, source.get(), 0, true, {0, 0, 35, 29}, {2, 1, 265, 257}, 255);
        // No reliable revision contract: the first command must already own its pixels.
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, 0xff336699));
        backend->fillPixels({113, 117, 137, 123}, {7, 91, 173, 79});
        backend->blitPixels(source.get(), {1, 2, 31, 25}, {126, 127, 93, 85}, 173);
        const SDL_FColor green{0, 1, 0, .37f};
        const SDL_Vertex triangle[] = {
            {{12, 13}, green, {}}, {{180, 14}, green, {}}, {{17, 201}, green, {}}};
        backend->triangles(triangle);
        backend->fillPixels({127, 125, 23, 49}, {201, 31, 17, 137});
        source.reset(); // Destroy before flush; snapshot ownership is sufficient.
        backend->flush();
        if (expected.empty())
            expected = snapshot(target.get());
        else
            CHECK(snapshot(target.get()) == expected);
        auto captured = Surface(backend->capture(), SDL_DestroySurface);
        REQUIRE(captured);
        auto next = pixels(37, 31);
        backend->bindTarget(next.get());
        backend->fillPixels({0, 0, 37, 31}, {43, 97, 151, 255});
        backend->present();
        CHECK(pixel(next.get(), 20, 20) == 0xff2b6197);
    }
}
TEST_CASE("recorder capacity and overlapping backing views retain immediate draw semantics" *
          doctest::test_suite("SoftwareRenderer"))
{
    std::vector<Uint32> expected;
    for (const char *execution : {"immediate", "serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        ComputeExecutor executor;
        executor.configure(3);
        GAGCore::PresentationWorkScope scope(executor);
        auto target = pixels(269, 261), portable = pixels(269, 261);
        auto backend = makeSoftwareRenderBackend(target.get());
        for (int y = 0; y < 261; ++y)
            for (int x = 0; x < 269; ++x)
            {
                const Uint32 value = 0xff000000 | ((x * 17u + y * 43u) & 0xffffff);
                std::memcpy(static_cast<Uint8 *>(target->pixels) + y * target->pitch + x * 4,
                            &value, 4);
                std::memcpy(static_cast<Uint8 *>(portable->pixels) + y * portable->pitch + x * 4,
                            &value, 4);
            }
        auto borrowed = Surface(
            SDL_CreateSurfaceFrom(20, 17, target->format,
                                  static_cast<Uint8 *>(target->pixels) + 5 * target->pitch + 3 * 4,
                                  target->pitch),
            SDL_DestroySurface);
        REQUIRE(borrowed);
        auto portableBorrowed = Surface(
            SDL_CreateSurfaceFrom(20, 17, portable->format,
                static_cast<Uint8 *>(portable->pixels) + 5 * portable->pitch + 3 * 4,
                portable->pitch), SDL_DestroySurface);
        REQUIRE(portableBorrowed);
        backend->fillPixels({3, 5, 20, 17}, {71, 117, 191, 255});
        referenceFill(portable.get(), {3, 5, 20, 17}, {71, 117, 191, 255});
        backend->blitPixels(borrowed.get(), {0, 0, 20, 17}, {134, 128, 40, 34}, 173);
        referenceNearest(portable.get(), portableBorrowed.get(), {0, 0, 20, 17},
                         {134, 128, 40, 34}, 173);
        for (int i = 0; i < 1030; ++i)
        {
            backend->fillPixels({i % 269, i % 261, 3, 2}, {Uint8(i), 31, 77, 173});
            referenceFill(portable.get(), {i % 269, i % 261, 3, 2}, {Uint8(i), 31, 77, 173});
        }
        backend->flush();
        CHECK(snapshot(target.get()) == snapshot(portable.get()));
        if (expected.empty())
            expected = snapshot(target.get());
        else
            CHECK(snapshot(target.get()) == expected);
    }
}

TEST_CASE("pending borrowed pixels survive destruction before flush and prove tiled execution" *
          doctest::test_suite("SoftwareRenderer"))
{
    glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "parallel");
    ComputeExecutor executor;
    executor.configure(4);
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    PresentationWorkScope scope(executor);
    auto target = pixels(271, 263);
    auto backend = makeSoftwareRenderBackend(target.get());
    backend->fillPixels({0, 0, 271, 263}, {11, 23, 47, 255});
    {
        auto source = pixels(19, 17);
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, 0xff759bc1));
        backend->blit(nullptr, source.get(), 1, true, {0, 0, 19, 17}, {1, 2, 267, 257}, 255);
    }
    auto reused = pixels(19, 17);
    REQUIRE(SDL_FillSurfaceRect(reused.get(), nullptr, 0xff010203));
    backend->flush();
    CHECK(pixel(target.get(), 129, 130) == 0xff759bc1);
    CHECK(backend->operations().rasterTiles > 1);
    CHECK(executor.graphicsMetrics().parallelBatches > 0);
}
TEST_CASE("failed raster batches preserve completed content and recover after owner discard" *
          doctest::test_suite("SoftwareRenderer"))
{
    glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "parallel");
    struct Failure : PresentationWork
    {
        void executePresentationJobs(std::size_t count,
                                     const std::function<void(std::size_t)> &job) override
        {
            if (count)
                job(0);
            throw std::runtime_error("injected raster failure");
        }
    } failure;
    auto initial = pixels(271, 263);
    auto *original = initial.release();
    SoftwareFramePresenter presenter(original);
    auto backend = makeSoftwareRenderBackend(original);
    backend->fillPixels({0, 0, 271, 263}, {17, 29, 43, 255});
    backend->flush();
    presenter.complete();
    const auto expected = snapshot(presenter.completed());
    backend->bindTarget(presenter.begin(true));
    backend->fillPixels({0, 0, 271, 263}, {211, 113, 71, 255});
    {
        PresentationWorkScope scope(failure);
        CHECK_THROWS_WITH_AS(backend->flush(), "injected raster failure", std::runtime_error);
    }
    CHECK(snapshot(presenter.completed()) == expected);
    CHECK_THROWS_AS(backend->capture(), std::runtime_error);
    CHECK_THROWS_AS(backend->present(), std::runtime_error);
    backend->discardFrame();
    backend->bindTarget(presenter.discard());
    backend->bindTarget(presenter.begin(true));
    backend->fillPixels({128, 127, 3, 5}, {31, 73, 151, 255});
    backend->flush();
    presenter.complete();
    CHECK(pixel(presenter.completed(), 129, 128) == 0xff1f4997);
    CHECK(pixel(presenter.completed(), 2, 3) == 0xff111d2b);
    auto capture = Surface(backend->capture(), SDL_DestroySurface);
    REQUIRE(capture);
}
TEST_CASE("non-sRGB textured quads retain SDL color conversion" *
          doctest::test_suite("SoftwareRenderer"))
{
    auto source = pixels(7, 5);
    REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, 0x794173a5));
    REQUIRE(SDL_SetSurfaceColorspace(source.get(), SDL_COLORSPACE_SRGB_LINEAR));
    auto expected = pixels(19, 17);
    REQUIRE(SDL_FillSurfaceRect(expected.get(), nullptr, 0xff123456));
    auto legacy = makeSDLSoftwareGeometryBackend(expected.get());
    legacy->blit(source.get(), source.get(), 1, false, {0, 0, 7, 5}, {2, 3, 13, 11}, 173);
    legacy->flush();
    for (const char *execution : {"immediate", "serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        auto target = pixels(19, 17);
        REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0xff123456));
        auto backend = makeSoftwareRenderBackend(target.get());
        backend->blit(source.get(), source.get(), 1, false, {0, 0, 7, 5}, {2, 3, 13, 11}, 173);
        backend->flush();
        CHECK(snapshot(target.get()) == snapshot(expected.get()));
        CHECK(backend->operations().rasterFallbacks == 1);
    }
}

TEST_CASE("anonymous logical SDL blits retain untextured geometry semantics" *
          doctest::test_suite("SoftwareRenderer"))
{
    auto source = pixels(7, 5);
    REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, 0x794173a5));
    auto expected = pixels(19, 17);
    REQUIRE(SDL_FillSurfaceRect(expected.get(), nullptr, 0xff123456));
    auto legacy = makeSDLSoftwareGeometryBackend(expected.get());
    legacy->blit(nullptr, source.get(), 0, false, {0, 0, 7, 5}, {2, 3, 13, 11}, 173);
    legacy->flush();
    for (const char *execution : {"immediate", "serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        auto target = pixels(19, 17);
        REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0xff123456));
        auto backend = makeSoftwareRenderBackend(target.get());
        backend->blit(nullptr, source.get(), 0, false, {0, 0, 7, 5}, {2, 3, 13, 11}, 173);
        backend->flush();
        CHECK(snapshot(target.get()) == snapshot(expected.get()));
        CHECK(backend->operations().rasterFallbacks == 1);
    }
}

TEST_CASE("owner drawing errors abandon the frame before capture or presentation" *
          doctest::test_suite("SoftwareRenderer"))
{
    for (const char *execution : {"immediate", "serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        auto initial = pixels(19, 17);
        auto *original = initial.release();
        SoftwareFramePresenter presenter(original);
        auto backend = makeSoftwareRenderBackend(original);
        backend->fillPixels({0, 0, 19, 17}, {17, 29, 43, 255});
        backend->flush();
        presenter.complete();
        const auto expected = snapshot(presenter.completed());
        backend->bindTarget(presenter.begin(true));
        backend->fillPixels({0, 0, 19, 17}, {211, 113, 71, 255});
        CHECK_THROWS_AS(backend->fill({std::numeric_limits<float>::quiet_NaN(), 0, 3, 3},
                                      {31, 73, 151, 255}), std::invalid_argument);
        CHECK(snapshot(presenter.completed()) == expected);
        CHECK_THROWS_AS(backend->capture(), std::runtime_error);
        CHECK_THROWS_AS(backend->present(), std::runtime_error);
        backend->discardFrame();
        backend->bindTarget(presenter.discard());
        backend->bindTarget(presenter.begin(true));
        backend->fillPixels({2, 3, 1, 1}, {31, 73, 151, 255});
        backend->flush();
        presenter.complete();
        CHECK(pixel(presenter.completed(), 2, 3) == 0xff1f4997);
        CHECK(pixel(presenter.completed(), 4, 5) == 0xff111d2b);
    }
}

TEST_CASE("first frame discard establishes a blank base for partial updates" *
          doctest::test_suite("SoftwareRenderer"))
{
    auto initial = pixels(9, 7);
    auto *first = initial.release();
    SoftwareFramePresenter presenter(first);
    REQUIRE(SDL_FillSurfaceRect(first, nullptr, 0xffeeddcc));
    CHECK(presenter.completed() == nullptr);
    auto *restored = presenter.discard();
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 9; ++x)
            CHECK(pixel(restored, x, y) == 0xff000000);
    REQUIRE(SDL_FillSurfaceRect(restored, nullptr, 0xff112233));
    presenter.complete();
    auto *drawing = presenter.begin(true);
    REQUIRE(SDL_FillSurfaceRect(drawing, nullptr, 0xffeeddcc));
    presenter.discard();
    CHECK(pixel(presenter.completed(), 3, 4) == 0xff112233);
}
TEST_CASE("shared rendering reservation pressure selects immediate software fallback" * doctest::test_suite("SoftwareRenderer"))
{
    glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION","parallel");
    const auto before=RenderWorkReservation::available();
    RenderWorkReservation occupied;REQUIRE(occupied.reserve(before));
    auto target=pixels(33,31);auto backend=makeSoftwareRenderBackend(target.get());
    backend->fillPixels({0,0,33,31},{29,71,137,255});backend->flush();
    CHECK(pixel(target.get(),7,9)==0xff1d4789);
    CHECK(backend->operations().rasterBatches==0);
    occupied.release();CHECK(RenderWorkReservation::available()==before);
    backend->fillPixels({1,2,3,4},{47,91,157,255});backend->flush();
    CHECK(backend->operations().rasterBatches==1);
}

TEST_CASE("very wide recorded targets retain bounded scratch and exact painter order" *
          doctest::test_suite("SoftwareRenderer"))
{
    std::vector<Uint32> expected;
    for (const char *execution : {"immediate", "serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        auto target = pixels(300000, 1), source = pixels(7, 1);
        auto backend = makeSoftwareRenderBackend(target.get());
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, 0xff719bc3));
        backend->fillPixels({0, 0, target->w, 1}, {11, 23, 47, 255});
        backend->blitPixels(source.get(), {0, 0, 7, 1}, {13, 0, 299971, 1}, 173);
        backend->fillPixels({128, 0, 257, 1}, {89, 51, 117, 79});
        backend->flush();
        if (expected.empty()) expected = snapshot(target.get());
        else CHECK(snapshot(target.get()) == expected);
        CHECK(backend->operations().rasterPeakRetainedBytes < 2u*1024u*1024u);
    }
}
TEST_CASE("render reservation rollback and commit preserve prior captures" *
          doctest::test_suite("SoftwareRenderer"))
{
    const auto available = RenderWorkReservation::available();
    RenderWorkReservation committed;
    REQUIRE(committed.reserve(128));
    try
    {
        RenderWorkReservation pending;
        REQUIRE(pending.reserve(256));
        throw std::runtime_error("injected capture failure");
    }
    catch (const std::runtime_error&) {}
    CHECK(RenderWorkReservation::available() == available-128);
    {
        RenderWorkReservation pending;
        REQUIRE(pending.reserve(256));
        committed.absorb(pending);
    }
    CHECK(RenderWorkReservation::available() == available-384);
    committed.release();
    CHECK(RenderWorkReservation::available() == available);
}

TEST_CASE("graphics context CPU pixel borrowing fences recorded main and offscreen drawing" *
          doctest::test_suite("SoftwareRenderer"))
{
    for (const char *execution : {"immediate", "serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        GraphicContext context(64, 48, GraphicContext::NOAUDIO, "CPU pixel observation");
        // Native 1x contexts use immediate facade drawing until a transform
        // activates the software backend. Keep it active while borrowing pixels.
        context.beginMapTransform(1, 1, 0, 0, 0, 64, 48);
        context.drawFilledRect(-1, 0, 64, 48, Color(71, 117, 191));
        CHECK(pixel(context.getSDLSurface(), 31, 23) == 0xff4775bf);
        REQUIRE(SDL_FillSurfaceRect(context.getSDLSurface(), nullptr, 0xff173b61));
        context.drawFilledRect(1, 3, 7, 5, Color(151, 43, 79));
        CHECK(pixel(context.getSDLSurface(), 4, 5) == 0xff972b4f);
        CHECK(pixel(context.getSDLSurface(), 31, 23) == 0xff173b61);
        auto offscreen = pixels(19, 17);
        context.drawToSurface(offscreen.get(), 1, [&] {
            context.drawFilledRect(0, 0, 19, 17, Color(43, 97, 151));
            CHECK(context.getSDLSurface() == offscreen.get());
            CHECK(pixel(context.getSDLSurface(), 9, 8) == 0xff2b6197);
        });
        CHECK(pixel(context.getSDLSurface(), 31, 23) == 0xff173b61);
        context.endMapTransform();
    }
}

TEST_CASE("memory capture rejects failed raster output and resumes after owner discard" *
          doctest::test_suite("SoftwareRenderer"))
{
    glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "parallel");
    struct Failure : PresentationWork
    {
        void executePresentationJobs(std::size_t count,
                                     const std::function<void(std::size_t)> &job) override
        {
            if (count)
                job(0);
            throw std::runtime_error("injected memory capture raster failure");
        }
    } failure;
    GraphicContext context(271, 263, GraphicContext::NOAUDIO, "CPU capture recovery");
    context.drawFilledRect(0, 0, 271, 263, Color(17, 29, 43));
    context.nextFrame();
    const auto completed = snapshot(context.completedFrame());
    // A translated map pass activates the recorder on native 1x displays.
    context.beginMapTransform(1, 1, 0, 0, 0, 271, 263);
    bool rejected = false;
    context.requestMemoryCapture([&](auto image, auto, auto error) {
        CHECK_FALSE(image);
        CHECK(error == "injected memory capture raster failure");
        rejected = true;
    });
    context.drawFilledRect(-1, 0, 271, 263, Color(211, 113, 71));
    {
        PresentationWorkScope scope(failure);
        CHECK_THROWS_AS(context.nextFrame(), std::runtime_error);
    }
    CHECK(rejected);
    CHECK(snapshot(context.completedFrame()) == completed);
    context.discardFrame();
    CHECK(snapshot(context.completedFrame()) == completed);
    context.endMapTransform();

    std::shared_ptr<SDL_Surface> captured;
    context.requestMemoryCapture([&](auto image, auto, auto error) {
        CHECK(error.empty());
        captured = std::move(image);
    });
    context.beginMapTransform(1, 1, 0, 0, 0, 271, 263);
    context.drawFilledRect(127, 127, 3, 5, Color(31, 73, 151));
    context.nextFrame();
    context.endMapTransform();
    REQUIRE(captured);
    Uint8 red, green, blue, alpha;
    REQUIRE(SDL_ReadSurfacePixel(captured.get(), 129, 128, &red, &green, &blue, &alpha));
    CHECK(red == 31);
    CHECK(green == 73);
    CHECK(blue == 151);
    REQUIRE(SDL_ReadSurfacePixel(captured.get(), 2, 3, &red, &green, &blue, &alpha));
    CHECK(red == 17);
    CHECK(green == 29);
    CHECK(blue == 43);
}

TEST_CASE("clipped and transparent recorded draws retain no commands or source pixels" *
          doctest::test_suite("SoftwareRenderer"))
{
    for (const char *execution : {"serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        const auto available = RenderWorkReservation::available();
        auto target = pixels(271, 263), source = pixels(37, 31);
        REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0xff123456));
        auto backend = makeSoftwareRenderBackend(target.get());
        const SDL_Rect clip{3, 5, 7, 11};
        backend->clip(&clip);
        backend->fillPixels({13, 5, 23, 29}, {47, 91, 157, 255});
        backend->blitPixels(source.get(), {0, 0, 37, 31}, {3, 17, 37, 31}, 173);
        backend->blit(nullptr, source.get(), 0, true, {0, 0, 37, 31},
                      {-39, 5, 37, 31}, 255);
        backend->fillPixels({3, 5, 7, 11}, {47, 91, 157, 0});
        backend->blitPixels(source.get(), {0, 0, 37, 31}, {3, 5, 7, 11}, 0);
        backend->flush();
        CHECK(backend->operations().rasterBatches == 0);
        CHECK(backend->operations().rasterSnapshotBytes == 0);
        CHECK(RenderWorkReservation::available() == available);
        CHECK(pixel(target.get(), 5, 9) == 0xff123456);
    }
}

TEST_CASE("narrow recorded commands preserve painter order on very wide targets" *
          doctest::test_suite("SoftwareRenderer"))
{
    std::vector<Uint32> expected;
    for (const char *execution : {"immediate", "serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        auto target = pixels(300000, 1);
        REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0xff123456));
        auto backend = makeSoftwareRenderBackend(target.get());
        for (int i = 0; i < 1030; ++i)
            backend->fillPixels({149000 + i % 17, 0, 3, 1},
                                {Uint8(i), Uint8(i * 3), 117, 79});
        backend->flush();
        if (expected.empty()) expected = snapshot(target.get());
        else CHECK(snapshot(target.get()) == expected);
        CHECK(backend->operations().rasterPeakRetainedBytes < 2u * 1024u * 1024u);
    }
}

TEST_CASE("dense translucent regions retain painter order without serializing the segment" *
          doctest::test_suite("SoftwareRenderer"))
{
    struct ReverseJobs : PresentationWork
    {
        std::vector<std::size_t> submitted;
        void executePresentationJobs(std::size_t count,
                                     const std::function<void(std::size_t)> &job) override
        {
            submitted.push_back(count);
            for (std::size_t i = count; i > 0; --i) job(i - 1);
        }
    } dispatcher;
    // More references per initial tile than the packed-job ceiling: expensive
    // tiles must subdivide instead of sending the entire frame back to the owner.
    constexpr int layers = 40;
    auto expected = pixels(269, 261);
    REQUIRE(SDL_FillSurfaceRect(expected.get(), nullptr, 0x79416385));
    for (int i = 0; i < layers; ++i)
        referenceFill(expected.get(), {0, 0, 269, 261},
            {Uint8(i * 31), Uint8(231 - i * 3), Uint8(i * 17), Uint8(23 + i % 9)});
    for (const char *execution : {"immediate", "serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        PresentationWorkScope scope(dispatcher);
        auto target = pixels(269, 261);
        REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x79416385));
        auto backend = makeSoftwareRenderBackend(target.get());
        for (int i = 0; i < layers; ++i)
            backend->fill({0, 0, 269, 261},
                {Uint8(i * 31), Uint8(231 - i * 3), Uint8(i * 17), Uint8(23 + i % 9)});
        backend->flush();
        CHECK(snapshot(target.get()) == snapshot(expected.get()));
        CHECK(backend->operations().rasterFallbacks == 0);
        if (std::strcmp(execution, "parallel") == 0)
        {
            REQUIRE(dispatcher.submitted.size() == 1);
            CHECK(dispatcher.submitted.front() > 1);
            CHECK(backend->operations().rasterSerialBatches == 0);
            CHECK(backend->operations().rasterBatches == 1);
        }
    }
}

TEST_CASE("reference overflow completes ordered prefixes with bounded metadata" *
          doctest::test_suite("SoftwareRenderer"))
{
    struct ReverseJobs : PresentationWork
    {
        std::size_t batches = 0;
        void executePresentationJobs(std::size_t count,
                                     const std::function<void(std::size_t)> &job) override
        {
            ++batches;
            for (std::size_t i = count; i > 0; --i) job(i - 1);
        }
    } dispatcher;
    constexpr int extent = 257, layers = 1024;
    auto expected = pixels(extent, extent);
    REQUIRE(SDL_FillSurfaceRect(expected.get(), nullptr, 0x79416385));
    // Uniform layers let an independent one-pixel oracle establish the complete
    // painter-order result without another full-screen raster traversal.
    auto oracle = pixels(1, 1);
    REQUIRE(SDL_FillSurfaceRect(oracle.get(), nullptr, 0x79416385));
    for (int i = 0; i < layers; ++i)
        referenceFill(oracle.get(), {0, 0, 1, 1},
            {Uint8(i * 31), Uint8(231 - i * 3), Uint8(i * 17), Uint8(23 + i % 9)});
    REQUIRE(SDL_FillSurfaceRect(expected.get(), nullptr, pixel(oracle.get(), 0, 0)));
    glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "parallel");
    PresentationWorkScope scope(dispatcher);
    auto target = pixels(extent, extent);
    REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x79416385));
    auto backend = makeSoftwareRenderBackend(target.get());
    for (int i = 0; i < layers; ++i)
        backend->fillPixels({0, 0, extent, extent},
            {Uint8(i * 31), Uint8(231 - i * 3), Uint8(i * 17), Uint8(23 + i % 9)});
    backend->flush();
    CHECK(snapshot(target.get()) == snapshot(expected.get()));
    // Refinement exceeds the 65,536-reference capacity. Multiple batches prove
    // that ordered prefix splitting ran; each prefix still uses disjoint jobs.
    CHECK(dispatcher.batches > 1);
    CHECK(backend->operations().rasterBatches == dispatcher.batches);
    CHECK(backend->operations().rasterSerialBatches == 0);
    CHECK(backend->operations().rasterPeakRetainedBytes <= 2u * 1024u * 1024u);
}

TEST_CASE("fractional clipped transparent sprites use full destination sampling across tiles" *
          doctest::test_suite("SoftwareRenderer"))
{
    auto source = pixels(7, 5);
    for (int y = 0; y < source->h; ++y)
        for (int x = 0; x < source->w; ++x)
        {
            const Uint32 value = SDL_MapSurfaceRGBA(source.get(), Uint8(x * 37), Uint8(y * 51),
                Uint8(223 - x * 19), std::array<Uint8, 4>{0, 63, 137, 255}[(x + y) % 4]);
            std::memcpy(static_cast<Uint8 *>(source->pixels) + y * source->pitch + x * 4, &value, 4);
        }
    const SDL_Rect clip{11, 7, 243, 241};
    for (float zoom : {.5f, 1.3f, 2.f})
    {
        const SDL_FRect logical{-3.25f, -1.75f, 211.5f, 203.25f};
        const float left = logical.x * zoom + 1.25f;
        const float top = logical.y * zoom - .75f;
        const float right = (logical.x + logical.w) * zoom + 1.25f;
        const float bottom = (logical.y + logical.h) * zoom - .75f;
        // Uniform SDL quads truncate origin and extent independently. This
        // differs from the shared-edge snapping used by opaque terrain tiles.
        const SDL_Rect destination{int(left), int(top), int(right - left), int(bottom - top)};
        auto expected = pixels(271, 263);
        REQUIRE(SDL_FillSurfaceRect(expected.get(), nullptr, 0x674173a5));
        referenceNearest(expected.get(), source.get(), {1, 1, 5, 3}, destination, 173, false, clip, true);
        for (const char *execution : {"immediate", "serial", "parallel"})
        {
            INFO("zoom=", zoom, " execution=", std::string(execution));
            glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
            ComputeExecutor executor;
            executor.configure(3);
            executor.configureTelemetry(ComputeTelemetryMode::Trace);
            PresentationWorkScope scope(executor);
            auto target = pixels(271, 263);
            REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x674173a5));
            auto backend = makeSoftwareRenderBackend(target.get());
            // Physical transform bounds clip output independently of sprite mapping.
            backend->transform(zoom, 1.25f, -.75f, &clip);
            backend->blit(source.get(), source.get(), 1, false, {1, 1, 5, 3}, logical, 173);
            backend->flush();
            CHECK(snapshot(target.get()) == snapshot(expected.get()));
            CHECK(backend->operations().rasterFallbacks == 0);
            if (std::strcmp(execution, "parallel") == 0)
                CHECK(executor.graphicsMetrics().parallelBatches > 0);
        }
    }
}

TEST_CASE("transparent source revisions and destruction freeze recorded content" *
          doctest::test_suite("SoftwareRenderer"))
{
    for (const char *execution : {"immediate", "serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        ComputeExecutor executor;
        executor.configure(3);
        PresentationWorkScope scope(executor);
        auto actual = pixels(271, 263), expected = pixels(271, 263), source = pixels(9, 7);
        for (auto *target : {actual.get(), expected.get()})
            REQUIRE(SDL_FillSurfaceRect(target, nullptr, 0x71375981));
        auto backend = makeSoftwareRenderBackend(actual.get());
        const auto *identity = source.get();
        const SDL_Rect src{0, 0, 9, 7};
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, 0x7fcd7b31));
        backend->blit(identity, source.get(), 1, false, src, {0, 0, 269, 259}, 191);
        referenceNearest(expected.get(), source.get(), src, {0, 0, 269, 259}, 191, false, {0, 0, 271, 263}, true);
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, 0xad2b71db));
        backend->blit(identity, source.get(), 2, false, src, {129, 127, 139, 133}, 137);
        referenceNearest(expected.get(), source.get(), src, {129, 127, 139, 133}, 137, false, {0, 0, 271, 263}, true);
        backend->forget(identity);
        source.reset();
        auto reused = pixels(9, 7);
        REQUIRE(SDL_FillSurfaceRect(reused.get(), nullptr, 0xff010203));
        backend->flush();
        CHECK(snapshot(actual.get()) == snapshot(expected.get()));
        CHECK(backend->operations().rasterFallbacks == 0);
    }
}

TEST_CASE("packed source normalization preserves channels and transparent scaling" *
          doctest::test_suite("SoftwareRenderer"))
{
    for (SDL_PixelFormat format : {SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_RGBA32,
                                  SDL_PIXELFORMAT_XRGB8888, SDL_PIXELFORMAT_RGB565,
                                  SDL_PIXELFORMAT_RGB24})
    {
        Surface source(SDL_CreateSurface(9, 7, format), SDL_DestroySurface);
        REQUIRE(source);
        for (int y = 0; y < source->h; ++y)
            for (int x = 0; x < source->w; ++x)
            {
                const SDL_Rect point{x, y, 1, 1};
                REQUIRE(SDL_FillSurfaceRect(source.get(), &point,
                    SDL_MapSurfaceRGBA(source.get(), Uint8(x * 27), Uint8(y * 39),
                                       Uint8(211 - x * 13), Uint8((x * 43 + y * 29) % 256))));
            }
        auto expected = pixels(271, 263);
        REQUIRE(SDL_FillSurfaceRect(expected.get(), nullptr, 0x614b7197));
        referenceNearest(expected.get(), source.get(), {1, 1, 7, 5}, {-3, 2, 273, 259}, 173, false, {0, 0, 271, 263}, true);
        for (const char *execution : {"immediate", "serial", "parallel"})
        {
            INFO("format=", std::string(SDL_GetPixelFormatName(format)), " execution=", std::string(execution));
            glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
            ComputeExecutor executor;
            executor.configure(3);
            PresentationWorkScope scope(executor);
            auto target = pixels(271, 263);
            REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x614b7197));
            auto backend = makeSoftwareRenderBackend(target.get());
            backend->blit(source.get(), source.get(), 1, false, {1, 1, 7, 5}, {-3, 2, 273, 259}, 173);
            backend->flush();
            CHECK(snapshot(target.get()) == snapshot(expected.get()));
            CHECK(backend->operations().rasterFallbacks == 0);
        }
    }
}

TEST_CASE("formerly SDL translucent rectangles retain fractional coverage and fixed-point samples" *
          doctest::test_suite("SoftwareRenderer"))
{
    // Three-into-nine and six-into-nine ratios expose SDL's truncated 16.16
    // center step: exact rational sampling would choose different texels.
    for (SDL_PixelFormat format : {SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_RGBA32})
    {
        Surface source(SDL_CreateSurface(8, 6, format), SDL_DestroySurface);
        REQUIRE(source);
        for (int y = 0; y < source->h; ++y)
            for (int x = 0; x < source->w; ++x)
            {
                const SDL_Rect point{x, y, 1, 1};
                REQUIRE(SDL_FillSurfaceRect(source.get(), &point,
                    SDL_MapSurfaceRGBA(source.get(), Uint8(20 + x * 29), Uint8(31 + y * 37), 211, 127)));
            }
        for (const SDL_FRect destination : std::array<SDL_FRect, 3>{{
                 {4.75f, 3.75f, 9.f, 9.f}, {-3.75f, -.75f, 19.25f, 13.5f},
                 {7.5f, 9.5f, 14.75f, 11.75f}}})
            for (float scale : {.5f, 1.f, 1.3f, 2.f})
                for (const char *execution : {"immediate", "serial", "parallel"})
                {
                    INFO("format=", std::string(SDL_GetPixelFormatName(format)), " scale=", scale,
                         " x=", destination.x, " execution=", std::string(execution));
                    glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
                    auto actual = pixels(64, 48), legacy = pixels(64, 48);
                    for (auto *target : {actual.get(), legacy.get()})
                        REQUIRE(SDL_FillSurfaceRect(target, nullptr, 0xff173b61));
                    auto backend = makeSoftwareRenderBackend(actual.get());
                    auto sdl = makeSDLSoftwareGeometryBackend(legacy.get());
                    for (auto *renderer : {backend.get(), sdl.get()})
                    {
                        renderer->transform(scale, 1.25f, -.75f, nullptr);
                        renderer->blit(source.get(), source.get(), 1, false,
                                      {1, 1, 6, 3}, destination, 173);
                        renderer->flush();
                    }
                    checkIsolatedSDLChannels(actual.get(), legacy.get(), 0xff173b61);
                    CHECK(backend->operations().rasterFallbacks == 0);
                    for (auto *target : {actual.get(), legacy.get()})
                        REQUIRE(SDL_FillSurfaceRect(target, nullptr, 0xff173b61));
                    for (auto *renderer : {backend.get(), sdl.get()})
                    {
                        renderer->fill(destination, {191, 137, 79, 127});
                        renderer->flush();
                    }
                    checkIsolatedSDLChannels(actual.get(), legacy.get(), 0xff173b61);
                }
    }
}

TEST_CASE("isolated sprite alpha remains equivalent to SDL on translucent offscreen targets" *
          doctest::test_suite("SoftwareRenderer"))
{
    for (SDL_PixelFormat format : {SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_RGBA32})
    {
        Surface source(SDL_CreateSurface(8, 6, format), SDL_DestroySurface);
        REQUIRE(source);
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr,
                                   SDL_MapSurfaceRGBA(source.get(), 191, 137, 79, 127)));
        for (SDL_PixelFormat targetFormat : {SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_RGBA32})
        for (Uint8 destinationAlpha : {0, 79, 191, 255})
            for (Uint8 opacity : {127, 255})
                for (const SDL_FRect destination : std::array<SDL_FRect, 2>{{{2, 3, 8, 6}, {2, 3, 16, 12}}})
                    for (const char *execution : {"immediate", "serial", "parallel"})
                    {
                        INFO("format=", std::string(SDL_GetPixelFormatName(format)), " destination alpha=",
                             int(destinationAlpha), " target=", std::string(SDL_GetPixelFormatName(targetFormat)),
                             " opacity=", int(opacity), " width=",
                             destination.w, " execution=", std::string(execution));
                        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
                        Surface actual(SDL_CreateSurface(32, 24, targetFormat), SDL_DestroySurface);
                        Surface legacy(SDL_CreateSurface(32, 24, targetFormat), SDL_DestroySurface);
                        REQUIRE(actual); REQUIRE(legacy);
                        const Uint32 background = SDL_MapSurfaceRGBA(actual.get(), 23, 59, 97, destinationAlpha);
                        for (auto *target : {actual.get(), legacy.get()})
                            REQUIRE(SDL_FillSurfaceRect(target, nullptr, background));
                        auto backend = makeSoftwareRenderBackend(actual.get());
                        auto sdl = makeSDLSoftwareGeometryBackend(legacy.get());
                        for (auto *renderer : {backend.get(), sdl.get()})
                        {
                            renderer->blit(source.get(), source.get(), 1, false,
                                          {0, 0, 8, 6}, destination, opacity);
                            renderer->flush();
                        }
                        checkIsolatedSDLChannels(actual.get(), legacy.get(), background);
                    }
    }
}

TEST_CASE("out of bounds textured sources retain SDL geometry and output scissors" *
          doctest::test_suite("SoftwareRenderer"))
{
    // SDL only simplifies textured triangles to copies when their UVs lie in
    // [0,1]. Out-of-bounds sources retain triangle sampling through the ordered
    // fallback; merely clipping and stretching the source would change pixels.
    auto source = pixels(9, 7);
    for (int y = 0; y < source->h; ++y)
        for (int x = 0; x < source->w; ++x)
        {
            const Uint32 value = SDL_MapSurfaceRGBA(source.get(), Uint8(19 + 25 * x),
                                                   Uint8(23 + 33 * y), 191, 255);
            std::memcpy(static_cast<Uint8 *>(source->pixels) + y * source->pitch + x * 4,
                        &value, 4);
        }
    for (const SDL_Rect sourceRect : std::array<SDL_Rect, 4>{{
             {-2, -1, 6, 4}, {6, 4, 7, 5}, {-9, 0, 8, 3}, {1, 2, 4, 3}}})
        for (float scale : {.5f, 1.3f, 2.f})
            for (bool clipped : {false, true})
                for (const char *execution : {"immediate", "serial", "parallel"})
                {
                    INFO("source x=", sourceRect.x, " y=", sourceRect.y,
                         " scale=", scale, " clipped=", clipped, " execution=", std::string(execution));
                    glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
                    auto actual = pixels(64, 48), legacy = pixels(64, 48);
                    for (auto *target : {actual.get(), legacy.get()})
                        REQUIRE(SDL_FillSurfaceRect(target, nullptr, 0xff173b61));
                    auto backend = makeSoftwareRenderBackend(actual.get());
                    auto sdl = makeSDLSoftwareGeometryBackend(legacy.get());
                    for (auto *renderer : {backend.get(), sdl.get()})
                    {
                        renderer->transform(scale, 1.25f, -.75f, nullptr);
                        const SDL_Rect clip{3, 2, 13, 11};
                        if (clipped) renderer->clip(&clip);
                        renderer->blit(source.get(), source.get(), 1, false,
                                      sourceRect, {-3.75f, 2.75f, 29.25f, 23.5f}, 173);
                        renderer->flush();
                    }
                    checkIsolatedSDLChannels(actual.get(), legacy.get(), 0xff173b61);
                    const bool outside = sourceRect.x < 0 || sourceRect.y < 0 ||
                        sourceRect.x + sourceRect.w > source->w || sourceRect.y + sourceRect.h > source->h;
                    CHECK((backend->operations().rasterFallbacks > 0) == outside);
                }
}

TEST_CASE("canceled executor raster batches retain completed pixels until owner recovery" *
          doctest::test_suite("SoftwareRenderer"))
{
#ifndef __EMSCRIPTEN__
    if constexpr (!GAGCore::ThreadSupport::available) return;
    glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "parallel");
    auto initial = pixels(271, 263);
    auto *original = initial.release();
    SoftwareFramePresenter presenter(original);
    auto backend = makeSoftwareRenderBackend(original);
    backend->fillPixels({0, 0, 271, 263}, {17, 29, 43, 255});
    backend->flush();
    presenter.complete();
    const auto completed = snapshot(presenter.completed());
    backend->bindTarget(presenter.begin(true));
    backend->fillPixels({0, 0, 271, 263}, {211, 113, 71, 137});

    ComputeExecutor executor;
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    executor.configure(2);
    std::promise<void> entered, release;
    auto enteredFuture = entered.get_future();
    auto released = release.get_future();
    auto scene = executor.submitPresentation(1, [&](auto)
    {
        entered.set_value();
        released.wait_for(std::chrono::seconds(5));
    });
    REQUIRE(enteredFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    std::atomic<bool> admitted{false};
    std::thread reconfigure([&]
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (executor.graphicsMetrics().batches == 0 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        admitted.store(executor.graphicsMetrics().batches == 1);
        // Scene preparation owns the sole worker: reconfiguration cancels the
        // queued raster batch before any output region can be completed.
        executor.configure(1);
    });
    {
        PresentationWorkScope scope(executor);
        CHECK_THROWS_WITH_AS(backend->flush(), "Synchronous graphics batch was canceled",
                             std::runtime_error);
    }
    CHECK(admitted.load());
    CHECK_FALSE(scene->finished());
    CHECK(snapshot(presenter.completed()) == completed);
    CHECK_THROWS_AS(backend->capture(), std::runtime_error);
    CHECK_THROWS_AS(backend->present(), std::runtime_error);
    release.set_value();
    reconfigure.join();

    backend->discardFrame();
    backend->bindTarget(presenter.discard());
    backend->bindTarget(presenter.begin(true));
    backend->fillPixels({128, 127, 3, 5}, {31, 73, 151, 255});
    {
        PresentationWorkScope scope(executor);
        backend->flush();
    }
    presenter.complete();
    CHECK(pixel(presenter.completed(), 129, 128) == 0xff1f4997);
    CHECK(pixel(presenter.completed(), 2, 3) == 0xff111d2b);
    auto captured = Surface(backend->capture(), SDL_DestroySurface);
    REQUIRE(captured);
#endif
}

TEST_CASE("uniform tinted textured quads retain SDL alpha and portable channel arithmetic" *
          doctest::test_suite("SoftwareRenderer"))
{
    for (SDL_PixelFormat sourceFormat : {SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_RGBA32})
        for (SDL_PixelFormat targetFormat : {SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_RGBA32})
            for (Uint8 sourceAlpha : {63, 127, 255})
                for (Uint8 opacity : {127, 255})
                    for (const char *execution : {"immediate", "serial", "parallel"})
                    {
                        INFO("source=", std::string(SDL_GetPixelFormatName(sourceFormat)),
                             " target=", std::string(SDL_GetPixelFormatName(targetFormat)),
                             " alpha=", int(sourceAlpha), " opacity=", int(opacity),
                             " execution=", std::string(execution));
                        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
                        Surface source(SDL_CreateSurface(8, 8, sourceFormat), SDL_DestroySurface);
                        Surface actual(SDL_CreateSurface(32, 24, targetFormat), SDL_DestroySurface);
                        Surface expected(SDL_CreateSurface(32, 24, targetFormat), SDL_DestroySurface);
                        Surface legacy(SDL_CreateSurface(32, 24, targetFormat), SDL_DestroySurface);
                        REQUIRE(source); REQUIRE(actual); REQUIRE(expected); REQUIRE(legacy);
                        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr,
                            SDL_MapSurfaceRGBA(source.get(), 111, 193, 71, sourceAlpha)));
                        const auto background = SDL_MapSurfaceRGBA(actual.get(), 23, 59, 97, 79);
                        for (auto *target : {actual.get(), expected.get(), legacy.get()})
                            REQUIRE(SDL_FillSurfaceRect(target, nullptr, background));
                        const SDL_Color tint{37, 129, 211, opacity};
                        const auto vertices = rectangleVertices({2, 3, 6, 6},
                            {tint.r / 255.f, tint.g / 255.f, tint.b / 255.f, opacity / 255.f},
                            {1 / 8.f, 1 / 8.f, 6 / 8.f, 6 / 8.f});
                        auto backend = makeSoftwareRenderBackend(actual.get());
                        auto sdl = makeSDLSoftwareGeometryBackend(legacy.get());
                        for (auto *renderer : {backend.get(), sdl.get()})
                        {
                            renderer->triangles(vertices, source.get(), source.get(), 1);
                            renderer->flush();
                        }
                        referenceNearest(expected.get(), source.get(), {1, 1, 6, 6},
                            {2, 3, 6, 6}, opacity, false, {0, 0, 32, 24}, true, tint);
                        CHECK(snapshot(actual.get()) == snapshot(expected.get()));
                        checkIsolatedSDLChannels(actual.get(), legacy.get(), background);
                        CHECK(backend->operations().rasterFallbacks == 0);
                    }
}

TEST_CASE("uniform screen quads ignore map transforms while retaining native display scale" *
          doctest::test_suite("SoftwareRenderer"))
{
    for (const char *execution : {"immediate", "serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        auto actual = pixels(64, 48), expected = pixels(64, 48), legacy = pixels(64, 48);
        for (auto *target : {actual.get(), expected.get(), legacy.get()})
            REQUIRE(SDL_FillSurfaceRect(target, nullptr, 0xff173b61));
        auto backend = makeSoftwareRenderBackend(actual.get());
        auto sdl = makeSDLSoftwareGeometryBackend(legacy.get());
        const auto vertices = rectangleVertices({3.75f, 4.25f, 11.5f, 9.5f},
                                               {151 / 255.f, 89 / 255.f, 43 / 255.f, 127 / 255.f});
        for (auto *renderer : {backend.get(), sdl.get()})
        {
            renderer->nativeLogicalSize(32, 24);
            renderer->transform(.5f, 7.25f, -3.75f, nullptr);
            renderer->flush();
        }
        // Logical-size configuration clears the framebuffer. Establish the
        // comparison background afterward, with SDL's queued clear completed.
        for (auto *target : {actual.get(), legacy.get()})
            REQUIRE(SDL_FillSurfaceRect(target, nullptr, 0xff173b61));
        for (auto *renderer : {backend.get(), sdl.get()})
        {
            renderer->screenTriangles(vertices);
            renderer->flush();
        }
        referenceFill(expected.get(), {7, 8, 23, 19}, {151, 89, 43, 127});
        CHECK(snapshot(actual.get()) == snapshot(expected.get()));
        checkIsolatedSDLChannels(actual.get(), legacy.get(), 0xff173b61);
        CHECK(backend->operations().rasterFallbacks == 0);
    }
}

TEST_CASE("eligible quad runs fence intervening arbitrary geometry in painter order" *
          doctest::test_suite("SoftwareRenderer"))
{
    const auto first = rectangleVertices({0, 0, 271, 263},
                                       {191 / 255.f, 137 / 255.f, 79 / 255.f, 127 / 255.f});
    const std::array<SDL_Vertex, 3> geometry{{
        {{7, 9}, {1, 0, 0, .37f}, {}},
        {{257, 13}, {0, 1, 0, .63f}, {}},
        {{19, 251}, {0, 0, 1, .79f}, {}}}};
    const auto last = rectangleVertices({113, 117, 137, 123},
                                      {31 / 255.f, 73 / 255.f, 151 / 255.f, 137 / 255.f});
    std::vector<SDL_Vertex> stream(first.begin(), first.end());
    stream.insert(stream.end(), geometry.begin(), geometry.end());
    stream.insert(stream.end(), last.begin(), last.end());
    auto expected = pixels(271, 263);
    REQUIRE(SDL_FillSurfaceRect(expected.get(), nullptr, 0xff173b61));
    referenceFill(expected.get(), {0, 0, 271, 263}, {191, 137, 79, 127});
    {
        auto legacy = makeSDLSoftwareGeometryBackend(expected.get());
        legacy->triangles(geometry);
        legacy->flush();
    }
    referenceFill(expected.get(), {113, 117, 137, 123}, {31, 73, 151, 137});
    for (const char *execution : {"immediate", "serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        ComputeExecutor executor;
        executor.configure(3);
        PresentationWorkScope scope(executor);
        auto actual = pixels(271, 263);
        REQUIRE(SDL_FillSurfaceRect(actual.get(), nullptr, 0xff173b61));
        auto backend = makeSoftwareRenderBackend(actual.get());
        backend->triangles(stream);
        backend->flush();
        CHECK(snapshot(actual.get()) == snapshot(expected.get()));
        CHECK(backend->operations().rasterFallbacks == 1);
    }
}
