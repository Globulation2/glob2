#include "CommandLine.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Loaded-game benchmark. Run with an isolated profile; no desktop input is generated.
#include "GlobalContainer.h"
#include "TorusPicking.h"
#include "DynamicClouds.h"
#include <any>
#define private public
#include "TorusView.h"
#undef private
#include "GameGUI.h"
#include "Engine.h"
#include "Team.h"
#include "Unit.h"
#include "Player.h"
#include "AI.h"
#include "Order.h"
#include "Utilities.h"
#include <BinaryStream.h>
#include <Toolkit.h>
#include <MapCamera.h>
#include <RenderBatch.h>
#include <MapGeometryCache.h>
#include <PerformanceTelemetry.h>
#include "TorusMapFixture.h"
#include "render/scene/SceneExtract.h"
#include "render/SoftwareTerrainCache.h"
#include "render/OverviewTerrainCache.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif

GlobalContainer *globalContainer = nullptr;
class TorusRenderBenchmark
{
    static void prepareScene(GameGUI &gui)
    {
        gui.game.snapshots().invalidateBoundary(); // Synthetic fixtures edit public records.
        const auto request = gui.sceneRequest();
        SceneExtractor().prepare(gui.game.captureReadBoundary({}, true,
            SceneExtractor::requirements(request)), request, gui.view.render.ownScene);
        gui.view.scene = &gui.view.render.ownScene;
    }

    static int population(const Game &game)
    {
        int total = 0;
        for (int team = 0; team < game.mapHeader.getNumberOfTeams(); ++team)
            for (int slot = 0; slot < Unit::MAX_COUNT; ++slot)
                if (game.teams[team]->myUnits[slot]) ++total;
        return total;
    }

    static int mappedPopulation(const Game &game)
    {
        int total = 0;
        for (int y = 0; y < game.map.getH(); ++y)
            for (int x = 0; x < game.map.getW(); ++x)
            {
                total += game.map.getGroundUnit(x, y) != NOGUID;
                total += game.map.getAirUnit(x, y) != NOGUID;
            }
        return total;
    }

    static void populateSynthetic(Game &game, int worldW, int worldH)
    {
        const char *requested = std::getenv("GLOB2_BENCH_UNITS");
        const int count = requested ? std::atoi(requested) : 1000;
        // Distinct interior cells avoid seam copies and viewport clipping.
        const int columns = std::min(worldW / 32, game.map.getW()) - 4;
        const int rows = std::min(worldH / 32, game.map.getH()) - 4;
        assert(columns > 0 && rows > 0 && count >= 0 && count <= columns * rows);
        for (int team = game.mapHeader.getNumberOfTeams(); team < Team::MAX_COUNT; ++team) game.addTeam();
        GameHeader header;
        header.setRandomSeed(1);
        header.setNumberOfPlayers(Team::MAX_COUNT);
        for (int team = 0; team < Team::MAX_COUNT; ++team)
            header.getBasePlayer(team) = BasePlayer(team, "Benchmark", team, BasePlayer::P_LOCAL);
        game.setGameHeader(header, false);
        for (int i = 0; i < count; ++i)
        {
            const int cell = int(static_cast<long long>(i) * columns * rows / std::max(1, count));
            Unit *unit = game.addUnit(2 + cell % columns, 2 + cell / columns,
                i % Team::MAX_COUNT, i % 3, 0, (i * 17) & 255, 0, 0);
            assert(unit);
        }
    }

    static void advanceAiTick(GameGUI &gui)
    {
        Game &game = gui.game;
        std::vector<unsigned> actors;
        for(int player=0;player<game.gameHeader.getNumberOfPlayers();++player)
            if(game.players[player]->ai)actors.push_back(unsigned(player));
        for(const auto& [actor,scheduled]:game.prepareAIOrders(actors,false)) {
            scheduled->sender=actor;
            gui.executeOrder(game.validateAIOrder(scheduled,actor));
        }
        game.syncStep(gui.localTeamNo);
    }

    static void warmupCheckpoint(GameGUI &gui)
    {
        Game &game = gui.game;
        const int naturalCount = population(game);
        if (const char *minimum = std::getenv("GLOB2_BENCH_MIN_UNITS"))
        {
            int count = naturalCount;
            const int target = std::atoi(minimum);
            for (int cell = 0; cell < game.map.getW() * game.map.getH() && count < target; ++cell)
                if (game.addUnit(cell % game.map.getW(), cell / game.map.getW(),
                    count % game.mapHeader.getNumberOfTeams(), count % 3, 0, 0, 0, 0)) ++count;
            assert(count >= target);
        }
        const char *requestedTicks = std::getenv("GLOB2_BENCH_AI_TICKS");
        const int ticks = requestedTicks ? std::atoi(requestedTicks) : 0;
        for (int tick = 0; tick < ticks; ++tick) advanceAiTick(gui);
        std::printf("AI checkpoint tick=%u units=%d natural_units=%d advanced_ticks=%d\n",
            game.stepCounter, population(game), naturalCount, ticks);
    }

    static int detailForZoom(const Game &game, double zoom)
    {
        return std::getenv("GLOB2_BENCH_NATIVE_CLOUD_DETAIL") ? 0 :
            DynamicClouds::gridLimitForZoom(game.map.getW(), game.map.getH(),
                globalContainer->settings.cloudPatchSize, zoom);
    }

#ifdef HAVE_OPENGL
    static void captureFramebuffer(const char *path = nullptr)
    {
        if (!path) path = std::getenv("GLOB2_BENCH_CAPTURE");
        if (!path) return;
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        std::vector<unsigned char> pixels(viewport[2] * viewport[3] * 3);
        GLint alignment;
        glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, viewport[2], viewport[3], GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        glPixelStorei(GL_PACK_ALIGNMENT, alignment);
        std::ofstream image(path, std::ios::binary);
        image << "P6\n" << viewport[2] << " " << viewport[3] << "\n255\n";
        for (int row = viewport[3] - 1; row >= 0; --row)
            image.write(reinterpret_cast<const char *>(pixels.data() + row * viewport[2] * 3), viewport[2] * 3);
        assert(image.good());
    }
#endif

public:
static int run(int argc, char **argv)
{
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    globalContainer = new GlobalContainer;
    std::vector<std::string> launchArgs{"play"};
        launchArgs.insert(launchArgs.end(),argv+1,argv+argc);
        globalContainer->applyCommand(Cli::parse(launchArgs));
    globalContainer->load();
    // Finish deferred HD publication before comparing renderer workloads.
    GAGCore::Sprite::setHighResolution(globalContainer->settings.highResolutionArtwork);
#ifdef HAVE_OPENGL
    if (!SDL_GL_GetCurrentContext()) return 1;
    if (!std::getenv("GLOB2_BENCH_VISIBLE")) SDL_HideWindow(SDL_GL_GetCurrentWindow());
    SDL_GL_SetSwapInterval(0);
    {
        GameGUI gui;
        const char *path = std::getenv("GLOB2_BENCH_MAP");
        const char *savedGame = std::getenv("GLOB2_BENCH_GAME");
        if (savedGame)
        {
            GAGCore::BinaryInputStream stream(glob2OpenMapOrSaveInputStreamBackend(*GAGCore::Toolkit::getFileManager(), savedGame));
            assert(gui.load(&stream));
            path = savedGame;
        }
        else if (std::getenv("GLOB2_BENCH_SIZE") || (std::getenv("GLOB2_BENCH_FLAT") && !path))
        {
            const char *size = std::getenv("GLOB2_BENCH_SIZE");
            if (!size) size = "256x256";
            int w = 0, h = 0;
            assert(std::sscanf(size, "%dx%d", &w, &h) == 2);
            gui.init();
            makeTorusMapFixture(gui.game, w, h);
            path = "synthetic checkerboard";
        }
        else
        {
            auto mapHeader = Engine::loadMapHeader(path ? path : "maps/Oazis.map");
            GameHeader gameHeader;
            gameHeader.setRandomSeed(1);
            for (int i = 0; i < mapHeader.getNumberOfTeams(); ++i)
                gameHeader.getBasePlayer(i) = BasePlayer(i, "Benchmark", i, BasePlayer::P_LOCAL);
            gameHeader.setNumberOfPlayers(mapHeader.getNumberOfTeams());
            assert(gui.loadFromHeaders(mapHeader, gameHeader, true, true));
        }
        gui.localPlayer = gui.localTeamNo = 0;
        gui.adjustLocalTeam();
        gui.adjustInitialViewport();
        prepareScene(gui);
        int width = globalContainer->gfx->getW() - 160, height = globalContainer->gfx->getH();
        int x = 0, y = 0;
        TorusView view;
        const int frames = std::max(1, std::getenv("GLOB2_BENCH_FRAMES") ? std::atoi(std::getenv("GLOB2_BENCH_FRAMES")) : 100);
        std::printf("GPU=%s map=%s %dx%d viewport=%dx%d frames=%d\n", glGetString(GL_RENDERER),
            path ? path : "maps/Oazis.map", gui.game.map.getW(), gui.game.map.getH(), width, height, frames);
        std::fflush(stdout);
        auto measure = [&](const char *label, auto draw)
        {
            const char *mode = std::getenv("GLOB2_BENCH_MODE");
            if (mode && std::strcmp(mode, label)) return;
            std::vector<double> times, cpuTimes;
            const int warmupFrames = std::max(0, std::getenv("GLOB2_BENCH_WARMUP_FRAMES")
                ? std::atoi(std::getenv("GLOB2_BENCH_WARMUP_FRAMES")) : 2);
            for (int i = -warmupFrames; i < frames; ++i)
            {
                glFinish();
                const auto scopesBefore = PerformanceTelemetry::collector().window;
                Uint64 start = SDL_GetPerformanceCounter();
                const auto cpuStart = std::clock();
                draw();
                glFinish();
                assert(glGetError() == GL_NO_ERROR);
                double ms = 1000.0 * (SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
                if (std::getenv("GLOB2_BENCH_FRAME_TIMES"))
                {
                    std::printf("FRAME mode=%s frame=%d elapsed_ms=%.6f\n", label, i, ms);
                    const auto &scopes = PerformanceTelemetry::collector().window;
                    for (auto id : {PerformanceTelemetry::Id::TerrainCache, PerformanceTelemetry::Id::Terrain,
                        PerformanceTelemetry::Id::Resources, PerformanceTelemetry::Id::GroundUnits})
                        std::printf("FRAME_SCOPE frame=%d scope=%u elapsed_ms=%.6f\n", i, unsigned(id),
                            (scopes[unsigned(id)].time.total - scopesBefore[unsigned(id)].time.total) / 1e6);
                    if (auto *cache = gui.view.render.existingTerrainCache())
                        std::printf("FRAME_CACHE frame=%d bytes=%zu mask_bytes=%zu rebuilds=%llu hits=%llu reductions=%llu resolution=%d downsample=%d\n", i,
                            cache->bytes(), cache->maskBytes(), static_cast<unsigned long long>(cache->cacheRebuilds()),
                            static_cast<unsigned long long>(cache->cacheHits()),
                            static_cast<unsigned long long>(cache->cacheReductions()),
                            cache->samplingResolution(), cache->samplingReduction());
                    if (gui.view.render.overviewCache)
                        std::printf("FRAME_OVERVIEW frame=%d composed_cells=%llu\n", i,
                            static_cast<unsigned long long>(gui.view.render.overviewCache->composedCells()));
                }
                if (i < 0 && i >= -warmupFrames && i < -warmupFrames + 8)
                    std::printf("WARMUP frame=%d elapsed_ms=%.3f process_cpu_ms=%.3f\n", i + warmupFrames, ms,
                        1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC);
                if (i >= 0)
                {
                    times.push_back(ms);
                    if (cpuStart != std::clock_t(-1))
                        cpuTimes.push_back(1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC);
                }
            }
            std::sort(times.begin(), times.end());
            std::printf("%s median=%.3f ms p95=%.3f ms\n", label, times[times.size()/2], times[(times.size()*95+99)/100-1]);
            std::printf("%s p99=%.3f ms max=%.3f ms\n", label,
                times[(times.size()*99+99)/100-1], times.back());
#ifndef _WIN32
            if (!cpuTimes.empty())
            {
                std::sort(cpuTimes.begin(), cpuTimes.end());
                std::printf("%s process_cpu_median=%.3f ms process_cpu_p95=%.3f ms\n",
                    label, cpuTimes[cpuTimes.size()/2], cpuTimes[(cpuTimes.size()*95+99)/100-1]);
            }
#endif
            std::fflush(stdout);
            if (std::strncmp(label, "Torus", 5) == 0) captureFramebuffer();
        };
        if (std::getenv("GLOB2_BENCH_FLAT"))
        {
            MapCamera camera;
            camera.resize(width, height, gui.game.map.getW()*32, gui.game.map.getH()*32);
            camera.zoom = camera.minimumZoom();
            // As the game's own view does, so the detail matches what a player sees.
            gui.view.render.minimumZoom = camera.minimumZoom();
            if (std::getenv("GLOB2_BENCH_FULL_MAP"))
                camera.zoom = std::min(double(width)/(gui.game.map.getW()*32), double(height)/(gui.game.map.getH()*32));
            // GLOB2_BENCH_ZOOM measures one zoom level instead of the minimum;
            // GLOB2_BENCH_ADAPTIVE_ZOOM=0 draws it with uniform scaling.
            if (const char *zoom = std::getenv("GLOB2_BENCH_ZOOM"))
                camera.zoom = std::clamp(std::atof(zoom), camera.minimumZoom(), MapCamera::MAX_ZOOM);
            if (const char *adaptive = std::getenv("GLOB2_BENCH_ADAPTIVE_ZOOM"))
                globalContainer->settings.adaptiveZoomDetail = std::atoi(adaptive) != 0;
            const int worldW = int(std::ceil(camera.visibleW()));
            const int worldH = int(std::ceil(camera.visibleH()));
            if (savedGame) warmupCheckpoint(gui);
            else populateSynthetic(gui.game, worldW, worldH);
            // Interactive drawing consumes a published scene. Keep extraction
            // outside the renderer timing interval rather than recapturing the
            // complete simulation through the fixture adapter on every draw.
            prepareScene(gui);
            const int count = population(gui.game);
            const int cloudGridLimit = detailForZoom(gui.game, camera.zoom);
            // GLOB2_BENCH_FOG=1 draws the first team's fog of war instead of the whole map;
            // GLOB2_BENCH_SMOOTH_FOG=0 draws it without the smooth fog fade.
            if (const char *smoothFog = std::getenv("GLOB2_BENCH_SMOOTH_FOG"))
                globalContainer->settings.smoothFog = std::atoi(smoothFog) != 0;
            const Uint32 options = (std::getenv("GLOB2_BENCH_FOG") ? 0 : Game::DRAW_WHOLE_MAP) | (std::getenv("GLOB2_BENCH_BARS") ? Game::DRAW_HEALTH_FOOD_BAR : 0)
                | (std::getenv("GLOB2_BENCH_AREAS") ? Game::DRAW_AREA : 0);
            std::printf("mapped_units=%d (excludes units inside buildings)\n", mappedPopulation(gui.game));
            std::printf("flat zoom=%.6f world=%dx%d total_units=%d shader=%d cloud_grid_limit=%d\n", camera.zoom, worldW, worldH, count, globalContainer->gfx->hasUnitShader(), cloudGridLimit);
            for (bool clouds : {false, true})
            {
                globalContainer->settings.clouds = clouds;
                globalContainer->settings.cloudShadows = clouds;
                const Uint32 initialChecksum = gui.game.checkSum(nullptr, nullptr, nullptr, true);
                const char *mode = std::getenv("GLOB2_BENCH_MODE");
                if (mode && std::strcmp(mode, clouds ? "2D clouds" : "2D no clouds")) continue;
                PerformanceTelemetry::collector().reset();
                int cameraFrame = 0;
                if (!clouds && std::getenv("GLOB2_BENCH_COMPARE_RENDERER"))
                    globalContainer->gfx->setRenderBatchEnabled(false);
                measure(clouds ? "2D clouds" : "2D no clouds", [&] {
                    globalContainer->gfx->resetDrawCallCount();
                    globalContainer->gfx->setClipRect();
                    globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH(), GAGCore::Color(0, 0, 0));
                    const bool sweep = std::getenv("GLOB2_BENCH_CAMERA_SWEEP");
                    const bool motion = std::getenv("GLOB2_BENCH_CAMERA_MOTION");
                    const bool pan = motion || std::getenv("GLOB2_BENCH_CAMERA_PAN");
                    // A repeatable wheel-like zoom cycle with sub-tile scrolling.
                    // Unlike the seam stress sweep this does not teleport the view.
                    const double zoom = motion ? std::min(MapCamera::MAX_ZOOM,
                        camera.zoom * std::pow(4.0, (1 - std::cos(cameraFrame * 6.283185307179586 / 120)) / 2))
                        : sweep ? camera.zoom * (1 + cameraFrame%8) : camera.zoom;
                    // GLOB2_BENCH_PAN_X/Y place a fixed camera's top-left tile, so a
                    // zoomed-in measurement can look at a colony instead of open water.
                    const int fixedPanX = std::getenv("GLOB2_BENCH_PAN_X") ? std::atoi(std::getenv("GLOB2_BENCH_PAN_X")) : 0;
                    const int fixedPanY = std::getenv("GLOB2_BENCH_PAN_Y") ? std::atoi(std::getenv("GLOB2_BENCH_PAN_Y")) : 0;
                    const int panX = pan ? (fixedPanX + cameraFrame / 8)%gui.game.map.getW()
                        : sweep ? (cameraFrame*37)%gui.game.map.getW() : fixedPanX;
                    const int panY = pan ? (fixedPanY + cameraFrame / 16)%gui.game.map.getH()
                        : sweep ? (cameraFrame*19)%gui.game.map.getH() : fixedPanY;
                    int drawW = int(std::ceil(width/zoom)), drawH = int(std::ceil(height/zoom));
                    if (!sweep && std::getenv("GLOB2_BENCH_FULL_MAP"))
                    {
                        drawW = std::min(drawW, gui.game.map.getW()*32);
                        drawH = std::min(drawH, gui.game.map.getH()*32);
                    }
                    // GLOB2_BENCH_FRACTION shifts the map by that many map pixels, as a
                    // camera between tiles does; seams between tiles only show then.
                    const float fraction = std::getenv("GLOB2_BENCH_FRACTION") ? float(std::atof(std::getenv("GLOB2_BENCH_FRACTION"))) : 0.f;
                    globalContainer->gfx->beginMapTransform(zoom,
                        -(pan ? cameraFrame % 8 * 4.f : fraction)*zoom,
                        -(pan ? cameraFrame % 16 * 2.f : fraction)*zoom, 0, 0, width, height);
                    const bool pausePresentation = std::getenv("GLOB2_BENCH_PAUSE_PRESENTATION");
                    if (pausePresentation) gui.view.render.animationTime = 22;
                    Game::drawMap(0, 0, drawW, drawH, 0, 0,
                        panX, panY, 0, gui.view, options, nullptr, nullptr, pausePresentation,
                        detailForZoom(gui.game, zoom));
                    globalContainer->gfx->endMapTransform();
                    ++cameraFrame;
                });
                std::printf("draw_calls=%lu texture_bytes=%zu\n", globalContainer->gfx->getDrawCallCount(), GAGCore::DrawableSurface::allocatedTextureBytes());
                if (auto *batch = globalContainer->gfx->getRenderBatch())
                {
                    const auto geometry = batch->geometryCache().stats();
                    const auto textures = batch->stats();
                    std::printf("STEADY_CACHE pending=%zu attempts=%llu promotions=%llu array_bytes=%zu geometry_bytes=%zu builds=%llu deferred=%llu\n",
                        textures.pendingTextures, textures.warmAttempts, textures.warmPromotions,
                        textures.textureBytes, geometry.bytes, geometry.builds, geometry.deferred);
                }
                PerformanceTelemetry::collector().write(std::cout, "BENCH_SCOPE", 0, false);
                assert(gui.game.checkSum(nullptr, nullptr, nullptr, true) == initialChecksum);
                std::printf("simulation_checksum=%08x\n", initialChecksum);
                // Optional same-process comparisons hold camera and simulation
                // fixed within each pair. Readback/checksum work is deliberately
                // outside the timing interval. Cloud animation has separate
                // mutable presentation state, so compare only the no-cloud pass.
                if (!clouds && std::getenv("GLOB2_BENCH_COMPARE_RENDERER"))
                {
                    const bool advance = std::getenv("GLOB2_BENCH_COMPARE_AI");
                    std::vector<double> pairedCpu[2];
                    for (int pair = -8; pair < frames; ++pair)
                    {
                        if (advance) advanceAiTick(gui);
                        if (advance) prepareScene(gui);
                        const auto checksum = gui.game.checkSum(nullptr, nullptr, nullptr, true);
                        const int cameraIndex = pair + 8;
                        const bool sweep = std::getenv("GLOB2_BENCH_CAMERA_SWEEP");
                        const double zoom = sweep ? camera.zoom * (1 + cameraIndex % 8) : camera.zoom;
                        const int panX = sweep ? (cameraIndex * 37) % gui.game.map.getW() : 0;
                        const int panY = sweep ? (cameraIndex * 19) % gui.game.map.getH() : 0;
                        int drawW = int(std::ceil(width / zoom)), drawH = int(std::ceil(height / zoom));
                        if (!sweep && std::getenv("GLOB2_BENCH_FULL_MAP"))
                        {
                            drawW = std::min(drawW, gui.game.map.getW() * 32);
                            drawH = std::min(drawH, gui.game.map.getH() * 32);
                        }
                        std::vector<unsigned char> reference;
                        for (int variant = 0; variant < 2; ++variant)
                        {
                            auto *gfx = globalContainer->gfx;
                            gfx->setRenderBatchEnabled(variant != 0);
                            gfx->setClipRect();
                            gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), GAGCore::Color(0, 0, 0));
                            gfx->resetDrawCallCount();
                            gfx->beginMapTransform(zoom, 0, 0, 0, 0, width, height);
                            gui.view.render.animationTime = 22;
                            glFinish();
                            const auto cpuStart = std::clock();
                            Game::drawMap(0, 0, drawW, drawH, 0, 0, panX, panY, 0,
                                gui.view, options, nullptr, nullptr, true, detailForZoom(gui.game, zoom));
                            gfx->endMapTransform();
                            glFinish();
                            const double cpu = 1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC;
                            assert(glGetError() == GL_NO_ERROR);
                            assert(gui.game.checkSum(nullptr, nullptr, nullptr, true) == checksum);
                            if (pair >= 0) pairedCpu[variant].push_back(cpu);
                            GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
                            std::vector<unsigned char> pixels(size_t(viewport[2]) * viewport[3] * 4);
                            glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3],
                                GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                            if (!variant) reference = std::move(pixels);
                            else
                            {
                                int maximumDelta = 0; size_t changed = 0;
                                for (size_t i = 0; i < pixels.size(); ++i)
                                {
                                    const int delta = std::abs(int(reference[i]) - int(pixels[i]));
                                    maximumDelta = std::max(maximumDelta, delta);
                                    changed += delta != 0;
                                }
                                std::printf("COMPARE pair=%d tick=%u checksum=%08x max_delta=%d changed_channels=%zu\n",
                                    pair, gui.game.stepCounter, checksum, maximumDelta, changed);
                                std::fflush(stdout);
                                assert(maximumDelta <= 1 && changed <= 100);
                            }
                            if (pair == frames - 1)
                                if (const char *prefix = std::getenv("GLOB2_BENCH_COMPARE_CAPTURE_PREFIX"))
                                {
                                    const std::string output = std::string(prefix) +
                                        (variant ? "-optimized.ppm" : "-immediate.ppm");
                                    captureFramebuffer(output.c_str());
                                }
                            std::printf("PAIRED pair=%d impl=%s cpu_ms=%.6f draws=%lu\n",
                                pair, variant ? "optimized" : "immediate", cpu, gfx->getDrawCallCount());
                        }
                    }
                    globalContainer->gfx->setRenderBatchEnabled(true);
                    for (int variant = 0; variant < 2; ++variant)
                    {
                        auto& values = pairedCpu[variant];
                        std::sort(values.begin(), values.end());
                        std::printf("PAIRED_SUMMARY impl=%s cpu_median=%.6f frames=%zu\n",
                            variant ? "optimized" : "immediate", values[values.size() / 2], values.size());
                    }
                    if (auto *batch = globalContainer->gfx->getRenderBatch())
                    {
                        const auto stats = batch->geometryCache().stats();
                        std::printf("RENDER_CACHE array_bytes=%zu geometry_bytes=%zu entries=%zu hits=%llu misses=%llu\n",
                            batch->textureBytes(), stats.bytes, stats.entries, stats.hits, stats.misses);
                    }
                    std::printf("COMPARE_FINAL tick=%u checksum=%08x\n", gui.game.stepCounter,
                        gui.game.checkSum(nullptr, nullptr, nullptr, true));
                }
                captureFramebuffer();
                if (std::getenv("GLOB2_BENCH_VISIBLE")) globalContainer->gfx->nextFrame();
            }
            gui.view.scene = nullptr;
        }
        else
        for (bool clouds : {false, true})
        {
            const Uint32 initialChecksum = gui.game.checkSum(nullptr, nullptr, nullptr, true);
            globalContainer->settings.clouds = clouds;
            globalContainer->settings.cloudShadows = clouds;
            measure(clouds ? "2D clouds" : "2D no clouds", [&] {
                globalContainer->gfx->setClipRect();
                Game::drawMap(0, 0, width, height, 0, 0, x, y, 0, gui.view, Game::DRAW_WHOLE_MAP);
            });
            view.reset();
            view.toggle();
            // The ring is drawn at the flat camera's zoom: fully zoomed out,
            // the whole ring, unless GLOB2_BENCH_ZOOM names another.
            float ringZoom = std::min(1.f, std::max(float(width) / (gui.game.map.getW() * 32), float(height) / (gui.game.map.getH() * 32)));
            gui.view.render.minimumZoom = ringZoom;
            if (const char *zoom = std::getenv("GLOB2_BENCH_ZOOM"))
                ringZoom = std::clamp(float(std::atof(zoom)), ringZoom, float(MapCamera::MAX_ZOOM));
            assert(view.draw(gui.view.drawnScene(),gui.game.gui, 0, Game::DRAW_WHOLE_MAP, x, y, width, height, ringZoom));
            measure(clouds ? "Torus clouds" : "Torus no clouds", [&] {
                view.amount = 1;
                view.lastFrame = SDL_GetTicks();
                view.setViewport((x + 1) & gui.game.map.getMaskW(), (y + 1) & gui.game.map.getMaskH());
                assert(view.draw(gui.view.drawnScene(),gui.game.gui, 0, Game::DRAW_WHOLE_MAP, x, y, width, height, ringZoom));
            });
            size_t bytes = 0;
            for (const auto &tile : view.tiles) bytes += size_t(tile.textureW) * tile.textureH * 4;
            std::printf("capture=%dx%d tiles=%zu pixels/cell=%d texture_bytes=%zu cloud=%dx%d\n",
                        view.worldW * view.pixelsPerCell, view.worldH * view.pixelsPerCell,
                        view.tiles.size(), view.pixelsPerCell, bytes, view.cloudW, view.cloudH);
            view.reset();
            assert(gui.game.checkSum(nullptr, nullptr, nullptr, true) == initialChecksum);
            std::printf("simulation_checksum=%08x\n", initialChecksum);
        }
    }
#endif
    delete globalContainer;
    return 0;
}
};

int main(int argc, char **argv)
{
    return TorusRenderBenchmark::run(argc, argv);
}
