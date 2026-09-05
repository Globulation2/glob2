// SPDX-License-Identifier: GPL-3.0-or-later
// Loaded-game benchmark. Uses an isolated profile and generates no desktop input.
#include "GlobalContainer.h"
#include "TorusPicking.h"
#include "DynamicClouds.h"
#define private public
#include "TorusView.h"
#undef private
#include "GameGUI.h"
#include "Engine.h"
#include "Team.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif

GlobalContainer *globalContainer = nullptr;
int main(int argc, char **argv)
{
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    globalContainer = new GlobalContainer;
    globalContainer->parseArgs(argc, argv);
    globalContainer->load();
#ifdef HAVE_OPENGL
    if (!SDL_GL_GetCurrentContext()) return 1;
    SDL_HideWindow(SDL_GL_GetCurrentWindow());
    SDL_GL_SetSwapInterval(0);
    {
        GameGUI gui;
        const char *path = std::getenv("GLOB2_BENCH_MAP");
        auto mapHeader = Engine::loadMapHeader(path ? path : "maps/Oazis.map");
        GameHeader gameHeader;
        for (int i = 0; i < mapHeader.getNumberOfTeams(); ++i)
            gameHeader.getBasePlayer(i) = BasePlayer(i, "Benchmark", i, BasePlayer::P_LOCAL);
        gameHeader.setNumberOfPlayers(mapHeader.getNumberOfTeams());
        gui.localPlayer = gui.localTeamNo = 0;
        assert(gui.loadFromHeaders(mapHeader, gameHeader, true, true));
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
            std::vector<double> times;
            for (int i = -2; i < frames; ++i)
            {
                glFinish();
                Uint64 start = SDL_GetPerformanceCounter();
                draw();
                glFinish();
                assert(glGetError() == GL_NO_ERROR);
                double ms = 1000.0 * (SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
                if (i >= 0) times.push_back(ms);
            }
            std::sort(times.begin(), times.end());
            std::printf("%s median=%.3f ms p95=%.3f ms\n", label, times[times.size()/2], times[(times.size()-1)*95/100]);
            std::fflush(stdout);
        };
        for (bool clouds : {false, true})
        {
            if (clouds) globalContainer->settings.optionFlags &= ~GlobalContainer::OPTION_LOW_SPEED_GFX;
            else globalContainer->settings.optionFlags |= GlobalContainer::OPTION_LOW_SPEED_GFX;
            measure(clouds ? "2D clouds" : "2D no clouds", [&] {
                globalContainer->gfx->setClipRect();
                gui.game.drawMap(0, 0, width, height, 0, 0, x, y, 0, Game::DRAW_WHOLE_MAP);
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
            if (clouds && std::getenv("GLOB2_BENCH_CAPTURE"))
            {
                GLint viewport[4];
                glGetIntegerv(GL_VIEWPORT, viewport);
                std::vector<unsigned char> pixels(viewport[2] * viewport[3] * 3);
                GLint alignment;
                glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, viewport[2], viewport[3], GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
                glPixelStorei(GL_PACK_ALIGNMENT, alignment);
                std::ofstream image(std::getenv("GLOB2_BENCH_CAPTURE"), std::ios::binary);
                image << "P6\n" << viewport[2] << " " << viewport[3] << "\n255\n";
                for (int row = viewport[3] - 1; row >= 0; --row)
                    image.write(reinterpret_cast<const char *>(pixels.data() + row * viewport[2] * 3), viewport[2] * 3);
            }
            view.reset();
        }
    }
#endif
    delete globalContainer;
}
