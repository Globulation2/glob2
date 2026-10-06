// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <GraphicContext.h>
#include <Toolkit.h>
#include <SpriteDrawBatch.h>
#include <RenderBackend.h>
#include <memory>
#include <cstring>
#include <vector>
#include <stdexcept>
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

void batchFailures()
{
    glob2test::ToolkitScope toolkit;
    auto *gfx = GAGCore::Toolkit::initGraphic(64, 64, 0, "sprite batch failure recovery");
    auto backend = std::make_unique<FailingRenderBackend>();
    auto *failure = backend.get();
    gfx->portableRenderer = std::move(backend);
        gfx->renderer = gfx->portableRenderer.get();
        gfx->nativeSoftware = false;
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("data/gfx/ressource"));
    auto *surface = sprite.images[0];
    const auto revision = surface->contentRevision();
    auto draw = [&]
    {
        GAGCore::SpriteDrawBatch batch(gfx, &sprite);
        gfx->drawSprite(0, 0, &sprite, 0);
    };
    REQUIRE_THROWS_AS(draw(), std::runtime_error);
    REQUIRE(surface->contentRevision() == revision);
    REQUIRE(failure->submissions == 1);
    failure->fail = false;
    REQUIRE_NOTHROW(draw());
    REQUIRE(surface->contentRevision() == revision);
    REQUIRE(failure->submittedVertices == 6);
    failure->fail = true;
    REQUIRE_THROWS_AS(([&]
    {
        GAGCore::SpriteDrawBatch batch(gfx, &sprite);
        gfx->drawSprite(0, 0, &sprite, 0);
        throw std::runtime_error("abort frame");
    }()), std::runtime_error);
    REQUIRE(failure->submissions == 2);
    REQUIRE_THROWS_AS(([&]
    {
        GAGCore::SpriteDrawBatch batch(gfx, &sprite);
        for (int i = 0; i < 4200; ++i) gfx->drawSprite(0, 0, &sprite, 0);
    }()), std::runtime_error);
    REQUIRE(failure->submissions == 3);
    failure->fail = false;
    REQUIRE_NOTHROW(draw());
    REQUIRE(failure->submittedVertices == 12);
    // Cache-backed team-color sprites must submit immediately: their surfaces
    // may be evicted before a deferred batch would otherwise consume them.
    sprite.dynamicTeamColor = true;
    {
        GAGCore::SpriteDrawBatch batch(gfx, &sprite);
        const int before = failure->submissions;
        gfx->drawSprite(0, 0, &sprite, 0);
        REQUIRE(failure->submissions == before + 1);
    }
}

void batchPixels(bool highResolution, bool portable = false)
{
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    glob2test::ToolkitScope toolkit;
    auto *gfx = GAGCore::Toolkit::initGraphic(640, 480, portable ? GAGCore::GraphicContext::PORTABLEGPU : GAGCore::GraphicContext::USEGPU, "resource batch parity");
    REQUIRE(gfx->hasPortableRenderer() == portable);
#ifdef HAVE_OPENGL
    if (!portable) REQUIRE(SDL_GL_GetCurrentContext() != nullptr);
#endif
    auto readPixels = [&]
    {
        std::vector<unsigned char> pixels;
#ifdef HAVE_OPENGL
        if (!portable)
        {
            GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
            pixels.resize(viewport[2] * viewport[3] * 4);
            glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            REQUIRE(glGetError() == GL_NO_ERROR);
            return pixels;
        }
#endif
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> captured(gfx->renderer->capture(), SDL_DestroySurface);
        REQUIRE(captured);
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(SDL_ConvertSurface(captured.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        REQUIRE(rgba);
        pixels.resize(rgba->w * rgba->h * 4);
        for (int row = 0; row < rgba->h; ++row)
            std::memcpy(pixels.data() + row * rgba->w * 4, static_cast<unsigned char*>(rgba->pixels) + row * rgba->pitch, rgba->w * 4);
        return pixels;
    };
    GAGCore::Sprite::setHighResolution(highResolution);
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("data/gfx/ressource"));
    if (!portable) REQUIRE(sprite.createTextureAtlas(true));
    if (highResolution) REQUIRE(GAGCore::Sprite::highResolutionStats().cpuBytes > 0);
    // Warm every standalone texture before checking submission counts.
    for (unsigned i = 0; i < sprite.getFrameCount(); ++i) gfx->drawSprite(0, 0, &sprite, i);
    gfx->finishDrawingSprite(&sprite, 255);

    for (float zoom : {1.f, .5f, .0732421875f, 1.37f, 4.f})
    {
        auto capture = [&](bool batched)
        {
            gfx->setClipRect();
            gfx->drawFilledRect(0, 0, 640, 480, 17, 23, 31);
            gfx->beginMapTransform(zoom, 11.25f, 9.5f, 7, 5, 280, 240);
            auto draw = [&]
            {
                for (int i = 0; i < 8400; ++i)
                {
                    // Repeated HD runs, alternating native/HD, overlaps and
                    // alpha changes. The final run crosses the 4096-quad limit.
                    unsigned frame = i < 100 ? (i / 4) % sprite.getFrameCount() :
                                     i < 200 ? (i % 2 ? 0 : 40) : 0;
                    Uint8 alpha = i < 200 && i % 11 == 0 ? 127 : 255;
                    gfx->drawSprite(float(i % 70), float(i % 57), &sprite, frame, alpha);
                    if (!batched) gfx->finishDrawingSprite(&sprite, alpha);
                }
            };
            if (batched)
            {
                GAGCore::SpriteDrawBatch batch(gfx, &sprite);
                REQUIRE_THROWS_AS(GAGCore::SpriteDrawBatch(gfx, &sprite), std::logic_error);
                draw();
            }
            else draw();
            gfx->endMapTransform();
            return readPixels();
        };
        REQUIRE(capture(false) == capture(true));
    }
    gfx->resetDrawCallCount();
    {
        GAGCore::SpriteDrawBatch batch(gfx, &sprite);
        for (int i = 0; i < 10000; ++i) gfx->drawSprite(20, 20, &sprite, 0);
    }
    REQUIRE(gfx->getDrawCallCount() == 3);
    auto disjointColumns = [&](bool batched)
    {
        gfx->drawFilledRect(0, 0, 640, 480, 17, 23, 31);
        gfx->resetDrawCallCount();
        auto draw = [&]
        {
            for (int row = 0; row < 40; ++row)
                for (unsigned frame : {0u, 1u, 40u})
                {
                    // Different texture columns do not overlap, but draws
                    // within a column do. Only the former may change order.
                    const float x = frame == 0 ? 20 : frame == 1 ? 200 : 400;
                    gfx->drawSprite(x, float(20 + (row % 8) * 50), &sprite, frame);
                    if (!batched) gfx->finishDrawingSprite(&sprite, 255);
                }
        };
        if (batched) { GAGCore::SpriteDrawBatch batch(gfx, &sprite); draw(); }
        else draw();
        // Packed HD frames now share one atlas, just like native frames.
        // Standalone HD frames and portable textures retain three columns.
        const bool standaloneHD = highResolution && !sprite.highResolutionAtlas;
        if (batched) REQUIRE(gfx->getDrawCallCount() == (standaloneHD || portable ? 3 : 1));
        return readPixels();
    };
    REQUIRE(disjointColumns(false) == disjointColumns(true));
    // Unwinding must discard deferred geometry and release ownership.
    try
    {
        GAGCore::SpriteDrawBatch batch(gfx, &sprite);
        gfx->drawSprite(20, 20, &sprite, 0);
        throw std::runtime_error("abort draw");
    }
    catch (const std::runtime_error&) {}
    REQUIRE_NOTHROW(GAGCore::SpriteDrawBatch(gfx, &sprite));
    GAGCore::Sprite::setHighResolution(false);
}
}

#ifdef HAVE_OPENGL
TEST_CASE("ordered HD resource sprite batch retains pixels [display]") { batchPixels(true); }
TEST_CASE("ordered native resource sprite batch retains pixels [display]") { batchPixels(false); }
#endif

TEST_CASE("ordered portable resource sprite batch retains pixels [display]") { batchPixels(false, true); }

TEST_CASE("resource sprite batch recovers from submission errors") { batchFailures(); }
