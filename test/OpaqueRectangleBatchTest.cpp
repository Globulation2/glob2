// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <GraphicContext.h>
#include <OpaqueRectangleBatch.h>
#include <RenderBackend.h>
#include <Toolkit.h>
#include <SDL.h>
#include <cstring>
#include <memory>
#include <stdexcept>
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
    void blit(const void*, SDL_Surface*, std::uint64_t, bool,
              const SDL_Rect&, const SDL_FRect&, Uint8) override
    {
        ++submissions;
        if (fail) throw std::runtime_error("submission failed");
        submittedVertices += 6;
    }
    void fill(const SDL_FRect&, SDL_Color) override {}
    void clip(const SDL_Rect*) override {}
    void transform(float, float, float, const SDL_Rect*) override {}
    void triangles(std::span<const SDL_Vertex> vertices, const void*, SDL_Surface*, std::uint64_t) override
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

class OpaqueRectangleBatchTest
{
public:
    static void batchFailures()
    {
        glob2test::ToolkitScope toolkit;
        auto *gfx = GAGCore::Toolkit::initGraphic(640, 480, 0, "batch failure recovery");
        auto backend = std::make_unique<FailingRenderBackend>();
        auto *failure = backend.get();
        gfx->portableRenderer = std::move(backend);
        gfx->renderer = gfx->portableRenderer.get();
        gfx->nativeSoftware = false;
        // Emulate an accelerated backend: CPU fills deliberately bypass batches.
        gfx->optionFlags |= GAGCore::GraphicContext::PORTABLEGPU;
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
        glob2test::ToolkitScope toolkit;
        const Uint32 flags = portable ? GAGCore::GraphicContext::PORTABLEGPU : GAGCore::GraphicContext::USEGPU;
        auto *gfx = GAGCore::Toolkit::initGraphic(640, 480, flags, "batch pixel parity");
        REQUIRE(gfx->hasPortableRenderer() == portable);
#ifdef HAVE_OPENGL
        if (!portable)
        {
            REQUIRE((gfx->getOptionFlags() & GAGCore::GraphicContext::USEGPU) != 0);
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
};
}

TEST_SUITE("OpaqueRectangleBatch")
{
    TEST_CASE("recovers from submission errors") { OpaqueRectangleBatchTest::batchFailures(); }
#ifdef HAVE_OPENGL
    TEST_CASE("retains OpenGL pixels [display]") { OpaqueRectangleBatchTest::batchPixels(false); }
#endif
    TEST_CASE("retains portable renderer pixels [display]") { OpaqueRectangleBatchTest::batchPixels(true); }
}
