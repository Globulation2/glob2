// SPDX-License-Identifier: GPL-3.0-or-later
// Loaded-game benchmark. Run with an isolated profile; no desktop input is generated.
#include "GlobalContainer.h"
#include "TorusPicking.h"
#include "DynamicClouds.h"
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
#include <PerformanceTelemetry.h>
#include "TorusMapFixture.h"
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
        for (int tick = 0; tick < ticks; ++tick)
        {
            for (int player = 0; player < game.gameHeader.getNumberOfPlayers(); ++player)
                if (game.players[player]->ai)
                {
                    auto order = game.players[player]->ai->getOrder(false);
                    order->sender = player;
                    gui.executeOrder(order);
                }
            game.syncStep(gui.localTeamNo);
        }
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
    static void captureFramebuffer()
    {
        const char *path = std::getenv("GLOB2_BENCH_CAPTURE");
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
    globalContainer->parseArgs(argc, argv);
    globalContainer->load();
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
            for (int i = 0; i < mapHeader.getNumberOfTeams(); ++i)
                gameHeader.getBasePlayer(i) = BasePlayer(i, "Benchmark", i, BasePlayer::P_LOCAL);
            gameHeader.setNumberOfPlayers(mapHeader.getNumberOfTeams());
            assert(gui.loadFromHeaders(mapHeader, gameHeader, true, true));
        }
        gui.localPlayer = gui.localTeamNo = 0;
        gui.adjustLocalTeam();
        gui.adjustInitialViewport();
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
            for (int i = -2; i < frames; ++i)
            {
                glFinish();
                Uint64 start = SDL_GetPerformanceCounter();
                const auto cpuStart = std::clock();
                draw();
                glFinish();
                assert(glGetError() == GL_NO_ERROR);
                double ms = 1000.0 * (SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
                if (i >= 0)
                {
                    times.push_back(ms);
                    if (cpuStart != std::clock_t(-1))
                        cpuTimes.push_back(1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC);
                }
            }
            std::sort(times.begin(), times.end());
            std::printf("%s median=%.3f ms p95=%.3f ms\n", label, times[times.size()/2], times[(times.size()*95+99)/100-1]);
#ifndef _WIN32
            if (!cpuTimes.empty())
            {
                std::sort(cpuTimes.begin(), cpuTimes.end());
                std::printf("%s process_cpu_median=%.3f ms process_cpu_p95=%.3f ms\n",
                    label, cpuTimes[cpuTimes.size()/2], cpuTimes[(cpuTimes.size()*95+99)/100-1]);
            }
#endif
            std::fflush(stdout);
        };
        if (std::getenv("GLOB2_BENCH_FLAT"))
        {
            MapCamera camera;
            camera.resize(width, height, gui.game.map.getW()*32, gui.game.map.getH()*32);
            camera.zoom = camera.minimumZoom();
            if (std::getenv("GLOB2_BENCH_FULL_MAP"))
                camera.zoom = std::min(double(width)/(gui.game.map.getW()*32), double(height)/(gui.game.map.getH()*32));
            const int worldW = int(std::ceil(camera.visibleW()));
            const int worldH = int(std::ceil(camera.visibleH()));
            if (savedGame) warmupCheckpoint(gui);
            else populateSynthetic(gui.game, worldW, worldH);
            const int count = population(gui.game);
            const int cloudGridLimit = detailForZoom(gui.game, camera.zoom);
            const Uint32 options = Game::DRAW_WHOLE_MAP | (std::getenv("GLOB2_BENCH_BARS") ? Game::DRAW_HEALTH_FOOD_BAR : 0);
            const Uint32 initialChecksum = gui.game.checkSum(nullptr, nullptr, nullptr, true);
            std::printf("mapped_units=%d (excludes units inside buildings)\n", mappedPopulation(gui.game));
            std::printf("flat zoom=%.6f world=%dx%d total_units=%d shader=%d cloud_grid_limit=%d\n", camera.zoom, worldW, worldH, count, globalContainer->gfx->hasUnitShader(), cloudGridLimit);
            for (bool clouds : {false, true})
            {
                if (clouds) globalContainer->settings.optionFlags &= ~GlobalContainer::OPTION_LOW_SPEED_GFX;
                else globalContainer->settings.optionFlags |= GlobalContainer::OPTION_LOW_SPEED_GFX;
                const char *mode = std::getenv("GLOB2_BENCH_MODE");
                if (mode && std::strcmp(mode, clouds ? "2D clouds" : "2D no clouds")) continue;
                PerformanceTelemetry::collector().reset();
                int cameraFrame = 0;
                measure(clouds ? "2D clouds" : "2D no clouds", [&] {
                    globalContainer->gfx->resetDrawCallCount();
                    globalContainer->gfx->setClipRect();
                    globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH(), GAGCore::Color(0, 0, 0));
                    const bool sweep = std::getenv("GLOB2_BENCH_CAMERA_SWEEP");
                    const double zoom = sweep ? camera.zoom * (1 + cameraFrame%8) : camera.zoom;
                    const int panX = sweep ? (cameraFrame*37)%gui.game.map.getW() : 0;
                    const int panY = sweep ? (cameraFrame*19)%gui.game.map.getH() : 0;
                    int drawW = int(std::ceil(width/zoom)), drawH = int(std::ceil(height/zoom));
                    if (!sweep && std::getenv("GLOB2_BENCH_FULL_MAP"))
                    {
                        drawW = std::min(drawW, gui.game.map.getW()*32);
                        drawH = std::min(drawH, gui.game.map.getH()*32);
                    }
                    globalContainer->gfx->beginMapTransform(zoom, 0, 0, 0, 0, width, height);
                    gui.game.drawMap(0, 0, drawW, drawH, 0, 0,
                        panX, panY, 0, gui.view, options, nullptr, nullptr, false,
                        detailForZoom(gui.game, zoom));
                    globalContainer->gfx->endMapTransform();
                    ++cameraFrame;
                });
                std::printf("draw_calls=%lu texture_bytes=%zu\n", globalContainer->gfx->getDrawCallCount(), GAGCore::DrawableSurface::allocatedTextureBytes());
                PerformanceTelemetry::collector().write(std::cout, "BENCH_SCOPE", 0, false);
                assert(gui.game.checkSum(nullptr, nullptr, nullptr, true) == initialChecksum);
                std::printf("simulation_checksum=%08x\n", initialChecksum);
                captureFramebuffer();
                if (std::getenv("GLOB2_BENCH_VISIBLE")) globalContainer->gfx->nextFrame();
            }
        }
        else
        for (bool clouds : {false, true})
        {
            if (clouds) globalContainer->settings.optionFlags &= ~GlobalContainer::OPTION_LOW_SPEED_GFX;
            else globalContainer->settings.optionFlags |= GlobalContainer::OPTION_LOW_SPEED_GFX;
            measure(clouds ? "2D clouds" : "2D no clouds", [&] {
                globalContainer->gfx->setClipRect();
                gui.game.drawMap(0, 0, width, height, 0, 0, x, y, 0, gui.view, Game::DRAW_WHOLE_MAP);
            });
            view.reset();
            view.toggle();
            assert(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, width, height));
            measure(clouds ? "Torus clouds" : "Torus no clouds", [&] {
                view.amount = 1;
                view.lastFrame = SDL_GetTicks();
                view.setViewport((x + 1) & gui.game.map.getMaskW(), (y + 1) & gui.game.map.getMaskH());
                assert(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, width, height));
            });
            std::printf("atlas=%dx%d cloud=%dx%d\n", view.atlasW, view.atlasH, view.cloudW, view.cloudH);
            if (clouds) captureFramebuffer();
            view.reset();
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
