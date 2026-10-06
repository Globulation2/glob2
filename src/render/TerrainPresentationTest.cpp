// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#include "EngineFixtures.h"
#include <Environment.h>
#include "ScopedEnvironment.h"
#include <optional>
#include <fstream>
#include "TerrainPresentation.h"
#include "TerrainRegistry.h"
#include "terrain/TerrainCompositor.h"
#include "terrain/TerrainCatalogIO.h"
#include "scene/SceneMap.h"
#include "SoftwareTerrainCache.h"
#include "MapRenderState.h"
#include "RessourceType.h"
#include "MapThumbnail.h"
#include "MapImage.h"
#include "GenerationRequest.h"
#include "GameGUI.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <SDL3_image/SDL_image.h>
#include <RenderBackend.h>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif
#include <algorithm>
#include <cstring>
#include <cmath>
#include <string_view>

namespace
{
void importBeforeMatch(Game &game, std::string_view definitions)
{
	// Detached maps permit pre-match authoring; active games reject registry replacement.
	game.map.game = nullptr;
	game.map.importTerrainDefinitions(definitions);
	game.map.setGame(&game);
}

// Preserve real portable rendering while making device limits strict and visible.
// No test-only limit controls are needed in production rendering code.
class LimitedTerrainBackend final : public GAGCore::RenderBackend
{
	GAGCore::RenderBackend &delegate;
	void check(SDL_Surface *pixels)
	{
		if (!pixels)
			return;
		REQUIRE(pixels->w <= limit);
		REQUIRE(pixels->h <= limit);
		largestTexture = std::max({largestTexture, pixels->w, pixels->h});
	}

  public:
	int limit = 1024, largestTexture = 0;
	explicit LimitedTerrainBackend(GAGCore::RenderBackend &backend) : delegate(backend) {}
	int maximumTextureSize() const override { return limit; }
	void prepareTexture(const void *key, SDL_Surface *pixels, std::uint64_t revision) override
	{
		check(pixels);
		delegate.prepareTexture(key, pixels, revision);
	}
	void blit(const void *key, SDL_Surface *pixels, std::uint64_t revision, bool opaque,
			  const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) override
	{
		check(pixels);
		delegate.blit(key, pixels, revision, opaque, src, dst, alpha);
	}
	void blitLinear(const void *key, SDL_Surface *pixels, std::uint64_t revision,
					const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) override
	{
		check(pixels);
		delegate.blitLinear(key, pixels, revision, src, dst, alpha);
	}
	void triangles(std::span<const SDL_Vertex> vertices, const void *key, SDL_Surface *pixels,
				   std::uint64_t revision) override
	{
		check(pixels);
		delegate.triangles(vertices, key, pixels, revision);
	}
	void fill(const SDL_FRect &rect, SDL_Color color) override { delegate.fill(rect, color); }
	void clip(const SDL_Rect *rect) override { delegate.clip(rect); }
	void transform(float scale, float x, float y, const SDL_Rect *bounds) override
	{
		delegate.transform(scale, x, y, bounds);
	}
	void screenTriangles(std::span<const SDL_Vertex> vertices) override
	{
		delegate.screenTriangles(vertices);
	}
	void forget(const void *key) override { delegate.forget(key); }
	void reset() override { delegate.reset(); }
	void present() override { delegate.present(); }
	void flush() override { delegate.flush(); }
	void logicalSize(int width, int height) override { delegate.logicalSize(width, height); }
	void nativeLogicalSize(int width, int height) override
	{
		delegate.nativeLogicalSize(width, height);
	}
	void bindTarget(SDL_Surface *surface) override { delegate.bindTarget(surface); }
	SDL_Surface *capture() override { return delegate.capture(); }
	void outputSize(int &width, int &height) override { delegate.outputSize(width, height); }
	GAGCore::RenderOperations operations() const override { return delegate.operations(); }
};
class ScopedTerrainDeviceLimit
{
	GAGCore::GraphicContext &context;
	std::unique_ptr<GAGCore::RenderBackend> original;
	GAGCore::RenderBackend *previous;

  public:
	explicit ScopedTerrainDeviceLimit(GAGCore::GraphicContext &gfx)
		: context(gfx), original(std::move(gfx.portableRenderer)), previous(gfx.renderer)
	{
		REQUIRE(original);
		// Construct before publishing the wrapper so failure restores ownership.
		try
		{
			context.portableRenderer = std::make_unique<LimitedTerrainBackend>(*original);
			context.renderer = context.portableRenderer.get();
		}
		catch (...)
		{
			context.portableRenderer = std::move(original);
			throw;
		}
	}
	~ScopedTerrainDeviceLimit()
	{
		context.portableRenderer.reset();
		context.portableRenderer = std::move(original);
		context.renderer = previous;
	}
	LimitedTerrainBackend &backend()
	{
		return static_cast<LimitedTerrainBackend &>(*context.portableRenderer);
	}
};
std::vector<Uint8> terrainPixels(bool gpu)
{
	auto *gfx = globalContainer->gfx;
	GAGCore::Sprite::flushBatches(gfx);
#ifdef HAVE_OPENGL
	if (gpu)
	{
		glFinish();
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		std::vector<Uint8> result(viewport[2] * viewport[3] * 4);
		glReadPixels(0, 0, viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, result.data());
		return result;
	}
#endif
	auto *surface = SDL_ConvertSurface(gfx->getSDLSurface(), SDL_PIXELFORMAT_RGBA32);
	REQUIRE(surface);
	std::vector<Uint8> result(surface->w * surface->h * 4);
	for (int y = 0; y < surface->h; ++y)
		std::memcpy(result.data() + y * surface->w * 4,
					static_cast<Uint8 *>(surface->pixels) + y * surface->pitch, surface->w * 4);
	SDL_DestroySurface(surface);
	return result;
}
std::filesystem::path highResolutionFixture()
{
	const auto directory = glob2test::artifactDir() / "synthetic-hd";
	std::filesystem::create_directories(directory);
	std::filesystem::copy_file(
		glob2test::sourceRoot() / "test/fixtures/image-assets/terrain-hd-solid.webp",
		directory / "grass.webp", std::filesystem::copy_options::overwrite_existing);
	std::ofstream frames(directory / "frames.txt");
	frames << "GLOB2_HIGHRES 1\n";
	for (int variant = 0; variant < 16; ++variant)
		frames << "terrain" << variant << " 32 32 4 grass.webp -\n";
	return directory;
}

void layeredCache(bool gpu, bool hd = false)
{
	std::optional<glob2test::ScopedEnvironment> hdPath;
	if (hd)
	{
		hdPath.emplace("GLOB2_EXPERIMENT_TEXTURE_DIR", highResolutionFixture().string().c_str());
	}
	glob2test::HeadlessGlobals globals(
		{.display = true,
		 .width = 640,
		 .height = 480,
		 .screenFlags = gpu ? Uint32(GAGCore::GraphicContext::USEGPU) : 0u});
	glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true});
	auto &game = fixture.game;
	auto &map = game.map;
	if (hd)
	{
		GAGCore::Sprite::setHighResolution(true);
		REQUIRE(globals->terrain->baseFrame(0)->getW() == 128);
	}
	// Exercise every edge mask, torus neighbors, water backdrop, and mixed layers.
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 32; ++x)
		{
			if (x < 3)
				map.setTerrain(x, y, 256);
			if ((x + y * 3) % 7 == 0)
				map.setCellTerrain(x, y, ICE);
			else if ((x * 5 + y) % 11 == 0)
				map.setCellTerrain(x, y, TRAIL);
		}
	SceneMap scene;
	scene.extract(map);

	SoftwareTerrainCache cache;
	auto clear = [&]
	{
		globals->gfx->setClipRect();
		globals->gfx->drawFilledRect(0, 0, 640, 480, 17, 29, 41);
		game.drawMapWater(640, 480, 29, 30, 19);
	};
	bool memoryRecorded = false;
	const auto compare = [&]
	{
		clear();
		game.drawMapTerrain(0, 0, 19, 14, 29, 30, 0, Game::DRAW_WHOLE_MAP, scene);
		const auto expected = terrainPixels(gpu);
		clear();
		const auto gpuBefore = GAGCore::DrawableSurface::allocatedTextureBytes();
		REQUIRE(
			cache.prepare(scene, *globals->terrain, 0, 0, 19, 14, 29, 30, fixture.team->me, true));
		cache.draw(*globals->gfx);
		CHECK(terrainPixels(gpu) == expected);
		if (!memoryRecorded)
		{
			std::ofstream memory(glob2test::artifactDir() / "cache-memory.txt");
			memory << "cache_cpu_accounting_bytes "
				   << cache.chunks.size() * (SoftwareTerrainCache::ChunkStorageBytes +
											 (cache.resolution * cache.resolution - 1) *
												 SoftwareTerrainCache::ChunkPixels *
												 SoftwareTerrainCache::ChunkPixels * 4)
				   << '\n';
			memory << "cache_gpu_allocated_bytes "
				   << GAGCore::DrawableSurface::allocatedTextureBytes() - gpuBefore << '\n';
			memory << "prepared_source_bytes " << globals->terrainCompositor().sourceBytes()
				   << '\n';
			memoryRecorded = true;
		}
	};
	const auto checksum = fixture.checksum();
	compare();
	compare();
	if (hd)
	{
		CHECK(globals->terrainCompositor().scale() == 4);
		globals->gfx->beginMapTransform(.73f, .375f, .625f, 0, 0, 640, 480);
		compare();
		compare();
		globals->gfx->endMapTransform();
		CHECK(cache.bytes() <= SoftwareTerrainCache::GPUBudget);
	}
	CHECK(fixture.checksum() == checksum);
	if (!gpu)
		CHECK(cache.cacheHits() > 0);
	auto &compositor = globals->terrainCompositor();
	const auto before = compositor.describe(scene, 0, 0);
	map.setCellTerrain(0, 0, TRAIL);
	CHECK(compositor.describe(scene, 0, 0) == before);
	scene.extract(map);
	compare();
	// A scene retains its registry snapshot. Reimport changes material bindings
	// only on extraction, and invalidates both software and GPU composed pages.
	importBeforeMatch(
		game,
		R"({"schemaVersion":1,"terrains":[{"key":"test:custom","name":"Custom ice","base":"grass","properties":{},"appearance":"ice"}]})");
	const auto custom = *map.terrainRegistry().find("test:custom");
	map.setCellTerrain(0, 0, custom);
	scene.extract(map);
	compare();
	compare();
	const auto previousRegistry = scene.frozenTerrainRegistry();
	const auto previousRecipe = compositor.describe(scene, 0, 0);
	importBeforeMatch(
		game,
		R"({"schemaVersion":1,"terrains":[{"key":"test:custom","name":"Custom trail","base":"grass","properties":{},"appearance":"road"}]})");
	CHECK(scene.frozenTerrainRegistry() == previousRegistry);
	CHECK(compositor.describe(scene, 0, 0) == previousRecipe);
	scene.extract(map);
	CHECK(scene.appearanceAt(0, 0) == TRAIL);
	CHECK_FALSE(compositor.describe(scene, 0, 0) == previousRecipe);
	compare();
	// The terrain look seed travels with the extraction and is part of every
	// recipe, so a reroll recomposes software and GPU pages alike.
	const auto seededRecipe = compositor.describe(scene, 0, 0);
	map.setTerrainSeed(map.terrainSeed() + 1);
	CHECK(compositor.describe(scene, 0, 0) == seededRecipe);
	scene.extract(map);
	CHECK(compositor.describe(scene, 0, 0).seed == map.terrainSeed());
	CHECK_FALSE(compositor.describe(scene, 0, 0) == seededRecipe);
	compare();
	compare();
	// Content revisions invalidate prepared source pixels and composed pages.
	auto *source = globals->terrain->nativeFrame(272);
	source->drawFilledRect(0, 0, 32, 32, 201, 23, 189);
	compare();
	compare();
	if (!gpu)
		REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(),
							(glob2test::artifactDir() / "terrain-layers.png").string().c_str()));
}
} // namespace
TEST_SUITE("TerrainPresentation")
{
	TEST_CASE("overview palette follows detailed shore coverage across both wrapped axes [display] "
			  "[artifacts]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 512, .height = 512});
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4, .discovered = true});
		auto &map = fixture.game.map;
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
				map.setUMTerrain(x, y, x < 4 || y < 4 ? WATER : x < 7 || y < 7 ? SAND : GRASS);
		map.regenerateMap(0, 0, 16, 16);
		map.setCellTerrain(10, 10, ICE);
		SceneMap scene;
		scene.extract(map);
		auto &compositor = globals->terrainCompositor();
		compositor.prepare(false, 0);
		constexpr int samples = TerrainVisual::Compositor::OverviewSamples;
		GAGCore::DrawableSurface overview(16 * samples, 16 * samples);
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
			{
				const auto recipe = compositor.describe(scene, x, y);
				compositor.composeOverview(recipe, overview.getSDLSurface(), x * samples,
										   y * samples);
				const TerrainVisual::PreparedCoverage coverage(compositor.catalog(), recipe);
				for (int sy = 0; sy < samples; ++sy)
					for (int sx = 0; sx < samples; ++sx)
					{
						CAPTURE(x);
						CAPTURE(y);
						CAPTURE(sx);
						CAPTURE(sy);
						const auto mask = coverage.at((sx * 32 / samples + 16 / samples) * 256,
													  (sy * 32 / samples + 16 / samples) * 256);
						unsigned expected[3]{};
						for (int i = 0; i < 4; ++i)
							for (int k = 0; k < 3; ++k)
								expected[k] +=
									mask.weight[i] *
									compositor.catalog().materials[mask.material[i]].preview[k];
						const auto *row = reinterpret_cast<const Uint32 *>(
							static_cast<const Uint8 *>(overview.getSDLSurface()->pixels) +
							(y * samples + sy) * overview.getSDLSurface()->pitch);
						const auto pixel = row[x * samples + sx];
						CHECK((pixel >> 24) == 255);
						for (int k = 0; k < 3; ++k)
							CHECK(((pixel >> (16 - 8 * k)) & 255) == (expected[k] + 32768) / 65536);
					}
			}
		// Custom saved whole-cell palettes still override their appearance colour.
		auto custom = compositor.describe(scene, 10, 10);
		custom.samples.fill(compositor.catalog().bindings.at("ice"));
		const std::array<unsigned char, 3> customColor{17, 31, 47};
		GAGCore::DrawableSurface customOverview(samples, samples);
		compositor.composeOverview(custom, customOverview.getSDLSurface(), 0, 0, &customColor);
		for (int y = 0; y < samples; ++y)
			for (int x = 0; x < samples; ++x)
				CHECK(reinterpret_cast<const Uint32 *>(
						  static_cast<const Uint8 *>(customOverview.getSDLSurface()->pixels) +
						  y * customOverview.getSDLSurface()->pitch)[x] == 0xFF111F2Fu);
		MapRenderState render;
		render.detail.terrainOverview = .5f;
		map.setResource(9, 9, WHEAT, 1);
		scene.extract(map);
		Game::drawMapOverview(0, 0, 15, 15, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene, render);
		REQUIRE(render.overview->getW() == overview.getW());
		REQUIRE(render.overview->getH() == overview.getH());
		const auto *resource = globals->resourcesTypes.get(WHEAT);
		const int tint[] = {resource->minimapR, resource->minimapG, resource->minimapB};
		for (int y = 0; y < 16 * samples; ++y)
			for (int x = 0; x < 16 * samples; ++x)
			{
				const auto pixelAt = [&](auto &image)
				{
					auto *surface = image.getSDLSurface();
					return reinterpret_cast<const Uint32 *>(
						static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch)[x];
				};
				const auto ground = pixelAt(overview), actual = pixelAt(*render.overview);
				if (x / samples == 9 && y / samples == 9)
					for (int k = 0; k < 3; ++k)
						CHECK(((actual >> (16 - 8 * k)) & 255) ==
							  (((ground >> (16 - 8 * k)) & 255) + 3 * tint[k]) / 4);
				else
					CHECK(actual == ground);
			}
		// Preserve review evidence of the same coastline in both looks and mid-fade.
		GAGCore::DrawableSurface enlarged(512, 512);
		REQUIRE(SDL_BlitSurfaceScaled(overview.getSDLSurface(), nullptr, enlarged.getSDLSurface(),
									  nullptr, SDL_SCALEMODE_NEAREST));
		const auto evidence = glob2test::artifactDir() / "overview-alignment";
		std::filesystem::create_directories(evidence);
		CHECK(IMG_SavePNG(enlarged.getSDLSurface(), (evidence / "overview.png").string().c_str()));
		Game::drawMapWater(512, 512, 0, 0, 0);
		Game::drawMapTerrain(0, 0, 15, 15, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		Game::drawMapResources(0, 0, 15, 15, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		GAGCore::Sprite::flushBatches(globals->gfx);
		if (globals->gfx->renderer)
			globals->gfx->renderer->flush();
		CHECK(IMG_SavePNG(globals->gfx->getSDLSurface(),
						  (evidence / "detailed.png").string().c_str()));
		Game::drawMapOverview(0, 0, 15, 15, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene, render);
		GAGCore::Sprite::flushBatches(globals->gfx);
		if (globals->gfx->renderer)
			globals->gfx->renderer->flush();
		CHECK(IMG_SavePNG(globals->gfx->getSDLSurface(),
						  (evidence / "crossfade.png").string().c_str()));
	}

	TEST_CASE("terrain seed survives save and load and stays out of the simulation checksum")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture({.wDec = 5,
										 .hDec = 5,
										 .discovered = true,
										 .loadDefaultRace = true,
										 .header = true,
										 .seed = 7331});
		auto &map = fixture.game.map;
		const auto checksum = fixture.checksum();
		map.setTerrainSeed(0x1234abcdu);
		CHECK(fixture.checksum() == checksum);
		auto *backend = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream out(backend);
		fixture.game.save(&out, true, "terrain seed");
		out.flush();
		const std::string bytes = backend->takeContents();
		GameGUI restored(false);
		GAGCore::BinaryInputStream input(
			new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
		input.seekFromStart(0);
		REQUIRE(restored.game.load(&input));
		CHECK(restored.game.map.terrainSeed() == 0x1234abcdu);
		SceneMap scene;
		scene.extract(restored.game.map);
		CHECK(scene.terrainSeed() == 0x1234abcdu);
	}
	TEST_CASE("catalog palettes preserve distinct legacy shores and independent preview colors")
	{
		glob2test::HeadlessGlobals globals;
		auto catalog = TerrainVisual::loadCatalog();
		auto &water = catalog.materials[catalog.bindings.at("water")];
		water.minimap = {1, 2, 3};
		water.preview = {4, 5, 6};
		catalog.bindings["grass"] = catalog.bindings.at("ice");
		const auto minimap = TerrainVisual::minimapPalette(catalog);
		const auto overview = TerrainVisual::overviewPalette(catalog);
		const auto check = [](TerrainColor actual, TerrainColor expected)
		{
			CHECK(actual.r == expected.r);
			CHECK(actual.g == expected.g);
			CHECK(actual.b == expected.b);
		};
		check(minimap[WATER], {1, 2, 3});
		check(overview[WATER], {4, 5, 6});
		check(minimap[GRASS], minimap[ICE]);
		check(overview[GRASS], overview[ICE]);
		for (auto shore : {GRASS_SAND_SHORE, SAND_WATER_SHORE})
		{
			check(minimap[shore], terrainPresentation(shore).minimap);
			check(overview[shore], terrainPresentation(shore).overview);
		}
	}
	TEST_CASE("custom aliases share material recipes and keep whole-cell identity [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4});
		auto &map = fixture.game.map;
		importBeforeMatch(fixture.game, R"({"schemaVersion":1,"terrains":[
			{"key":"test:ice-a","name":"Ice A","base":"grass","properties":{},"appearance":"ice"},
			{"key":"test:ice-b","name":"Ice B","base":"sand","properties":{},"appearance":"ice"},
			{"key":"test:sand","name":"Custom sand","base":"grass","properties":{},"appearance":"sand"}
		]})");
		const auto a = *map.terrainRegistry().find("test:ice-a");
		const auto b = *map.terrainRegistry().find("test:ice-b");
		const auto sand = *map.terrainRegistry().find("test:sand");
		auto &compositor = globals->terrainCompositor();
		constexpr int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
		const TerrainType aliases[] = {ICE, a, b, a};
		map.setCellTerrain(0, 0, TRAIL);
		for (unsigned mask = 0; mask < 16; ++mask)
		{
			CAPTURE(mask);
			for (int side = 0; side < 4; ++side)
				map.setCellTerrain(dx[side], dy[side], mask & (1u << side) ? ICE : TRAIL);
			SceneMap expected;
			expected.extract(map);
			for (int side = 0; side < 4; ++side)
				map.setCellTerrain(dx[side], dy[side], mask & (1u << side) ? aliases[side] : TRAIL);
			SceneMap actual;
			actual.extract(map);
			for (int y = -1; y <= 1; ++y)
				for (int x = -1; x <= 1; ++x)
					CHECK(compositor.describe(actual, x, y) == compositor.describe(expected, x, y));
			for (int side = 0; side < 4; ++side)
				CHECK(actual.terrainTypeAt(dx[side], dy[side]) ==
					  (mask & (1u << side) ? aliases[side] : TRAIL));
		}
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
				map.setCellTerrain(x, y, sand);
		SceneMap scene;
		scene.extract(map);
		CHECK(scene.presentationTypeAt(0, 0) == sand);
		CHECK(scene.appearanceAt(0, 0) == SAND);
		const auto material = compositor.catalog().bindings.at("sand");
		for (const auto sample : compositor.describe(scene, 0, 0).samples)
			CHECK(sample == material);
	}
	TEST_CASE("custom thumbnail palettes retain embedded colors alongside catalog builtins")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4, .teams = 0});
		auto &map = fixture.game.map;
		importBeforeMatch(
			fixture.game,
			R"({"schemaVersion":1,"terrains":[{"key":"test:custom","name":"Custom","base":"grass","properties":{},"appearance":"sand"}]})");
		const auto custom = *map.terrainRegistry().find("test:custom");
		map.setCellTerrain(0, 0, custom);
		map.setCellTerrain(1, 0, ICE);
		MapThumbnail thumbnail;
		thumbnail.loadFromMap(map);
		REQUIRE(thumbnail.pixels());
		REQUIRE(thumbnail.pixels()->width == 16);
		const auto palette = TerrainVisual::minimapPalette(TerrainVisual::loadCatalog());
		const TerrainColor expected[] = {map.terrainPresentation(custom).preview, palette[ICE]};
		for (int x = 0; x < 2; ++x)
		{
			CHECK(thumbnail.pixels()->rgb[x * 3] == expected[x].r);
			CHECK(thumbnail.pixels()->rgb[x * 3 + 1] == expected[x].g);
			CHECK(thumbnail.pixels()->rgb[x * 3 + 2] == expected[x].b);
		}
	}
	TEST_CASE("whole-cell layers preserve software terrain caching [display][artifacts]")
	{
		layeredCache(false);
	}
#ifdef HAVE_OPENGL
	TEST_CASE("whole-cell layers preserve OpenGL terrain caching [display]")
	{
		layeredCache(true);
	}
	TEST_CASE("HD material pages and fractional zoom preserve cached pixels with missing optional "
			  "atlas [display][artifacts]")
	{
		layeredCache(true, true);
	}
#endif
	TEST_CASE(
		"streaming pages match cached fractional pixels across wraps and small maps [display]")
	{
		for (bool gpu : {false, true})
			for (bool hd : {false, true})
				for (bool smallMap : {false, true})
				{
#ifndef HAVE_OPENGL
					if (gpu)
						continue;
#endif
					if (!gpu && hd)
						continue; // Software terrain uses native source pixels.
					CAPTURE(gpu);
					CAPTURE(hd);
					CAPTURE(smallMap);
					std::optional<glob2test::ScopedEnvironment> hdPath;
					if (hd)
						hdPath.emplace("GLOB2_EXPERIMENT_TEXTURE_DIR",
									   highResolutionFixture().string().c_str());
					glob2test::HeadlessGlobals globals(
						{.display = true,
						 .width = 256,
						 .height = 256,
						 .screenFlags = gpu ? Uint32(GAGCore::GraphicContext::USEGPU) : 0u});
					glob2test::HeadlessGame fixture(
						{.wDec = smallMap ? 3 : 5, .hDec = smallMap ? 3 : 5, .discovered = true});
					auto &map = fixture.game.map;
					for (int y = 0; y < map.getH(); ++y)
						for (int x = 0; x < map.getW(); ++x)
							map.setCellTerrain(
								x, y,
								x < 3 ? WATER : (y == 4 ? TRAIL : ((x + y) % 3 ? ICE : GRASS)));
					GAGCore::Sprite::setHighResolution(hd);
					SceneMap scene;
					scene.extract(map);
					const int vx = map.getW() - 3, vy = map.getH() - 2;
					const auto begin = [&]
					{
						globals->gfx->drawFilledRect(0, 0, 256, 256, 17, 29, 41);
						globals->gfx->beginMapTransform(.73f, .375f, .625f, 0, 0, 256, 256);
					};
					SoftwareTerrainCache cache;
					begin();
					REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 7, 7, vx, vy,
										  fixture.team->me, true));
					cache.draw(*globals->gfx);
					globals->gfx->endMapTransform();
					const auto expected = terrainPixels(gpu);
					const auto gpuBefore = GAGCore::DrawableSurface::allocatedTextureBytes();
					begin();
					fixture.game.drawMapTerrain(0, 0, 7, 7, vx, vy, 0, Game::DRAW_WHOLE_MAP, scene);
					globals->gfx->endMapTransform();
					CHECK(terrainPixels(gpu) == expected);
					CHECK(GAGCore::DrawableSurface::allocatedTextureBytes() == gpuBefore);
				}
	}
	TEST_CASE("emergency tiles remain deterministic and cover opaque fractional terrain [display]")
	{
		for (bool gpu : {false, true})
			for (bool hd : {false, true})
			{
#ifndef HAVE_OPENGL
				if (gpu)
					continue;
#endif
				if (!gpu && hd)
					continue;
				CAPTURE(gpu);
				CAPTURE(hd);
				std::optional<glob2test::ScopedEnvironment> hdPath;
				if (hd)
					hdPath.emplace("GLOB2_EXPERIMENT_TEXTURE_DIR",
								   highResolutionFixture().string().c_str());
				glob2test::HeadlessGlobals globals(
					{.display = true,
					 .width = 256,
					 .height = 256,
					 .screenFlags = gpu ? Uint32(GAGCore::GraphicContext::USEGPU) : 0u});
				glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true});
				for (int y = 0; y < 32; ++y)
					for (int x = 0; x < 32; ++x)
						fixture.game.map.setCellTerrain(
							x, y, y == 4 ? TRAIL : ((x + y) % 3 ? ICE : GRASS));
				GAGCore::Sprite::setHighResolution(hd);
				SceneMap scene;
				scene.extract(fixture.game.map);
				const auto checksum = fixture.checksum();
				const auto draw = [&](float zoom, bool emergency)
				{
					globals->gfx->drawFilledRect(0, 0, 256, 256, 251, 3, 249);
					globals->gfx->beginMapTransform(zoom, 0, 0, 0, 0, 256, 256);
					if (emergency)
						SoftwareTerrainCache::drawUncached(
							scene, *globals->terrain, 0, 0, 7, 7, 0, 0, fixture.team->me, true, 0,
							SoftwareTerrainCache::FallbackMode::EmergencyTiles);
					else
					{
						SoftwareTerrainCache cache;
						REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 7, 7, 0, 0,
											  fixture.team->me, true));
						cache.draw(*globals->gfx);
					}
					globals->gfx->endMapTransform();
					return terrainPixels(gpu);
				};
				if (!hd)
					CHECK(draw(1.f, true) == draw(1.f, false));
				const auto pixels = draw(.73f, true);
				CHECK(pixels == draw(.73f, true));
				// Full opacity must hide the conspicuous clear color at every shared
				// tile edge. Do not confuse this invariant with equal resampling phases:
				// emergency tile mip/nearest sampling differs from page sampling.
				const int side = int(std::sqrt(pixels.size() / 4));
				REQUIRE(side * side * 4 == int(pixels.size()));
				unsigned holes = 0;
				for (int y = 10 * side / 256; y < 170 * side / 256; ++y)
					for (int x = 10 * side / 256; x < 170 * side / 256; ++x)
					{
						const int row = gpu ? side - 1 - y : y;
						const auto at = (row * side + x) * 4;
						holes += pixels[at] == 251 && pixels[at + 1] == 3 && pixels[at + 2] == 249;
					}
				CHECK(holes == 0);
				CHECK(fixture.checksum() == checksum);
			}
	}
	TEST_CASE("tiled capture keeps whole-map density and warm pages at narrow edges [display]")
	{
		glob2test::ScopedEnvironment hdPath("GLOB2_EXPERIMENT_TEXTURE_DIR",
											highResolutionFixture().string().c_str());
		glob2test::HeadlessGlobals globals(
			{.display = true,
			 .width = 256,
			 .height = 256,
			 .screenFlags = Uint32(GAGCore::GraphicContext::PORTABLEGPU)});
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 6, .discovered = true});
		GAGCore::Sprite::setHighResolution(true);
		SceneMap scene;
		scene.extract(fixture.game.map);
		// An independently sized strip fits HD pages, unlike its whole capture.
		SoftwareTerrainCache independent;
		REQUIRE(independent.prepare(scene, *globals->terrain, 0, 0, 6, 63, 0, 0, fixture.team->me,
									true));
		CHECK(globals->terrainCompositor().scale() == 4);
		CHECK(independent.resolution == 2);
		SoftwareTerrainCache tiled;
		REQUIRE(tiled.prepare(scene, *globals->terrain, 0, 0, 63, 63, 0, 0, fixture.team->me, true,
							  0, true));
		CHECK(tiled.resolution == 1);
		const auto rebuilds = tiled.cacheRebuilds();
		for (const auto &strip : {SDL_Rect{0, 0, 7, 64}, SDL_Rect{0, 0, 64, 7}})
		{
			REQUIRE(tiled.prepare(scene, *globals->terrain, strip.x, strip.y, strip.x + strip.w - 1,
								  strip.y + strip.h - 1, 0, 0, fixture.team->me, true, 0, true));
			CHECK(tiled.resolution == 1);
			CHECK(tiled.cacheRebuilds() == rebuilds);
			CHECK(tiled.bytes() <= SoftwareTerrainCache::GPUBudget);
		}
	}
	TEST_CASE(
		"portable texture limits reduce HD pages and admit only fitting fallback tiles [display]")
	{
		glob2test::ScopedEnvironment hdPath("GLOB2_EXPERIMENT_TEXTURE_DIR",
											highResolutionFixture().string().c_str());
		glob2test::HeadlessGlobals globals(
			{.display = true,
			 .width = 256,
			 .height = 256,
			 .screenFlags = Uint32(GAGCore::GraphicContext::PORTABLEGPU)});
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true});
		GAGCore::Sprite::setHighResolution(true);
		SceneMap scene;
		scene.extract(fixture.game.map);
		ScopedTerrainDeviceLimit device(*globals->gfx);
		auto &backend = device.backend();
		for (int limit : {1024, 512, 128, 32})
		{
			CAPTURE(limit);
			backend.limit = limit;
			backend.largestTexture = 0;
			CHECK(globals->gfx->maximumTextureSize() == limit);
			SoftwareTerrainCache cache;
			const bool admitted =
				cache.prepare(scene, *globals->terrain, 0, 0, 7, 7, 0, 0, fixture.team->me, true);
			CHECK(globals->terrainCompositor().scale() == 4);
			CHECK(admitted == (limit >= SoftwareTerrainCache::ChunkPixels));
			if (admitted)
			{
				CHECK(cache.resolution == limit / SoftwareTerrainCache::ChunkPixels);
				cache.draw(*globals->gfx);
				CHECK(backend.largestTexture == limit);
			}
			else
			{
				CHECK(cache.bytes() == 0);
				SoftwareTerrainCache::drawUncached(scene, *globals->terrain, 0, 0, 7, 7, 0, 0,
												   fixture.team->me, true);
				CHECK(backend.largestTexture == 32);
			}
			globals->gfx->renderer->flush();
		}
		backend.limit = 16;
		backend.largestTexture = 0;
		CHECK_THROWS_AS(SoftwareTerrainCache::drawUncached(scene, *globals->terrain, 0, 0, 7, 7, 0,
														   0, fixture.team->me, true),
						std::runtime_error);
		CHECK(backend.largestTexture == 0);
	}
	TEST_CASE("zoomed-out GPU terrain retains pages across frames and wrapped views [display] "
			  "[artifacts]")
	{
		for (Uint32 flags : {Uint32(GAGCore::GraphicContext::PORTABLEGPU),
							 Uint32(GAGCore::GraphicContext::USEGPU)})
		{
#ifndef HAVE_OPENGL
			if (flags == GAGCore::GraphicContext::USEGPU)
				continue;
#endif
			CAPTURE(flags);
			const bool portable = flags == GAGCore::GraphicContext::PORTABLEGPU;
			glob2test::HeadlessGlobals globals(
				{.display = true, .width = 1152, .height = 896, .screenFlags = flags});
			glob2test::HeadlessGame fixture({.wDec = 8, .hDec = 8, .discovered = true});
			auto &map = fixture.game.map;
			{
				auto edit = map.editTerrain();
				for (int y = 0; y < map.getH(); ++y)
					for (int x = 0; x < map.getW(); ++x)
						if (x % 32 < 4)
							map.setCellTerrain(x, y, WATER);
						else if (y % 32 < 4)
							map.setCellTerrain(x, y, ICE);
			}
			SceneMap scene;
			scene.extract(map);
			const auto checksum = fixture.checksum();
			auto &gfx = *globals->gfx;
			SoftwareTerrainCache cache;
			bool admitted = false;
			const auto draw = [&](int vx, int vy)
			{
				gfx.drawFilledRect(0, 0, 1152, 896, 17, 29, 41);
				gfx.beginMapTransform(.25f, .375f, .625f, 0, 0, 1152, 896);
				// 80 canonical pages including the partially visible edge pages.
				admitted = cache.prepare(scene, *globals->terrain, 0, 0, 144, 112, vx, vy,
										 fixture.team->me, true, 19);
				if (admitted)
					cache.draw(gfx);
				else
					SoftwareTerrainCache::drawUncached(scene, *globals->terrain, 0, 0, 144, 112, vx,
													   vy, fixture.team->me, true, 19);
				gfx.endMapTransform();
				if (portable)
					gfx.renderer->flush();
#ifdef HAVE_OPENGL
				else
					glFinish();
#endif
			};
			const auto start = std::chrono::steady_clock::now();
			draw(249, 250);
			const auto cold = std::chrono::steady_clock::now();
			const auto rebuilds = cache.cacheRebuilds();
			for (int i = 0; i < 5; ++i)
				draw(249, 250);
			const auto warm = std::chrono::steady_clock::now();
			std::ofstream timing(glob2test::artifactDir() /
								 (portable ? "zoom-portable.txt" : "zoom-opengl.txt"));
			timing << "cached " << admitted << "\ncold_ms "
				   << std::chrono::duration<double, std::milli>(cold - start).count()
				   << "\nwarm_ms "
				   << std::chrono::duration<double, std::milli>(warm - cold).count() / 5
				   << "\nbytes " << cache.bytes() << "\nrebuilds " << cache.cacheRebuilds() << '\n';
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> capture(nullptr,
																				SDL_DestroySurface);
			if (portable)
				capture.reset(gfx.renderer->capture());
			else
			{
				auto rgba = terrainPixels(true);
				capture.reset(SDL_CreateSurface(gfx.getDrawableW(), gfx.getDrawableH(),
												SDL_PIXELFORMAT_RGBA32));
				REQUIRE(capture);
				for (int y = 0; y < capture->h; ++y)
					std::memcpy(static_cast<Uint8 *>(capture->pixels) + y * capture->pitch,
								rgba.data() + (capture->h - 1 - y) * capture->w * 4,
								capture->w * 4);
			}
			REQUIRE(capture);
			REQUIRE(IMG_SavePNG(
				capture.get(),
				(glob2test::artifactDir() / (portable ? "zoom-portable.png" : "zoom-opengl.png"))
					.string()
					.c_str()));
			CHECK(admitted);
			if (!admitted)
				continue;
			CHECK(cache.bytes() <= SoftwareTerrainCache::GPUBudget);
			CHECK(cache.cacheRebuilds() == rebuilds);
			CHECK(cache.cacheHits() >= rebuilds * 5);
			REQUIRE(!cache.chunks.empty());
			CHECK(cache.chunks.front()->image->getW() < SoftwareTerrainCache::ChunkPixels);
			// Check the reduced pixels against independent area averages of native
			// composition, including partially transparent coast tiles.
			const auto &chunk = *cache.chunks.front();
			const int divisor = SoftwareTerrainCache::ChunkPixels / chunk.image->getW();
			const int tileSize = 32 / divisor, count = divisor * divisor;
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> tile(
				SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
			REQUIRE(tile);
			unsigned mismatches = 0, partial = 0;
			for (int ty : {0, 3, 4, 15})
				for (int tx : {0, 3, 4, 15})
				{
					globals->terrainCompositor().compose(chunk.tiles[ty * 16 + tx].recipe,
														 tile.get(), 0, 0, 1);
					for (int y = 0; y < tileSize; ++y)
						for (int x = 0; x < tileSize; ++x)
						{
							unsigned a = 0, red = 0, green = 0, blue = 0;
							for (int dy = 0; dy < divisor; ++dy)
								for (int dx = 0; dx < divisor; ++dx)
								{
									const auto *row = reinterpret_cast<const Uint32 *>(
										static_cast<const Uint8 *>(tile->pixels) +
										(y * divisor + dy) * tile->pitch);
									const Uint32 p = row[x * divisor + dx];
									const unsigned alpha = p >> 24;
									a += alpha;
									red += ((p >> 16) & 255) * alpha;
									green += ((p >> 8) & 255) * alpha;
									blue += (p & 255) * alpha;
								}
							const auto *surface = chunk.image->getSDLSurface();
							const auto *row = reinterpret_cast<const Uint32 *>(
								static_cast<const Uint8 *>(surface->pixels) +
								(ty * tileSize + y) * surface->pitch);
							const Uint32 p = row[tx * tileSize + x];
							partial += a > 0 && a < 255u * count;
							mismatches += (p >> 24) != (a + count / 2) / count ||
										  ((p >> 16) & 255) != (a ? (red + a / 2) / a : 0) ||
										  ((p >> 8) & 255) != (a ? (green + a / 2) / a : 0) ||
										  (p & 255) != (a ? (blue + a / 2) / a : 0);
						}
				}
			CHECK(mismatches == 0);
			CHECK(partial > 0);
			// Equivalent wrapped coordinates reuse the same pages and pixels.
			const auto pixels = [&]
			{
				if (!portable)
					return terrainPixels(true);
				std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> capture(
					gfx.renderer->capture(), SDL_DestroySurface);
				REQUIRE(capture);
				const auto *data = static_cast<const Uint8 *>(capture->pixels);
				return std::vector<Uint8>(data, data + capture->pitch * capture->h);
			};
			const auto expected = pixels();
			draw(-7, -6);
			CHECK(pixels() == expected);
			CHECK(cache.cacheRebuilds() == rebuilds);
			cache.enabled = false;
			draw(-7, -6);
			CHECK(pixels() == expected);
			cache.enabled = true;
			map.setCellTerrain(0, 0, TRAIL);
			scene.extract(map);
			draw(-7, -6);
			CHECK(cache.cacheRebuilds() > rebuilds);
			CHECK(pixels() != expected);
			map.setCellTerrain(0, 0, WATER);
			CHECK(fixture.checksum() == checksum);
			// Returning to a close view restores native page density.
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, 0, 0, fixture.team->me,
								  true, 19));
			CHECK(cache.chunks.front()->image->getW() >= SoftwareTerrainCache::ChunkPixels);
			const int drawableW = gfx.drawableW, drawableH = gfx.drawableH;
			// The upper half of the crossfade on a 2x display lies just above
			// half native density. It must still cache the nearest density.
			gfx.drawableW = gfx.getW() * 2;
			gfx.drawableH = gfx.getH() * 2;
			gfx.beginMapTransform(.28f, 0, 0, 0, 0, 1152, 896);
			const bool retinaAdmitted = cache.prepare(scene, *globals->terrain, 0, 0, 144, 112, 249,
													  250, fixture.team->me, true, 19);
			gfx.endMapTransform();
			gfx.drawableW = drawableW;
			gfx.drawableH = drawableH;
			CHECK(retinaAdmitted);
			// A much denser output must not magnify coarse pages without bound.
			gfx.drawableW = gfx.getW() * 4;
			gfx.drawableH = gfx.getH() * 4;
			gfx.beginMapTransform(.25f, 0, 0, 0, 0, 1152, 896);
			const bool hidpiAdmitted = cache.prepare(scene, *globals->terrain, 0, 0, 144, 112, 249,
													 250, fixture.team->me, true, 19);
			gfx.endMapTransform();
			gfx.drawableW = drawableW;
			gfx.drawableH = drawableH;
			CHECK_FALSE(hidpiAdmitted);
		}
	}
	TEST_CASE("offscreen terrain density follows the target and stays stable across capture tiles "
			  "[display]")
	{
		glob2test::HeadlessGlobals globals(
			{.display = true,
			 .width = 256,
			 .height = 256,
			 .screenFlags = Uint32(GAGCore::GraphicContext::PORTABLEGPU)});
		glob2test::HeadlessGame fixture({.wDec = 8, .hDec = 8, .discovered = true});
		SceneMap scene;
		scene.extract(fixture.game.map);
		auto &gfx = *globals->gfx;
		// Restore all process-wide drawing state even if a REQUIRE aborts.
		struct RestoreTarget
		{
			GAGCore::GraphicContext &gfx;
			int width, height;
			float scale;
			~RestoreTarget()
			{
				gfx.endMapTransform();
				gfx.setRenderTargetScale(scale);
				gfx.drawableW = width;
				gfx.drawableH = height;
			}
		} restore{gfx, gfx.drawableW, gfx.drawableH, gfx.renderTargetScale};
		gfx.beginMapTransform(.25f, 0, 0, 0, 0, 256, 256);
		for (int windowScale : {1, 2})
		{
			CAPTURE(windowScale);
			gfx.drawableW = gfx.getW() * windowScale;
			gfx.drawableH = gfx.getH() * windowScale;
			// Torus captures compensate for the shown zoom: .25 * 4 gives
			// 32 physical pixels per tile, irrespective of the window DPI.
			gfx.setRenderTargetScale(4);
			SoftwareTerrainCache cache;
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, 0, 0, fixture.team->me,
								  true, 19, true));
			CHECK(cache.chunks.front()->image->getW() == SoftwareTerrainCache::ChunkPixels);
			// The same map captured at 8px per cell needs reduced pages. A
			// narrow edge must retain the complete capture's sampling density.
			gfx.setRenderTargetScale(1);
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 31, 31, 0, 0, fixture.team->me,
								  true, 19, true));
			const auto side = cache.chunks.front()->image->getW();
			CHECK(side < SoftwareTerrainCache::ChunkPixels);
			const auto rebuilds = cache.cacheRebuilds();
			for (const auto &strip : {SDL_Rect{0, 0, 7, 32}, SDL_Rect{0, 0, 32, 7}})
			{
				REQUIRE(cache.prepare(scene, *globals->terrain, strip.x, strip.y,
									  strip.x + strip.w - 1, strip.y + strip.h - 1, 0, 0,
									  fixture.team->me, true, 19, true));
				CHECK(cache.chunks.front()->image->getW() == side);
				CHECK(cache.cacheRebuilds() == rebuilds);
				CHECK(cache.bytes() <= SoftwareTerrainCache::GPUBudget);
			}
		}
	}
	TEST_CASE(
		"empty water pages skip software submissions and refresh after terrain edits [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		glob2test::HeadlessGame fixture(
			{.wDec = 5, .hDec = 5, .terrain = WATER, .discovered = true});
		SceneMap scene;
		scene.extract(fixture.game.map);
		SoftwareTerrainCache cache;
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
			SDL_CreateSurface(256, 256, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
		REQUIRE(pixels);
		// A scoped offscreen pass selects the real software backend on every host.
		globals->gfx->drawToSurface(
			pixels.get(), .73f,
			[&]
			{
				const auto draw = [&]
				{
					REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 7, 7, 0, 0,
										  fixture.team->me, true));
					const auto before = globals->gfx->renderer->operations();
					cache.draw(*globals->gfx);
					const auto after = globals->gfx->renderer->operations();
					return after.blits - before.blits + after.triangles - before.triangles;
				};
				CHECK(draw() == 0);
				fixture.game.map.setCellTerrain(4, 4, ICE);
				scene.extract(fixture.game.map);
				CHECK(draw() > 0);
				fixture.game.map.setCellTerrain(4, 4, WATER);
				scene.extract(fixture.game.map);
				CHECK(draw() == 0);
			});
	}
	TEST_CASE("image import keeps whole-cell material edges out of legacy gameplay [artifacts]")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 6, .teams = 0});
		GAGCore::DrawableSurface image(64, 64);
		const auto paint = [&](int x, int y, int w, int h, TerrainType type)
		{
			const auto c = terrainPresentation(type).image;
			image.drawFilledRect(x, y, w, h, c.r, c.g, c.b);
		};
		paint(0, 0, 64, 64, GRASS);
		paint(4, 4, 12, 12, WATER);
		paint(8, 8, 1, 1, TRAIL);
		paint(0, 8, 3, 8, WATER);
		paint(60, 8, 4, 8, WATER);
		paint(0, 10, 1, 1, ICE);
		paint(20, 10, 1, 1, TRAIL);
		image.drawFilledRect(45, 45, 2, 2, 255, 255, 255);
		const auto filename = (glob2test::artifactDir() / "terrain-import-edges.png").string();
		REQUIRE(IMG_SavePNG(image.getSDLSurface(), filename.c_str()));
		GenerationRequest request;
		request.wDec = request.hDec = 6;
		request.nbWorkers = 1;
		request.seed = 901;
		MapImageImportReport report;
		importMapImage(fixture.game, filename, request, 1, report, 0);
		const auto &map = fixture.game.map;
		CHECK(map.terrainTypeAt(8, 8) == TRAIL);
		CHECK(map.terrainTypeAt(7, 8) == WATER);
		CHECK(map.terrainTypeAt(8, 7) == WATER);
		CHECK(map.terrainTypeAt(7, 7) == WATER);
		CHECK_FALSE(map.terrainPropertiesAt(7, 8).walkable);
		CHECK(map.terrainTypeAt(0, 10) == ICE);
		CHECK(map.terrainTypeAt(63, 10) == WATER);
		CHECK(map.terrainTypeAt(20, 10) == TRAIL);
		CHECK(map.terrainTypeAt(19, 10) == GRASS);
		CHECK(map.terrainPropertiesAt(19, 10).buildable);
		// An ordinary grass/water boundary still receives the legacy shore repair.
		CHECK(map.terrainTypeAt(15, 5) != WATER);
		fixture.game.map.rebuildTerrain();
		CHECK(map.terrainTypeAt(7, 8) == WATER);
		CHECK(map.terrainTypeAt(8, 7) == WATER);
		CHECK(map.terrainTypeAt(7, 7) == WATER);
		CHECK(map.terrainTypeAt(63, 10) == WATER);
		CHECK(map.terrainTypeAt(19, 10) == GRASS);
	}
	TEST_CASE("every paintable built-in binds a material, has an editor icon and exports its own colour [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		auto &compositor = globals->terrainCompositor();
		const auto minimap = TerrainVisual::minimapPalette(compositor.catalog());
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .teams = 0});
		auto &map = fixture.game.map;
		std::vector<TerrainType> painted;
		for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
		{
			const auto type = TerrainType(i);
			CAPTURE(std::string(TerrainPresentations[i].name));
			if (!terrainPaintable(type))
				continue;
			CHECK(compositor.catalog().bindings.contains(TerrainPresentations[i].name));
			const auto [sprite, frame] = compositor.editorIcon(type);
			CHECK(sprite != nullptr);
			CHECK(frame < 65536);
			// Classic corner terrain exports through the undermap; whole-cell types
			// export their registered colour.
			if (terrainUsesLegacyCorners(type))
				continue;
			map.setCellTerrain(int(painted.size()) + 1, 1, type);
			painted.push_back(type);
		}
		(void)minimap;
		const auto filename = (glob2test::artifactDir() / "terrain-catalogue-colors.png").string();
		exportMapImage(fixture.game, filename);
		auto *source = IMG_Load(filename.c_str());
		REQUIRE(source);
		auto *image = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
		SDL_DestroySurface(source);
		REQUIRE(image);
		for (std::size_t n = 0; n < painted.size(); ++n)
		{
			CAPTURE(std::string(TerrainPresentations[painted[n]].name));
			const auto c = terrainPresentation(painted[n]).image;
			const auto *pixel = static_cast<Uint8 *>(image->pixels) + image->pitch + (n + 1) * 4;
			CHECK(pixel[0] == c.r);
			CHECK(pixel[1] == c.g);
			CHECK(pixel[2] == c.b);
		}
		SDL_DestroySurface(image);
	}
	TEST_CASE("export uses registered whole-cell material colors [artifacts]")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .teams = 0});
		auto &map = fixture.game.map;
		map.setCellTerrain(1, 1, ICE);
		map.setCellTerrain(2, 1, TRAIL);
		const auto filename = (glob2test::artifactDir() / "terrain-colors.png").string();
		exportMapImage(fixture.game, filename);
		auto *source = IMG_Load(filename.c_str());
		REQUIRE(source);
		auto *image = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
		SDL_DestroySurface(source);
		REQUIRE(image);
		for (int x = 1; x <= 2; ++x)
		{
			const auto c = terrainPresentation(x == 1 ? ICE : TRAIL).image;
			const auto *pixel = static_cast<Uint8 *>(image->pixels) + image->pitch + x * 4;
			CHECK(pixel[0] == c.r);
			CHECK(pixel[1] == c.g);
			CHECK(pixel[2] == c.b);
		}
		SDL_DestroySurface(image);
	}
}
#include <fstream>
#include <chrono>
TEST_SUITE("TerrainValidation")
{
	TEST_CASE("mixed terrain simulation trace and visual gallery [display][artifacts]")
	{
		const auto loadingStart = std::chrono::steady_clock::now();
		glob2test::HeadlessGlobals globals(
			{.display = true, .width = 1024, .height = 768, .seed = 7331});
		const auto graphicsReady = std::chrono::steady_clock::now();
		globals->terrainCompositor().prepare(false, 0);
		const auto sourcesReady = std::chrono::steady_clock::now();
		glob2test::HeadlessGame fixture({.wDec = 5,
										 .hDec = 5,
										 .discovered = true,
										 .loadDefaultRace = true,
										 .header = true,
										 .seed = 7331});
		auto &map = fixture.game.map;
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
			{
				if (x < 8)
					map.setTerrain(x, y, 128);
				if (x < 3)
					map.setTerrain(x, y, 256);
				if ((x - 17) * (x - 17) + (y - 10) * (y - 10) < 42)
					map.setCellTerrain(x, y, ICE);
				if (y == 20 || x == 25 || (x > 9 && x < 24 && y == x - 5))
					map.setCellTerrain(x, y, TRAIL);
			}
		map.setCellTerrain(31, 0, ICE);
		map.setCellTerrain(0, 0, TRAIL);
		fixture.addUnit(WORKER, 17, 10);
		fixture.addUnit(WORKER, 25, 20);
		std::ofstream trace(glob2test::artifactDir() / "checksums.txt");
		for (int t = 0; t < 256; ++t)
		{
			trace << t << ' ' << fixture.checksum() << '\n';
			fixture.step();
		}
		SceneMap scene;
		scene.extract(map);
		auto *gfx = globals->gfx;
		fixture.game.drawMapWater(1024, 768, 0, 0, 19);
		fixture.game.drawMapTerrain(0, 0, 31, 23, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		REQUIRE(IMG_SavePNG(gfx->getSDLSurface(),
							(glob2test::artifactDir() / "terrain-gallery.png").string().c_str()));
		SoftwareTerrainCache cache;
		auto start = std::chrono::steady_clock::now();
		REQUIRE(
			cache.prepare(scene, *globals->terrain, 0, 0, 31, 23, 0, 0, fixture.team->me, true));
		cache.draw(*gfx);
		auto cold = std::chrono::steady_clock::now();
		for (int i = 0; i < 30; ++i)
		{
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 31, 23, 0, 0, fixture.team->me,
								  true));
			cache.draw(*gfx);
		}
		auto warm = std::chrono::steady_clock::now();
		std::ofstream timing(glob2test::artifactDir() / "timing.txt");
		timing << "graphics_loading_ms "
			   << std::chrono::duration<double, std::milli>(graphicsReady - loadingStart).count()
			   << "\nterrain_source_loading_ms "
			   << std::chrono::duration<double, std::milli>(sourcesReady - graphicsReady).count()
			   << '\n';
		timing << "cold_ms " << std::chrono::duration<double, std::milli>(cold - start).count()
			   << "\nwarm_ms "
			   << std::chrono::duration<double, std::milli>(warm - cold).count() / 30
			   << "\ncache_bytes " << cache.bytes() << '\n';
		for (int pattern = 0; pattern < 2; ++pattern)
		{
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x)
				{
					map.setCellTerrain(
						x, y,
						pattern ? ((x + y) % 3 == 0 ? ICE : ((x * 3 + y) % 5 == 0 ? TRAIL : GRASS))
								: GRASS);
				}
			scene.extract(map);
			SoftwareTerrainCache measured;
			auto begin = std::chrono::steady_clock::now();
			REQUIRE(measured.prepare(scene, *globals->terrain, 0, 0, 31, 23, 0, 0, fixture.team->me,
									 true));
			measured.draw(*gfx);
			auto ready = std::chrono::steady_clock::now();
			timing << (pattern ? "dense" : "uniform") << "_cold_ms "
				   << std::chrono::duration<double, std::milli>(ready - begin).count() << '\n';
			for (int moving = 0; moving < 2; ++moving)
			{
				std::vector<double> samples;
				double preparation = 0, drawing = 0;
				for (int repeat = 0; repeat < 5; ++repeat)
				{
					const auto start = std::chrono::steady_clock::now();
					for (int frame = 0; frame < 60; ++frame)
					{
						const int x = moving ? (frame * 3) % 32 : 0,
								  y = moving ? (frame * 5) % 32 : 0;
						const auto a = std::chrono::steady_clock::now();
						REQUIRE(measured.prepare(scene, *globals->terrain, 0, 0, 31, 23, x, y,
												 fixture.team->me, true));
						const auto b = std::chrono::steady_clock::now();
						measured.draw(*gfx);
						const auto c = std::chrono::steady_clock::now();
						preparation += std::chrono::duration<double, std::milli>(b - a).count();
						drawing += std::chrono::duration<double, std::milli>(c - b).count();
					}
					samples.push_back(std::chrono::duration<double, std::milli>(
										  std::chrono::steady_clock::now() - start)
										  .count() /
									  60);
				}
				std::sort(samples.begin(), samples.end());
				timing << (pattern ? "dense" : "uniform") << (moving ? "_moving_ms " : "_warm_ms ")
					   << samples[2] << '\n';
				timing << (pattern ? "dense" : "uniform")
					   << (moving ? "_moving_prepare_ms " : "_warm_prepare_ms ")
					   << preparation / 300 << '\n';
				timing << (pattern ? "dense" : "uniform")
					   << (moving ? "_moving_draw_ms " : "_warm_draw_ms ") << drawing / 300 << '\n';
			}
			timing << (pattern ? "dense" : "uniform") << "_rebuilds " << measured.cacheRebuilds()
				   << "\n"
				   << (pattern ? "dense" : "uniform") << "_hits " << measured.cacheHits() << '\n';
		}

		timing << "prepared_source_bytes " << globals->terrainCompositor().sourceBytes() << '\n';
	}
}
