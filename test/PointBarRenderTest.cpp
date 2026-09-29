// SPDX-License-Identifier: GPL-3.0-or-later
// Run from the repository root with "gl" or "software". Uses its own profile.
#include "Game.h"
#include "GlobalContainer.h"
#include <GraphicContext.h>
#include <SDL.h>
#include <cassert>
#include <cstring>
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

class PointBarRenderTest
{
public:
    static int run(int argc, char **argv)
    {
        if (argc != 2 || (std::strcmp(argv[1], "gl") && std::strcmp(argv[1], "software")))
        {
            std::cerr << "Usage: point-bar-render-test gl|software\n";
            return 2;
        }
        const bool gpu = !std::strcmp(argv[1], "gl");
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
        GlobalContainer globals("glob2-point-bar-test");
        globalContainer = &globals;
        globals.settings = Settings();
        globals.settings.screenWidth = 640;
        globals.settings.screenHeight = 480;
        globals.settings.screenFlags = gpu ? GraphicContext::USEGPU : 0;
        globals.settings.mute = true;
        globals.load();
        assert(bool(globals.gfx->getOptionFlags() & GraphicContext::USEGPU) == gpu);
        if (SDL_GL_GetCurrentWindow()) SDL_HideWindow(SDL_GL_GetCurrentWindow());
        Game game(nullptr);
        auto pixels = [&](Game::BarOrientation orientation, int capacity, int current, int pending)
        {
            auto gfx = globals.gfx;
            gfx->setClipRect();
            gfx->drawFilledRect(0, 0, 128, 128, 17, 23, 31);
            game.drawPointBar(8, 8, orientation, capacity, current, pending,
                              255, 255, 255, 255, 64, 0);
            std::vector<unsigned char> result(128 * 128 * 4);
#ifdef HAVE_OPENGL
            if (gpu)
            {
                GLint viewport[4];
                glGetIntegerv(GL_VIEWPORT, viewport);
                const int width = 128 * viewport[2] / gfx->getW();
                const int height = 128 * viewport[3] / gfx->getH();
                result.resize(width * height * 4);
                glReadPixels(viewport[0], viewport[1] + viewport[3] - height, width, height,
                             GL_RGBA, GL_UNSIGNED_BYTE, result.data());
                assert(glGetError() == GL_NO_ERROR);
            }
            else
#endif
            {
                SDL_Surface *surface = SDL_ConvertSurfaceFormat(gfx->getSDLSurface(), SDL_PIXELFORMAT_RGBA32, 0);
                assert(surface);
                for (int row = 0; row < 128; ++row)
                    std::memcpy(result.data() + row * 128 * 4,
                                static_cast<unsigned char *>(surface->pixels) + row * surface->pitch, 128 * 4);
                SDL_FreeSurface(surface);
            }
            return result;
        };
        for (auto direction : {Game::LEFT_TO_RIGHT, Game::RIGHT_TO_LEFT, Game::TOP_TO_BOTTOM, Game::BOTTOM_TO_TOP})
        {
            // Verify that the readback really distinguishes filled and empty bars.
            assert(pixels(direction, 5, 5, 0) != pixels(direction, 5, 0, 0));
            assert(pixels(direction, 5, 2, 3) != pixels(direction, 5, 2, 0));
            for (int capacity : {0, 1, 5, 16})
            {
                const int current = capacity / 2;
                assert(pixels(direction, capacity, capacity, 0) ==
                       pixels(direction, capacity, capacity + 19, 7));
                assert(pixels(direction, capacity, current, capacity - current) ==
                       pixels(direction, capacity, current, capacity + 19));
                assert(pixels(direction, capacity, 0, 0) == pixels(direction, capacity, -1, -2));
                assert(pixels(direction, capacity, current, 0) == pixels(direction, capacity, current, -1));
            }
        }
        std::cout << "PASS: status-bar pixel bounds, both sections, all directions and zero capacity ("
                  << (gpu ? "OpenGL" : "software") << ")\n";
        return 0;
    }
};

int main(int argc, char **argv)
{
    return PointBarRenderTest::run(argc, argv);
}
