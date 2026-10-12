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
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
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
	TEST_CASE("integer transformed primitives retain SDL triangle blending exactly")
	{
		auto source = pixels(8, 8), expected = pixels(32, 32), actual = pixels(32, 32);
		for (int y = 0; y < 8; ++y)
			for (int x = 0; x < 8; ++x)
			{
				const Uint32 value = SDL_MapSurfaceRGBA(source.get(), 31 * x, 29 * y, 137,
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
		for (float scale : {1.f, 2.f})
			for (Uint8 alpha : {0, 63, 127, 128, 255})
			{
				for (auto *target : {expected.get(), actual.get()})
					SDL_FillSurfaceRect(target, nullptr, 0xff376b91);
				for (auto *backend : {reference.get(), optimized.get()})
				{
					backend->transform(scale, 3, 2, nullptr);
					backend->blit(source.get(), source.get(), 1, false, SDL_Rect{1, 1, 6, 6},
								  SDL_FRect{2, 2, 6, 6}, alpha);
					backend->fill(SDL_FRect{11, 3, 9, 11}, SDL_Color{151, 89, 43, alpha});
					backend->flush();
				}
				const auto actualPixels = snapshot(actual.get()),
						   expectedPixels = snapshot(expected.get());
				const auto mismatch =
					std::mismatch(actualPixels.begin(), actualPixels.end(), expectedPixels.begin());
				INFO("first difference index=", mismatch.first - actualPixels.begin(),
					 " actual=", mismatch.first != actualPixels.end() ? *mismatch.first : 0,
					 " expected=", mismatch.first != actualPixels.end() ? *mismatch.second : 0);
				INFO("scale=", scale, " alpha=", int(alpha),
					 " blit actual=", pixel(actual.get(), 6, 5),
					 " expected=", pixel(expected.get(), 6, 5),
					 " fill actual=", pixel(actual.get(), 15, 8),
					 " expected=", pixel(expected.get(), 15, 8));
				CHECK(snapshot(actual.get()) == snapshot(expected.get()));
			}
	}
	TEST_CASE("geometry-reference blits preserve mixed pixel formats and source modulation")
	{
		Surface source(SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
		REQUIRE(source);
		auto expected = pixels(20, 20), actual = pixels(20, 20);
		auto reference = makeSDLSoftwareGeometryBackend(expected.get());
		auto optimized = makeSoftwareRenderBackend(actual.get());
		SDL_SetSurfaceColorMod(source.get(), 127, 79, 191);
		SDL_SetSurfaceAlphaMod(source.get(), 79);
		for (Uint8 sourceAlpha : {127, 255})
			for (Uint8 drawAlpha : {127, 255})
			{
				SDL_FillSurfaceRect(source.get(), nullptr,
									SDL_MapSurfaceRGBA(source.get(), 151, 89, 43, sourceAlpha));
				for (auto *target : {expected.get(), actual.get()})
					SDL_FillSurfaceRect(target, nullptr, 0xff376b91);
				for (auto *backend : {reference.get(), optimized.get()})
				{
					backend->blit(source.get(), source.get(), sourceAlpha, sourceAlpha == 255,
								  SDL_Rect{0, 0, 8, 8}, SDL_FRect{2, 2, 8, 8}, drawAlpha);
					backend->flush();
				}
				INFO("source alpha=", int(sourceAlpha), " draw alpha=", int(drawAlpha),
					 " actual=", pixel(actual.get(), 3, 3),
					 " expected=", pixel(expected.get(), 3, 3));
				CHECK(snapshot(actual.get()) == snapshot(expected.get()));
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
	TEST_CASE("translucent fills preserve colors on 32-bit targets without alpha")
	{
		Surface expected(SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_XRGB8888), SDL_DestroySurface);
		Surface actual(SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_XRGB8888), SDL_DestroySurface);
		REQUIRE(expected);
		REQUIRE(actual);
		auto reference = makeSDLSoftwareGeometryBackend(expected.get());
		auto optimized = makeSoftwareRenderBackend(actual.get());
		for (Uint8 alpha : {0, 63, 127, 255})
		{
			for (auto *target : {expected.get(), actual.get()})
				SDL_FillSurfaceRect(target, nullptr, SDL_MapSurfaceRGB(target, 71, 83, 29));
			for (auto *backend : {reference.get(), optimized.get()})
			{
				backend->fill(SDL_FRect{2, 2, 8, 8}, SDL_Color{151, 89, 43, alpha});
				backend->flush();
			}
			Uint8 er, eg, eb, ar, ag, ab;
			SDL_GetRGB(pixel(expected.get(), 3, 3), SDL_GetPixelFormatDetails(expected->format),
					   nullptr, &er, &eg, &eb);
			SDL_GetRGB(pixel(actual.get(), 3, 3), SDL_GetPixelFormatDetails(actual->format),
					   nullptr, &ar, &ag, &ab);
			CHECK(er == ar);
			CHECK(eg == ag);
			CHECK(eb == ab);
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
    for (const char *execution : {"auto", "serial", "parallel"})
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
    for (const char *execution : {"auto", "serial", "parallel"})
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
        ComputeExecutor executor;
        executor.configure(3);
        GAGCore::PresentationWorkScope scope(executor);
        auto target = pixels(269, 261);
        auto backend = makeSoftwareRenderBackend(target.get());
        for (int y = 0; y < 261; ++y)
            for (int x = 0; x < 269; ++x)
            {
                const Uint32 value = 0xff000000 | ((x * 17u + y * 43u) & 0xffffff);
                std::memcpy(static_cast<Uint8 *>(target->pixels) + y * target->pitch + x * 4,
                            &value, 4);
            }
        auto borrowed = Surface(
            SDL_CreateSurfaceFrom(20, 17, target->format,
                                  static_cast<Uint8 *>(target->pixels) + 5 * target->pitch + 3 * 4,
                                  target->pitch),
            SDL_DestroySurface);
        REQUIRE(borrowed);
        backend->fillPixels({3, 5, 20, 17}, {71, 117, 191, 255});
        backend->blitPixels(borrowed.get(), {0, 0, 20, 17}, {134, 128, 40, 34}, 173);
        for (int i = 0; i < 1030; ++i)
            backend->fillPixels({i % 269, i % 261, 3, 2}, {Uint8(i), 31, 77, 173});
        backend->flush();
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
