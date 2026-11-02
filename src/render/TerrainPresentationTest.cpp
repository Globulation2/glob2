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
#include "TerrainCornerPresentation.h"
#include "terrain/TerrainCompositor.h"
#include "terrain/TerrainCatalogIO.h"
#include "scene/SceneMap.h"
#include "SoftwareTerrainCache.h"
#include "OverviewTerrainCache.h"
#include <Toolkit.h>
#include "MapRenderState.h"
#include "MapThumbnail.h"
#include "MapImage.h"
#include "GenerationRequest.h"
#include "GameGUI.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <SDL3_image/SDL_image.h>
#include <RenderBackend.h>
#include <RenderDevice.h>
#include <RendererSelection.h>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <cmath>
#include <string_view>
#include <map>

namespace
{
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
		REQUIRE(globals->terrain->baseFrame(0)->getW() == 128);
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
			game.drawMapTerrain(0, 0, 19, 14, 29, 30, 0, Game::DRAW_WHOLE_MAP, scene);
			cache.prepare(scene, *globals->terrain, 0, 0, 19, 14, 29, 30, fixture.team->me, true);
		};
		drawBoth();
		GAGCore::Sprite::finishHighResolution();
		drawBoth();
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
        // Dark water's surf advances in steps; times 19 and 25 fall inside one.
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
            CHECK(gfx.renderDevice()->telemetry().resourceBytes<=256u*1024*1024);
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
                pressure.device().telemetryFloor=252u*1024*1024;
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
		// Custom saved terrain palettes still override their appearance colour.
		auto custom = compositor.describe(scene, 10, 10);
		custom.corners.fill(compositor.catalog().bindings.at("ice"));
		const std::array<unsigned char, 3> customColor{17, 31, 47};
		const TerrainVisual::Compositor::CornerColors customCorners{&customColor, &customColor,
																   &customColor, &customColor};
		GAGCore::DrawableSurface customOverview(samples, samples);
		compositor.composeOverview(custom, customOverview.getSDLSurface(), 0, 0, &customCorners);
		for (int y = 0; y < samples; ++y)
			for (int x = 0; x < samples; ++x)
				CHECK(reinterpret_cast<const Uint32 *>(
						  static_cast<const Uint8 *>(customOverview.getSDLSurface()->pixels) +
						  y * customOverview.getSDLSurface()->pitch)[x] == 0xFF111F2Fu);
		MapRenderState render;
		render.detail.terrainOverview = .5f;
		map.setResourceByIndex(9, 9, WHEAT, 1);
		glob2test::observeMap(map,scene);
		Game::drawMapOverview(0, 0, 15, 15, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene, render);
		REQUIRE(render.overview->getW() == overview.getW());
		REQUIRE(render.overview->getH() == overview.getH());
		const auto& tint = map.resourceRegistry().presentation(static_cast<ResourceId>(WHEAT)).minimap;
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
		constexpr int samples = TerrainVisual::Compositor::OverviewSamples;
		GAGCore::DrawableSurface actual(40 * samples, 24 * samples), expected(40 * samples, 24 * samples);
		OverviewTerrainCache cache;
		const auto check = [&](int vx, int vy, bool wholeMap = true)
		{
			cache.copy(scene, -3, -2, 36, 21, vx, vy, fixture.team->me, wholeMap, actual.getSDLSurface());
			for (int y = -2; y <= 21; ++y)
				for (int x = -3; x <= 36; ++x)
					compositor.composeOverview(compositor.describe(scene, x + vx, y + vy),
						expected.getSDLSurface(), (x + 3) * samples, (y + 2) * samples);
			CHECK(std::memcmp(actual.getSDLSurface()->pixels, expected.getSDLSurface()->pixels,
				actual.getSDLSurface()->pitch * actual.getH()) == 0);
		};
		check(29, 30);
		const auto cold = cache.composedCells();
		check(29, 30);
		check(-3, -2);
		CHECK(cache.composedCells() == cold);
		map.setVertexTerrain(31, 0, GRASS); // Halo edit across both page and map seams.
		glob2test::observeMap(map, scene);
		check(29, 30);
		CHECK(cache.composedCells() > cold);
		map.setTerrainSeed(197);
		glob2test::observeMap(map, scene);
		check(29, 30);
		const auto seeded = cache.composedCells();
		map.setResourceByIndex(9, 9, WHEAT, 1);
		glob2test::observeMap(map, scene);
		cache.copy(scene, -3, -2, 36, 21, 29, 30, fixture.team->me, true, actual.getSDLSurface());
		CHECK(cache.composedCells() == seeded + 1);
		CHECK(std::memcmp(actual.getSDLSurface()->pixels, expected.getSDLSurface()->pixels,
			actual.getSDLSurface()->pitch * actual.getH()) != 0);
		// Hiding the resource restores its untinted ground, and revealing it
		// composes it once again rather than accumulating the tint.
		map.unsetMapDiscovered();
		glob2test::observeMap(map, scene);
		check(29, 30, false);
		CHECK(cache.composedCells() == seeded + 2);
		cache.copy(scene, -3, -2, 36, 21, 29, 30, fixture.team->me, true, actual.getSDLSurface());
		CHECK(cache.composedCells() == seeded + 3);
		map.setNoResource(9, 9, 1);
		glob2test::observeMap(map, scene);
		check(29, 30);
		CHECK(cache.composedCells() == seeded + 4);
	}
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
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
		// Interactive pages show native art until the declared HD window streams.
		independent.prepare(scene, *globals->terrain, 0, 0, 6, 63, 0, 0, fixture.team->me, true);
		GAGCore::Sprite::finishHighResolution();
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
		glob2test::observeMap(fixture.game.map,scene);
		ScopedTerrainDeviceLimit device(*globals->gfx);
		auto &backend = device.backend();
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
			// Check the reduced pixels against independent area averages of native
			// composition, including partially transparent coast tiles.
			const auto &chunk = *cache.copies.front().chunk;
			const int divisor = SoftwareTerrainCache::ChunkPixels / chunkPixels(chunk)->w;
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
		CHECK(map.terrainPropertiesAt(7, 7).walkable);
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
			// Each vertex exports its registered colour; the classic ones are
			// already covered by the ordinary map around them.
			if (classicTerrain(type))
				continue;
			map.paintCell(int(painted.size()) + 1, 1, type);
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
			fixture.game.drawMapTerrain(0, 0, 15, 15, viewport, viewport, 0, Game::DRAW_WHOLE_MAP,
										scene);
			fixture.game.drawMapResources(0, 0, 15, 15, viewport, viewport, 0,
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
			// the row cache must adopt that deterministic selection.
			map.setVertexTerrain(5, 5, RIDGE_ROCK);
			glob2test::observeMap(map, scene);
			const auto ordinary = draw(scene, false, 1, 0);
			CHECK(draw(scene, true, 1, 0) == ordinary);
			map.setVertexTerrain(5, 5, OUTCROP);
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
			const int frame = compositor.decorFrame(scene, 4, 4);
			CHECK(compositor.decorFrame(scene, 4, 4) == frame);
			CHECK(compositor.decorFrame(scene, 20, 20) == frame); // wrapped coordinates
			CHECK((compositor.decorSprite(scene, 4, 4) != nullptr) == (frame >= 0));
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
		const int four = frameFor({OUTCROP, RIDGE_ROCK, BOULDERS, HEDGE});
		const std::array<TerrainVisual::MaterialId, 4> ids{
			definitions.find("outcrop"), definitions.find("ridge_rock"),
			definitions.find("boulders"), definitions.find("hedge")};
		bool valid = false;
		for (unsigned k = 0; k < 4; ++k)
			valid |= four == definitions.decorFrame(
								 ids[k], 4, 4, true, map.terrainSeed(), 1u << k,
								 TerrainVisual::cornerColours(4, 4, 15, 15, map.terrainSeed()));
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
						const int frame = compositor.decorFrame(scene, x, y);
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
				CHECK(compositor.decorFrame(scene, x, y) ==
					  (mask == 15 ? catalog.decorFrame(id, x, y, false, scene.terrainSeed(), mask,
													   TerrainVisual::cornerColours(
														   x, y, 31, 31, scene.terrainSeed()))
								  : -1));
			}
		for (const auto [x, y] : {std::pair{31, 31}})
			CHECK(compositor.decorFrame(scene, x, y) ==
				  catalog.decorFrame(
					  catalog.find("hedge"), x, y, false, scene.terrainSeed(), 15,
					  TerrainVisual::cornerColours(x, y, 31, 31, scene.terrainSeed())));
		const int border = compositor.decorFrame(scene, 15, 27);
		const auto colours = TerrainVisual::cornerColours(15, 27, 31, 31, scene.terrainSeed());
		CHECK((border == catalog.decorFrame(catalog.find("hedge"), 15, 27, true,
											scene.terrainSeed(), 15, colours) ||
			   border == catalog.decorFrame(catalog.find("thicket"), 15, 27, true,
											scene.terrainSeed(), 15, colours)));

		fixture.game.drawMapTerrain(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(),
			(glob2test::artifactDir() / "connected-foliage-ground.png").string().c_str()));

		fixture.game.drawMapResources(0, 0, 31, 31, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		GAGCore::Sprite::flushBatches(globals->gfx);
		REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(),
			(glob2test::artifactDir() / "connected-foliage.png").string().c_str()));
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
			fixture.game.drawMapTerrain(0, 0, 31, 23, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene,
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
			{.display = true, .width = 1024, .height = 768, .seed = 7331});
		globals->terrainCompositor().prepare(false, 0);
		glob2test::HeadlessGame fixture({.wDec = 5,
										 .hDec = 5,
										 .discovered = true,
										 .loadDefaultRace = true,
										 .header = true,
										 .seed = 7331});
		auto &map = fixture.game.map;
		// Ocean and a sand beach on the left, as in the mixed gallery.
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
			{
				if (x < 5)
					map.paintCell(x, y, SAND);
				if (x < 2)
					map.paintCell(x, y, WATER);
			}
		// Every catalogue type as a 3x3 island on grass with a detached diagonal cell,
		// five per row on a four-cell pitch, so interior variants, boundaries and seams
		// are all visible in the 24 rendered rows.
		std::vector<TerrainType> types;
		for (unsigned i = BOULDERS; i < TERRAIN_COUNT; ++i)
			if (terrainPaintable(TerrainType(i)))
				types.push_back(TerrainType(i));
		REQUIRE(types.size() == 28);
		for (std::size_t n = 0; n < types.size(); ++n)
		{
			const int ox = 6 + int(n % 5) * 5, oy = int(n / 5) * 4; // rows 0..23 are rendered
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
		fixture.game.drawMapTerrain(0, 0, 31, 23, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
		// Raised obstacle decor is drawn with the resources, row by row.
		fixture.game.drawMapResources(0, 0, 31, 23, 0, 0, 0, Game::DRAW_WHOLE_MAP, scene);
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
				CHECK(compositor.decorFrame(scene, ox + 1, oy + 1) == -1);
				continue;
			}
			// Filled connected cells may also choose an alternative interior.
			auto full = decor.connected.empty() ? decor.full : decor.connected;
			full.insert(full.end(), decor.connectedInterior.begin(), decor.connectedInterior.end());
			CHECK(contains(full, compositor.decorFrame(scene, ox + 1, oy + 1)));
			// Painted cells cover vertices ox..ox+3, so the cells just outside
			// the island have two of its corners.
			CHECK(compositor.decorFrame(scene, ox - 1, oy + 1) == -1);
			CHECK(compositor.decorFrame(scene, ox + 3, oy + 1) == -1);
		}
		CHECK(compositor.decorFrame(scene, 5, 30) == -1);
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
		REQUIRE(keys.size() == 28);
		// One 4x composed tile per catalogue material, six per row.
		GAGCore::DrawableSurface sheet(6 * 128, 5 * 128);
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
					auto *hd = decor->baseFrame(frame);
					REQUIRE(hd);
					CHECK(hd->getW() == 128);
					CHECK(hd->getH() == 192);
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
