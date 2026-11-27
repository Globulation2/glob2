// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#include "TerrainGroupTable.h"
#include "HeadlessGlobals.h"
#include "Map.h"
#include "TerrainBuiltins.h"
#include "HeadlessGame.h"
#include "DrawFixtures.h"
#include <Environment.h>
#include "ScopedEnvironment.h"
#include <optional>
#include <fstream>
#include "TerrainPresentation.h"
#include "TerrainRegistry.h"
#include "TerrainCornerPresentation.h"
#include "terrain/TerrainCompositor.h"
#include "terrain/DirectTerrain.h"
#include "terrain/DirectTerrainData.h"
#include "terrain/TerrainCatalogIO.h"
#include "scene/SceneMap.h"
#include "SoftwareTerrainCache.h"
#include "OverviewTerrainCache.h"
#include <Toolkit.h>
#include "MapRenderState.h"
#include "ResourceRowCache.h"
#include "ResourceSprites.h"
#include "MapThumbnail.h"
#include "MapImage.h"
#include "GenerationRequest.h"
#include "GameGUI.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <SDL3_image/SDL_image.h>
#include <RenderBackend.h>
#include <RenderBatch.h>
#include <MapGeometryCache.h>
#include <RenderDevice.h>
#include <RendererSelection.h>
#include <RenderStateScope.h>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif
#include <algorithm>
#include <chrono>
#include <thread>
#include <set>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <cmath>
#include <string_view>
#include <map>

namespace
{
// Background mask builds take seconds in optimized builds and far longer under
// coverage instrumentation: wait on a clock, not an attempt count.
struct Patience
{
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now() + std::chrono::minutes(2);
	bool operator()() const { return std::chrono::steady_clock::now() < end; }
};
// Four panels: cliffs on the left of each, against thicket, hedge, rock
// spires and basalt columns, along a stepped border with a cliff spur and a
// pocket of the neighbour, all inside a grass margin.
template <class Map>
void paintCliffPanels(Map &map)
{
	const TerrainType neighbours[4] = {THICKET, HEDGE, RIDGE_ROCK, OUTCROP};
	for (int panel = 0; panel < 4; ++panel)
	{
		const int ox = (panel & 1) * 16, oy = (panel >> 1) * 16;
		for (int y = 2; y <= 13; ++y)
			for (int x = 1; x <= 14; ++x)
			{
				static constexpr int border[12] = {6, 6, 7, 8, 8, 7, 6, 5, 5, 6, 7, 7};
				bool cliff = x < border[y - 2];
				if (y >= 5 && y <= 6 && x >= 9 && x <= 11) cliff = true;  // spur
				if (y >= 10 && y <= 11 && x >= 2 && x <= 3) cliff = false; // pocket
				map.setVertexTerrain(ox + x, oy + y, cliff ? CLIFFS : neighbours[panel]);
			}
	}
}
// The one raised frame a cell draws, or -1; a cell drawing several slots fails.
int onlyDecorFrame(const TerrainVisual::Compositor &compositor, const SceneMap &map, int x, int y)
{
	int frame = -1, drawn = 0;
	for (const auto &layer : compositor.decorLayers(map, x, y))
		if (layer.frame >= 0)
		{
			frame = layer.frame;
			++drawn;
			CHECK(layer.sprite);
		}
	CHECK(drawn <= 1);
	return frame;
}
// Keep the real accelerated device, sampling, upload path and presentation
// surface while selecting the existing CPU compositor as the reference.
class CPUCompositionDevice : public GAGCore::RenderDevice
{
    GAGCore::RenderDevice &device;
  public:
    bool forceCPU=true, retainCoverage=false;
    std::size_t telemetryFloor=0;
    std::map<const GAGCore::CoverageData *,std::shared_ptr<const GAGCore::CoverageData>> heldCoverage;
    explicit CPUCompositionDevice(GAGCore::RenderDevice &device):device(device) {}
    GAGCore::RenderDeviceCapabilities capabilities() const override {
        auto result=device.capabilities();if(forceCPU)result.terrainComposition=false;return result;
    }
    bool canBeginFrame() override {return device.canBeginFrame();}
    GAGCore::RenderSubmission submit(GAGCore::RenderFrame frame) override {
        if(retainCoverage)for(const auto &packet:frame.packets)
            if(packet.weightedCompose && packet.weightedCompose->coverage)
                heldCoverage.emplace(packet.weightedCompose->coverage.get(),packet.weightedCompose->coverage);
        return device.submit(std::move(frame));
    }
    std::size_t heldCoverageBytes() const {
        std::size_t bytes=0;for(const auto &[key,data]:heldCoverage)
            bytes+=sizeof(GAGCore::CoverageData)+data->samples.capacity()*sizeof(GAGCore::CoverageSample);
        return bytes;
    }
    GAGCore::RenderCompletion completion(GAGCore::RenderSubmission value) override {return device.completion(value);}
    void present() override {device.present();}
    void discardFrame() override {device.discardFrame();}
    void reset() override {device.reset();}
    std::uint64_t generation() const override {return device.generation();}
    GAGCore::RenderTelemetry telemetry() const override {auto result=device.telemetry();result.resourceBytes=std::max(result.resourceBytes,telemetryFloor);return result;}
    bool capture(GAGCore::CaptureRequest request) override {return device.capture(std::move(request));}
    bool pollCapture(GAGCore::CaptureResult &result) override {return device.pollCapture(result);}
    SDL_Surface *captureSynchronously(GAGCore::CaptureRequest request) override {return device.captureSynchronously(std::move(request));}
    void cancelCaptures() override {device.cancelCaptures();}
    void outputSize(int &width,int &height) override {device.outputSize(width,height);}
};
class ScopedCPUComposition
{
    GAGCore::GraphicContext &context;
    std::unique_ptr<GAGCore::RenderDevice> original;
  public:
    explicit ScopedCPUComposition(GAGCore::GraphicContext &context):context(context),original(std::move(context.presentationDevice)) {
        REQUIRE(original);
        context.presentationDevice=std::make_unique<CPUCompositionDevice>(*original);
    }
    ~ScopedCPUComposition() {context.presentationDevice=std::move(original);}
    CPUCompositionDevice &device(){return static_cast<CPUCompositionDevice &>(*context.presentationDevice);}
};
// Model a slow retirement consumer retaining CPU restoration sources after
// actual GPU completion. It does not pin GPU allocations or fake their fences.
class ScopedCoverageRetention
{
    GAGCore::GraphicContext &context;
    std::unique_ptr<GAGCore::RenderDevice> original;
    std::unique_ptr<GAGCore::RenderBackend> adapter;
    GAGCore::RenderBackend *previous;
  public:
    explicit ScopedCoverageRetention(GAGCore::GraphicContext &context):context(context),
        original(std::move(context.presentationDevice)),adapter(std::move(context.deviceRenderer)),previous(context.renderer) {
        auto proxy=std::make_unique<CPUCompositionDevice>(*original);
        proxy->forceCPU=false;proxy->retainCoverage=true;
        context.presentationDevice=std::move(proxy);
        context.deviceRenderer=GAGCore::makeDeviceRenderBackend(*context.presentationDevice,*context.renderResources(),512,512);
        context.renderer=context.deviceRenderer.get();
    }
    ~ScopedCoverageRetention() {
        context.deviceRenderer.reset();context.presentationDevice.reset();
        context.presentationDevice=std::move(original);context.deviceRenderer=std::move(adapter);context.renderer=previous;
    }
    CPUCompositionDevice &device(){return static_cast<CPUCompositionDevice &>(*context.presentationDevice);}
};
SDL_Surface *chunkPixels(const SoftwareTerrainCache::Chunk &chunk)
{
	return chunk.image ? chunk.image->getSDLSurface() : chunk.parked.get();
}

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
	void prepareTexture(const void *key, SDL_Surface *pixels, std::uint64_t revision, bool linear = false) override
	{
		check(pixels);
		delegate.prepareTexture(key, pixels, revision, linear);
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
	// Catalogue grass, sand and ice draw from their own 8x8-block banks.
	for (const char *bank : {"terrain-grass", "terrain-sand", "terrain-ice"})
		for (int variant = 0; variant < 256; ++variant)
			frames << bank << variant << " 32 32 4 grass.webp -\n";
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
		// baseFrame only demands the HD layers; wait for them to load. On legacy
		// GL this sheet keeps its HD layers as textures alone, which baseFrame
		// (CPU sampling) does not return, so check the layer itself.
		globals->terrain->baseFrame(0);
		GAGCore::Sprite::finishHighResolution();
		REQUIRE(globals->terrain->experimentImages.size() > 0);
		REQUIRE(globals->terrain->experimentImages[0]);
		REQUIRE(globals->terrain->experimentImages[0]->getW() == 128);
	}
	// Exercise every edge mask, torus neighbors, water tiles, and mixed layers.
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 32; ++x)
		{
			if (x < 3)
				map.paintCell(x, y, WATER);
			if ((x + y * 3) % 7 == 0)
				map.paintCell(x, y, ICE);
			else if ((x * 5 + y) % 11 == 0)
				map.paintCell(x, y, TRAIL);
		}
	SceneMap scene;
	glob2test::observeMap(map,scene);

	SoftwareTerrainCache cache;
	auto clear = [&]
	{
		globals->gfx->setClipRect();
		globals->gfx->drawFilledRect(0, 0, 640, 480, 17, 29, 41);
	};
	bool memoryRecorded = false;
	const auto compare = [&]
	{
		// Each path declares its visible HD window on a draw; stream it, then
		// draw once more so both have adopted the published frames before the
		// measured draws (the game path adopts them a frame later).
		const auto drawBoth = [&]
		{
			clear();
			MapDrawer::drawMapTerrain(0, 0, 19, 14, 29, 30, 0, Game::DRAW_WHOLE_MAP, scene);
			cache.prepare(scene, *globals->terrain, 0, 0, 19, 14, 29, 30, fixture.team->me, true);
		};
		drawBoth();
		GAGCore::Sprite::finishHighResolution();
		drawBoth();
		clear();
		MapDrawer::drawMapTerrain(0, 0, 19, 14, 29, 30, 0, Game::DRAW_WHOLE_MAP, scene);
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
		CHECK(cache.bytes() <= SoftwareTerrainCache::GPUCacheBudget);
		CHECK(cache.residentTextureBytes() <= SoftwareTerrainCache::GPUBudget);
	}
	CHECK(fixture.checksum() == checksum);
	if (!gpu)
		CHECK(cache.cacheHits() > 0);
	auto &compositor = globals->terrainCompositor();
	const auto before = compositor.describe(scene, 0, 0);
	map.paintCell(0, 0, TRAIL);
	CHECK(compositor.describe(scene, 0, 0) == before);
	glob2test::observeMap(map,scene);
	compare();
	// A scene retains its registry snapshot. Reimport changes material bindings
	// only on extraction, and invalidates both software and GPU composed pages.
	importBeforeMatch(
		game,
		R"({"schemaVersion":1,"terrains":[{"key":"test:custom","name":"Custom ice","base":"grass","properties":{},"appearance":"ice"}]})");
	const auto custom = *map.terrainRegistry().find("test:custom");
	map.paintCell(0, 0, custom);
	glob2test::observeMap(map,scene);
	compare();
	compare();
	const auto previousRegistry = scene.frozenTerrainRegistry();
	const auto previousRecipe = compositor.describe(scene, 0, 0);
	importBeforeMatch(
		game,
		R"({"schemaVersion":1,"terrains":[{"key":"test:custom","name":"Custom trail","base":"grass","properties":{},"appearance":"road"}]})");
	CHECK(scene.frozenTerrainRegistry() == previousRegistry);
	CHECK(compositor.describe(scene, 0, 0) == previousRecipe);
	glob2test::observeMap(map,scene);
	CHECK(scene.appearanceAt(0, 0) == TRAIL);
	CHECK_FALSE(compositor.describe(scene, 0, 0) == previousRecipe);
	compare();
	// The terrain look seed travels with the extraction and is part of every
	// recipe, so a reroll recomposes software and GPU pages alike.
	const auto seededRecipe = compositor.describe(scene, 0, 0);
	map.setTerrainSeed(map.terrainSeed() + 1);
	CHECK(compositor.describe(scene, 0, 0) == seededRecipe);
	glob2test::observeMap(map,scene);
	CHECK(compositor.describe(scene, 0, 0).seed == map.terrainSeed());
	CHECK_FALSE(compositor.describe(scene, 0, 0) == seededRecipe);
	compare();
	compare();
	// Content revisions invalidate prepared source pixels and composed pages.
	// Repaint the ice material's own frames, which its cells draw (exports
	// leave the legacy tileset only the menu frames, and no material uses it).
	// Ice is also the ground under trail stones, so cells whose corners are all
	// trail must recompose too.
	const auto &ice = compositor.catalog().materials[compositor.catalog().find("ice")];
	auto *iceSprite = GAGCore::Toolkit::findSprite(ice.sprite);
	REQUIRE(iceSprite);
	int repainted = 0;
	for (int frame = 0; frame < iceSprite->getFrameCount(); ++frame)
		if (auto *source = iceSprite->nativeFrame(unsigned(frame)))
		{
			source->drawFilledRect(0, 0, 32, 32, 201, 23, 189);
			++repainted;
		}
	REQUIRE(repainted > 0);
	compare();
	compare();
	if (!gpu)
		REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(),
							(glob2test::artifactDir() / "terrain-layers.png").string().c_str()));
}
} // namespace
TEST_SUITE("TerrainPresentation")
{
	TEST_CASE("streamed art tiles give static variants one tile and animated ones a whole pair")
	{
		DirectTerrainData::ArtTiles tiles;
		tiles.reset(8);
		unsigned tile = 0;
		// Four static variants fill two pairs, not four.
		std::vector<unsigned> placed;
		for (unsigned slot = 1; slot <= 4; ++slot)
		{
			REQUIRE(tiles.claim(slot, 1, tile));
			placed.push_back(tile);
		}
		std::sort(placed.begin(), placed.end());
		CHECK(placed == std::vector<unsigned>{0, 1, 2, 3});
		// An animated variant takes an even-aligned pair.
		REQUIRE(tiles.claim(5, 2, tile));
		CHECK(tile % 2 == 0);
		CHECK(tiles.owners[tile] == 6);
		CHECK(tiles.owners[tile + 1] == 6);
		// Releasing one static tile of each full pair leaves no whole pair
		// free: a second animated variant cannot claim one ...
		tiles.release(placed[0], 1);
		tiles.release(placed[2], 1);
		REQUIRE(tiles.freePairs.size() == 1);
		REQUIRE(tiles.claim(6, 2, tile));
		CHECK_FALSE(tiles.claim(7, 2, tile));
		REQUIRE(tiles.freeHalves.size() == 2);
		// ... until one static variant moves beside the other.
		unsigned moved = 0, to = 0;
		REQUIRE(tiles.freePair(moved, to));
		CHECK(tiles.owners[to] == moved + 1);
		CHECK(tiles.owners[to ^ 1u] != 0);
		CHECK(tiles.freeHalves.empty());
		REQUIRE(tiles.claim(7, 2, tile));
		CHECK(tile % 2 == 0);
		CHECK(tiles.owners[tile] == 8);
		// Full: nothing is left to free.
		CHECK_FALSE(tiles.freePair(moved, to));
		CHECK_FALSE(tiles.claim(8, 1, tile));
		// Releasing both halves of a pair frees the pair.
		tiles.release(tile, 2);
		CHECK(tiles.freePairs.size() == 1);
	}

	TEST_CASE("resource artwork requests are lazy and captures can wait for them [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		const auto registry = ResourceRegistry::builtins();
		const auto gold = registry->find("gold-ore");
		const auto iron = registry->find("iron-ore");
		REQUIRE(gold);
		REQUIRE(iron);
		const auto &goldPath = registry->presentation(*gold).sprite;
		const auto &ironPath = registry->presentation(*iron).sprite;
		auto &artwork = ResourceSprites::resolve(registry, MapAssetBundle::empty());
		REQUIRE_FALSE(GAGCore::Toolkit::findSprite(goldPath));
		CHECK_FALSE(artwork.sprite(*gold));
		auto *ready = artwork.sprite(*gold, /*waitForLoad=*/true);
		REQUIRE(ready);
		CHECK(artwork.sprite(*gold) == ready);
		CHECK_FALSE(GAGCore::Toolkit::findSprite(ironPath));
	}

    GLOB2_TEST_CASE("GPU terrain pages match CPU composition through edits wrapping recovery and live switches", "[display][isolated][artifacts][writes-preferences]")
    {
        using namespace GAGCore;
        glob2test::HeadlessGlobals globals({.display=true,.width=512,.height=512,
            .beforeLoad=[](GlobalContainer &container){container.settings.rendererBackend=RendererBackend::Software;}});
        auto &gfx=*globals->gfx;
        std::vector<RendererBackend> available;
        for(const auto &choice:gfx.rendererAvailability())
            if(choice.available && (choice.backend==RendererBackend::SDLGPU || choice.backend==RendererBackend::ProgrammableGL))available.push_back(choice.backend);
        if(available.empty()){MESSAGE("GPU terrain integration omitted: no compiled native renderer assets/device path");return;}
        glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.discovered=true});auto &map=fixture.game.map;
        for(int y=0;y<32;++y)for(int x=0;x<32;++x)
            map.setVertexTerrain(x,y,((x+y)%5==0)?WATER:((x*3+y)%7==0)?SAND:GRASS);
        // A dark water pool: its waves and its wash against the shore, on both paths.
        for(int y=10;y<14;++y)for(int x=20;x<24;++x)map.setVertexTerrain(x,y,DARK_WATER);
        SceneMap scene;glob2test::observeMap(map,scene);
        SoftwareTerrainCache retained;
        unsigned comparisons=0;
        const auto switchTo=[&](RendererBackend backend) {
            REQUIRE(gfx.requestBackendSwitch(backend));gfx.nextFrame();INFO(gfx.backendSwitchStatus().reason);
            REQUIRE(gfx.backendSwitchStatus().phase==RendererSwitchPhase::Complete);REQUIRE(gfx.backendSwitchStatus().active==backend);
        };
        const auto draw=[&](SoftwareTerrainCache &cache,int vx,int vy,int scale,int reduction,int animation,bool whole) {
            for(unsigned attempt=0;attempt<32;++attempt) {
                gfx.setClipRect();gfx.drawFilledRect(0,0,512,512,17,29,41);
                gfx.beginMapTransform(1,0,0,0,0,512,512);
                REQUIRE(cache.prepareAtResolution(scene,*globals->terrain,0,0,15,15,vx,vy,fixture.team->me,whole,animation,scale,false,reduction));
                cache.draw(gfx);gfx.endMapTransform();gfx.renderer->flush();
                CHECK(cache.bytes()<=SoftwareTerrainCache::GPUCacheBudget);
                CHECK(cache.residentTextureBytes()<=SoftwareTerrainCache::GPUBudget);
                if(didRecordDeviceFrame(*gfx.renderer))break;
                // Dense retained CPU pages can require several bounded upload
                // submissions. Retry the same view until it is published.
                gfx.nextFrame();
            }
            REQUIRE(didRecordDeviceFrame(*gfx.renderer));
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(gfx.renderer->capture(),SDL_DestroySurface);REQUIRE(pixels);
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> rgba(SDL_ConvertSurface(pixels.get(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);REQUIRE(rgba);
            std::vector<Uint8> result(std::size_t(rgba->w)*rgba->h*4);
            for(int y=0;y<rgba->h;++y)std::memcpy(result.data()+std::size_t(y)*rgba->w*4,static_cast<Uint8 *>(rgba->pixels)+y*rgba->pitch,rgba->w*4);
            CHECK(cache.bytes()<=SoftwareTerrainCache::GPUCacheBudget);CHECK(cache.residentTextureBytes()<=SoftwareTerrainCache::GPUBudget);
            CHECK(gfx.renderDevice()->telemetry().resourceBytes<=GAGCore::platformRenderBudget().total);
            gfx.nextFrame();return result;
        };
        const auto compare=[&](int vx,int vy,int scale,int reduction,int animation,bool whole) {
            const auto checksum=fixture.checksum();
            const auto actual=draw(retained,vx,vy,scale,reduction,animation,whole);
            const bool gpu=bool(retained.gpuPages);
            std::vector<Uint8> expected;
            {ScopedCPUComposition reference(gfx);SoftwareTerrainCache cpu;expected=draw(cpu,vx,vy,scale,reduction,animation,whole);CHECK_FALSE(bool(cpu.gpuPages));}
            REQUIRE(actual.size()==expected.size());
            unsigned differences=0,maximum=0;
            for(std::size_t i=0;i<actual.size();++i){const auto difference=unsigned(std::abs(int(actual[i])-expected[i]));differences+=difference!=0;maximum=std::max(maximum,difference);}
            INFO("view="<<vx<<","<<vy<<" scale="<<scale<<" reduction="<<reduction<<" animation="<<animation<<" gpu="<<gpu<<" channelDifferences="<<differences<<" max="<<maximum);
            CHECK(maximum==0);CHECK(fixture.checksum()==checksum);++comparisons;
            if(scale==4)CHECK_FALSE(gpu); // One HD coverage page exceeds the32MiB preflight bound.
            else REQUIRE(gpu);
        };
        for(auto backend:available) {
            switchTo(backend);REQUIRE(gfx.renderDevice());
            if(!gfx.renderDevice()->capabilities().terrainComposition){MESSAGE("Terrain shaders unavailable for this backend; integration omitted");continue;}
            compare(0,0,1,0,19,true);
            const auto rebuilds=retained.cacheRebuilds();compare(0,0,1,0,19,true);CHECK(retained.cacheRebuilds()==rebuilds);
            map.paintCell(0,0,ICE);glob2test::observeMap(map,scene);compare(0,0,1,0,25,true);CHECK(retained.cacheRebuilds()>rebuilds);
            compare(31,31,1,0,25,true);compare(63,63,1,0,25,true);
            for(int divisor:{2,4,8,16,32})compare(0,0,1,divisor,25,true);
            map.unsetMapDiscovered();map.setMapDiscovered(0,0,8,16,fixture.team->me);glob2test::observeMap(map,scene);compare(31,31,1,0,25,false);
            compare(0,0,4,0,25,true);
            {
                ScopedCPUComposition composition(gfx);
                SoftwareTerrainCache migration;
                const auto cpuPixels=draw(migration,0,0,4,0,25,true);
                CHECK_FALSE(bool(migration.gpuPages));
                composition.device().forceCPU=false;
                draw(migration,0,0,1,0,25,true);
                REQUIRE(bool(migration.gpuPages));
                const auto beforeReturn=migration.cacheRebuilds();
                composition.device().forceCPU=true;
                const auto restored=draw(migration,0,0,4,0,25,true);
                CHECK_FALSE(bool(migration.gpuPages));
                CHECK(migration.cacheRebuilds()==beforeReturn);
                CHECK(restored==cpuPixels);
                // Retaining several densities must still leave room for new
                // composed targets and masks, including queued mask leases.
                for(int density:{4,2,1})
                    for(int vy:{0,16})for(int vx:{0,16})
                        draw(migration,vx,vy,density,0,25,true);
                composition.device().forceCPU=false;
                const auto bounded=draw(migration,15,15,1,0,25,true);
                REQUIRE(bool(migration.gpuPages));
                composition.device().forceCPU=true;
                SoftwareTerrainCache reference;
                CHECK(bounded==draw(reference,15,15,1,0,25,true));
            }
            {
                // Model concurrent retained atlas/geometry allocations without
                // allocating fake GPU resources or weakening the actual cap.
                ScopedCPUComposition pressure(gfx);pressure.device().forceCPU=false;
                // Leave 4 MiB of the platform's total budget, too little for a page.
                pressure.device().telemetryFloor=GAGCore::platformRenderBudget().total-4u*1024*1024;
                SoftwareTerrainCache constrained;
                const auto reference=draw(constrained,0,0,1,0,25,true);
                CHECK_FALSE(bool(constrained.gpuPages));
                pressure.device().telemetryFloor=0;
                SoftwareTerrainCache promoted;
                const auto actual=draw(promoted,0,0,1,0,25,true);
                REQUIRE(bool(promoted.gpuPages));CHECK(actual==reference);
            }
            for(unsigned iteration=0;iteration<3;++iteration) {
                const auto checksum=fixture.checksum();switchTo(RendererBackend::Software);CHECK(fixture.checksum()==checksum);switchTo(backend);
                compare(0,0,1,0,25,true);
            }
            gfx.recoverPresentationDevice();compare(0,0,1,0,25,true);
            retained=SoftwareTerrainCache{};
            {
                ScopedCoverageRetention retirement(gfx);
                bool exhausted=false;
                for(unsigned iteration=0;iteration<64 && !exhausted;++iteration) {
                    // Rotating palette assignments creates fresh mixed recipes.
                    for(int y=0;y<32;++y)for(int x=0;x<32;++x)
                        map.setVertexTerrain(x,y,TerrainType((x*3+y+iteration)%3));
                    glob2test::observeMap(map,scene);SoftwareTerrainCache pressure;
                    const bool admitted=pressure.prepareAtResolution(scene,*globals->terrain,0,0,15,15,0,0,fixture.team->me,true,25,1,false,0);
                    if(!admitted){exhausted=true;break;}
                    pressure.draw(gfx);gfx.renderer->flush();REQUIRE(didRecordDeviceFrame(*gfx.renderer));
                    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> completed(gfx.renderer->capture(),SDL_DestroySurface);REQUIRE(completed);
                    gfx.nextFrame();
                    CHECK(retirement.device().heldCoverageBytes()<=40u*1024*1024);
                }
                REQUIRE(exhausted);CHECK(retirement.device().heldCoverageBytes()>0);
                retirement.device().heldCoverage.clear();gfx.nextFrame();
                SoftwareTerrainCache restored;
                REQUIRE(restored.prepareAtResolution(scene,*globals->terrain,0,0,15,15,0,0,fixture.team->me,true,25,1,false,0));
                REQUIRE(bool(restored.gpuPages));
                restored.draw(gfx);gfx.renderer->flush();gfx.nextFrame();
            }
            // The retirement proxy used an independent command recorder. Draw
            // through the restored adapter before capturing its frame identity
            // for the next switch, just as a normal presentation owner does.
            compare(0,0,1,0,25,true);
        }
        CHECK(comparisons>0);
        std::ofstream report(glob2test::artifactDir()/"gpu-terrain-integration.txt");report<<"exact CPU comparisons="<<comparisons<<"\n";
    }

	TEST_CASE("overview palette follows detailed shore coverage across both wrapped axes [display] "
			  "[artifacts]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 512, .height = 512});
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4, .discovered = true});
		auto &map = fixture.game.map;
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
				map.setVertexTerrain(x, y, x < 4 || y < 4 ? WATER : x < 7 || y < 7 ? SAND : GRASS);
		// Away from (9,9), where wheat needs pure grass.
		map.paintCell(12, 12, ICE);
		SceneMap scene;
		glob2test::observeMap(map,scene);
		auto &compositor = globals->terrainCompositor();
		compositor.prepare(false, 0);
		// A cell of this 1:1 view spans 32 pixels, so the overview takes many samples.
		constexpr int samples = OverviewTerrainCache::ManySamples;
		GAGCore::DrawableSurface overview(16 * samples, 16 * samples);
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
			{
				const auto recipe = compositor.describe(scene, x, y);
				compositor.composeOverview(recipe, overview.getSDLSurface(), x * samples,
										   y * samples, samples);
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
		// Custom saved terrain palettes still override their appearance colour.
		auto custom = compositor.describe(scene, 10, 10);
		custom.corners.fill(compositor.catalog().bindings.at("ice"));
		const std::array<unsigned char, 3> customColor{17, 31, 47};
		const TerrainVisual::Compositor::CornerColors customCorners{&customColor, &customColor,
																   &customColor, &customColor};
		GAGCore::DrawableSurface customOverview(samples, samples);
		compositor.composeOverview(custom, customOverview.getSDLSurface(), 0, 0, samples,
								   &customCorners);
		for (int y = 0; y < samples; ++y)
			for (int x = 0; x < samples; ++x)
				CHECK(reinterpret_cast<const Uint32 *>(
						  static_cast<const Uint8 *>(customOverview.getSDLSurface()->pixels) +
						  y * customOverview.getSDLSurface()->pitch)[x] == 0xFF111F2Fu);
		map.setResourceByIndex(9, 9, WHEAT, 1);
		map.setResourceAmount(map.coordToIndex(9, 9), 2);
		glob2test::observeMap(map,scene);
		// The view as its pages compose it; the game draws those pages directly.
		OverviewTerrainCache pages;
		GAGCore::DrawableSurface drawn(16 * samples, 16 * samples);
		pages.update(scene, 0, 0, 15, 15, 0, 0, 0, true, samples, drawn);
		const auto& tint = map.resourceRegistry().presentation(static_cast<ResourceId>(WHEAT)).minimap;
		// Two of five wheat: a 2x2 square centred in the cell over unchanged ground.
		const int square = OverviewTerrainCache::resourceSquare(2, 5, samples);
		REQUIRE(square == 2);
		const int inset = (samples - square) / 2;
		for (int y = 0; y < 16 * samples; ++y)
			for (int x = 0; x < 16 * samples; ++x)
			{
				const auto pixelAt = [&](auto &image)
				{
					auto *surface = image.getSDLSurface();
					return reinterpret_cast<const Uint32 *>(
						static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch)[x];
				};
				const auto ground = pixelAt(overview), actual = pixelAt(drawn);
				if (x / samples == 9 && y / samples == 9 && x % samples >= inset &&
					x % samples < inset + square && y % samples >= inset &&
					y % samples < inset + square)
					CHECK(actual == (0xFF000000u | Uint32(tint[0]) << 16 | Uint32(tint[1]) << 8 |
									 tint[2]));
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
		MapDrawer::drawMapTerrain(0, 0, 15, 15, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		MapDrawer::drawMapResources(0, 0, 15, 15, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		GAGCore::Sprite::flushBatches(globals->gfx);
		if (globals->gfx->renderer)
			globals->gfx->renderer->flush();
		CHECK(IMG_SavePNG(globals->gfx->getSDLSurface(),
						  (evidence / "detailed.png").string().c_str()));
		MapRenderState render;
		render.detail.terrainOverview = .5f;
		MapDrawer::drawMapOverview(0, 0, 15, 15, 0, 0, 0, Game::DRAW_WHOLE_MAP | Game::DRAW_COMPLETE_TERRAIN,
								   scene, render);
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
		glob2test::observeMap(restored.game.map,scene);
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
		map.paintCell(0, 0, TRAIL);
		for (unsigned mask = 0; mask < 16; ++mask)
		{
			CAPTURE(mask);
			for (int side = 0; side < 4; ++side)
				map.paintCell(dx[side], dy[side], mask & (1u << side) ? ICE : TRAIL);
			SceneMap expected;
			glob2test::observeMap(map,expected);
			for (int side = 0; side < 4; ++side)
				map.paintCell(dx[side], dy[side], mask & (1u << side) ? aliases[side] : TRAIL);
			SceneMap actual;
			glob2test::observeMap(map,actual);
			for (int y = -1; y <= 1; ++y)
				for (int x = -1; x <= 1; ++x)
					CHECK(compositor.describe(actual, x, y) == compositor.describe(expected, x, y));
			// A vertex only the side's own cell writes: later sides share the others.
			constexpr int vx[4] = {0, 2, 0, -1}, vy[4] = {-1, 0, 2, 0};
			for (int side = 0; side < 4; ++side)
				CHECK(actual.vertexTerrainAt(vx[side], vy[side]) ==
					  (mask & (1u << side) ? aliases[side] : TRAIL));
		}
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
				map.paintCell(x, y, sand);
		SceneMap scene;
		glob2test::observeMap(map,scene);
		CHECK(scene.presentationTypeAt(0, 0) == sand);
		CHECK(scene.appearanceAt(0, 0) == SAND);
		const auto material = compositor.catalog().bindings.at("sand");
		for (const auto corner : compositor.describe(scene, 0, 0).corners)
			CHECK(corner == material);
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
		map.paintCell(0, 0, custom);
		map.paintCell(1, 0, ICE);
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
	// Every version-4 border is drawn with the smooth field (#1466), so
	// composing these HD pages takes two to three minutes in optimized builds:
	// over the display timeout in the -O0 coverage build.
	TEST_CASE("HD material pages and fractional zoom preserve cached pixels with missing optional "
			  "atlas [display][artifacts][slow]")
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
							map.paintCell(
								x, y,
								x < 3 ? WATER : (y == 4 ? TRAIL : ((x + y) % 3 ? ICE : GRASS)));
					GAGCore::Sprite::setHighResolution(hd);
					SceneMap scene;
					glob2test::observeMap(map,scene);
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
					MapDrawer::drawMapTerrain(0, 0, 7, 7, vx, vy, 0, Game::DRAW_WHOLE_MAP, scene);
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
						fixture.game.map.paintCell(
							x, y, y == 4 ? TRAIL : ((x + y) % 3 ? ICE : GRASS));
				GAGCore::Sprite::setHighResolution(hd);
				SceneMap scene;
				glob2test::observeMap(fixture.game.map,scene);
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
	TEST_CASE("overview pages retain wrapped samples and invalidate edits, resources and visibility [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 256, .height = 256});
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true});
		auto &map = fixture.game.map;
		map.paintCell(0, 0, ICE);
		map.paintCell(31, 31, SAND);
		SceneMap scene;
		glob2test::observeMap(map, scene);
		auto &compositor = globals->terrainCompositor();
		compositor.prepare(false, 0);
		constexpr int samples = OverviewTerrainCache::FewSamples;
		GAGCore::DrawableSurface actual(40 * samples, 24 * samples), expected(40 * samples, 24 * samples);
		OverviewTerrainCache cache;
		const auto check = [&](int vx, int vy, bool wholeMap = true)
		{
			cache.update(scene, -3, -2, 36, 21, vx, vy, fixture.team->me, wholeMap, samples, actual);
			for (int y = -2; y <= 21; ++y)
				for (int x = -3; x <= 36; ++x)
					compositor.composeOverview(compositor.describe(scene, x + vx, y + vy),
						expected.getSDLSurface(), (x + 3) * samples, (y + 2) * samples, samples);
			CHECK(std::memcmp(actual.getSDLSurface()->pixels, expected.getSDLSurface()->pixels,
				actual.getSDLSurface()->pitch * actual.getH()) == 0);
		};
		check(29, 30);
		const auto cold = cache.composedCells();
		const auto originalRevision = actual.contentRevision();
		check(29, 30);
		check(-3, -2);
		CHECK(cache.composedCells() == cold);
		CHECK(actual.contentRevision() == originalRevision);
		// Resizing back to the same extent replaces the pixel storage.
		actual.setRes(8 * samples, 8 * samples);
		actual.setRes(40 * samples, 24 * samples);
		check(29, 30);
		CHECK(cache.composedCells() == cold);
		map.setVertexTerrain(31, 0, GRASS); // Halo edit across both page and map seams.
		glob2test::observeMap(map, scene);
		const auto resizedRevision = actual.contentRevision();
		check(29, 30);
		CHECK(cache.composedCells() > cold);
		CHECK(actual.contentRevision() > resizedRevision);
		map.setTerrainSeed(197);
		glob2test::observeMap(map, scene);
		check(29, 30);
		const auto seeded = cache.composedCells();
		map.setResourceByIndex(9, 9, WHEAT, 1);
		glob2test::observeMap(map, scene);
		cache.update(scene, -3, -2, 36, 21, 29, 30, fixture.team->me, false, samples, actual);
		CHECK(cache.composedCells() == seeded + 1);
		CHECK(std::memcmp(actual.getSDLSurface()->pixels, expected.getSDLSurface()->pixels,
			actual.getSDLSurface()->pitch * actual.getH()) != 0);
		// Hiding the resource restores its untinted ground, and revealing it
		// composes it once again rather than accumulating the tint.
		map.unsetMapDiscovered();
		glob2test::observeMap(map, scene);
		check(29, 30, false);
		CHECK(cache.composedCells() == seeded + 2);
		cache.update(scene, -3, -2, 36, 21, 29, 30, fixture.team->me, true, samples, actual);
		CHECK(cache.composedCells() == seeded + 3);
		map.setNoResource(9, 9, 1);
		glob2test::observeMap(map, scene);
		check(29, 30);
		CHECK(cache.composedCells() == seeded + 4);

		// A narrow view composes just its visible cells. Panning within the
		// same page builds one newly exposed column and reuses the overlap.
		OverviewTerrainCache partial;
		GAGCore::DrawableSurface small(4 * samples, 4 * samples);
		partial.update(scene, 0, 0, 3, 3, 0, 0, fixture.team->me, true, samples, small);
		CHECK(partial.composedCells() == 16);
		partial.update(scene, 0, 0, 3, 3, 1, 0, fixture.team->me, true, samples, small);
		CHECK(partial.composedCells() == 20);
		partial.update(scene, 0, 0, 3, 3, 0, 0, fixture.team->me, true, samples, small);
		CHECK(partial.composedCells() == 20);
	}

	TEST_CASE("overview resource squares grow from the centre with stock and follow screen density")
	{
		using Cache = OverviewTerrainCache;
		// Rounded down: only full stock reaches half the cell.
		for (const auto &[stock, few, many] :
			 {std::tuple{0u, 1, 1}, {1u, 1, 1}, {2u, 1, 2}, {3u, 1, 2}, {4u, 1, 3}, {5u, 2, 4},
			  {9u, 2, 4}})
		{
			CAPTURE(stock);
			CHECK(Cache::resourceSquare(stock, 5, Cache::FewSamples) == few);
			CHECK(Cache::resourceSquare(stock, 5, Cache::ManySamples) == many);
		}
		CHECK(Cache::resourceSquare(3, 4, Cache::ManySamples) == 3);
		CHECK(Cache::resourceSquare(4, 4, Cache::ManySamples) == 4);
		CHECK(Cache::resourceSquare(0, 0, Cache::ManySamples) == 4);
		// Many samples from six device pixels per cell, kept down to five.
		CHECK(Cache::samplesFor(3.5, 0, 100, 0) == Cache::FewSamples);
		CHECK(Cache::samplesFor(5.5, Cache::FewSamples, 100, 0) == Cache::FewSamples);
		CHECK(Cache::samplesFor(6, Cache::FewSamples, 100, 0) == Cache::ManySamples);
		CHECK(Cache::samplesFor(5.5, Cache::ManySamples, 100, 0) == Cache::ManySamples);
		CHECK(Cache::samplesFor(4.9, Cache::ManySamples, 100, 0) == Cache::FewSamples);
		// Never more than the device can hold in one texture.
		CHECK(Cache::samplesFor(32, 0, 512, 4096) == Cache::ManySamples);
		CHECK(Cache::samplesFor(32, Cache::ManySamples, 513, 4096) == Cache::FewSamples);
		CHECK(Cache::samplesFor(32, 0, 513, 0) == Cache::ManySamples);
	}

	TEST_CASE("overview pages recompose a resource cell when its stock changes the square [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 256, .height = 256});
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true});
		auto &map = fixture.game.map;
		map.setResourceByIndex(9, 9, WHEAT, 1);
		REQUIRE(map.getResource(9, 9).type == WHEAT);
		SceneMap scene;
		auto &compositor = globals->terrainCompositor();
		compositor.prepare(false, 0);
		const auto &registry = map.resourceRegistry();
		const auto &tint = registry.presentation(static_cast<ResourceId>(WHEAT)).minimap;
		const Uint32 wheat = 0xFF000000u | Uint32(tint[0]) << 16 | Uint32(tint[1]) << 8 | tint[2];
		unsigned capacity = 0;
		for (const auto &yield : registry.yields(static_cast<ResourceId>(WHEAT)))
			capacity += yield.capacity;
		REQUIRE(capacity == 5);
		for (const int samples : {OverviewTerrainCache::FewSamples, OverviewTerrainCache::ManySamples})
		{
			CAPTURE(samples);
			OverviewTerrainCache cache;
			GAGCore::DrawableSurface image(16 * samples, 16 * samples);
			// Every sample of the cell is wheat inside the centred square and ground outside it.
			const auto checkSquare = [&](int side)
			{
				const int inset = (samples - side) / 2;
				const auto *surface = image.getSDLSurface();
				for (int y = 0; y < samples; ++y)
					for (int x = 0; x < samples; ++x)
					{
						const auto pixel = reinterpret_cast<const Uint32 *>(
							static_cast<const Uint8 *>(surface->pixels) +
							(9 * samples + y) * surface->pitch)[9 * samples + x];
						CHECK((pixel == wheat) == (x >= inset && x < inset + side && y >= inset &&
												   y < inset + side));
					}
			};
			// A stock change that resizes the square recomposes just this cell; one that
			// keeps its size (common at four samples, where squares are 1 or 2 wide)
			// recomposes nothing.
			int side = 0;
			for (const Uint32 stock : {1u, 3u, 5u, 4u, 2u})
			{
				CAPTURE(stock);
				map.setResourceAmount(map.coordToIndex(9, 9), stock);
				glob2test::observeMap(map, scene);
				const auto before = cache.composedCells();
				cache.update(scene, 0, 0, 15, 15, 0, 0, fixture.team->me, true, samples, image);
				const int square = OverviewTerrainCache::resourceSquare(stock, capacity, samples);
				checkSquare(square);
				if (side)
					CHECK(cache.composedCells() - before == (square != side ? 1u : 0u));
				side = square;
			}
			// Changing density replaces every page rather than mixing resolutions.
			const auto before = cache.composedCells();
			const int other = samples == OverviewTerrainCache::FewSamples ? OverviewTerrainCache::ManySamples : OverviewTerrainCache::FewSamples;
			GAGCore::DrawableSurface resized(16 * other, 16 * other);
			cache.update(scene, 0, 0, 15, 15, 0, 0, fixture.team->me, true, other, resized);
			CHECK(cache.samples() == other);
			CHECK(cache.composedCells() - before == 256);
		}
	}
	TEST_CASE("interactive overview draws the whole-map base beneath cells still composing and prewarms around the view [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 256, .height = 256});
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 6, .discovered = true});
		auto &map = fixture.game.map;
		for (int y = 10; y < 30; ++y)
			for (int x = 20; x < 44; ++x)
				map.setVertexTerrain(x, y, WATER);
		SceneMap scene;
		glob2test::observeMap(map, scene);
		globals->terrainCompositor().prepare(false, 0);
		auto &gfx = *globals->gfx;
		OverviewTerrainCache cache;
		const auto draw = [&](int vx, int vy)
		{
			cache.draw(scene, 0, 0, 7, 7, vx, vy, fixture.team->me, true, OverviewTerrainCache::FewSamples, gfx, 255,
					   false);
			return cache.drawMetrics();
		};
		// The base is composed by a worker as the map loads; once it is ready no
		// visible cell is ever transparent, whatever is still composing.
		for (int frame = 0; frame < 2000 && !cache.baseReady(); ++frame)
		{
			draw(0, 0);
			SDL_Delay(1);
		}
		REQUIRE(cache.baseReady());
		for (const auto &[vx, vy] : {std::pair{16, 16}, {40, 8}, {60, 60}})
		{
			const auto metrics = draw(vx, vy);
			CHECK(metrics.cells == 64);
			CHECK(metrics.holes == 0);
		}
		// Prewarming spirals out from the view's centre until it has composed
		// every page of a map this small.
		for (int frame = 0; frame < 4000 && cache.composedCells() < 64u * 64u; ++frame)
		{
			cache.prewarm(scene, fixture.team->me, true, 33, 47, OverviewTerrainCache::FewSamples);
			SDL_Delay(1);
		}
		CHECK(cache.composedCells() >= 64u * 64u);
		// Then the view finds every cell composed: nothing falls back.
		const auto settled = draw(24, 40);
		CHECK(settled.base == 0);
		CHECK(settled.other == 0);
		CHECK(settled.holes == 0);
	}
	GLOB2_TEST_CASE("prewarmed overview pages upload ahead so a zoom out draws every page on its first frame",
					"[display][isolated][writes-preferences]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 512, .height = 512,
			.beforeLoad = [](GlobalContainer &container)
			{ container.settings.rendererBackend = RendererBackend::Software; }});
		auto &gfx = *globals->gfx;
		// The first presentation device that starts here (SDL GPU, then Programmable GL).
		bool device = false;
		for (const auto &choice : gfx.rendererAvailability())
			if (!device && choice.available &&
				(choice.backend == RendererBackend::SDLGPU || choice.backend == RendererBackend::ProgrammableGL) &&
				gfx.requestBackendSwitch(choice.backend))
			{
				gfx.nextFrame();
				device = gfx.backendSwitchStatus().phase == RendererSwitchPhase::Complete &&
					gfx.backendSwitchStatus().active == choice.backend;
			}
		if (!device)
		{
			MESSAGE("No presentation device: overview upload-ahead coverage skipped");
			return;
		}
		// 256 by 256 cells: sixteen pages, twice what a device uploads in a frame.
		glob2test::HeadlessGame fixture({.wDec = 8, .hDec = 8, .discovered = true});
		auto &map = fixture.game.map;
		for (int y = 40; y < 90; ++y)
			for (int x = 100; x < 180; ++x)
				map.setVertexTerrain(x, y, WATER);
		SceneMap scene;
		glob2test::observeMap(map, scene);
		globals->terrainCompositor().prepare(false, 0);
		constexpr int samples = OverviewTerrainCache::FewSamples;
		const auto zoomOut = [&](OverviewTerrainCache &cache)
		{
			gfx.nextFrame();
			cache.draw(scene, 0, 0, 255, 255, 0, 0, fixture.team->me, true, samples, gfx, 255, false);
			return cache.drawMetrics();
		};
		// Frames before the zoom out: the cache prewarms the farthest zoom's
		// density and uploads its complete pages with each frame's allowance.
		const auto warm = [&](OverviewTerrainCache &cache, bool ahead)
		{
			for (Patience waiting; waiting() && (!cache.baseReady() || cache.composedCells() < 256u * 256u ||
												 (ahead && cache.pagesUploadedAhead() < 16));)
			{
				gfx.nextFrame();
				cache.prewarm(scene, fixture.team->me, true, 128, 128, samples);
				if (ahead)
					cache.uploadAhead();
				SDL_Delay(1);
			}
			REQUIRE(cache.baseReady());
			REQUIRE(cache.composedCells() >= 256u * 256u);
		};
		CHECK(OverviewTerrainCache::PageTiles == 64);
		OverviewTerrainCache ahead;
		warm(ahead, true);
		CHECK(ahead.pagesUploadedAhead() == 16);
		const auto first = zoomOut(ahead);
		CHECK(first.cells == 256u * 256u);
		CHECK(first.base == 0);
		CHECK(first.other == 0);
		CHECK(first.holes == 0);
		CHECK(first.fading == 0);
		// Without the uploads ahead, the first frame shows the base where the
		// allowance ran out, and the next one completes the view.
		OverviewTerrainCache onDemand;
		warm(onDemand, false);
		CHECK(zoomOut(onDemand).base > 0);
		const auto second = zoomOut(onDemand);
		CHECK(second.base == 0);
		CHECK(second.other == 0);
	}
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)

	TEST_CASE("Legacy GL game-load preload leaves sprites and fills drawing as before [display]")
	{
		// GameGUI::prepareDirectTerrain builds the whole-map base at game load,
		// outside any draw. Its texture uploads must not leave another texture
		// unit active or a binding behind the sprite path's cache: on macOS
		// that drew resource sprites blank and health bars and the HUD black.
		glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480,
			.screenFlags = Uint32(GAGCore::GraphicContext::USEGPU),
			.beforeLoad = [](GlobalContainer &g) { g.settings.rendererBackend = GAGCore::RendererBackend::LegacyGL; }});
		auto &gfx = *globals->gfx;
		REQUIRE(gfx.isLegacyGLScreen());
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 5, .terrain = WATER, .discovered = true});
		const TerrainType kinds[] = {WATER, GRASS, SAND};
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 64; ++x)
				fixture.game.map.setVertexTerrain(x, y, kinds[((x / 3) * 7 + (y / 3) * 5) % 3]);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		GAGCore::Sprite resources;
		REQUIRE(resources.load("data/gfx/ressource"));
		const auto draw = [&]
		{
			gfx.setClipRect();
			gfx.drawFilledRect(0, 0, 640, 480, 11, 23, 37);
			for (int i = 0; i < std::min(resources.getFrameCount(), 32); ++i)
				gfx.drawSprite(20 + (i % 8) * 52, 20 + (i / 8) * 52, &resources, i);
			// A health bar: an opaque fill and a translucent one.
			gfx.drawFilledRect(40, 300, 120, 6, 40, 220, 40);
			gfx.drawFilledRect(40, 310, 120, 6, 220, 40, 40, 128);
			return terrainPixels(true);
		};
		draw();
		const auto before = draw();
		GLint unitBefore = 0, programBefore = 0;
		glGetIntegerv(GL_ACTIVE_TEXTURE, &unitBefore);
		glGetIntegerv(GL_CURRENT_PROGRAM, &programBefore);
		DirectTerrain atlas;
		for (Patience waiting; waiting() && !atlas.textureBytes();)
		{
			atlas.preload(gfx, scene);
			if (!atlas.textureBytes()) SDL_Delay(1);
		}
		REQUIRE(atlas.textureBytes() > 0);
		GLint unitAfter = 0, programAfter = 0;
		glGetIntegerv(GL_ACTIVE_TEXTURE, &unitAfter);
		glGetIntegerv(GL_CURRENT_PROGRAM, &programAfter);
		CHECK(unitAfter == unitBefore);
		CHECK(programAfter == programBefore);
		CHECK(glGetError() == GL_NO_ERROR);
		const auto after = draw();
		std::size_t changed = 0;
		for (std::size_t i = 0; i < before.size(); ++i) changed += before[i] != after[i];
		CHECK(changed == 0);
	}

	TEST_CASE("Legacy GL single pass animates water and shores and hides undiscovered terrain [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480,
			.screenFlags = Uint32(GAGCore::GraphicContext::USEGPU),
			.beforeLoad = [](GlobalContainer &g) { g.settings.rendererBackend = GAGCore::RendererBackend::LegacyGL; }});
		auto &gfx = *globals->gfx;
		REQUIRE(gfx.isLegacyGLScreen());
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 5, .terrain = WATER, .discovered = true});
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		GAGCore::Sprite::setHighResolution(true);
		struct Restore { ~Restore() { GAGCore::Sprite::setHighResolution(false); } } restore;
		DirectTerrain atlas;
		gfx.beginMapTransform(.76f, 0, 0, 0, 0, 640, 480);
		const auto prepare = [&](int time, bool whole, int attempts)
		{
			bool ready = false;
			for (int attempt = 0; attempt < attempts && !ready; ++attempt)
				ready = atlas.prepare(gfx, scene, 0, 0, 31, 15, 0, 0, fixture.team->me, whole, time);
			return ready;
		};
		REQUIRE(prepare(0, true, 200));
		REQUIRE_FALSE(atlas.preparedRegions().empty());
		GAGCore::Sprite::finishHighResolution();
		// Water's crossfade changes the pixels every frame, from the one pass.
		std::vector<Uint8> previous;
		for (int time = 0; time < 9; ++time)
		{
			CAPTURE(time);
			gfx.drawFilledRect(0, 0, 640, 480, 11, 23, 37);
			REQUIRE(prepare(time, true, 1));
			atlas.drawPrepared(gfx, 0, 0, true, time);
			const auto pixels = terrainPixels(true);
			if (time > 0) CHECK(pixels != previous);
			previous = pixels;
		}

		// A shoreline: land and water cells draw together and keep animating.
		for (int y = 0; y < 32; ++y)
			for (int x = 12; x <= 20; ++x)
				fixture.game.map.setVertexTerrain(x, y, GRASS);
		glob2test::observeMap(fixture.game.map, scene);
		REQUIRE(prepare(0, true, 300));
		REQUIRE_FALSE(atlas.preparedRegions().empty());
		GAGCore::Sprite::finishHighResolution();
		for (int time = 0; time < 9; ++time)
		{
			gfx.drawFilledRect(0, 0, 640, 480, 11, 23, 37);
			REQUIRE(prepare(time, true, 1));
			atlas.drawPrepared(gfx, 0, 0, true, time);
			const auto pixels = terrainPixels(true);
			if (time > 0) CHECK(pixels != previous);
			previous = pixels;
		}

		// Changing discovery hides land and water alike.
		fixture.game.map.unsetMapDiscovered();
		glob2test::observeMap(fixture.game.map, scene);
		for (int i = 0; i < 3; ++i)
		{
			gfx.drawFilledRect(0, 0, 2048, 2048, 11, 23, 37);
			const auto hidden = terrainPixels(true);
			REQUIRE(prepare(9, false, 1));
			CHECK(atlas.preparedRegions().empty());
			atlas.drawPrepared(gfx, 0, 0, false, 9);
			CHECK(terrainPixels(true) == hidden);
		}
		gfx.endMapTransform();
	}

	TEST_CASE("Legacy GL zoom keeps each cell's old mask until its new density lands [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480,
			.screenFlags = Uint32(GAGCore::GraphicContext::USEGPU),
			.beforeLoad = [](GlobalContainer &g) { g.settings.rendererBackend = GAGCore::RendererBackend::LegacyGL; }});
		auto &gfx = *globals->gfx;
		REQUIRE(gfx.isLegacyGLScreen());
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 5, .terrain = WATER, .discovered = true});
		// Blocks of grass, sand and water: most cells are mixed.
		const TerrainType kinds[] = {WATER, GRASS, SAND};
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 64; ++x)
				fixture.game.map.setVertexTerrain(x, y, kinds[((x / 3) * 7 + (y / 3) * 5) % 3]);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		DirectTerrain atlas;
		const auto settle = [&](float scale, int right, int bottom)
		{
			gfx.beginMapTransform(scale, 0, 0, 0, 0, 640, 480);
			for (Patience waiting; waiting();)
			{
				const bool ready = atlas.prepare(gfx, scene, 0, 0, right, bottom, 0, 0, fixture.team->me, true, 0);
				if (ready && !atlas.preparing() && !atlas.pendingHdMasks()) break;
				SDL_Delay(1);
			}
			REQUIRE_FALSE(atlas.pendingHdMasks());
			gfx.endMapTransform();
		};
		const auto frame = [&](float scale, int right, int bottom)
		{
			gfx.beginMapTransform(scale, 0, 0, 0, 0, 640, 480);
			gfx.drawFilledRect(0, 0, 640, 480, 11, 23, 37);
			atlas.drawPrepared(gfx, 0, 0, true, 0);
			auto pixels = terrainPixels(true);
			gfx.endMapTransform();
			return pixels;
		};
		// 32-texel masks at this zoom, then 64-texel masks at twice it.
		settle(.76f, 26, 19);
		gfx.beginMapTransform(1.6f, 0, 0, 0, 0, 640, 480);
		REQUIRE(atlas.prepare(gfx, scene, 0, 0, 12, 9, 0, 0, fixture.team->me, true, 0));
		gfx.endMapTransform();
		const auto pending = atlas.pendingHdMasks();
		REQUIRE(pending > 20); // The new density has barely started.
		const auto stepped = frame(1.6f, 12, 9);
		settle(1.6f, 12, 9);
		const auto built = frame(1.6f, 12, 9);
		// Old masks differ from new ones only in contour detail. A cell drawn
		// from its first material alone would differ across whole cells.
		std::size_t differing = 0;
		for (std::size_t i = 0; i + 3 < built.size(); i += 4)
		{
			int largest = 0;
			for (int k = 0; k < 3; ++k) largest = std::max(largest, std::abs(int(built[i + k]) - int(stepped[i + k])));
			differing += largest > 48;
		}
		const double share = double(differing) / double(built.size() / 4);
		CAPTURE(pending);
		CAPTURE(share);
		CHECK(share < .03);
	}

	// Before the whole-map base is built (and with no retired atlas), some of
	// the pass's samplers have no texture. Legacy GL binds a 1x1 placeholder
	// there: a unit without a texture is "unloadable" to the driver even on
	// branches the shader skips, and some drivers fault on it.
	TEST_CASE("Legacy GL binds a placeholder to every sampler without a texture on its first draw [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480,
			.screenFlags = Uint32(GAGCore::GraphicContext::USEGPU),
			.beforeLoad = [](GlobalContainer &g) { g.settings.rendererBackend = GAGCore::RendererBackend::LegacyGL; }});
		auto &gfx = *globals->gfx;
		if (!gfx.isLegacyGLScreen() || !DirectTerrain::legacyPassAvailable())
		{
			MESSAGE("Legacy GL direct terrain unavailable; placeholder check omitted");
			return;
		}
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 5, .terrain = WATER, .discovered = true});
		const TerrainType kinds[] = {WATER, GRASS, SAND};
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 64; ++x)
				fixture.game.map.setVertexTerrain(x, y, kinds[((x / 3) * 7 + (y / 3) * 5) % 3]);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		DirectTerrain atlas;
		while (glGetError() != GL_NO_ERROR) {}
		bool drawn = false;
		for (Patience waiting; waiting() && !drawn;)
		{
			gfx.beginMapTransform(1.f, 0, 0, 0, 0, 640, 480);
			if (atlas.prepare(gfx, scene, 0, 0, 19, 14, 0, 0, fixture.team->me, true, 0))
			{
				atlas.drawPrepared(gfx, 0, 0, true, 0);
				drawn = true;
			}
			gfx.endMapTransform();
			if (!drawn) SDL_Delay(1);
		}
		REQUIRE(drawn);
		terrainPixels(true); // Finishes the draw.
		CHECK(glGetError() == GL_NO_ERROR);
		// No retired atlas yet, at least: those units take the placeholder.
		CHECK(atlas.placeholderBindings() >= 3);
	}

	TEST_CASE("Legacy GL frees the spare mask atlas once the view has been still [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480,
			.screenFlags = Uint32(GAGCore::GraphicContext::USEGPU),
			.beforeLoad = [](GlobalContainer &g) { g.settings.rendererBackend = GAGCore::RendererBackend::LegacyGL; }});
		auto &gfx = *globals->gfx;
		REQUIRE(gfx.isLegacyGLScreen());
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 5, .terrain = WATER, .discovered = true});
		const TerrainType kinds[] = {WATER, GRASS, SAND};
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 64; ++x)
				fixture.game.map.setVertexTerrain(x, y, kinds[((x / 3) * 7 + (y / 3) * 5) % 3]);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		DirectTerrain atlas;
		const auto settle = [&](float scale, int right, int bottom)
		{
			gfx.beginMapTransform(scale, 0, 0, 0, 0, 640, 480);
			for (Patience waiting; waiting();)
			{
				const bool ready = atlas.prepare(gfx, scene, 0, 0, right, bottom, 0, 0, fixture.team->me, true, 0);
				if (ready && !atlas.preparing() && !atlas.pendingHdMasks()) break;
				SDL_Delay(1);
			}
			REQUIRE_FALSE(atlas.pendingHdMasks());
			gfx.endMapTransform();
		};
		const auto frame = [&](float scale)
		{
			gfx.beginMapTransform(scale, 0, 0, 0, 0, 640, 480);
			gfx.drawFilledRect(0, 0, 640, 480, 11, 23, 37);
			atlas.drawPrepared(gfx, 0, 0, true, 0);
			auto pixels = terrainPixels(true);
			gfx.endMapTransform();
			return pixels;
		};
		settle(.76f, 26, 19);
		const auto single = atlas.textureBytes();
		// A zoom retires the first atlas; complete, it stays allocated as the spare.
		settle(1.6f, 12, 9);
		const auto withSpare = atlas.textureBytes();
		CHECK(withSpare > single);
		const auto reference = frame(1.6f);
		atlas.spareLifetimeMs = 0;
		settle(1.6f, 12, 9);
		CHECK(atlas.textureBytes() < withSpare);
		CHECK(frame(1.6f) == reference);
		// The next zoom allocates its atlas again and completes.
		settle(.76f, 26, 19);
		settle(1.6f, 12, 9);
		CHECK(frame(1.6f) == reference);
	}

	TEST_CASE("Legacy GL draws a zoomed-out view from the base mask alone once it covers the map [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480,
			.screenFlags = Uint32(GAGCore::GraphicContext::USEGPU),
			.beforeLoad = [](GlobalContainer &g) { g.settings.rendererBackend = GAGCore::RendererBackend::LegacyGL; }});
		auto &gfx = *globals->gfx;
		REQUIRE(gfx.isLegacyGLScreen());
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 5, .terrain = WATER, .discovered = true});
		const TerrainType kinds[] = {WATER, GRASS, SAND};
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 64; ++x)
				fixture.game.map.setVertexTerrain(x, y, kinds[((x / 3) * 7 + (y / 3) * 5) % 3]);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		DirectTerrain atlas;
		const auto prepare = [&](float scale, int right, int bottom)
		{
			gfx.beginMapTransform(scale, 0, 0, 0, 0, 640, 480);
			const bool ready = atlas.prepare(gfx, scene, 0, 0, right, bottom, 0, 0, fixture.team->me, true, 0);
			gfx.endMapTransform();
			return ready && !atlas.preparing() && !atlas.pendingHdMasks();
		};
		const auto frame = [&](float scale)
		{
			gfx.beginMapTransform(scale, 0, 0, 0, 0, 640, 480);
			gfx.drawFilledRect(0, 0, 640, 480, 11, 23, 37);
			atlas.drawPrepared(gfx, 0, 0, true, 0);
			auto pixels = terrainPixels(true);
			gfx.endMapTransform();
			return pixels;
		};
		// Eight texels a cell, the base mask's own density. The workers build
		// the base ahead of any view mask; once it covers the map the view
		// draws every mixed cell from it and keeps no atlas of its own. Density
		// counts drawable pixels, so a high-density display needs a smaller
		// map scale.
		const float baseScale = .24f / float(gfx.getRasterScale());
		for (Patience waiting; waiting() && (!prepare(baseScale, 63, 31) || atlas.pendingBaseMasks());)
			SDL_Delay(1);
		REQUIRE_FALSE(atlas.pendingBaseMasks());
		REQUIRE(prepare(baseScale, 63, 31));
		CHECK(atlas.splitCells() == 0);
		CHECK(atlas.textureBytes() < 20u * 1024 * 1024);
		const auto fromBase = frame(baseScale);
		CHECK(fromBase != std::vector<Uint8>(fromBase.size(), 0));
		// Zooming in builds an atlas at the new density again.
		for (Patience waiting; waiting() && !prepare(.76f, 26, 19);) SDL_Delay(1);
		CHECK_FALSE(atlas.pendingHdMasks());
		CHECK(atlas.textureBytes() > 20u * 1024 * 1024);
	}

	TEST_CASE("Legacy GL cells never seen draw their whole-map base mask until their own lands [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480,
			.screenFlags = Uint32(GAGCore::GraphicContext::USEGPU),
			.beforeLoad = [](GlobalContainer &g) { g.settings.rendererBackend = GAGCore::RendererBackend::LegacyGL; }});
		auto &gfx = *globals->gfx;
		REQUIRE(gfx.isLegacyGLScreen());
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 5, .terrain = WATER, .discovered = true});
		const TerrainType kinds[] = {WATER, GRASS, SAND};
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 64; ++x)
				fixture.game.map.setVertexTerrain(x, y, kinds[((x / 3) * 7 + (y / 3) * 5) % 3]);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		DirectTerrain atlas;
		// The left half at 32-texel masks, until the base covers the whole map.
		gfx.beginMapTransform(.76f, 0, 0, 0, 0, 640, 480);
		for (Patience waiting; waiting();)
		{
			const bool ready = atlas.prepare(gfx, scene, 0, 0, 26, 19, 0, 0, fixture.team->me, true, 0);
			if (ready && !atlas.preparing() && !atlas.pendingHdMasks() && !atlas.pendingBaseMasks()) break;
			SDL_Delay(1);
		}
		REQUIRE_FALSE(atlas.pendingHdMasks());
		REQUIRE_FALSE(atlas.pendingBaseMasks());
		// The right half has never been shown: its first frame draws the base.
		REQUIRE(atlas.prepare(gfx, scene, 0, 0, 26, 19, 32, 0, fixture.team->me, true, 0));
		const auto pending = atlas.pendingHdMasks();
		REQUIRE(pending > 20);
		const auto frame = [&]
		{
			gfx.drawFilledRect(0, 0, 640, 480, 11, 23, 37);
			atlas.drawPrepared(gfx, 32, 0, true, 0);
			return terrainPixels(true);
		};
		const auto first = frame();
		for (Patience waiting; waiting() && atlas.pendingHdMasks();)
		{
			atlas.prepare(gfx, scene, 0, 0, 26, 19, 32, 0, fixture.team->me, true, 0);
			SDL_Delay(1);
		}
		REQUIRE_FALSE(atlas.pendingHdMasks());
		const auto built = frame();
		gfx.endMapTransform();
		std::size_t differing = 0;
		for (std::size_t i = 0; i + 3 < built.size(); i += 4)
		{
			int largest = 0;
			for (int k = 0; k < 3; ++k) largest = std::max(largest, std::abs(int(built[i + k]) - int(first[i + k])));
			differing += largest > 48;
		}
		const double share = double(differing) / double(built.size() / 4);
		CAPTURE(pending);
		CAPTURE(share);
		CHECK(share < .05);
	}

	TEST_CASE("Legacy GL mixed cells split between their corners until any mask lands [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480,
			.screenFlags = Uint32(GAGCore::GraphicContext::USEGPU),
			.beforeLoad = [](GlobalContainer &g) { g.settings.rendererBackend = GAGCore::RendererBackend::LegacyGL; }});
		auto &gfx = *globals->gfx;
		REQUIRE(gfx.isLegacyGLScreen());
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 5, .terrain = WATER, .discovered = true});
		const TerrainType kinds[] = {WATER, GRASS, SAND};
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 64; ++x)
				fixture.game.map.setVertexTerrain(x, y, kinds[((x / 3) * 7 + (y / 3) * 5) % 3]);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		DirectTerrain atlas;
		gfx.beginMapTransform(.76f, 0, 0, 0, 0, 640, 480);
		const auto frame = [&]
		{
			gfx.drawFilledRect(0, 0, 640, 480, 11, 23, 37);
			atlas.drawPrepared(gfx, 0, 0, true, 0);
			return terrainPixels(true);
		};
		// The first frame after the tables: no mask at any density yet, and
		// the base atlas is still empty.
		bool ready = false;
		for (int attempt = 0; attempt < 2000 && !ready; ++attempt)
			ready = atlas.prepare(gfx, scene, 0, 0, 26, 19, 0, 0, fixture.team->me, true, 0);
		REQUIRE(ready);
		const auto pending = atlas.pendingHdMasks();
		REQUIRE(pending > 100);
		const auto first = frame();
		for (Patience waiting; waiting() && atlas.pendingHdMasks();)
		{
			atlas.prepare(gfx, scene, 0, 0, 26, 19, 0, 0, fixture.team->me, true, 0);
			SDL_Delay(1);
		}
		REQUIRE_FALSE(atlas.pendingHdMasks());
		const auto built = frame();
		gfx.endMapTransform();
		// The split differs from the built masks only near the contours. Mixed
		// cells drawn whole in their top-left material differ in about 28% of
		// the view here; the split in about 8%.
		std::size_t differing = 0;
		for (std::size_t i = 0; i + 3 < built.size(); i += 4)
		{
			int largest = 0;
			for (int k = 0; k < 3; ++k) largest = std::max(largest, std::abs(int(built[i + k]) - int(first[i + k])));
			differing += largest > 48;
		}
		const double share = double(differing) / double(built.size() / 4);
		CAPTURE(pending);
		CAPTURE(share);
		CHECK(share < .14);
	}

	// One direct terrain frame over the whole 640x480 screen, as RGBA rows top
	// down, or nothing when the pass drew nothing this frame.
	std::vector<Uint8> directTerrainFrame(GAGCore::GraphicContext &gfx, DirectTerrain &atlas, const SceneMap &scene,
										  Uint32 teams, float zoom, int right, int bottom, bool complete,
										  bool whole = true, int time = 0)
	{
		const bool legacy = gfx.isLegacyGLScreen();
		gfx.setClipRect();
		gfx.drawFilledRect(0, 0, 640, 480, 11, 23, 37);
		gfx.beginMapTransform(zoom, 0, 0, 0, 0, 640, 480);
		const bool ready = atlas.prepare(gfx, scene, 0, 0, right, bottom, 0, 0, teams, whole, time, complete);
		if (ready) atlas.drawPrepared(gfx, 0, 0, whole, time);
		gfx.endMapTransform();
		// Captures run at the display's pixel density (twice 640x480 on a
		// Retina display); one sample per logical pixel keeps every check in
		// logical pixels.
		std::vector<Uint8> rows(std::size_t(640) * 480 * 4);
		const auto sample = [&](const Uint8 *pixels, int width, int height, std::size_t pitch, bool bottomUp)
		{
			REQUIRE(width * 480 == height * 640);
			const int scale = width / 640;
			for (int y = 0; y < 480; ++y)
			{
				const int source = bottomUp ? height - 1 - y * scale : y * scale;
				for (int x = 0; x < 640; ++x)
					std::memcpy(rows.data() + (std::size_t(y) * 640 + x) * 4, pixels + source * pitch + std::size_t(x) * scale * 4, 4);
			}
		};
		if (legacy)
		{
			if (!ready) return {};
			const auto pixels = terrainPixels(true);
			GLint viewport[4];
			glGetIntegerv(GL_VIEWPORT, viewport);
			// glReadPixels rows run bottom-up.
			sample(pixels.data(), viewport[2], viewport[3], std::size_t(viewport[2]) * 4, true);
			return rows;
		}
		gfx.renderer->flush();
		const bool drawn = ready && GAGCore::didRecordDeviceFrame(*gfx.renderer);
		if (drawn)
		{
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> captured(gfx.renderer->capture(), SDL_DestroySurface);
			REQUIRE(captured);
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
				SDL_ConvertSurface(captured.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
			REQUIRE(rgba);
			sample(static_cast<const Uint8 *>(rgba->pixels), rgba->w, rgba->h, std::size_t(rgba->pitch), false);
		}
		gfx.nextFrame();
		return drawn ? rows : std::vector<Uint8>{};
	}

	// SDL GPU and Programmable GL draw Legacy GL's streamed pass through
	// streamed device textures and terrain-direct-hd: the same HD artwork and
	// display-density masks, from 16 to 128 texels a cell, animated and with
	// undiscovered cells hidden.
	void checkStreamedDirectTerrain(GAGCore::RendererBackend backend)
	{
		const auto name = std::string(GAGCore::rendererBackendName(backend));
		struct View { float zoom; int right, bottom, time; bool whole; };
		// Zooms keep pixel centres off texel and cell boundaries, where
		// interpolation rounding alone would pick a neighbouring texel.
		const View views[] = {{.45f, 45, 34, 0, true}, {1.f, 20, 15, 7, true}, {2.f, 10, 8, 13, true},
							  {4.f, 5, 4, 21, true}, {1.f, 20, 15, 7, false}};
		const auto paint = [](glob2test::HeadlessGame &fixture)
		{
			// Blocks of water, grass and sand: most cells are mixed shores.
			const TerrainType kinds[] = {WATER, GRASS, SAND};
			for (int y = 0; y < 64; ++y)
				for (int x = 0; x < 64; ++x)
					fixture.game.map.setVertexTerrain(x, y, kinds[((x / 3) * 7 + (y / 3) * 5) % 3]);
			fixture.game.map.unsetMapDiscovered();
			fixture.game.map.setMapDiscovered(0, 0, 14, 9, fixture.team->me);
		};
		struct HighResolution
		{
			HighResolution() { GAGCore::Sprite::setHighResolution(true); }
			~HighResolution() { GAGCore::Sprite::setHighResolution(false); }
		};
		std::vector<std::vector<Uint8>> expected;
		{
			glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480,
				.screenFlags = Uint32(GAGCore::GraphicContext::USEGPU),
				.beforeLoad = [](GlobalContainer &g) { g.settings.rendererBackend = GAGCore::RendererBackend::LegacyGL; }});
			auto &gfx = *globals->gfx;
			HighResolution hd;
			if (!gfx.isLegacyGLScreen() || !DirectTerrain::legacyPassAvailable())
			{
				MESSAGE("Legacy GL direct terrain unavailable; comparison omitted");
				return;
			}
			glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 6, .terrain = WATER, .discovered = true});
			paint(fixture);
			SceneMap scene;
			glob2test::observeMap(fixture.game.map, scene);
			DirectTerrain atlas;
			for (const auto &view : views)
			{
				auto rows = directTerrainFrame(gfx, atlas, scene, fixture.team->me, view.zoom, view.right, view.bottom,
											   true, view.whole, view.time);
				REQUIRE_FALSE(rows.empty());
				REQUIRE_FALSE(atlas.pendingHdMasks());
				expected.push_back(std::move(rows));
			}
		}
		glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480,
			.beforeLoad = [backend](GlobalContainer &g) { g.settings.rendererBackend = backend; }});
		auto &gfx = *globals->gfx;
		HighResolution hd;
		if (!DirectTerrain::streamedPassAvailable(gfx))
		{
			MESSAGE(name << " streamed direct terrain unavailable (no device or terrain-direct-hd shader); comparison omitted");
			return;
		}
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 6, .terrain = WATER, .discovered = true});
		paint(fixture);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		DirectTerrain atlas;
		for (std::size_t index = 0; index < std::size(views); ++index)
		{
			const auto &view = views[index];
			CAPTURE(view.zoom);
			CAPTURE(view.whole);
			std::vector<Uint8> frame;
			for (int attempt = 0; attempt < 32 && frame.empty(); ++attempt)
				frame = directTerrainFrame(gfx, atlas, scene, fixture.team->me, view.zoom, view.right, view.bottom,
										   true, view.whole, view.time);
			REQUIRE_FALSE(frame.empty());
			REQUIRE_FALSE(atlas.failed());
			REQUIRE_FALSE(atlas.pendingHdMasks());
			unsigned maximum = 0;
			std::size_t differing = 0;
			for (int y = 0; y < 480; ++y)
				for (int x = 0; x < 640; ++x)
				{
					const auto *actual = frame.data() + (std::size_t(y) * 640 + x) * 4;
					const auto *reference = expected[index].data() + (std::size_t(y) * 640 + x) * 4;
					unsigned largest = 0;
					for (int k = 0; k < 3; ++k)
						largest = std::max(largest, unsigned(std::abs(int(actual[k]) - int(reference[k]))));
					maximum = std::max(maximum, largest);
					differing += largest > 2;
				}
			CAPTURE(maximum);
			CAPTURE(differing);
			// Both draw the one shared shader over the same texel values; only
			// interpolation rounding differs between drivers.
			CHECK(differing <= 640 * 480 / 1000);
		}
	}

	TEST_CASE("SDL GPU streamed direct terrain matches Legacy GL's HD pass at every density [display]")
	{
		checkStreamedDirectTerrain(GAGCore::RendererBackend::SDLGPU);
	}

	TEST_CASE("Programmable GL streamed direct terrain matches Legacy GL's HD pass at every density [display]")
	{
		checkStreamedDirectTerrain(GAGCore::RendererBackend::ProgrammableGL);
	}

	// A fast zoom out changes the art density, and the art atlas refills at the
	// new one over several frames while the old one keeps drawing. Variants the
	// old atlas never held draw the resident low-resolution art meanwhile: every
	// frame of the refill shows each cell's own materials, never another
	// variant's tile or an empty one (the stale-record zoom glitch). Compared in
	// 16-pixel blocks with the completed frame, low-resolution art only softens
	// detail; a foreign tile changes a block's colour outright.
	void checkZoomDrawsOwnArt(GAGCore::RendererBackend backend)
	{
		const auto name = std::string(GAGCore::rendererBackendName(backend));
		const bool legacyBackend = backend == GAGCore::RendererBackend::LegacyGL;
		glob2test::HeadlessGlobals globals({.display = true, .width = 640, .height = 480,
			.screenFlags = legacyBackend ? Uint32(GAGCore::GraphicContext::USEGPU) : 0u,
			.beforeLoad = [backend](GlobalContainer &g) { g.settings.rendererBackend = backend; }});
		auto &gfx = *globals->gfx;
		if (legacyBackend ? !gfx.isLegacyGLScreen() || !DirectTerrain::legacyPassAvailable()
						  : !DirectTerrain::streamedPassAvailable(gfx))
		{
			MESSAGE(name << " direct terrain unavailable; zoom check omitted");
			return;
		}
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 6, .terrain = WATER, .discovered = true});
		// Blocks of water, grass and sand: shores of every orientation, so the
		// whole map holds many more variants than a few cells do.
		const TerrainType kinds[] = {WATER, GRASS, SAND};
		for (int y = 0; y < 64; ++y)
			for (int x = 0; x < 64; ++x)
				fixture.game.map.setVertexTerrain(x, y, kinds[((x / 3) * 7 + (y / 3) * 5) % 3]);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		DirectTerrain atlas;
		const Uint32 teams = fixture.team->me;
		// Settled close up: a few cells' variants at 128 pixels a cell.
		bool settled = false;
		for (Patience waiting; waiting() && !settled;)
		{
			const auto frame = directTerrainFrame(gfx, atlas, scene, teams, 4.f, 4, 3, false);
			settled = !frame.empty() && !atlas.pendingHdMasks() && !atlas.artMetrics().absent;
			if (!settled) SDL_Delay(1);
		}
		REQUIRE(settled);
		// Out to the whole map at 16 pixels a cell, frame after frame while
		// the atlas refills.
		constexpr float wholeMapZoom = .3f; // Not `far`: a Windows header macro.
		std::vector<std::vector<Uint8>> refilling;
		std::size_t mostAbsent = 0;
		for (Patience waiting; waiting();)
		{
			auto frame = directTerrainFrame(gfx, atlas, scene, teams, wholeMapZoom, 63, 63, false);
			const auto art = atlas.artMetrics();
			CAPTURE(art.visible);
			CAPTURE(art.absent);
			REQUIRE(art.stale == 0);
			mostAbsent = std::max(mostAbsent, art.absent);
			if (!frame.empty() && art.absent) refilling.push_back(std::move(frame));
			if (!art.absent && !atlas.pendingHdMasks()) break;
			SDL_Delay(1);
		}
		CAPTURE(mostAbsent);
		REQUIRE(mostAbsent > 0); // The zoom drew variants the drawing atlas did not hold.
		REQUIRE_FALSE(refilling.empty());
		const auto complete = directTerrainFrame(gfx, atlas, scene, teams, wholeMapZoom, 63, 63, true);
		REQUIRE_FALSE(complete.empty());
		const auto blockMean = [](const std::vector<Uint8> &rows, int bx, int by, int channel)
		{
			unsigned sum = 0;
			for (int y = by * 16; y < by * 16 + 16; ++y)
				for (int x = bx * 16; x < bx * 16 + 16; ++x)
					sum += rows[(std::size_t(y) * 640 + x) * 4 + channel];
			return int(sum / 256);
		};
		// The map covers 64 cells of 9.6 pixels: 38 blocks across, and the
		// screen's 30 down.
		constexpr int blocksWide = 38, blocksHigh = 30;
		for (std::size_t index = 0; index < refilling.size(); ++index)
		{
			std::size_t foreign = 0;
			int largest = 0;
			for (int by = 0; by < blocksHigh; ++by)
				for (int bx = 0; bx < blocksWide; ++bx)
				{
					int difference = 0;
					for (int k = 0; k < 3; ++k)
						difference = std::max(difference, std::abs(blockMean(refilling[index], bx, by, k) -
																	blockMean(complete, bx, by, k)));
					largest = std::max(largest, difference);
					foreign += difference > 24;
				}
			CAPTURE(index);
			CAPTURE(largest);
			CAPTURE(foreign);
			CHECK(foreign == 0);
		}
	}

	TEST_CASE("Legacy GL zoom refills the art atlas without drawing foreign or empty tiles [display]")
	{
		checkZoomDrawsOwnArt(GAGCore::RendererBackend::LegacyGL);
	}

	TEST_CASE("SDL GPU zoom refills the art atlas without drawing foreign or empty tiles [display]")
	{
		checkZoomDrawsOwnArt(GAGCore::RendererBackend::SDLGPU);
	}

	TEST_CASE("Programmable GL zoom refills the art atlas without drawing foreign or empty tiles [display]")
	{
		checkZoomDrawsOwnArt(GAGCore::RendererBackend::ProgrammableGL);
	}

	TEST_CASE("generated terrain mip uploads stay uncompressed across animation updates [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 256, .height = 256,
			.screenFlags = Uint32(GAGCore::GraphicContext::USEGPU)});
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
			SDL_CreateSurface(128, 128, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
		REQUIRE(pixels);
		TerrainVisual::Surface image(pixels.get(), true);
		pixels.release();
		for (Uint32 color : {0xFF319ABCu, 0xFFAD6712u})
		{
			SDL_FillSurfaceRect(image.getSDLSurface(), nullptr, color);
			image.markPixelsChanged();
			image.prepareTexture();
			GLint compressed = 1, width = 0;
			glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED, &compressed);
			glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &width);
			CHECK(compressed == GL_FALSE);
			CHECK(width == 64);
			CHECK(glGetError() == GL_NO_ERROR);
		}
	}
#endif
	TEST_CASE("complete torus terrain working set survives repeated atlas sweeps [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 256, .height = 256,
			.screenFlags = Uint32(GAGCore::GraphicContext::PORTABLEGPU)});
		glob2test::HeadlessGame fixture({.wDec = 8, .hDec = 8, .discovered = true});
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		SoftwareTerrainCache cache;
		const auto sweep = [&]()
		{
			for (int y = 0; y < 256; y += 64)
				for (int x = 0; x < 256; x += 64)
				{
					REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 65, 65, x - 1, y - 1,
						fixture.team->me, true, 19, true));
					cache.draw(*globals->gfx);
					CHECK(cache.bytes() <= SoftwareTerrainCache::GPUCacheBudget);
					CHECK(cache.residentTextureBytes() <= SoftwareTerrainCache::GPUBudget);
				}
		};
		sweep();
		const auto rebuilds = cache.cacheRebuilds();
		CHECK(rebuilds == 256);
		sweep();
		CHECK(cache.cacheRebuilds() == rebuilds);
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
		glob2test::observeMap(fixture.game.map,scene);
		// An independently sized strip fits HD pages, unlike its whole capture.
		SoftwareTerrainCache independent;
		{
			// Portable views compose HD only zoomed in past a screen pixel per
			// logical pixel: two of them here, whatever the display density.
			const float screenPixel = 1.f / float(globals->gfx->getRasterScale());
			const GAGCore::MapTransformScope zoomed(*globals->gfx, 2 * screenPixel, 0, 0, {0, 0, 256, 256});
			// Interactive pages show native art until the declared HD window streams.
			independent.prepare(scene, *globals->terrain, 0, 0, 6, 63, 0, 0, fixture.team->me, true);
			GAGCore::Sprite::finishHighResolution();
			REQUIRE(independent.prepare(scene, *globals->terrain, 0, 0, 6, 63, 0, 0, fixture.team->me,
										true));
		}
		CHECK(globals->terrainCompositor().scale() == 4);
		// Portable pages compose at the display density (two screen pixels per
		// logical pixel here).
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
		glob2test::observeMap(fixture.game.map,scene);
		ScopedTerrainDeviceLimit device(*globals->gfx);
		auto &backend = device.backend();
		// Portable views compose HD only zoomed in past a screen pixel per
		// logical pixel: two of them here, whatever the display density.
		const float screenPixel = 1.f / float(globals->gfx->getRasterScale());
		const GAGCore::MapTransformScope zoomed(*globals->gfx, 2 * screenPixel, 0, 0, {0, 0, 256, 256});
		for (int limit : {1024, 512, 128, 32})
		{
			CAPTURE(limit);
			backend.limit = limit;
			backend.largestTexture = 0;
			CHECK(globals->gfx->maximumTextureSize() == limit);
			SoftwareTerrainCache cache;
			cache.prepare(scene, *globals->terrain, 0, 0, 7, 7, 0, 0, fixture.team->me, true);
			GAGCore::Sprite::finishHighResolution();
			const bool admitted =
				cache.prepare(scene, *globals->terrain, 0, 0, 7, 7, 0, 0, fixture.team->me, true);
			CHECK(globals->terrainCompositor().scale() == 4);
			CHECK(admitted == (limit >= SoftwareTerrainCache::ChunkPixels));
			if (admitted)
			{
				// Pages hold fewer cells at higher density: every page is
				// ChunkPixels wide, so a limit that admits one keeps the
				// display density.
				CHECK(cache.resolution == 2);
				cache.draw(*globals->gfx);
				CHECK(backend.largestTexture == SoftwareTerrainCache::ChunkPixels);
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
#ifdef HAVE_OPENGL
	TEST_CASE("GPU terrain retains CPU pages when the visible textures exceed its budget [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 1600, .height = 1280,
			.screenFlags = GAGCore::GraphicContext::USEGPU});
		glob2test::HeadlessGame fixture({.wDec = 8, .hDec = 8, .discovered = true});
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		SoftwareTerrainCache cache;
		auto &gfx = *globals->gfx;
		const auto draw = [&](bool cached)
		{
			gfx.drawFilledRect(0, 0, 1600, 1280, 17, 29, 41);
			gfx.beginMapTransform(.75f, .375f, .625f, 0, 0, 1600, 1280);
			if (cached)
			{
				REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 125, 100, 15, 15,
					fixture.team->me, true, 19));
				cache.draw(gfx);
			}
			else SoftwareTerrainCache::drawUncached(scene, *globals->terrain, 0, 0, 125, 100,
				15, 15, fixture.team->me, true, 19);
			gfx.endMapTransform();
			return terrainPixels(true);
		};
		const auto checksum = fixture.checksum();
		const auto expected = draw(false);
		CHECK(draw(true) == expected);
		const auto rebuilt = cache.cacheRebuilds();
		CHECK(draw(true) == expected);
		CHECK(cache.cacheRebuilds() == rebuilt);
		CHECK(cache.bytes() <= SoftwareTerrainCache::GPUCacheBudget);
		CHECK(cache.residentTextureBytes() <= SoftwareTerrainCache::GPUBudget);
		CHECK(std::any_of(cache.chunks.begin(), cache.chunks.end(),
			[](const auto &chunk) { return bool(chunk->parked); }));
		CHECK(fixture.checksum() == checksum);
	}
#endif

	TEST_CASE("GPU terrain zoom round trips reuse pixels and refresh parked pages [display]")
	{
		for (Uint32 flags : {Uint32(GAGCore::GraphicContext::PORTABLEGPU),
							 Uint32(GAGCore::GraphicContext::USEGPU)})
		{
#ifndef HAVE_OPENGL
			if (flags == GAGCore::GraphicContext::USEGPU) continue;
#endif
			CAPTURE(flags);
			glob2test::HeadlessGlobals globals({.display = true, .width = 512, .height = 512,
				.screenFlags = flags});
			glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true});
			auto &map = fixture.game.map;
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 3; ++x) map.paintCell(x, y, WATER);
			SceneMap scene;
			glob2test::observeMap(map, scene);
			auto &gfx = *globals->gfx;
			SoftwareTerrainCache cache;
			const auto draw = [&](SoftwareTerrainCache &pages, int scale, int time, int reduction = 0, bool whole = true)
			{
				gfx.drawFilledRect(0, 0, 512, 512, 17, 29, 41);
				gfx.beginMapTransform(.75f, .375f, .625f, 0, 0, 512, 512);
				REQUIRE(pages.prepareAtResolution(scene, *globals->terrain, 0, 0, 15, 15,
					31, 31, fixture.team->me, whole, time, scale, false, reduction));
				pages.draw(gfx);
				gfx.endMapTransform();
				CHECK(pages.bytes() <= SoftwareTerrainCache::GPUBudget);
			};
			const auto pixels = [&]
			{
				if (flags == GAGCore::GraphicContext::USEGPU) return terrainPixels(true);
				std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> captured(
					gfx.renderer->capture(), SDL_DestroySurface);
				REQUIRE(captured);
				std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
					SDL_ConvertSurface(captured.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
				REQUIRE(rgba);
				std::vector<Uint8> result(size_t(rgba->w) * rgba->h * 4);
				for (int y = 0; y < rgba->h; ++y)
					std::memcpy(result.data() + size_t(y) * rgba->w * 4,
						static_cast<Uint8 *>(rgba->pixels) + y * rgba->pitch, rgba->w * 4);
				return result;
			};
			draw(cache, 2, 19);
			const auto original = pixels();
			const auto *resident = cache.copies.front().chunk->image.get();
			draw(cache, 1, 19);
			CHECK(std::any_of(cache.chunks.begin(), cache.chunks.end(),
				[resident](const auto &chunk) { return chunk->image.get() == resident; }));
			const auto rebuilt = cache.cacheRebuilds();
			draw(cache, 2, 19);
			CHECK(cache.cacheRebuilds() == rebuilt);
			CHECK(pixels() == original);
			CHECK(cache.copies.front().chunk->image.get() == resident);
			// Exercise retirement explicitly even when this small view fits all
			// textures; the large-view test also retires them under real pressure.
			draw(cache, 1, 19);
			for (auto &chunk : cache.chunks)
				if (chunk->scale == 2 && chunk->image)
				{
					chunk->parked.reset(chunk->image->takePixels());
					chunk->image.reset();
				}
			CHECK(std::any_of(cache.chunks.begin(), cache.chunks.end(),
				[](const auto &chunk) { return bool(chunk->parked); }));
			// Terrain edits and animation changes must invalidate a parked level.
			map.paintCell(0, 0, SAND);
			glob2test::observeMap(map, scene);
			const auto checksum = fixture.checksum();
			draw(cache, 2, 25);
			CHECK(cache.cacheRebuilds() > rebuilt);
			const auto refreshed = pixels();
			SoftwareTerrainCache fresh;
			draw(fresh, 2, 25);
			CHECK(pixels() == refreshed);
			draw(cache, 1, 25);
			draw(cache, 1, 25, 2);
			const auto reduced = pixels();
			CHECK(cache.cacheReductions() > 0);
			SoftwareTerrainCache reducedFresh;
			draw(reducedFresh, 1, 25, 2);
			CHECK(pixels() == reduced);
			CHECK(reducedFresh.cacheReductions() == 0);
			CHECK(fixture.checksum() == checksum);
			map.unsetMapDiscovered();
			map.setMapDiscovered(0, 0, 8, 16, fixture.team->me);
			glob2test::observeMap(map, scene);
			draw(cache, 2, 25, 0, false);
			const auto hidden = pixels();
			SoftwareTerrainCache freshHidden;
			draw(freshHidden, 2, 25, 0, false);
			CHECK(pixels() == hidden);
		}
	}

	TEST_CASE("presentation trims free idle terrain pages in bounded steps and keep the view [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 512, .height = 512,
			.screenFlags = Uint32(GAGCore::GraphicContext::PORTABLEGPU)});
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 6, .discovered = true});
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		auto &gfx = *globals->gfx;
		SoftwareTerrainCache cache;
		const auto draw = [&](int viewX, int viewY)
		{
			gfx.drawFilledRect(0, 0, 512, 512, 17, 29, 41);
			gfx.beginMapTransform(1.f, 0.f, 0.f, 0, 0, 512, 512);
			REQUIRE(cache.prepareAtResolution(scene, *globals->terrain, 0, 0, 15, 15,
				viewX, viewY, fixture.team->me, true, 0, 1));
			cache.draw(gfx);
			gfx.endMapTransform();
		};
		const auto drawn = [&] { return std::count_if(cache.chunks.begin(), cache.chunks.end(),
			[&](const auto &chunk) { return chunk->used == cache.frame; }); };
		// Visit the whole 64x64 map, then settle on one corner.
		for (int y = 0; y < 64; y += 16)
			for (int x = 0; x < 64; x += 16) draw(x, y);
		draw(0, 0);
		const auto view = drawn();
		const auto resident = cache.chunks.size();
		REQUIRE(resident > std::size_t(view) + 8);
		GAGCore::PresentationTrimSubscription::notify(GAGCore::PresentationTrim::LowMemory);
		draw(0, 0);
		// At most eight idle pages go a frame; the view's pages never do.
		CHECK(cache.chunks.size() >= resident - 8);
		CHECK(cache.chunks.size() < resident);
		for (int frame = 0; frame < 64 && cache.trimBefore; ++frame) draw(0, 0);
		CHECK(cache.trimBefore == 0);
		CHECK(cache.chunks.size() == std::size_t(drawn()));
		CHECK(drawn() == view);
		// Backgrounded, every idle page goes at once.
		draw(32, 32);
		CHECK(cache.chunks.size() > std::size_t(drawn()));
		GAGCore::PresentationTrimSubscription::notify(GAGCore::PresentationTrim::Background);
		CHECK(cache.chunks.size() == std::size_t(drawn()));
		CHECK(cache.trimBefore == 0);
		draw(0, 0); // Trimmed pages compose again.
		CHECK(drawn() == view);
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
							map.paintCell(x, y, WATER);
						else if (y % 32 < 4)
							map.paintCell(x, y, ICE);
			}
			SceneMap scene;
			glob2test::observeMap(map,scene);
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
			CHECK(cache.bytes() <= SoftwareTerrainCache::GPUCacheBudget);
			CHECK(cache.residentTextureBytes() <= SoftwareTerrainCache::GPUBudget);
			CHECK(cache.cacheRebuilds() == rebuilds);
			CHECK(cache.cacheHits() >= rebuilds * 5);
			REQUIRE(!cache.chunks.empty());
			CHECK(chunkPixels(*cache.copies.front().chunk)->w < SoftwareTerrainCache::ChunkPixels);
			// Check the reduced pixels of uniform cells against independent area
			// averages of native composition, including partially transparent
			// coast tiles, and mixed cells against their reduced composition.
			const auto &chunk = *cache.copies.front().chunk;
			const int divisor = SoftwareTerrainCache::ChunkPixels / chunkPixels(chunk)->w;
			const int tileSize = 32 / divisor, count = divisor * divisor;
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> tile(
				SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
			REQUIRE(tile);
			unsigned mismatches = 0, partial = 0, mixed = 0;
			for (int ty : {0, 3, 4, 15})
				for (int tx : {0, 3, 4, 15})
				{
					const auto &recipe = chunk.tiles[ty * 16 + tx].recipe;
					if (!recipe.uniform())
					{
						++mixed;
						globals->terrainCompositor().compose(recipe, tile.get(), 0, 0, 1, nullptr, nullptr,
															 divisor);
						const auto *surface = chunkPixels(chunk);
						for (int y = 0; y < tileSize; ++y)
							mismatches += std::memcmp(
								static_cast<const Uint8 *>(tile->pixels) + y * tile->pitch,
								static_cast<const Uint8 *>(surface->pixels) + (ty * tileSize + y) * surface->pitch +
									tx * tileSize * 4,
								std::size_t(tileSize) * 4) != 0;
						continue;
					}
					globals->terrainCompositor().compose(recipe, tile.get(), 0, 0, 1);
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
							const auto *surface = chunkPixels(chunk);
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
			// Terrain is opaque since water became an ordinary material, so these
			// tiles carry no partial coverage; the arithmetic above still covers it.
			(void)partial;
			(void)mixed;
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
			map.paintCell(0, 0, TRAIL);
			glob2test::observeMap(map,scene);
			draw(-7, -6);
			CHECK(cache.cacheRebuilds() > rebuilds);
			CHECK(pixels() != expected);
			map.paintCell(0, 0, WATER);
			CHECK(fixture.checksum() == checksum);
			// Returning to a close view restores native page density.
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, 0, 0, fixture.team->me,
								  true, 19));
			CHECK(chunkPixels(*cache.copies.front().chunk)->w >= SoftwareTerrainCache::ChunkPixels);
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
			CHECK(hidpiAdmitted);
			if (hidpiAdmitted)
				CHECK(chunkPixels(*cache.copies.front().chunk)->w == SoftwareTerrainCache::ChunkPixels);
			CHECK(cache.bytes() <= SoftwareTerrainCache::GPUCacheBudget);
			CHECK(cache.residentTextureBytes() <= SoftwareTerrainCache::GPUBudget);
		}
	}
	TEST_CASE("portable pages compose HD frames only above one screen pixel per logical pixel [display]")
	{
		glob2test::ScopedEnvironment hdPath("GLOB2_EXPERIMENT_TEXTURE_DIR",
			highResolutionFixture().string().c_str());
		glob2test::HeadlessGlobals globals({.display = true, .width = 256, .height = 256,
			.screenFlags = Uint32(GAGCore::GraphicContext::PORTABLEGPU)});
		REQUIRE(globals->gfx->backendSwitchStatus().active == GAGCore::RendererBackend::PortableSDL);
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true});
		auto &map = fixture.game.map;
		for (int y = 0; y < map.getH(); ++y)
			for (int x = 0; x < map.getW(); ++x)
				map.paintCell(x, y, x < 3 ? WATER : ((x + y) % 3 ? ICE : GRASS));
		struct Restore
		{
			const std::int64_t delay = GAGCore::Sprite::highResolutionDetailDelay();
			const GAGCore::Sprite::RetiredHighResolutionBudget budget =
				GAGCore::Sprite::retiredHighResolutionBudget();
			~Restore()
			{
				GAGCore::Sprite::setHighResolution(false);
				GAGCore::Sprite::setHighResolutionDetailDelay(delay);
				GAGCore::Sprite::setRetiredHighResolutionBudget(budget);
			}
		} restore;
		GAGCore::Sprite::setHighResolution(true);
		GAGCore::Sprite::setHighResolutionDetailDelay(3'600'000'000'000);
		GAGCore::Sprite::setRetiredHighResolutionBudget({64u * 1024 * 1024, 3'600'000'000'000});
		SceneMap scene;
		glob2test::observeMap(map, scene);
		auto &compositor = globals->terrainCompositor(map.frozenAssetBundle());
		SoftwareTerrainCache cache;
		const auto draw = [&](float zoom)
		{
			globals->gfx->drawFilledRect(0, 0, 256, 256, 17, 29, 41);
			globals->gfx->beginMapTransform(zoom, 0, 0, 0, 0, 256, 256);
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 7, 7, 0, 0, fixture.team->me, true));
			cache.draw(*globals->gfx);
			globals->gfx->endMapTransform();
		};
		// HD layers resident on the material and decor sprites the view uses.
		const auto resident = [&](bool decor)
		{
			std::set<GAGCore::Sprite *> sprites;
			for (const auto &material : compositor.catalog().materials)
				if (const auto &name = decor ? material.decor.sprite : material.sprite; !name.empty())
					if (auto *sprite = GAGCore::Toolkit::getSprite(name)) sprites.insert(sprite);
			std::size_t frames = 0;
			for (auto *sprite : sprites)
				frames += std::count_if(sprite->experimentImages.begin(), sprite->experimentImages.end(),
					[](auto *image) { return image != nullptr; });
			return frames;
		};
		const auto settle = [&](float zoom)
		{
			draw(zoom);
			GAGCore::Sprite::finishHighResolution();
			draw(zoom);
		};
		// Zooms below are screen pixels per logical pixel; a high-density
		// display draws more screen pixels at the same map zoom.
		const float screenPixel = 1.f / float(globals->gfx->getRasterScale());

		// A view at one screen pixel per logical pixel composes native frames
		// and asks for no HD frame.
		settle(1 * screenPixel);
		CHECK(compositor.scale() == 1);
		CHECK(cache.samplingResolution() == 1);
		CHECK(resident(false) == 0);

		// Zooming in takes HD frames at once; decor still draws native sprites.
		settle(2 * screenPixel);
		CHECK(compositor.scale() == 4);
		CHECK(cache.samplingResolution() > 1);
		CHECK(resident(false) > 0);
		CHECK(resident(true) == 0);

		// Zooming back out keeps them until the view has wanted less for the
		// delay; pages meanwhile compose them at the display density.
		draw(1 * screenPixel);
		CHECK(compositor.scale() == 4);
		CHECK(cache.samplingResolution() == 1);

		// Then pages compose native frames, HD pages go and frames retire.
		GAGCore::Sprite::setHighResolutionDetailDelay(0);
		draw(1 * screenPixel);
		CHECK(compositor.scale() == 1);
		CHECK(cache.samplingResolution() == 1);
		CHECK(std::all_of(cache.chunks.begin(), cache.chunks.end(),
			[](const auto &chunk) { return chunk->scale == 1; }));
		CHECK(GAGCore::Sprite::retiredHighResolutionBytes() > 0);
		GAGCore::Sprite::setRetiredHighResolutionBudget({64u * 1024 * 1024, 0});
		GAGCore::Sprite::pollHighResolution(1);
		CHECK(resident(false) == 0);
		CHECK(GAGCore::Sprite::retiredHighResolutionBytes() == 0);

		// And zooming in again raises detail on the next frame.
		settle(1.25f * screenPixel);
		CHECK(compositor.scale() == 4);
		CHECK(resident(false) > 0);
	}
	TEST_CASE("a portable page uploads past idle parked pages over the budget [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 256, .height = 256,
			.screenFlags = Uint32(GAGCore::GraphicContext::PORTABLEGPU)});
		REQUIRE(globals->gfx->backendSwitchStatus().active == GAGCore::RendererBackend::PortableSDL);
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true});
		auto &map = fixture.game.map;
		for (int y = 0; y < map.getH(); ++y)
			for (int x = 0; x < map.getW(); ++x)
				map.paintCell(x, y, (x + y) % 3 ? ICE : GRASS);
		SceneMap scene;
		glob2test::observeMap(map, scene);
		SoftwareTerrainCache cache;
		const auto prepare = [&]
		{
			globals->gfx->drawFilledRect(0, 0, 256, 256, 17, 29, 41);
			globals->gfx->beginMapTransform(1, 0, 0, 0, 0, 256, 256);
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 7, 7, 0, 0, fixture.team->me, true));
		};
		const auto finish = [&]
		{
			cache.draw(*globals->gfx);
			globals->gfx->endMapTransform();
		};
		prepare();
		finish();
		prepare();
		auto visible = std::find_if(cache.chunks.begin(), cache.chunks.end(),
			[&](const auto &chunk) { return chunk->used == cache.frame && chunk->image; });
		REQUIRE(visible != cache.chunks.end());
		// The visible page lost its texture, its pixels parked, while pages of an
		// earlier zoom that this frame does not draw (HD, 2048 pixels square)
		// fill the budget and hold no texture to retire.
		auto *page = visible->get();
		page->parked.reset(page->image->takePixels());
		page->image.reset();
		std::size_t added = 0;
		while (cache.bytes() <= SoftwareTerrainCache::GPUCacheBudget)
		{
			auto idle = std::make_unique<SoftwareTerrainCache::Chunk>();
			idle->x = 1024 + int(added++) * SoftwareTerrainCache::ChunkTiles;
			idle->scale = 4;
			idle->valid = true;
			cache.chunks.push_back(std::move(idle));
		}
		// Uploading it again evicts idle pages instead of failing the frame.
		REQUIRE_NOTHROW(finish());
		CHECK(page->image);
		CHECK(!page->parked);
		CHECK(std::size_t(std::count_if(cache.chunks.begin(), cache.chunks.end(),
			[](const auto &chunk) { return chunk->scale == 4; })) < added);
		CHECK(cache.bytes() <= SoftwareTerrainCache::GPUCacheBudget);
	}
	TEST_CASE("a portable page retired after its pixels were released composes again [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 256, .height = 256,
			.screenFlags = Uint32(GAGCore::GraphicContext::PORTABLEGPU)});
		REQUIRE(globals->gfx->backendSwitchStatus().active == GAGCore::RendererBackend::PortableSDL);
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true});
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		SoftwareTerrainCache cache;
		const auto draw = [&]
		{
			globals->gfx->beginMapTransform(1, 0, 0, 0, 0, 256, 256);
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 7, 7, 0, 0, fixture.team->me, true));
			cache.draw(*globals->gfx);
			globals->gfx->endMapTransform();
		};
		draw();
		REQUIRE(!cache.chunks.empty());
		// What retiring a texture leaves when the portable renderer had already
		// released the page's pixels: takePixels() has nothing to park.
		auto &page = *cache.chunks.front();
		page.image.reset();
		page.parked.reset();
		page.valid = false;
		draw();
		CHECK(page.valid);
		CHECK(chunkPixels(page) != nullptr);
	}
	TEST_CASE("an interactive portable view reduces native pages on workers to the synchronous pixels [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 256, .height = 256,
			.screenFlags = Uint32(GAGCore::GraphicContext::PORTABLEGPU)});
		REQUIRE(globals->gfx->backendSwitchStatus().active == GAGCore::RendererBackend::PortableSDL);
		glob2test::HeadlessGame fixture({.wDec = 7, .hDec = 7, .discovered = true});
		auto &map = fixture.game.map;
		for (int y = 0; y < map.getH(); ++y)
			for (int x = 0; x < map.getW(); ++x)
				// Mixed cells in the page under test; plain grass keeps the rest cheap.
				map.paintCell(x, y, x >= 16 || y >= 16 ? GRASS : x < 3 ? WATER : ((x + y) % 3 ? ICE : GRASS));
		SceneMap scene;
		glob2test::observeMap(map, scene);
		// Zooms below are screen pixels per logical pixel.
		const float screenPixel = 1.f / float(globals->gfx->getRasterScale());
		struct Reduced
		{
			std::vector<Uint32> pixels;
			std::uint64_t reductions = 0;
		};
		const auto reduce = [&](bool interactive)
		{
			SoftwareTerrainCache cache;
			cache.deferBuilds = interactive;
			// An interactive view composes on workers: draw until nothing waits.
			const auto settle = [&](float zoom, int right, int bottom)
			{
				for (Patience waiting; waiting();)
				{
					globals->gfx->beginMapTransform(zoom, 0, 0, 0, 0, 256, 256);
					REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, right, bottom, 0, 0, fixture.team->me, true));
					cache.draw(*globals->gfx);
					globals->gfx->endMapTransform();
					if (cache.pendingPages() == 0 && cache.complete() &&
						std::all_of(cache.copies.begin(), cache.copies.end(), [&](const auto &copy)
							{ return copy.chunk->downsample == cache.samplingReduction(); }))
						return;
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				FAIL("the interactive view never completed");
			};
			settle(screenPixel, 7, 7);
			REQUIRE(cache.samplingReduction() == 1);
			// The whole map exceeds the page budget at native density.
			settle(.25f * screenPixel, 127, 127);
			REQUIRE(cache.samplingReduction() > 1);
			Reduced result;
			result.reductions = cache.cacheReductions();
			for (const auto &chunk : cache.chunks)
				if (chunk->x == 0 && chunk->y == 0 && chunk->downsample > 1)
				{
					const auto *pixels = chunkPixels(*chunk);
					REQUIRE(pixels);
					// Cells water reaches animate: the portable renderer's
					// interactive pages hold them at a keyframe's time and draw
					// keyframes over them, so only the still cells compare.
					const int still = 4 * 32 / chunk->downsample;
					for (int y = 0; y < pixels->h; ++y)
					{
						const auto *row = reinterpret_cast<const Uint32 *>(
							static_cast<const Uint8 *>(pixels->pixels) + y * pixels->pitch);
						result.pixels.insert(result.pixels.end(), row + still, row + pixels->w);
					}
				}
			return result;
		};
		const Reduced synchronous = reduce(false), interactive = reduce(true);
		REQUIRE(!synchronous.pixels.empty());
		CHECK(synchronous.reductions > 0);
		CHECK(interactive.reductions > 0);
		CHECK(interactive.pixels == synchronous.pixels);
	}
	TEST_CASE("a portable zoom into HD stands in with a larger coarser page, clipped to each small page [display]")
	{
		glob2test::ScopedEnvironment hdPath("GLOB2_EXPERIMENT_TEXTURE_DIR",
											highResolutionFixture().string().c_str());
		glob2test::HeadlessGlobals globals({.display = true, .width = 256, .height = 256,
			.screenFlags = Uint32(GAGCore::GraphicContext::PORTABLEGPU)});
		REQUIRE(globals->gfx->backendSwitchStatus().active == GAGCore::RendererBackend::PortableSDL);
		glob2test::HeadlessGame fixture({.wDec = 7, .hDec = 7, .discovered = true});
		GAGCore::Sprite::setHighResolution(true);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		SoftwareTerrainCache cache;
		cache.deferBuilds = true;
		// Zooms below are screen pixels per logical pixel.
		const float screenPixel = 1.f / float(globals->gfx->getRasterScale());
		const auto frame = [&](float zoom, int right, int bottom)
		{
			globals->gfx->beginMapTransform(zoom * screenPixel, 0, 0, 0, 0, 256, 256);
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, right, bottom, 0, 0, fixture.team->me, true));
			cache.draw(*globals->gfx);
			globals->gfx->endMapTransform();
		};
		const auto settle = [&](float zoom, int right, int bottom)
		{
			for (Patience waiting; waiting();)
			{
				frame(zoom, right, bottom);
				if (cache.pendingPages() == 0 && cache.complete() &&
					std::all_of(cache.copies.begin(), cache.copies.end(), [&](const auto &copy)
						{ return copy.chunk->scale == cache.samplingResolution(); }))
					return;
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			FAIL("the interactive view never completed");
		};
		// Native density: one 16-cell page holds the view.
		settle(1, 7, 7);
		REQUIRE(cache.samplingResolution() == 1);
		REQUIRE(cache.pageTiles == SoftwareTerrainCache::ChunkTiles);
		// At three screen pixels per logical pixel, the view wants HD pages of
		// four times density and fewer cells.
		frame(3, 5, 5);
		REQUIRE(cache.samplingResolution() == 4);
		CHECK(cache.pageTiles == SoftwareTerrainCache::pageTilesAt(4));
		CHECK(cache.pageTiles < SoftwareTerrainCache::ChunkTiles);
		// Until they arrive, a larger page of a coarser density draws each of
		// them, clipped to it: nothing falls to the overview.
		CHECK(cache.complete());
		CHECK(cache.backdropPages().empty());
		CHECK(cache.frameCoverage().backdropCells == 0);
		CHECK(cache.frameCoverage().standInCells == cache.frameCoverage().pageCells);
		for (const auto &copy : cache.copies)
		{
			CAPTURE(copy.clip.x);
			CAPTURE(copy.clip.y);
			CHECK(copy.chunk->scale < 4);
			CHECK(copy.chunk->span > cache.pageTiles);
			CHECK(copy.clip.w == cache.pageTiles * 32);
			CHECK(copy.clip.h == cache.pageTiles * 32);
			// Drawn where the small page it stands in for is.
			CHECK((copy.x + copy.clip.x) % (cache.pageTiles * 32) == 0);
			CHECK(copy.x + copy.clip.x < 8 * 32);
		}
		GAGCore::Sprite::finishHighResolution();
		settle(3, 5, 5);
		for (const auto &copy : cache.copies)
			CHECK(copy.chunk->span == cache.pageTiles);
	}
	TEST_CASE("a portable jump to pages never composed draws the resident base, not the overview [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 256, .height = 256,
			.screenFlags = Uint32(GAGCore::GraphicContext::PORTABLEGPU)});
		REQUIRE(globals->gfx->backendSwitchStatus().active == GAGCore::RendererBackend::PortableSDL);
		glob2test::HeadlessGame fixture({.wDec = 7, .hDec = 7, .discovered = true});
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		SoftwareTerrainCache cache;
		cache.deferBuilds = true;
		const auto frame = [&](int vx, int vy)
		{
			globals->gfx->beginMapTransform(1, 0, 0, 0, 0, 256, 256);
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 7, 7, vx, vy, fixture.team->me, true));
			cache.draw(*globals->gfx);
			globals->gfx->endMapTransform();
		};
		// The whole 128-cell map at 16 pixels a cell: 64 pages of 256 pixels.
		const std::size_t whole = 64u * 256 * 256 * 4;
		for (Patience waiting; waiting() && !(cache.pendingPages() == 0 && cache.baseTextureBytes() == whole);)
		{
			frame(0, 0);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		REQUIRE(cache.basePixelsPerCell() == 16);
		REQUIRE(cache.baseTextureBytes() == whole);
		// Its pages hold textures alone.
		CHECK(cache.basePixelBytes() == 0);
		// A jump to cells no page has composed at any density draws the base
		// beneath them while they compose: nothing falls to the overview.
		frame(64, 64);
		CHECK(cache.complete());
		CHECK(cache.backdropPages().empty());
		CHECK(cache.frameCoverage().backdropCells == 0);
		CHECK(cache.frameCoverage().standInCells == cache.frameCoverage().pageCells);
		REQUIRE_FALSE(cache.copies.empty());
		for (const auto &copy : cache.copies)
		{
			CHECK(copy.chunk->resident);
			CHECK(copy.chunk->downsample == 2);
		}
	}
	TEST_CASE("portable pages without HD artwork sample coverage at the display density [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 256, .height = 256,
			.screenFlags = Uint32(GAGCore::GraphicContext::PORTABLEGPU)});
		REQUIRE(globals->gfx->backendSwitchStatus().active == GAGCore::RendererBackend::PortableSDL);
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 6, .discovered = true});
		GAGCore::Sprite::setHighResolution(false);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		// Native material frames throughout; the pages' composition scale
		// follows the direct pass's mask density: a power of two at least the
		// display density, up to four. Zooms are screen pixels per logical
		// pixel.
		const float screenPixel = 1.f / float(globals->gfx->getRasterScale());
		for (const auto &[zoom, scale] : {std::pair{1.f, 1}, {1.5f, 2}, {2.f, 2}, {3.f, 4}, {4.f, 4}})
		{
			CAPTURE(zoom);
			SoftwareTerrainCache cache;
			globals->gfx->beginMapTransform(zoom * screenPixel, 0, 0, 0, 0, 256, 256);
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 7, 7, 0, 0, fixture.team->me, true));
			cache.draw(*globals->gfx);
			globals->gfx->endMapTransform();
			CHECK(globals->terrainCompositor().scale() == 1);
			CHECK(cache.samplingResolution() == scale);
			CHECK(cache.pageTiles == SoftwareTerrainCache::pageTilesAt(scale));
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
		glob2test::observeMap(fixture.game.map,scene);
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
			// native raster scale, irrespective of the window DPI. The terrain
			// working set still has to fit the complete capture budget.
			gfx.setRenderTargetScale(4);
			SoftwareTerrainCache cache;
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, 0, 0, fixture.team->me,
								  true, 19, true));
			CHECK(chunkPixels(*cache.copies.front().chunk)->w == SoftwareTerrainCache::ChunkPixels *
				SoftwareTerrainCache::capturePixelsPerCell(scene.getW(), scene.getH()) / 32);
			// The same map captured at 8px per cell needs reduced pages. A
			// narrow edge must retain the complete capture's sampling density.
			gfx.setRenderTargetScale(1);
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 31, 31, 0, 0, fixture.team->me,
								  true, 19, true));
			const auto side = chunkPixels(*cache.copies.front().chunk)->w;
			CHECK(side < SoftwareTerrainCache::ChunkPixels);
			const auto rebuilds = cache.cacheRebuilds();
			for (const auto &strip : {SDL_Rect{0, 0, 7, 32}, SDL_Rect{0, 0, 32, 7}})
			{
				REQUIRE(cache.prepare(scene, *globals->terrain, strip.x, strip.y,
									  strip.x + strip.w - 1, strip.y + strip.h - 1, 0, 0,
									  fixture.team->me, true, 19, true));
				CHECK(chunkPixels(*cache.copies.front().chunk)->w == side);
				CHECK(cache.cacheRebuilds() == rebuilds);
				CHECK(cache.bytes() <= SoftwareTerrainCache::GPUCacheBudget);
				CHECK(cache.residentTextureBytes() <= SoftwareTerrainCache::GPUBudget);
			}
		}
	}
	TEST_CASE(
		"undiscovered pages skip software submissions and refresh after discovery [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		glob2test::HeadlessGame fixture(
			{.wDec = 5, .hDec = 5, .terrain = WATER, .discovered = false});
		SceneMap scene;
		glob2test::observeMap(fixture.game.map,scene);
		SoftwareTerrainCache cache;
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
			SDL_CreateSurface(256, 256, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
		REQUIRE(pixels);
		// A scoped offscreen pass selects the real software backend on every host.
		globals->gfx->drawToSurface(
			pixels.get(), .73f,
			[&]
			{
				const auto draw = [&](bool wholeMap)
				{
					REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 7, 7, 0, 0,
										  fixture.team->me, wholeMap));
					const auto before = globals->gfx->renderer->operations();
					cache.draw(*globals->gfx);
					const auto after = globals->gfx->renderer->operations();
					return after.blits - before.blits + after.triangles - before.triangles;
				};
				// Water is an ordinary opaque material; only undiscovered cells
				// compose to empty tiles and submit nothing.
				CHECK(draw(false) == 0);
				fixture.game.map.setMapDiscovered(2, 2, 3, 3, fixture.team->me);
				glob2test::observeMap(fixture.game.map,scene);
				CHECK(draw(false) > 0);
				CHECK(draw(true) > 0);
			});
	}
	TEST_CASE("image import reads each pixel as a terrain vertex [artifacts]")
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
		// Catalogue terrain only by its exact colour: the dark-water colour itself
		// imports, while an off-palette dark blue nearest to it still becomes classic
		// water (a block, so the shore repair leaves its centre as water).
		paint(30, 30, 1, 1, DARK_WATER);
		image.drawFilledRect(31, 27, 7, 7, 0, 0, 128);
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
		// Each pixel is a vertex; a lone pixel colours the four cells around it.
		CHECK(map.vertexTerrainAt(8, 8) == TRAIL);
		CHECK(map.vertexTerrainAt(7, 8) == WATER);
		CHECK(map.vertexTerrainAt(8, 7) == WATER);
		CHECK(map.vertexTerrainAt(7, 7) == WATER);
		CHECK_FALSE(map.terrainPropertiesAt(7, 7).walkable);
		CHECK(map.terrainPropertiesAt(7, 7).swimmable);
		CHECK_FALSE(map.terrainPropertiesAt(6, 6).walkable);
		CHECK(map.vertexTerrainAt(0, 10) == ICE);
		CHECK(map.vertexTerrainAt(63, 10) == WATER);
		CHECK(map.vertexTerrainAt(20, 10) == TRAIL);
		CHECK(map.vertexTerrainAt(19, 10) == GRASS);
		CHECK(map.terrainPropertiesAt(18, 10).buildable);
		CHECK_FALSE(map.terrainPropertiesAt(19, 10).buildable);
		CHECK(map.vertexTerrainAt(30, 30) == DARK_WATER);
		CHECK(map.vertexTerrainAt(34, 30) == WATER);
		// An ordinary grass/water boundary still receives a sand beach.
		CHECK(map.vertexTerrainAt(15, 5) == SAND);
		CHECK(map.vertexTerrainAt(16, 5) == SAND);
	}
	TEST_CASE("every paintable built-in binds a material, has an editor icon and exports its own colour [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		auto &compositor = globals->terrainCompositor();
		const auto minimap = TerrainVisual::minimapPalette(compositor.catalog());
		// Wide enough for one row holding every non-classic paintable type.
		glob2test::HeadlessGame fixture({.wDec = 7, .hDec = 5, .teams = 0});
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
			// Each vertex exports its registered colour; the classic ones are
			// already covered by the ordinary map around them.
			if (classicTerrain(type))
				continue;
			map.paintCell(int(painted.size()) + 1, 1, type);
			painted.push_back(type);
		}
		REQUIRE(painted.size() + 1 < std::size_t(map.getW()));
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
		map.paintCell(1, 1, ICE);
		map.paintCell(2, 1, TRAIL);
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
#include "MapDrawer.h"
TEST_SUITE("TerrainValidation")
{
	GLOB2_TEST_CASE("decorated borders match cached and ordinary rendering",
					"[display][isolated][artifacts][writes-preferences]")
	{
		using namespace GAGCore;
		glob2test::HeadlessGlobals globals(
			{.display = true,
			 .width = 512,
			 .height = 512,
			 .beforeLoad = [](GlobalContainer &container)
			 { container.settings.rendererBackend = RendererBackend::Software; }});
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4, .discovered = true});
		auto &map = fixture.game.map;
		{
			auto edit = map.editTerrain();
			for (int y = 0; y < 16; ++y)
				for (int x = 0; x < 16; ++x)
					map.setVertexTerrain(x, y,
										 y >= 9   ? (x < 7 ? HEDGE : THICKET)
										 : x < 5  ? RIDGE_ROCK
										 : x < 9  ? OUTCROP
										 : x < 12 ? GRASS
												  : WATER);
			// Cliffs touching ridge rock, outcrop, hedge and grass: cells that
			// draw a cliff edge and the hedge in separate decor slots. Crags
			// beside the cliffs keep clear of them and draw nothing.
			for (int y = 6; y <= 11; ++y)
				for (int x = 3; x <= 9; ++x)
					if (x <= 6 || y <= 7)
						map.setVertexTerrain(x, y, CLIFFS);
		}
		for (const auto [x, y] : {std::pair{3, 5}, std::pair{5, 5}, std::pair{6, 10}})
			map.setResourceByIndex(x, y, WOOD, 3);
		auto &gfx = *globals->gfx;
		std::vector<RendererBackend> backends{RendererBackend::Software};
		std::ofstream backendEvidence(glob2test::artifactDir() / "rendering-backends.txt");
		REQUIRE(backendEvidence.good());
		for (const auto &choice : gfx.rendererAvailability())
		{
			backendEvidence << rendererBackendName(choice.backend)
							<< " available=" << choice.available << " reason=" << choice.reason
							<< '\n';
			if (choice.available && (choice.backend == RendererBackend::SDLGPU ||
									 choice.backend == RendererBackend::ProgrammableGL))
				backends.push_back(choice.backend);
		}
		if (backends.size() == 1)
			MESSAGE("Decor GPU parity coverage unavailable: no accelerated backend");
		const auto draw = [&](const SceneMap &scene, bool whole, int zoom, int viewport)
		{
			gfx.setClipRect();
			gfx.drawFilledRect(0, 0, 512, 512, 17, 29, 41);
			gfx.beginMapTransform(float(zoom), 0, 0, 0, 0, 512, 512);
			MapDrawer::drawMapTerrain(0, 0, 15, 15, viewport, viewport, 0, Game::DRAW_WHOLE_MAP,
										scene);
			MapDrawer::drawMapResources(0, 0, 15, 15, viewport, viewport, 0,
										  whole ? Game::DRAW_WHOLE_MAP : 0, scene);
			GAGCore::Sprite::flushBatches(&gfx);
			gfx.endMapTransform();
			if (gfx.renderer)
				gfx.renderer->flush();
			// Software mode can draw directly to its surface without a backend.
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> captured(
				gfx.renderer ? gfx.renderer->capture() : SDL_DuplicateSurface(gfx.getSDLSurface()),
				SDL_DestroySurface);
			REQUIRE(captured);
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
				SDL_ConvertSurface(captured.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
			REQUIRE(rgba);
			std::vector<Uint8> pixels(size_t(rgba->w) * rgba->h * 4);
			for (int row = 0; row < rgba->h; ++row)
				std::memcpy(pixels.data() + size_t(row) * rgba->w * 4,
							static_cast<Uint8 *>(rgba->pixels) + row * rgba->pitch, rgba->w * 4);
			if (whole)
				REQUIRE(IMG_SavePNG(
					rgba.get(),
					(glob2test::artifactDir() /
					 ("decorated-borders-" +
					  std::string(rendererBackendName(gfx.backendSwitchStatus().active)) + "-" +
					  std::to_string(zoom) + "x-view" + std::to_string(viewport) + ".png"))
						.string()
						.c_str()));
			gfx.nextFrame();
			return pixels;
		};
		SceneMap scene;
		glob2test::observeMap(map, scene);
		for (const auto backend : backends)
		{
			CAPTURE(int(backend));
			REQUIRE(gfx.requestBackendSwitch(backend));
			gfx.nextFrame();
			REQUIRE(gfx.backendSwitchStatus().phase == RendererSwitchPhase::Complete);
			REQUIRE(gfx.backendSwitchStatus().active == backend);
			const auto identity = gfx.renderingIdentity();
			backendEvidence << "tested=" << identity.backend << " glVendor=" << identity.glVendor
							<< " glRenderer=" << identity.glRenderer << '\n';
			for (const auto [zoom, viewport] : {std::pair{1, 0}, std::pair{1, 15}, std::pair{3, 3}})
			{
				CAPTURE(zoom);
				CAPTURE(viewport);
				const auto ordinary = draw(scene, false, zoom, viewport);
				CHECK(draw(scene, true, zoom, viewport) == ordinary);
				CHECK(draw(scene, true, zoom, viewport) == ordinary); // warmed geometry
			}
			// Changing corner membership can change the selected material/frame;
			// the row cache must adopt that deterministic selection. The vertex
			// is clear of the cliffs, so its crags draw (clear_of_masses).
			map.setVertexTerrain(5, 2, RIDGE_ROCK);
			glob2test::observeMap(map, scene);
			const auto ordinary = draw(scene, false, 1, 0);
			CHECK(draw(scene, true, 1, 0) == ordinary);
			map.setVertexTerrain(5, 2, OUTCROP);
			glob2test::observeMap(map, scene);
		}
		backendEvidence.flush();
		REQUIRE(backendEvidence.good());
	}

	TEST_CASE("decorated borders choose one material and suppress undecorated corners [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4, .discovered = true});
		auto &map = fixture.game.map;
		importBeforeMatch(fixture.game, R"({"schemaVersion":1,"terrains":[
            {"key":"test:raised","name":"Raised","base":"grass","properties":{},"appearance":"grass"},
            {"key":"test:plain","name":"Plain","base":"grass","properties":{},"appearance":"outcrop"},
            {"key":"test:ridge-alias","name":"Ridge alias","base":"grass","properties":{},"appearance":"ridge_rock"}
        ]})");
		auto catalog = TerrainVisual::loadCatalog();
		auto custom = catalog.materials[catalog.find("outcrop")];
		custom.key = "test:raised";
		const auto customId = TerrainVisual::MaterialId(catalog.materials.size());
		catalog.materials.push_back(custom);
		catalog.bindings[custom.key] = customId;
		catalog.bindings["test:plain"] = catalog.find("grass");
		TerrainVisual::Compositor compositor(std::move(catalog));
		const auto &definitions = compositor.catalog();
		const auto raised = *map.terrainRegistry().find("test:raised");
		const auto plain = *map.terrainRegistry().find("test:plain");
		const auto alias = *map.terrainRegistry().find("test:ridge-alias");
		const auto random = fixture.game.syncRandom;
		const auto frameFor = [&](std::array<TerrainType, 4> corners)
		{
			for (unsigned k = 0; k < 4; ++k)
				map.setVertexTerrain(4 + int(k & 1), 4 + int(k >> 1), corners[k]);
			SceneMap scene;
			glob2test::observeMap(map, scene);
			const int frame = onlyDecorFrame(compositor, scene, 4, 4);
			CHECK(onlyDecorFrame(compositor, scene, 4, 4) == frame);
			CHECK(onlyDecorFrame(compositor, scene, 20, 20) == frame); // wrapped coordinates
			return frame;
		};
		for (const auto pair : {std::pair{OUTCROP, RIDGE_ROCK}, std::pair{HEDGE, THICKET},
								std::pair{raised, RIDGE_ROCK}})
			for (unsigned mask = 0; mask < 16; ++mask)
			{
				CAPTURE(pair.first);
				CAPTURE(pair.second);
				CAPTURE(mask);
				std::array<TerrainType, 4> corners;
				for (unsigned k = 0; k < 4; ++k)
					corners[k] = mask & (1u << k) ? pair.first : pair.second;
				const int frame = frameFor(corners);
				const auto first =
					pair.first == raised
						? customId
						: definitions.find(pair.first == HEDGE ? "hedge" : "outcrop");
				const auto second =
					definitions.find(pair.second == THICKET ? "thicket" : "ridge_rock");
				const bool foliage = pair.first == HEDGE;
				const auto expected = [&](TerrainVisual::MaterialId id, unsigned ownMask)
				{
					return definitions.decorFrame(
						id, 4, 4, ownMask != 15, map.terrainSeed(), foliage ? 15 : ownMask,
						TerrainVisual::cornerColours(4, 4, 15, 15, map.terrainSeed()));
				};
				CHECK(((mask != 0 && frame == expected(first, mask)) ||
					   (mask != 15 && frame == expected(second, 15 ^ mask))));
			}
		// Foliage and the other obstacles draw separate slots, each from its own
		// corners: the hedge corner as a foliage edge, one rock among the rest.
		for (unsigned k = 0; k < 4; ++k)
			map.setVertexTerrain(4 + int(k & 1), 4 + int(k >> 1),
								 std::array{OUTCROP, RIDGE_ROCK, BOULDERS, HEDGE}[k]);
		SceneMap fourScene;
		glob2test::observeMap(map, fourScene);
		const auto four = compositor.decorLayers(fourScene, 4, 4);
		const auto colours = TerrainVisual::cornerColours(4, 4, 15, 15, map.terrainSeed());
		CHECK(four[0].frame == -1);
		CHECK(four[1].frame == -1);
		CHECK(four[2].frame == definitions.decorFrame(definitions.find("hedge"), 4, 4, true,
													  map.terrainSeed(), 8, colours));
		const std::array<TerrainVisual::MaterialId, 3> rocks{
			definitions.find("outcrop"), definitions.find("ridge_rock"), definitions.find("boulders")};
		bool valid = false;
		for (unsigned k = 0; k < 3; ++k)
			valid |= four[3].frame == definitions.decorFrame(rocks[k], 4, 4, true, map.terrainSeed(),
															 1u << k, colours);
		CHECK(valid);
		for (const auto undecorated : {GRASS, SAND, WATER, plain})
			CHECK(frameFor({OUTCROP, RIDGE_ROCK, OUTCROP, undecorated}) == -1);
		CHECK(frameFor({RIDGE_ROCK, alias, alias, RIDGE_ROCK}) ==
			  definitions.decorFrame(
				  definitions.find("ridge_rock"), 4, 4, false, map.terrainSeed(), 15,
				  TerrainVisual::cornerColours(4, 4, 15, 15, map.terrainSeed())));
		for (unsigned minorityCorners : {1u, 2u})
		{
			for (int y = 0; y < 16; y += 2)
				for (int x = 0; x < 16; x += 2)
					for (unsigned k = 0; k < 4; ++k)
						map.setVertexTerrain(x + int(k & 1), y + int(k >> 1),
											 k >= 4 - minorityCorners ? RIDGE_ROCK : OUTCROP);
			unsigned minority = 0, total = 0;
			for (unsigned seed = 1; seed <= 32; ++seed)
			{
				map.setTerrainSeed(seed);
				SceneMap scene;
				glob2test::observeMap(map, scene);
				for (int y = 0; y < 16; y += 2)
					for (int x = 0; x < 16; x += 2)
					{
						const int frame = onlyDecorFrame(compositor, scene, x, y);
						const auto &frames =
							definitions.materials[definitions.find("ridge_rock")].decor.edge;
						minority += std::find(frames.begin(), frames.end(), frame) != frames.end();
						++total;
					}
			}
			const double share = double(minority) / total;
			CHECK(share > double(minorityCorners) / 4 - .08);
			CHECK(share < double(minorityCorners) / 4 + .08);
		}
		CHECK(fixture.game.syncRandom == random);
	}

	TEST_CASE("connected foliage corners strips and filled patches [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(
			{.display = true, .width = 1024, .height = 1024, .seed = 7331});
		globals->terrainCompositor().prepare(false, 0);
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true,
			.loadDefaultRace = true, .header = true, .seed = 7331});
		auto &map = fixture.game.map;
		for (int panel = 0; panel < 2; ++panel)
		{
			const auto type = panel ? THICKET : HEDGE;
			for (unsigned mask = 0; mask < 16; ++mask)
			{
				const int x = panel * 16 + 1 + int(mask % 4) * 3, y = 1 + int(mask / 4) * 3;
				for (unsigned k = 0; k < 4; ++k)
					map.setVertexTerrain(x + (k & 1), y + (k >> 1), mask & (1u << k) ? type : GRASS);
			}
			for (int y = 14; y < 23; ++y)
				for (int x = 1; x < 15; ++x)
					if (y == 14 || (y >= 16 && !(x >= 6 && x <= 8 && y >= 18 && y <= 20)))
						map.setVertexTerrain(panel * 16 + x, y, type);
		}
		// A connected patch crossing both toroidal seams uses the same masks.
		for (int y : {31, 0})
			for (int x : {31, 0}) map.setVertexTerrain(x, y, HEDGE);
		for (int y = 26; y <= 29; ++y)
			for (int x = 4; x <= 27; ++x) map.setVertexTerrain(x, y, x < 16 ? HEDGE : THICKET);

		for (int panel = 0; panel < 2; ++panel)
		{
			map.setResourceByIndex(panel * 16 + 7, 19, WOOD, 0);
			for (int x : {5, 7, 9}) map.setResourceByIndex(panel * 16 + x, 24, WOOD, 0);
		}

		SceneMap scene;
		glob2test::observeMap(map, scene);
		auto &compositor = globals->terrainCompositor();
		const auto &catalog = compositor.catalog();
		for (int panel = 0; panel < 2; ++panel)
			for (unsigned mask = 0; mask < 16; ++mask)
			{
				const int x = panel * 16 + 1 + int(mask % 4) * 3, y = 1 + int(mask / 4) * 3;
				const auto id = catalog.find(panel ? "thicket" : "hedge");
				CHECK(onlyDecorFrame(compositor, scene, x, y) ==
					  (mask == 15 ? catalog.decorFrame(id, x, y, false, scene.terrainSeed(), mask,
													   TerrainVisual::cornerColours(
														   x, y, 31, 31, scene.terrainSeed()))
								  : -1));
			}
		for (const auto [x, y] : {std::pair{31, 31}})
			CHECK(onlyDecorFrame(compositor, scene, x, y) ==
				  catalog.decorFrame(
					  catalog.find("hedge"), x, y, false, scene.terrainSeed(), 15,
					  TerrainVisual::cornerColours(x, y, 31, 31, scene.terrainSeed())));
		const int border = onlyDecorFrame(compositor, scene, 15, 27);
		const auto colours = TerrainVisual::cornerColours(15, 27, 31, 31, scene.terrainSeed());
		CHECK((border == catalog.decorFrame(catalog.find("hedge"), 15, 27, true,
											scene.terrainSeed(), 15, colours) ||
			   border == catalog.decorFrame(catalog.find("thicket"), 15, 27, true,
											scene.terrainSeed(), 15, colours)));

		MapDrawer::drawMapTerrain(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(),
			(glob2test::artifactDir() / "connected-foliage-ground.png").string().c_str()));

		MapDrawer::drawMapResources(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		GAGCore::Sprite::flushBatches(globals->gfx);
		REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(),
			(glob2test::artifactDir() / "connected-foliage.png").string().c_str()));
	}

	GLOB2_TEST_CASE("fertile glints follow the presentation clock while the simulation tick stands still",
					"[display][isolated][writes-preferences]")
	{
		// The editor never steps the simulation, so a glint keyed to the scene
		// tick would freeze there. Overlays take the clock that animates lava
		// and water instead, through both resource drawing paths.
		glob2test::HeadlessGlobals globals(
			{.display = true,
			 .width = 512,
			 .height = 512,
			 .seed = 7331,
			 .beforeLoad = [](GlobalContainer &container)
			 { container.settings.rendererBackend = RendererBackend::Software; }});
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4, .discovered = true, .seed = 7331});
		auto &map = fixture.game.map;
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
				map.paintCell(x, y, LOAM);
		SceneMap scene;
		glob2test::observeMap(map, scene);
		REQUIRE(scene.tick == 0);
		auto &compositor = globals->terrainCompositor();
		const auto &catalog = compositor.catalog();
		const auto id = catalog.find("loam");
		const auto &overlay = catalog.materials[id].overlay;
		REQUIRE(!overlay.cycles.empty());
		const int period = overlay.ticks * int(overlay.cycles.front().size());
		// With the tick fixed, the frame still moves with the presentation time
		// and matches the catalog's choice for that time.
		unsigned moving = 0;
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
			{
				std::set<int> frames;
				for (int time = 0; time < period; time += overlay.ticks)
				{
					const int frame = compositor.overlayFrame(scene, x, y, time);
					CHECK(frame == catalog.overlayFrame(id, x, y, std::uint32_t(time), scene.terrainSeed()));
					frames.insert(frame);
				}
				moving += frames.size() > 1;
			}
		CHECK(moving > 0);
		// The cached rows refresh their glints when only the presentation clock
		// advances, and keep them when it stands still. Only the legacy GL
		// context owns the geometry batch the row cache draws through.
		auto &gfx = *globals->gfx;
		const auto availability = gfx.rendererAvailability();
		if (!std::any_of(availability.begin(), availability.end(), [](const auto &choice)
						 { return choice.available && choice.backend == RendererBackend::LegacyGL; }))
		{
			MESSAGE("Row cache coverage omitted: legacy GL unavailable");
			return;
		}
		REQUIRE(gfx.requestBackendSwitch(RendererBackend::LegacyGL));
		gfx.nextFrame();
		INFO(gfx.backendSwitchStatus().reason);
		REQUIRE(gfx.backendSwitchStatus().phase == RendererSwitchPhase::Complete);
		REQUIRE(gfx.backendSwitchStatus().active == RendererBackend::LegacyGL);
		MapRenderState render;
		const auto draw = [&]
		{
			// Rows are cached only inside a frame's draw batch, as MapDrawer::drawMap opens one.
			GAGCore::FrameDrawBatch frameBatch(&gfx);
			REQUIRE(gfx.getRenderBatch());
			gfx.setClipRect();
			gfx.beginMapTransform(1, 0, 0, 0, 0, 512, 512);
			MapDrawer::drawMapTerrain(0, 0, 15, 15, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene, render.animationTime);
			MapDrawer::drawMapResources(0, 0, 15, 15, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene, &render);
			GAGCore::Sprite::flushBatches(&gfx);
			gfx.endMapTransform();
			if (gfx.renderer)
				gfx.renderer->flush();
			gfx.nextFrame();
		};
		// The cached rows also need the resource sprite and the shared decor
		// and overlay sprites.
		REQUIRE(globalContainer->resources);
		REQUIRE(compositor.decorSprite());
		REQUIRE(compositor.overlaySprite());
		draw();
		REQUIRE(render.resourceRows);
		const auto revisions = [&]
		{
			std::vector<std::uint64_t> out;
			for (const auto &row : render.resourceRows->rows)
				out.push_back(row.overlay.revision);
			return out;
		};
		const auto frames = [&]
		{
			std::vector<int> out;
			for (const auto &row : render.resourceRows->rows)
				out.insert(out.end(), row.overlay.frames.begin(), row.overlay.frames.end());
			return out;
		};
		const auto before = revisions();
		const auto framesBefore = frames();
		bool animated = false;
		for (const auto &row : render.resourceRows->rows)
			animated |= !row.overlay.animated.empty();
		REQUIRE(animated);
		draw();
		CHECK(revisions() == before);
		CHECK(frames() == framesBefore);
		render.animationTime += overlay.ticks;
		draw();
		CHECK(revisions() != before);
		CHECK(frames() != framesBefore);
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
				CHECK(render.resourceRows->rows[y].overlay.frames[x] ==
					  compositor.overlayFrame(scene, x, y, render.animationTime));
	}

	GLOB2_TEST_CASE("cached decor rows adopt HD frames that arrive after they were recorded",
					"[display][isolated][writes-preferences]")
	{
		// Decor HD frames stream only above 32 pixels a cell, so the rows of a
		// view seen at ordinary zoom are recorded with native frames. Zooming in
		// streams the HD frames without changing any row's frame indices; the
		// cached rows must still draw them, as the uncached path does.
		glob2test::HeadlessGlobals globals(
			{.display = true, .width = 512, .height = 512,
			 .screenFlags = Uint32(GAGCore::GraphicContext::USEGPU),
			 .beforeLoad = [](GlobalContainer &g) { g.settings.rendererBackend = GAGCore::RendererBackend::LegacyGL; }});
		auto &gfx = *globals->gfx;
		if (!gfx.isLegacyGLScreen())
		{
			MESSAGE("Row cache coverage omitted: legacy GL unavailable");
			return;
		}
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4, .discovered = true});
		auto &map = fixture.game.map;
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
				map.setVertexTerrain(x, y, (x / 4 + y / 4) % 2 ? RIDGE_ROCK : OUTCROP);
		SceneMap scene;
		glob2test::observeMap(map, scene);
		GAGCore::Sprite::setHighResolution(true);
		struct Restore { ~Restore() { GAGCore::Sprite::setHighResolution(false); } } restore;
		auto &compositor = globals->terrainCompositor();
		auto *decor = compositor.decorSprite();
		REQUIRE(decor);
		MapRenderState render;
		const auto draw = [&](bool whole, float zoom)
		{
			{
				GAGCore::FrameDrawBatch frameBatch(&gfx);
				REQUIRE(gfx.getRenderBatch());
				gfx.setClipRect();
				gfx.drawFilledRect(0, 0, 512, 512, 17, 29, 41);
				gfx.beginMapTransform(zoom, 0, 0, 0, 0, 512, 512);
				MapDrawer::drawMapResources(0, 0, 15, 15, 0, 0, 0, whole ? Game::DRAW_WHOLE_MAP : 0, scene,
											whole ? &render : nullptr);
				GAGCore::Sprite::flushBatches(&gfx);
				gfx.endMapTransform();
			}
			auto pixels = terrainPixels(true);
			gfx.nextFrame();
			return pixels;
		};
		const auto decorRevisions = [&]
		{
			std::vector<std::uint64_t> out;
			for (const auto &row : render.resourceRows->rows)
				for (const auto &layer : row.decor) out.push_back(layer.revision);
			return out;
		};
		// Native frames only: record every row, and warm them.
		compositor.declareVisible(0, scene, 0, 0, 15, 15, false);
		const auto nativeRevision = decor->highResolutionRevision();
		const auto ordinaryNative = draw(false, 1);
		for (int warm = 0; warm < 4; ++warm) draw(true, 1);
		CHECK(draw(true, 1) == ordinaryNative);
		REQUIRE(render.resourceRows);
		unsigned visibleDecor = 0;
		for (const auto &row : render.resourceRows->rows)
			for (const auto &layer : row.decor) visibleDecor += layer.visibleFrames;
		REQUIRE(visibleDecor > 0);

		// Zoomed in, the decor's HD frames stream and are published. Nothing has
		// drawn them yet, so their textures upload during the rows' captures.
		compositor.declareVisible(0, scene, 0, 0, 15, 15, true);
		GAGCore::Sprite::finishHighResolution();
		REQUIRE(decor->highResolutionRevision() != nativeRevision);
		// The row frames did not change, only the artwork they resolve to: rows
		// keep their revisions and re-record within the per-frame geometry
		// budget, replaying the previous resolution until then and never
		// dropping to a direct draw. A slow host may need many frames.
		auto &cache = gfx.getRenderBatch()->geometryCache();
		const auto recorded = decorRevisions();
		const auto before = cache.stats();
		bool settled = false;
		for (int frame = 0; frame < 256 && !settled; ++frame)
		{
			const auto start = cache.stats();
			draw(true, 4);
			const auto end = cache.stats();
			settled = end.builds == start.builds && end.staleArtwork == start.staleArtwork;
		}
		REQUIRE(settled);
		const auto after = cache.stats();
		CHECK(after.deferred == before.deferred);
		CHECK(after.staleArtwork > before.staleArtwork);
		CHECK(after.builds > before.builds);
		CHECK(decorRevisions() == recorded);
		const auto ordinaryHd = draw(false, 4);
		CHECK(draw(true, 4) == ordinaryHd);
		CHECK(cache.stats().builds == after.builds);
	}

	TEST_CASE("raised cliffs corners strips a hole and their walls [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(
			{.display = true, .width = 1024, .height = 1024, .seed = 7331});
		globals->terrainCompositor().prepare(false, 0);
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true,
			.loadDefaultRace = true, .header = true, .seed = 7331});
		auto &map = fixture.game.map;
		// Every corner mask on its own, as in the foliage panels.
		for (unsigned mask = 0; mask < 16; ++mask)
		{
			const int x = 1 + int(mask % 4) * 3, y = 1 + int(mask / 4) * 3;
			for (unsigned k = 0; k < 4; ++k)
				map.setVertexTerrain(x + (k & 1), y + (k >> 1), mask & (1u << k) ? CLIFFS : GRASS);
		}
		// A plateau with a hole, a thin strip and a spur, so south, east and west
		// faces, inner corners and single-cell widths all show.
		for (int y = 2; y <= 12; ++y)
			for (int x = 17; x <= 30; ++x)
				if (!(x >= 21 && x <= 24 && y >= 6 && y <= 9))
					map.setVertexTerrain(x, y, CLIFFS);
		for (int x = 2; x <= 13; ++x)
			map.setVertexTerrain(x, 16, CLIFFS);
		for (int y = 15; y <= 22; ++y)
			map.setVertexTerrain(20, y, CLIFFS);
		// Cliffs against hedge: each keeps its own edge, so the cliff shows its wall.
		for (int y = 26; y <= 29; ++y)
			for (int x = 4; x <= 27; ++x)
				map.setVertexTerrain(x, y, x < 16 ? CLIFFS : HEDGE);

		SceneMap scene;
		glob2test::observeMap(map, scene);
		auto &compositor = globals->terrainCompositor();
		const auto &catalog = compositor.catalog();
		const auto id = catalog.find("cliffs");
		REQUIRE(!catalog.materials[id].decor.connected.empty());
		// Cliffs draw their edges on open ground: every corner mask, alone on the
		// grass, shows its own frame (foliage would draw nothing there).
		for (unsigned mask = 0; mask < 16; ++mask)
		{
			const int x = 1 + int(mask % 4) * 3, y = 1 + int(mask / 4) * 3;
			CAPTURE(mask);
			CHECK(onlyDecorFrame(compositor, scene, x, y) ==
				  (mask ? catalog.decorFrame(id, x, y, mask != 15, scene.terrainSeed(), mask,
											 TerrainVisual::cornerColours(x, y, 31, 31, scene.terrainSeed()))
						: -1));
		}
		// Where cliffs meet hedge, each draws its own corners in its own slot,
		// the cliffs standing back from the hedge across a valley.
		{
			const auto layers = compositor.decorLayers(scene, 15, 27);
			const auto colours = TerrainVisual::cornerColours(15, 27, 31, 31, scene.terrainSeed());
			CHECK(layers[0].frame == catalog.decorForeignFrame(id, 5, 10, colours));
			CHECK(layers[0].frame >= 0);
			CHECK(layers[1].frame == -1);
			CHECK(layers[2].frame == catalog.decorFrame(catalog.find("hedge"), 15, 27, true,
														scene.terrainSeed(), 10, colours));
			CHECK(layers[3].frame == -1);
		}
		// Where cliffs, hedge and grass meet, the cliffs still draw their edge
		// on the open ground but the hedge, beside grass, draws nothing.
		map.setVertexTerrain(15, 25, GRASS);
		SceneMap mixed;
		glob2test::observeMap(map, mixed);
		const auto mixedLayers = compositor.decorLayers(mixed, 15, 25);
		CHECK(mixedLayers[0].frame >= 0);
		CHECK(mixedLayers[2].frame == -1);
		// The plateau's frames come from the cliffs set: 20 native pixels of raised
		// top above the cell and 16 of shadow below it, 68 in all.
		const int inside = onlyDecorFrame(compositor, scene, 18, 3);
		REQUIRE(inside >= 0);
		CHECK(compositor.decorLayers(scene, 18, 3)[0].sprite->getH(inside) == 68);

		MapDrawer::drawMapTerrain(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		MapDrawer::drawMapResources(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		GAGCore::Sprite::flushBatches(globals->gfx);
		REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(),
			(glob2test::artifactDir() / "raised-cliffs.png").string().c_str()));
	}

	// Tall decor rises into the cells above its own: a unit standing behind it
	// (in the row above) is hidden where the decor covers it, while the ground
	// pass still draws the decor under the units in its own row and in front.
	TEST_CASE("tall decor hides the ground units behind it [display][artifacts]")
	{
		constexpr int Size = 512;
		glob2test::HeadlessGlobals globals({.display = true, .width = Size, .height = Size, .seed = 7331});
		globals->terrainCompositor().prepare(false, 0);
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4, .discovered = true,
			.loadDefaultRace = true, .header = true, .seed = 7331});
		auto &map = fixture.game.map;
		for (int y = 6; y <= 11; ++y)
			for (int x = 3; x <= 12; ++x)
				map.setVertexTerrain(x, y, CLIFFS);
		SceneMap scene;
		glob2test::observeMap(map, scene);
		auto &compositor = globals->terrainCompositor();
		// A cliff cell whose decor rises above it, with open grass above to stand on.
		int cx = -1, cy = -1, rise = 0;
		for (int y = 1; y < 15 && cx < 0; ++y)
			for (int x = 1; x < 15 && cx < 0; ++x)
			{
				const auto layers = compositor.decorLayers(scene, x, y);
				for (unsigned slot = 0; slot < layers.size(); ++slot)
				{
					const auto &layer = layers[slot];
					if (!layer.sprite || layer.frame < 0)
						continue;
					const int up = TerrainVisual::decorOffsetY(layer.sprite->getH(layer.frame)) +
								   TerrainVisual::Compositor::decorSlotRise(slot);
					// Along the middle of the plateau's top edge, solid across the cell.
					if (up >= 16 && x >= 5 && x <= 10 && map.isGrass(x, y - 1) &&
						map.isFreeForGroundUnit(x, y - 1, false, 1))
					{
						cx = x, cy = y, rise = up;
						break;
					}
				}
			}
		REQUIRE(cx >= 0);
		ViewState view;
		const auto draw = [&]
		{
			auto *gfx = globals->gfx;
			gfx->setClipRect();
			gfx->drawFilledRect(0, 0, Size, Size, 0, 0, 0);
			gfx->beginMapTransform(1.f, 0, 0, 0, 0, Size, Size);
			glob2test::drawMap(fixture.game, 0, 0, Size, Size, 0, 0, 0, 0, 0, view,
							   Uint32(Game::DRAW_NO_CLOUD_LAYER | Game::DRAW_WHOLE_MAP), nullptr, nullptr, true);
			gfx->endMapTransform();
			GAGCore::Sprite::flushBatches(gfx);
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
				SDL_ConvertSurface(gfx->getSDLSurface(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
			REQUIRE(pixels);
			gfx->nextFrame();
			return pixels;
		};
		const auto without = draw();
		Unit *unit = fixture.game.addUnit(cx, cy - 1, 0, WORKER, 0, 0, 0, 0);
		REQUIRE(unit);
		const auto with = draw();
		REQUIRE(IMG_SavePNG(with.get(), (glob2test::artifactDir() / "decor-hides-unit.png").string().c_str()));
		// Pixels the unit changes in the cell above the decor: inside the decor's
		// rise (the band just above its cell) and in the rest of the cell.
		// The window's surface may hold more pixels than its logical size (HiDPI).
		REQUIRE(with->w * Size == with->h * Size);
		const int density = std::max(1, with->w / Size);
		const auto at = [&](const SDL_Surface *surface, int x, int y)
		{ return static_cast<const Uint8 *>(surface->pixels) + y * density * surface->pitch + x * density * 4; };
		const auto differs = [&](int x, int y)
		{
			const auto *a = at(without.get(), x, y);
			const auto *b = at(with.get(), x, y);
			return std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]) > 24;
		};
		// Where the frame without the unit shows the decor's rise (cliff, not the
		// grass around it), the unit must not show.
		const auto decor = [&](int x, int y)
		{
			const auto *a = at(without.get(), x, y);
			return a[0] + 8 >= a[1];
		};
		int risen = 0, behind = 0, above = 0;
		for (int y = (cy - 1) * 32; y < cy * 32; ++y)
			for (int x = cx * 32; x < cx * 32 + 32; ++x)
			{
				const bool covered = y >= cy * 32 - rise && decor(x, y);
				behind += covered;
				if (differs(x, y))
					++(covered ? risen : above);
			}
		CAPTURE(behind);
		CAPTURE(cx);
		CAPTURE(cy);
		CAPTURE(rise);
		CAPTURE(risen);
		CAPTURE(above);
		REQUIRE(behind > 0); // The decor rises into the cell the unit stands in,
		CHECK(above > 0);    // the unit shows where it does not,
		// and is hidden where it does, but for a few pixels of the decor's soft edge.
		CHECK(risen * 20 <= behind);
	}

	// Decor at its cell's south edge stands in front of what stands in the
	// cell's open part: a unit beside a glacier's jutting corner, in the corner's
	// own cell, is hidden by the corner, not drawn over it.
	TEST_CASE("decor at a cell's south edge hides the unit standing in the cell [display][artifacts]")
	{
		constexpr int Size = 512;
		glob2test::HeadlessGlobals globals({.display = true, .width = Size, .height = Size, .seed = 7331});
		globals->terrainCompositor().prepare(false, 0);
		glob2test::HeadlessGame fixture({.wDec = 4, .hDec = 4, .discovered = true,
			.loadDefaultRace = true, .header = true, .seed = 7331});
		auto &map = fixture.game.map;
		// A glacier with a step: its corner at vertex (6, 8) juts into cell (5, 7).
		for (int y = 1; y <= 15; ++y)
			for (int x = (y < 8 ? 9 : 6); x <= 14; ++x)
				map.setVertexTerrain(x, y, GLACIER);
		constexpr int cx = 5, cy = 7;
		REQUIRE(map.isFreeForGroundUnit(cx, cy, false, 1));
		ViewState view;
		const auto draw = [&]
		{
			auto *gfx = globals->gfx;
			gfx->setClipRect();
			gfx->drawFilledRect(0, 0, Size, Size, 0, 0, 0);
			gfx->beginMapTransform(1.f, 0, 0, 0, 0, Size, Size);
			glob2test::drawMap(fixture.game, 0, 0, Size, Size, 0, 0, 0, 0, 0, view,
							   Uint32(Game::DRAW_NO_CLOUD_LAYER | Game::DRAW_WHOLE_MAP), nullptr, nullptr, true);
			gfx->endMapTransform();
			GAGCore::Sprite::flushBatches(gfx);
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
				SDL_ConvertSurface(gfx->getSDLSurface(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
			REQUIRE(pixels);
			gfx->nextFrame();
			return pixels;
		};
		const auto without = draw();
		REQUIRE(fixture.game.addUnit(cx, cy, 0, WARRIOR, 3, 0, 0, 0));
		const auto with = draw();
		REQUIRE(IMG_SavePNG(with.get(), (glob2test::artifactDir() / "corner-hides-unit.png").string().c_str()));
		REQUIRE(with->w * Size == with->h * Size);
		const int density = std::max(1, with->w / Size);
		const auto at = [&](const SDL_Surface *surface, int x, int y)
		{ return static_cast<const Uint8 *>(surface->pixels) + y * density * surface->pitch + x * density * 4; };
		int iced = 0, shown = 0, open = 0;
		for (int y = cy * 32 - 16; y < cy * 32 + 32; ++y)
			for (int x = cx * 32 - 16; x < cx * 32 + 48; ++x)
			{
				const auto *a = at(without.get(), x, y), *b = at(with.get(), x, y);
				const bool ice = a[2] > a[0] + 16 && a[2] > a[1]; // Bluish: the glacier, not grass.
				const bool differs = std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]) > 24;
				iced += ice;
				if (differs)
					++(ice ? shown : open);
			}
		CAPTURE(iced);
		CAPTURE(shown);
		CAPTURE(open);
		REQUIRE(iced > 0);
		CHECK(open > 0);          // The unit shows over the grass of its cell,
		CHECK(shown * 20 <= iced); // not over the glacier's corner in front of it.
	}

	TEST_CASE("cliffs beside foliage and crags [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(
			{.display = true, .width = 1024, .height = 1024, .seed = 7331});
		globals->terrainCompositor().prepare(false, 0);
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true,
			.loadDefaultRace = true, .header = true, .seed = 7331});
		auto &map = fixture.game.map;
		paintCliffPanels(map);
		SceneMap scene;
		glob2test::observeMap(map, scene);
		auto &compositor = globals->terrainCompositor();
		const auto &catalog = compositor.catalog();
		const char *keys[4] = {"thicket", "hedge", "ridge_rock", "outcrop"};
		for (int panel = 0; panel < 4; ++panel)
		{
			// The cell straddling the border's first row: cliffs on its left
			// corners, the neighbour on its right ones. Foliage draws its own
			// edge there; crags keep clear of the cliffs (clear_of_masses).
			const int x = (panel & 1) * 16 + 5, y = (panel >> 1) * 16 + 2;
			const auto layers = compositor.decorLayers(scene, x, y);
			const auto colours = TerrainVisual::cornerColours(x, y, 31, 31, scene.terrainSeed());
			const auto neighbour = catalog.find(keys[panel]);
			CAPTURE(keys[panel]);
			// Against foliage the cliffs stand back across a valley; against
			// crags they draw their plain edge.
			const auto cliffs = catalog.find("cliffs");
			CHECK(layers[0].frame == (panel < 2 ? catalog.decorForeignFrame(cliffs, 5, 10, colours) :
				  catalog.decorFrame(cliffs, x, y, true, scene.terrainSeed(), 5, colours)));
			CHECK(layers[2].frame == (panel < 2 ?
				  catalog.decorFrame(neighbour, x, y, true, scene.terrainSeed(), 10, colours) : -1));
			CHECK(layers[3].frame == -1);
			// Away from the cliffs, beyond the spur and the pocket, the crags draw.
			const int fx = (panel & 1) * 16 + 12, fy = (panel >> 1) * 16 + 11;
			if (panel >= 2)
				CHECK(onlyDecorFrame(compositor, scene, fx, fy) ==
					  catalog.decorFrame(neighbour, fx, fy, false, scene.terrainSeed(), 15,
										 TerrainVisual::cornerColours(fx, fy, 31, 31, scene.terrainSeed())));
		}
		MapDrawer::drawMapTerrain(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		MapDrawer::drawMapResources(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		GAGCore::Sprite::flushBatches(globals->gfx);
		REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(),
			(glob2test::artifactDir() / "cliffs-beside-obstacles.png").string().c_str()));
	}

	TEST_CASE("spires and pillars keep clear of mass rims [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(
			{.display = true, .width = 1024, .height = 1024, .seed = 7331});
		globals->terrainCompositor().prepare(false, 0);
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true,
			.loadDefaultRace = true, .header = true, .seed = 7331});
		auto &map = fixture.game.map;
		// Outcrop in the north half, stone pillars in the south, crossed by a
		// cliff band three vertices wide (x 10..12), and a glacier column at
		// x 31 and row at y 31, which the torus puts beside x 0 and y 0.
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				map.setVertexTerrain(x, y, x >= 10 && x <= 12 ? CLIFFS : x == 31 || y == 31 ? GLACIER :
									 y < 16 ? OUTCROP : BOULDERS);
		SceneMap scene;
		glob2test::observeMap(map, scene);
		auto &compositor = globals->terrainCompositor();
		const auto &catalog = compositor.catalog();
		const auto colours = [&](int x, int y) { return TerrainVisual::cornerColours(x, y, 31, 31, scene.terrainSeed()); };
		for (const auto [y, key] : {std::pair{6, "outcrop"}, std::pair{24, "boulders"}})
		{
			CAPTURE(key);
			const auto id = catalog.find(key);
			REQUIRE(catalog.materials[id].decor.clearOfMasses == 0);
			const auto drawn = [&](int x)
			{ return catalog.decorFrame(id, x, y, false, scene.terrainSeed(), 15, colours(x, y)); };
			// Cell x spans vertices x..x+1; only a mass on its own corners hides it, so
			// spires run right up to a cliff's foot (x 8 and 13) and to the glacier
			// across the wrap (x 0 and 29).
			for (const int x : {0, 1, 3, 7, 8, 13, 14, 28, 29})
			{
				CAPTURE(x);
				CHECK(drawn(x) >= 0);
				CHECK(onlyDecorFrame(compositor, scene, x, y) == drawn(x));
			}
			// Cells with mass corners still draw the mass's rim, alone.
			for (const int x : {9, 12, 30})
			{
				CAPTURE(x);
				const auto layers = compositor.decorLayers(scene, x, y);
				CHECK(layers[0].frame >= 0);
				CHECK(layers[1].frame == -1);
				CHECK(layers[3].frame == -1);
			}
		}
		// Across the vertical wrap: row 30 touches the glacier row and draws only its
		// rim; rows 0 and 29 stop just short of it and keep their outcrop.
		const auto outcrop = catalog.find("outcrop");
		const auto boulders = catalog.find("boulders");
		CHECK(onlyDecorFrame(compositor, scene, 3, 0) ==
			  catalog.decorFrame(outcrop, 3, 0, false, scene.terrainSeed(), 15, colours(3, 0)));
		CHECK(onlyDecorFrame(compositor, scene, 3, 29) ==
			  catalog.decorFrame(boulders, 3, 29, false, scene.terrainSeed(), 15, colours(3, 29)));
		CHECK(compositor.decorLayers(scene, 3, 30)[1].frame == -1);

		MapDrawer::drawMapTerrain(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		MapDrawer::drawMapResources(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		GAGCore::Sprite::flushBatches(globals->gfx);
		REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(),
			(glob2test::artifactDir() / "obstacles-clear-of-masses.png").string().c_str()));
	}

	TEST_CASE("icefall and other masses stand apart across valleys [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(
			{.display = true, .width = 1024, .height = 1024, .seed = 7331});
		globals->terrainCompositor().prepare(false, 0);
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true,
			.loadDefaultRace = true, .header = true, .seed = 7331});
		auto &map = fixture.game.map;
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				map.setVertexTerrain(x, y, DEEP_SNOW);
		// An icefall with a hole, a strip and a lone vertex on the snow.
		for (int y = 2; y <= 12; ++y)
			for (int x = 2; x <= 15; ++x)
				if (!(x >= 7 && x <= 9 && y >= 6 && y <= 8))
					map.setVertexTerrain(x, y, ICEFALL);
		for (int x = 2; x <= 9; ++x)
			map.setVertexTerrain(x, 15, ICEFALL);
		map.setVertexTerrain(13, 15, ICEFALL);
		// Icefall against cliffs and against glacier along straight borders, and
		// against hedge along a ragged one.
		for (int y = 2; y <= 12; ++y)
			for (int x = 18; x <= 29; ++x)
				map.setVertexTerrain(x, y, x < 23 ? ICEFALL : y < 7 ? CLIFFS : GLACIER);
		for (int y = 17; y <= 27; ++y)
			for (int x = 4; x <= 27; ++x)
				map.setVertexTerrain(x, y, x < 15 + (y % 2) ? ICEFALL : HEDGE);
		// Three masses and a hedge meeting at one cell, then four masses.
		map.setVertexTerrain(25, 29, ICEFALL);
		map.setVertexTerrain(26, 29, CLIFFS);
		map.setVertexTerrain(25, 30, HEDGE);
		map.setVertexTerrain(26, 30, GLACIER);
		map.setVertexTerrain(29, 29, ICEFALL);
		map.setVertexTerrain(30, 29, CLIFFS);
		map.setVertexTerrain(29, 30, GLACIER);
		map.setVertexTerrain(30, 30, OBSIDIAN_RIDGE);

		SceneMap scene;
		glob2test::observeMap(map, scene);
		auto &compositor = globals->terrainCompositor();
		const auto &catalog = compositor.catalog();
		const auto icefall = catalog.find("icefall"), cliffs = catalog.find("cliffs");
		REQUIRE(!catalog.materials[icefall].decor.connected.empty());
		CHECK(catalog.materials[icefall].decor.connectedOpen);
		CHECK(catalog.materials[icefall].decor.connectedTiers);
		const auto colours = [&](int x, int y) { return TerrainVisual::cornerColours(x, y, 31, 31, scene.terrainSeed()); };
		// A plain icefall cell draws one mass frame.
		CHECK(onlyDecorFrame(compositor, scene, 4, 4) ==
			  catalog.decorFrame(icefall, 4, 4, false, scene.terrainSeed(), 15, colours(4, 4)));
		// Where icefall meets cliffs, each draws its own corners and stands back
		// from the other: cell (22, 3) has icefall on its west corners. Both hold
		// one north corner and two in all, so the lower material id draws first.
		const auto layers = compositor.decorLayers(scene, 22, 3);
		const int ice = catalog.decorForeignFrame(icefall, 0b0101, 0b1010, colours(22, 3));
		const int rock = catalog.decorForeignFrame(cliffs, 0b1010, 0b0101, colours(22, 3));
		CHECK(ice >= 0);
		CHECK(rock >= 0);
		CHECK(std::minmax(layers[0].frame, layers[1].frame) == std::minmax(ice, rock));
		// Where three or four masses meet, every one draws, or a missing one
		// would end in a square notch along the neighbouring cells' edges. Each
		// stands back from every other corner, all masses or foliage here. Masses
		// rank by more north corners, then more corners, then lower material id,
		// and take slots 0 onward in that order.
		const auto glacier = catalog.find("glacier"), obsidian = catalog.find("obsidian_ridge");
		const auto apart = [&](auto id, unsigned own, int x, int y) {
			const int frame = catalog.decorForeignFrame(id, own, 15u ^ own, colours(x, y));
			CHECK(frame >= 0);
			return frame;
		};
		// Cell (22, 6): icefall on the west corners, cliffs north-east, glacier
		// south-east.
		{
			const auto three = compositor.decorLayers(scene, 22, 6);
			CHECK(three[0].frame == apart(icefall, 0b0101, 22, 6));
			CHECK(three[1].frame == apart(cliffs, 0b0010, 22, 6));
			CHECK(three[2].frame == apart(glacier, 0b1000, 22, 6));
			CHECK(three[3].frame == -1);
		}
		// Cell (25, 29): icefall north-west, cliffs north-east, hedge south-west,
		// glacier south-east. The hedge draws after all three masses, in slot 3.
		{
			const auto mixed = compositor.decorLayers(scene, 25, 29);
			const auto own = [&](auto id) { return id == icefall ? 0b0001u : 0b0010u; };
			const auto first = std::min(icefall, cliffs), second = std::max(icefall, cliffs);
			CHECK(mixed[0].frame == apart(first, own(first), 25, 29));
			CHECK(mixed[1].frame == apart(second, own(second), 25, 29));
			CHECK(mixed[2].frame == apart(glacier, 0b1000, 25, 29));
			const int hedge = catalog.decorFrame(catalog.find("hedge"), 25, 29, true, scene.terrainSeed(), 0b0100,
				colours(25, 29));
			CHECK(hedge >= 0);
			CHECK(mixed[3].frame == hedge);
		}
		// Cell (29, 29): one mass on each corner.
		{
			const auto four = compositor.decorLayers(scene, 29, 29);
			const auto own = [&](auto id) {
				return id == icefall ? 0b0001u : id == cliffs ? 0b0010u : id == glacier ? 0b0100u : 0b1000u;
			};
			const auto expect = [&](auto id) { return apart(id, own(id), 29, 29); };
			CHECK(four[0].frame == expect(std::min(icefall, cliffs)));
			CHECK(four[1].frame == expect(std::max(icefall, cliffs)));
			CHECK(four[2].frame == expect(std::min(glacier, obsidian)));
			CHECK(four[3].frame == expect(std::max(glacier, obsidian)));
		}
		// Hedge and thicket still join each other: one foliage frame, no mass.
		map.setVertexTerrain(6, 30, HEDGE);
		map.setVertexTerrain(7, 30, HEDGE);
		map.setVertexTerrain(6, 31, THICKET);
		map.setVertexTerrain(7, 31, THICKET);
		SceneMap foliage;
		glob2test::observeMap(map, foliage);
		const auto joined = compositor.decorLayers(foliage, 6, 30);
		CHECK(joined[0].frame == -1);
		CHECK(joined[1].frame == -1);

		MapDrawer::drawMapTerrain(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		MapDrawer::drawMapResources(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		GAGCore::Sprite::flushBatches(globals->gfx);
		REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(),
			(glob2test::artifactDir() / "raised-icefall.png").string().c_str()));
	}

	TEST_CASE("large barrier masses terrace toward their middle [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(
			{.display = true, .width = 1024, .height = 1024, .seed = 7331});
		globals->terrainCompositor().prepare(false, 0);
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true,
			.loadDefaultRace = true, .header = true, .seed = 7331});
		auto &map = fixture.game.map;
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				map.setVertexTerrain(x, y, GRASS);
		// A broad granite massif, a narrow sandstone ridge, and an obsidian patch
		// touching the granite: terraces, a single tier and a valley all show.
		for (int y = 2; y <= 17; ++y)
			for (int x = 2; x <= 19; ++x)
				if ((x - 10) * (x - 10) + (y - 9) * (y - 9) * 2 < 90)
					map.setVertexTerrain(x, y, GRANITE_TORS);
		for (int y = 21; y <= 23; ++y)
			for (int x = 3; x <= 28; ++x)
				map.setVertexTerrain(x, y, SANDSTONE_BENCHES);
		for (int y = 4; y <= 15; ++y)
			for (int x = 20; x <= 29; ++x)
				map.setVertexTerrain(x, y, OBSIDIAN_RIDGE);

		SceneMap scene;
		glob2test::observeMap(map, scene);
		auto &compositor = globals->terrainCompositor();
		const auto field = compositor.tierField(scene);
		REQUIRE(field);
		using TerrainVisual::Compositor;
		// The massif's middle stands at least two terraces up; its rim and the
		// three-vertex-wide ridge stay on the ground tier.
		CHECK(compositor.decorTierFrame(scene, *field, 10, 9, 1) >= 0);
		CHECK(compositor.decorTierFrame(scene, *field, 10, 9, 2) >= 0);
		CHECK(compositor.decorTierFrame(scene, *field, 2, 9, 1) == -1);
		CHECK(compositor.decorTierFrame(scene, *field, 12, 21, 1) == -1);
		// decorLayers places them in the terrace slots.
		const auto middle = compositor.decorLayers(scene, 10, 9, field.get());
		CHECK(middle[Compositor::TierSlot].frame == compositor.decorTierFrame(scene, *field, 10, 9, 1));
		CHECK(middle[Compositor::TierSlot + 1].frame == compositor.decorTierFrame(scene, *field, 10, 9, 2));
		// Terraces nest: a cell never has a higher terrace without the lower one.
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				for (int k = 2; k <= TerrainVisual::MaxDecorTiers; ++k)
					if (compositor.decorTierFrame(scene, *field, x, y, k) >= 0)
						CHECK(compositor.decorTierFrame(scene, *field, x, y, k - 1) >= 0);
		// Foliage never terraces.
		for (int y = 26; y <= 31; ++y)
			for (int x = 0; x < 32; ++x)
				map.setVertexTerrain(x, y, HEDGE);
		SceneMap hedged;
		glob2test::observeMap(map, hedged);
		const auto hedgeField = compositor.tierField(hedged);
		REQUIRE(hedgeField);
		CHECK(compositor.decorTierFrame(hedged, *hedgeField, 10, 28, 1) == -1);

		MapDrawer::drawMapTerrain(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		MapDrawer::drawMapResources(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		GAGCore::Sprite::flushBatches(globals->gfx);
		REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(),
			(glob2test::artifactDir() / "terraced-masses.png").string().c_str()));
	}

	// Draws eight-tile views of a painted 32x32 map at 4x with HD frames on
	// programmable OpenGL, terrain and decor as the game draws them, and saves
	// each as <name>-hd.png. Skips where programmable OpenGL is unavailable.
	struct HdView
	{
		const char *name;
		int x, y;
	};
	template <class Paint>
	void captureHd(Paint paint, std::initializer_list<HdView> views)
	{
		glob2test::HeadlessGlobals globals(
			{.display = true, .width = 1024, .height = 1024,
			 .screenFlags = Uint32(GAGCore::GraphicContext::USEGPU), .seed = 7331});
		auto &gfx = *globals->gfx;
		bool programmable = false;
		for (const auto &choice : gfx.rendererAvailability())
			programmable |= choice.backend == RendererBackend::ProgrammableGL && choice.available;
		if (!programmable)
		{
			MESSAGE("No programmable OpenGL: HD capture skipped");
			return;
		}
		REQUIRE(gfx.requestBackendSwitch(RendererBackend::ProgrammableGL));
		gfx.nextFrame();
		REQUIRE(gfx.backendSwitchStatus().active == RendererBackend::ProgrammableGL);
		REQUIRE(gfx.renderer);
		GAGCore::Sprite::setHighResolution(true);
		struct Restore
		{
			~Restore() { GAGCore::Sprite::setHighResolution(false); }
		} restore;
		globals->terrainCompositor().prepare(true, 0);
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true,
			.loadDefaultRace = true, .header = true, .seed = 7331});
		paint(fixture.game.map);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		for (const auto &view : views)
		{
			MapRenderState render;
			{
				GAGCore::FrameDrawBatch frameBatch(&gfx);
				gfx.setClipRect();
				gfx.beginMapTransform(4, 0, 0, 0, 0, 1024, 1024);
				MapDrawer::drawMapTerrain(0, 0, 7, 7, view.x, view.y, 0, Game::DRAW_WHOLE_MAP, scene, 0);
				MapDrawer::drawMapResources(0, 0, 7, 7, view.x, view.y, 0, Game::DRAW_WHOLE_MAP, scene, &render);
				GAGCore::Sprite::flushBatches(&gfx);
				gfx.endMapTransform();
			}
			gfx.renderer->flush();
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> captured(gfx.renderer->capture(),
																				 SDL_DestroySurface);
			REQUIRE(captured);
			CHECK(IMG_SavePNG(captured.get(),
				(glob2test::artifactDir() / (std::string(view.name) + "-hd.png")).string().c_str()));
			gfx.nextFrame();
		}
	}

	// Switching to programmable OpenGL saves the renderer choice.
	GLOB2_TEST_CASE("cliffs beside foliage and crags in HD",
					"[display][isolated][artifacts][writes-preferences]")
	{
		// The stepped border of the thicket and rock-spire panels.
		captureHd([](auto &map) { paintCliffPanels(map); },
				  {{"cliffs-beside-thicket", 2, 1}, {"cliffs-beside-crags", 2, 17}});
	}

	GLOB2_TEST_CASE("paths along staircases and corners in HD",
					"[display][isolated][artifacts][writes-preferences]")
	{
		captureHd(
			[](auto &map)
			{
				for (int y = 0; y < 32; ++y)
					for (int x = 0; x < 32; ++x)
					{
						// A brick path three vertices wide along a 45-degree
						// staircase, and a trail plaza with a notch and a bump.
						const int d = x - y;
						if (y >= 2 && y <= 28 && d >= -12 && d < -9)
							map.setVertexTerrain(x, y, BRICK_PATH);
						if (x >= 18 && x <= 28 && y >= 4 && y <= 12 && !(x == 28 && y == 8))
							map.setVertexTerrain(x, y, TRAIL);
					}
				map.setVertexTerrain(17, 8, TRAIL);
			},
			{{"brick-path-staircase", 0, 12}, {"trail-plaza-corner", 15, 2}});
	}

	GLOB2_TEST_CASE("stone paths end on whole stones in HD",
					"[display][isolated][artifacts][writes-preferences]")
	{
		// Four stone paths, each a staircase beside a block with a notch and a bump.
		static constexpr TerrainType paths[4] = {SANDSTONE, SLATE_ASHLAR, OVERGROWN_ROAD, MOONSTONE_LOZENGE};
		captureHd(
			[](auto &map)
			{
				for (int panel = 0; panel < 4; ++panel)
				{
					const int ox = (panel & 1) * 16, oy = (panel >> 1) * 16;
					for (int y = 1; y <= 14; ++y)
						for (int x = 1; x <= 14; ++x)
						{
							const bool stairs = x - y >= -1 && x - y < 2 && x <= 7;
							const bool block = x >= 9 && x <= 14 && y >= 2 && y <= 8 && !(x == 14 && y == 5);
							if (stairs || block || (x == 8 && y == 4))
								map.setVertexTerrain(ox + x, oy + y, paths[panel]);
						}
				}
			},
			{{"sandstone", 3, 0}, {"slate-ashlar", 19, 0}, {"mossy-setts", 3, 16}, {"moonstone-lozenge", 19, 16}});
	}

	TEST_CASE("shoreline surf animation frames around an island [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(
			{.display = true, .width = 1024, .height = 768, .seed = 7331});
		auto &compositor = globals->terrainCompositor();
		compositor.prepare(false, 0);
		glob2test::HeadlessGame fixture({.wDec = 5,
										 .hDec = 5,
										 .discovered = true,
										 .loadDefaultRace = true,
										 .header = true,
										 .seed = 7331});
		auto &map = fixture.game.map;
		// A sand-rimmed grass island with an irregular coast, a deep-water
		// channel and a dark-water pool, all in open water.
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
			{
				const double dx = x - 15.5, dy = (y - 11.5) * 1.3;
				const double angle = std::atan2(dy, dx), distance = std::sqrt(dx * dx + dy * dy);
				const double coast = 9 + 1.6 * std::sin(3 * angle) + 0.9 * std::cos(5 * angle + 1);
				map.paintCell(x, y, distance < coast ? (distance < coast - 3 ? GRASS : SAND)
													 : x > 27 ? DEEP_WATER : WATER);
				if (x >= 2 && x < 5 && y >= 17 && y < 21)
					map.paintCell(x, y, DARK_WATER);
				if (x >= 5 && x < 7 && y >= 17 && y < 21)
					map.paintCell(x, y, SAND);
			}
		SceneMap scene;
		glob2test::observeMap(map, scene);
		const auto &catalog = compositor.catalog();
		const int period = catalog.materials[catalog.find("water")].surf.period;
		REQUIRE(period > 0);
		const auto directory = glob2test::artifactDir() / "surf";
		std::filesystem::create_directories(directory);
		constexpr int frames = 24;
		for (int frame = 0; frame < frames; ++frame)
		{
			MapDrawer::drawMapTerrain(0, 0, 31, 23, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene,
										frame * period / frames);
			char name[32];
			std::snprintf(name, sizeof name, "surf-%02d.png", frame);
			REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(), (directory / name).string().c_str()));
		}
	}
	TEST_CASE("shore calm before and after along a coastline [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(
			{.display = true, .width = 1024, .height = 768, .seed = 7331});
		auto &compositor = globals->terrainCompositor();
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true, .seed = 7331});
		auto &map = fixture.game.map;
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
			{
				const double coast = 11 + 2.5 * std::sin(y * 0.45) + 1.2 * std::cos(y * 1.3);
				map.paintCell(x, y, x < coast - 3 ? GRASS : x < coast ? SAND : x > 24 ? DEEP_WATER : WATER);
			}
		SceneMap scene;
		glob2test::observeMap(map, scene);
		const auto directory = glob2test::artifactDir() / "shore-calm";
		std::filesystem::create_directories(directory);
		// HD cells, as accelerated renderers draw them, over a stretch of coast.
		constexpr int scale = 4, x0 = 6, y0 = 4, w = 14, h = 9, n = 32 * scale;
		const int time = 77;
		compositor.prepare(true, time);
		unsigned calmed = 0;
		for (bool calm : {false, true})
		{
			GAGCore::DrawableSurface image(w * n, h * n);
			for (int y = 0; y < h; ++y)
				for (int x = 0; x < w; ++x)
				{
					auto recipe = compositor.describe(scene, x0 + x, y0 + y);
					calmed += calm && recipe.calmed();
					if (!calm)
						recipe.calm = {};
					compositor.compose(recipe, image.getSDLSurface(), x * n, y * n, scale);
				}
			REQUIRE(IMG_SavePNG(image.getSDLSurface(),
								(directory / (calm ? "after.png" : "before.png")).string().c_str()));
		}
		CHECK(calmed > 0);
	}
	TEST_CASE("terrain catalogue gallery renders every group beside grass, sand and water [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(
			{.display = true, .width = 1024, .height = 2048, .seed = 7331});
		globals->terrainCompositor().prepare(false, 0);
		glob2test::HeadlessGame fixture({.wDec = 5,
										 .hDec = 6,
										 .discovered = true,
										 .loadDefaultRace = true,
										 .header = true,
										 .seed = 7331});
		auto &map = fixture.game.map;
		// Ocean and a sand beach on the left, as in the mixed gallery.
		for (int y = 0; y < 64; ++y)
			for (int x = 0; x < 32; ++x)
			{
				if (x < 5)
					map.paintCell(x, y, SAND);
				if (x < 2)
					map.paintCell(x, y, WATER);
			}
		// Every catalogue type as a 3x3 island on grass with a detached diagonal cell,
		// five per row on a four-cell pitch, so interior variants, boundaries and seams
		// are all visible in the 64 rendered rows (room for 80 types).
		std::vector<TerrainType> types;
		for (unsigned i = BOULDERS; i < TERRAIN_COUNT; ++i)
			if (terrainPaintable(TerrainType(i)))
				types.push_back(TerrainType(i));
		REQUIRE(types.size() == TERRAIN_COUNT - BOULDERS);
		REQUIRE(types.size() <= 80);
		for (std::size_t n = 0; n < types.size(); ++n)
		{
			const int ox = 6 + int(n % 5) * 5, oy = int(n / 5) * 4; // rows 0..63 are rendered
			for (int dy = 0; dy < 3; ++dy)
				for (int dx = 0; dx < 3; ++dx)
					map.paintCell(ox + dx, oy + dy, types[n]);
			map.paintCell(ox + 3, oy + 3, types[n]);
		}
		// Water-side samples: deep and dark water meet open water and the beach; lava
		// and a hole sit on the beach edge.
		for (int y = 2; y < 6; ++y)
			for (int x = 2; x < 4; ++x)
				map.paintCell(x, y, DEEP_WATER);
		for (int y = 8; y < 12; ++y)
			for (int x = 2; x < 4; ++x)
				map.paintCell(x, y, DARK_WATER);
		for (int y = 14; y < 17; ++y)
			map.paintCell(4, y, LAVA);
		for (int y = 19; y < 22; ++y)
			map.paintCell(4, y, VOID_HOLE);
		SceneMap scene;
		glob2test::observeMap(map,scene);
		MapDrawer::drawMapTerrain(0, 0, 31, 63, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		// Raised obstacle decor is drawn with the resources, row by row.
		MapDrawer::drawMapResources(0, 0, 31, 63, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		GAGCore::Sprite::flushBatches(globals->gfx);
		REQUIRE(IMG_SavePNG(
			globals->gfx->getSDLSurface(),
			(glob2test::artifactDir() / "terrain-catalogue-gallery.png").string().c_str()));
		// Obstacle islands carry decor: connected foliage follows corner masks;
		// other obstacles choose full/edge clusters. Open ground has none.
		auto &compositor = globals->terrainCompositor();
		REQUIRE(compositor.decorSprite());
		const auto &catalog = compositor.catalog();
		const auto contains = [](const std::vector<int> &frames, int frame)
		{ return std::find(frames.begin(), frames.end(), frame) != frames.end(); };
		for (std::size_t n = 0; n < types.size(); ++n)
		{
			const int ox = 6 + int(n % 5) * 5, oy = int(n / 5) * 4;
			const auto &decor =
				catalog.materials[catalog.bindings.at(terrainPresentation(types[n]).name)].decor;
			INFO(terrainPresentation(types[n]).name);
			if (decor.full.empty())
			{
				CHECK(onlyDecorFrame(compositor, scene, ox + 1, oy + 1) == -1);
				continue;
			}
			// Filled connected cells may also choose an alternative interior.
			auto full = decor.connected.empty() ? decor.full : decor.connected;
			full.insert(full.end(), decor.connectedInterior.begin(), decor.connectedInterior.end());
			CHECK(contains(full, onlyDecorFrame(compositor, scene, ox + 1, oy + 1)));
			// Painted cells cover vertices ox..ox+3, so the cells just outside
			// the island have two of its corners: only open-edged decor (cliffs)
			// draws its edge there.
			for (const int x : {ox - 1, ox + 3})
			{
				const int frame = onlyDecorFrame(compositor, scene, x, oy + 1);
				if (decor.connectedOpen)
					CHECK(contains(decor.connected, frame));
				else
					CHECK(frame == -1);
			}
		}
		CHECK(onlyDecorFrame(compositor, scene, 5, 30) == -1);
		// Painted islands keep their identity.
		for (std::size_t n = 0; n < types.size(); ++n)
			CHECK(map.terrainTypeAt(6 + int(n % 5) * 5, int(n / 5) * 4) == types[n]);
	}
	TEST_CASE("catalogue materials load 4x HD frames and compose at 4x [display][artifacts]")
	{
		// HD frames load only on the GPU renderers.
		glob2test::HeadlessGlobals globals(
			{.display = true,
			 .width = 640,
			 .height = 480,
			 .screenFlags = Uint32(GAGCore::GraphicContext::USEGPU)});
		GAGCore::Sprite::setHighResolution(true);
		struct Restore
		{
			~Restore() { GAGCore::Sprite::setHighResolution(false); }
		} restore;
		auto &compositor = globals->terrainCompositor();
		compositor.prepare(true, 0);
		const auto &catalog = compositor.catalog();
		std::vector<std::string> keys;
		for (unsigned i = BOULDERS; i < TERRAIN_COUNT; ++i)
			if (terrainPaintable(TerrainType(i)))
				keys.push_back(terrainPresentation(TerrainType(i)).name);
		REQUIRE(keys.size() == TERRAIN_COUNT - BOULDERS);
		// One 4x composed tile per catalogue material, six per row.
		GAGCore::DrawableSurface sheet(6 * 128, int((keys.size() + 5) / 6) * 128);
		for (std::size_t n = 0; n < keys.size(); ++n)
		{
			INFO(keys[n]);
			const auto id = catalog.bindings.at(keys[n]);
			const auto &material = catalog.materials[id];
			auto *sprite = GAGCore::Toolkit::getSprite(material.sprite);
			REQUIRE(sprite);
			for (const auto &variant : material.variants)
			{
				auto *hd = sprite->baseFrame(variant.frame);
				REQUIRE(hd);
				CHECK(hd->getW() == 128);
				CHECK(hd->getH() == 128);
			}
			if (!material.decor.connected.empty())
			{
				auto *decor = GAGCore::Toolkit::getSprite(material.decor.sprite);
				REQUIRE(decor);
				auto frames = material.decor.connected;
				frames.insert(frames.end(), material.decor.connectedInterior.begin(),
					material.decor.connectedInterior.end());
				for (int frame : frames)
				{
					// Foliage stands one cell above its own; terraces stand taller.
					auto *hd = decor->baseFrame(frame);
					REQUIRE(hd);
					CHECK(hd->getW() == 4 * decor->getW(frame));
					CHECK(hd->getH() == 4 * decor->getH(frame));
					CHECK(hd->getH() >= 256);
				}
			}
			else if (material.key == "boulders")
			{
				// Tall boulders: 128x128 native frames, 512x512 in HD.
				auto *decor = GAGCore::Toolkit::getSprite(material.decor.sprite);
				REQUIRE(decor);
				for (const auto &frames : {material.decor.full, material.decor.edge})
					for (int frame : frames)
					{
						CHECK(decor->getW(frame) == TerrainVisual::MaximumDecorFrame);
						CHECK(decor->getH(frame) == TerrainVisual::MaximumDecorFrame);
						auto *hd = decor->baseFrame(frame);
						REQUIRE(hd);
						CHECK(hd->getW() == 512);
						CHECK(hd->getH() == 512);
					}
			}
			TerrainVisual::Recipe recipe;
			recipe.corners.fill(id);
			recipe.width = recipe.height = 1;
			compositor.compose(recipe, sheet.getSDLSurface(), int(n % 6) * 128, int(n / 6) * 128, 4);
		}
		CHECK(IMG_SavePNG(sheet.getSDLSurface(),
						  (glob2test::artifactDir() / "terrain-catalogue-hd.png").string().c_str()));
	}
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
					map.paintCell(x, y, SAND);
				if (x < 3)
					map.paintCell(x, y, WATER);
				if ((x - 17) * (x - 17) + (y - 10) * (y - 10) < 42)
					map.paintCell(x, y, ICE);
				if (y == 20 || x == 25 || (x > 9 && x < 24 && y == x - 5))
					map.paintCell(x, y, TRAIL);
			}
		map.paintCell(31, 0, ICE);
		map.paintCell(0, 0, TRAIL);
		fixture.addUnit(WORKER, 17, 10);
		fixture.addUnit(WORKER, 25, 20);
		std::ofstream trace(glob2test::artifactDir() / "checksums.txt");
		for (int t = 0; t < 256; ++t)
		{
			trace << t << ' ' << fixture.checksum() << '\n';
			fixture.step();
		}
		SceneMap scene;
		glob2test::observeMap(map,scene);
		auto *gfx = globals->gfx;
		MapDrawer::drawMapTerrain(0, 0, 31, 23, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
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
					map.paintCell(
						x, y,
						pattern ? ((x + y) % 3 == 0 ? ICE : ((x * 3 + y) % 5 == 0 ? TRAIL : GRASS))
								: GRASS);
				}
			glob2test::observeMap(map,scene);
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
