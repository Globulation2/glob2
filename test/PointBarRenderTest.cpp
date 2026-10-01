// SPDX-License-Identifier: GPL-3.0-or-later
// Run from the repository root with "gl" or "software". Uses its own profile.
#include "EngineFixtures.h"
#include "Game.h"
#include "GlobalContainer.h"
#include <GraphicContext.h>
#include <OpaqueRectangleBatch.h>
#include <RenderBackend.h>
#include <memory>
#include <stdexcept>
#include <SDL.h>
#include <cstring>
#include <vector>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif


namespace
{
class FailingRenderBackend final : public GAGCore::RenderBackend
{
public:
    bool fail = true;
    int submissions = 0;
    size_t submittedVertices = 0;
    void clip(const SDL_Rect*) override {}
    void transform(float, float, float, const SDL_Rect*) override {}
    void triangles(std::span<const SDL_Vertex> vertices, const void*, SDL_Surface*, bool) override
    {
        ++submissions;
        if (fail) throw std::runtime_error("submission failed");
        submittedVertices += vertices.size();
    }
    void screenTriangles(std::span<const SDL_Vertex>) override {}
    void forget(const void*) override {}
    void reset() override {}
    void present() override {}
    void flush() override {}
    void logicalSize(int, int) override {}
    SDL_Surface *capture() override { return nullptr; }
    void outputSize(int&, int&) override {}
};

class PointBarRenderTest
{
public:
    static void batchFailures()
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display = true, .width = 640, .height = 480});
        auto gfx = globals->gfx;
        auto backend = std::make_unique<FailingRenderBackend>();
        auto *failure = backend.get();
        gfx->renderer = std::move(backend);
        auto draw = [&] {
            GAGCore::OpaqueRectangleBatch scope(gfx);
            gfx->drawFilledRect(0, 0, 10, 10, 78, 187, 78);
        };
        REQUIRE_THROWS_AS(draw(), std::runtime_error);
        REQUIRE(failure->submissions == 1);
        failure->fail = false;
        REQUIRE_NOTHROW(draw());
        REQUIRE(failure->submittedVertices == 6);
        failure->fail = true;
        const int submissions = failure->submissions;
        REQUIRE_THROWS_AS(([&] {
            GAGCore::OpaqueRectangleBatch scope(gfx);
            gfx->drawFilledRect(0, 0, 10, 10, 78, 187, 78);
            throw std::logic_error("drawing failed");
        })(), std::logic_error);
        REQUIRE(failure->submissions == submissions);
        // A capacity-triggered submission also clears ownership while unwinding.
        REQUIRE_THROWS_AS(([&] {
            GAGCore::OpaqueRectangleBatch scope(gfx);
            for (int i = 0; i < 4096; ++i) gfx->drawFilledRect(0, 0, 1, 1, 78, 187, 78);
        })(), std::runtime_error);
        failure->fail = false;
        REQUIRE_NOTHROW(draw());
        REQUIRE(failure->submittedVertices == 12);
    }

    static void batchPixels(bool portable)
    {
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display = true, .loadStrings = true,
            .width = 640, .height = 480, .screenFlags = Uint32(portable ? GraphicContext::PORTABLEGPU : GraphicContext::USEGPU)});
        auto gfx = globals->gfx;
        REQUIRE(gfx->hasPortableRenderer() == portable);
#ifdef HAVE_OPENGL
        if (!portable)
        {
            REQUIRE((gfx->getOptionFlags() & GraphicContext::USEGPU) != 0);
            REQUIRE(SDL_GL_GetCurrentContext() != nullptr);
        }
#endif
        for (float zoom : {1.f, .5f, .137f, 1.37f})
        {
            auto capture = [&](bool batch) {
                gfx->setClipRect();
                gfx->drawFilledRect(0,0,640,480,17,23,31);
                gfx->beginMapTransform(zoom, 11.25f, 9.5f, 7, 5, 280, 240);
                auto drawPrimitives = [&] {
                // Overlap, outlines and transparency must retain submission order.
                // Exceed the batch bound too, including clipping at fractional zoom.
                for (int i=0; i<4200; ++i)
                {
                    gfx->drawFilledRect(i%120, i%97, 31, 5, i%256, 78, 187);
                    if (i%101 == 0) gfx->drawRect(i%120, i%97, 4, 5, 31, 62, 19);
                    if (i%103 == 0) gfx->drawFilledRect(i%120, i%97, 2, 3, 230, 18, 93, 127);
                }
                for (int i=0; i<4200; ++i)
                    gfx->drawFilledRect(i%120, i%97, 2, 3, 78, 187, i%256);
                };
                if (batch)
                {
                    GAGCore::OpaqueRectangleBatch scope(gfx);
                    drawPrimitives();
                }
                else drawPrimitives();
                gfx->endMapTransform();
                std::vector<unsigned char> pixels;
#ifdef HAVE_OPENGL
                if (!portable)
                {
                    GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
                    pixels.resize(viewport[2]*viewport[3]*4);
                    glReadPixels(0,0,viewport[2],viewport[3],GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                    REQUIRE(glGetError() == GL_NO_ERROR);
                }
                else
#endif
                {
                    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(gfx->renderer->capture(), SDL_FreeSurface);
                    REQUIRE(surface);
                    auto converted = SDL_ConvertSurfaceFormat(surface.get(),SDL_PIXELFORMAT_RGBA32,0);
                    REQUIRE(converted);
                    pixels.resize(converted->w*converted->h*4);
                    for (int row=0; row<converted->h; ++row)
                        std::memcpy(pixels.data()+row*converted->w*4,
                            static_cast<unsigned char*>(converted->pixels)+row*converted->pitch,converted->w*4);
                    SDL_FreeSurface(converted);
                }
                return pixels;
            };
            REQUIRE(capture(false) == capture(true));
        }
    }
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
                SDL_Surface *surface = SDL_ConvertSurfaceFormat(gfx->getSDLSurface(), SDL_PIXELFORMAT_RGBA32, 0);
                REQUIRE(surface);
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
}

TEST_SUITE("PointBarRender")
{
    TEST_CASE("opaque rectangle batches recover from submission errors") { PointBarRenderTest::batchFailures(); }
#ifdef HAVE_OPENGL
	TEST_CASE("opaque rectangle batches retain OpenGL pixels [display]") { PointBarRenderTest::batchPixels(false); }
#endif
	TEST_CASE("opaque rectangle batches retain portable renderer pixels [display]") { PointBarRenderTest::batchPixels(true); }
	TEST_CASE("status-bar pixel bounds in software rendering") { PointBarRenderTest::run(false); }
#ifdef HAVE_OPENGL
	TEST_CASE("status-bar pixel bounds in OpenGL rendering [display]") { PointBarRenderTest::run(true); }
#endif
}
