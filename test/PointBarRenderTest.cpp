// SPDX-License-Identifier: GPL-3.0-or-later
// Run from the repository root with "gl" or "software". Uses its own profile.
#include "EngineFixtures.h"
#include "Game.h"
#include "GlobalContainer.h"
#include <GraphicContext.h>
#include <SDL3/SDL.h>
#include <cstring>
#include <vector>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif


class PointBarRenderTest
{
public:
    static void run(bool gpu)
    {
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display = true, .loadStrings = true, .width = 640, .height = 480,
                                                                     .screenFlags = gpu ? Uint32(GraphicContext::USEGPU) : 0});
        REQUIRE(bool(globals->gfx->getOptionFlags() & GraphicContext::USEGPU) == gpu);
        if (SDL_GL_GetCurrentWindow()) SDL_HideWindow(SDL_GL_GetCurrentWindow());
        Game game(nullptr);
        auto pixels = [&](Game::BarOrientation orientation, int capacity, int current, int pending)
        {
            auto gfx = globals->gfx;
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
                REQUIRE(glGetError() == GL_NO_ERROR);
            }
            else
#endif
            {
                SDL_Surface *surface = SDL_ConvertSurface(gfx->getSDLSurface(), SDL_PIXELFORMAT_RGBA32);
                REQUIRE(surface);
                for (int row = 0; row < 128; ++row)
                    std::memcpy(result.data() + row * 128 * 4,
                                static_cast<unsigned char *>(surface->pixels) + row * surface->pitch, 128 * 4);
                SDL_DestroySurface(surface);
            }
            return result;
        };
        for (auto direction : {Game::LEFT_TO_RIGHT, Game::RIGHT_TO_LEFT, Game::TOP_TO_BOTTOM, Game::BOTTOM_TO_TOP})
        {
            // Verify that the readback really distinguishes filled and empty bars.
            REQUIRE(pixels(direction, 5, 5, 0) != pixels(direction, 5, 0, 0));
            REQUIRE(pixels(direction, 5, 2, 3) != pixels(direction, 5, 2, 0));
            for (int capacity : {0, 1, 5, 16})
            {
                const int current = capacity / 2;
                REQUIRE(pixels(direction, capacity, capacity, 0) ==
                       pixels(direction, capacity, capacity + 19, 7));
                REQUIRE(pixels(direction, capacity, current, capacity - current) ==
                       pixels(direction, capacity, current, capacity + 19));
                REQUIRE(pixels(direction, capacity, 0, 0) == pixels(direction, capacity, -1, -2));
                REQUIRE(pixels(direction, capacity, current, 0) == pixels(direction, capacity, current, -1));
            }
        }
    }
};

TEST_SUITE("PointBarRender")
{
	TEST_CASE("status-bar pixel bounds in software rendering") { PointBarRenderTest::run(false); }
	TEST_CASE("status-bar pixel bounds in OpenGL rendering [display]") { PointBarRenderTest::run(true); }
}
