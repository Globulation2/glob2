// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <GraphicContext.h>
#include <MapGeometryCache.h>
#include <RenderBatch.h>
#include <Toolkit.h>
#include <memory>
#include <vector>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif

TEST_CASE("map geometry cache validates frames, texture lifetime and row ranges [display]")
{
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    glob2test::ToolkitScope toolkit;
    auto *gfx = GAGCore::Toolkit::initGraphic(320, 240,
        GAGCore::GraphicContext::USEGPU, "map geometry regression");
    auto *batch = gfx->getRenderBatch();
    REQUIRE(batch);
    auto& cache = batch->geometryCache();
    std::vector<int> frames(8, 0);
    auto surface = std::make_unique<GAGCore::DrawableSurface>(64, 64);
    surface->drawFilledRect(0, 0, 64, 64, GAGCore::Color(171, 73, 29));
    // Allocate/upload before capture, whose contract is immutable textures.
    gfx->drawSurface(0, 0, surface.get());
    const GAGCore::MapGeometryCache::Key key{&frames, 1, 0, 0, 8, 1};
    auto capture = [&]
    {
        GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
        std::vector<unsigned char> pixels(size_t(viewport[2]) * viewport[3] * 4);
        glReadPixels(0, 0, viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        REQUIRE(glGetError() == GL_NO_ERROR);
        return pixels;
    };
    auto draw = [&](bool cached, int first, int last, float zoom)
    {
        gfx->setClipRect();
        gfx->drawFilledRect(0, 0, 320, 240, 11, 23, 37);
        gfx->beginMapTransform(zoom, 0, 0, 0, 0, 320, 240);
        auto emit = [&](int begin, int end, int tx, int ty)
        {
            for (int tile = begin; tile <= end; ++tile)
                if (frames[tile] >= 0)
                    gfx->drawSurface(float(tile * 32 - 16 + tx), float(-16 + ty), surface.get());
        };
        // Art overlaps both neighboring source tiles. Filtering by its pixel
        // bounds or sorting quads would differ from the original row traversal.
        if (cached)
        {
            GAGCore::MapGeometryCache::Layer layer(cache);
            REQUIRE(cache.draw(key, frames, [&] { emit(0, 7, 0, 0); }, first, last, 10, 50));
        }
        else emit(first, last, 10, 50);
        gfx->endMapTransform();
        glFinish();
        return capture();
    };
    for (float zoom : {.0732421875f, .5f, 1.f, 1.37f, 4.f})
        for (auto range : {std::pair{0, 7}, std::pair{3, 5}, std::pair{0, 0}, std::pair{7, 7}})
            REQUIRE(draw(false, range.first, range.second, zoom) ==
                    draw(true, range.first, range.second, zoom));
    REQUIRE(cache.stats().hits > 0);
    REQUIRE(cache.stats().bytes <= 32 * 1024 * 1024);
    auto misses = cache.stats().misses;
    frames[4] = -1;
    REQUIRE(draw(false, 0, 7, 1) == draw(true, 0, 7, 1));
    REQUIRE(cache.stats().misses > misses);
    // The surface's dirty upload invalidates the retained texture generation.
    surface->drawFilledRect(0, 0, 64, 64, GAGCore::Color(31, 151, 83));
    auto reference = draw(false, 0, 7, 1);
    REQUIRE(reference == draw(true, 0, 7, 1));
    gfx->setRenderBatchEnabled(false);
    surface->drawFilledRect(0, 0, 64, 64, GAGCore::Color(151, 83, 31));
    reference = draw(false, 0, 7, 1);
    gfx->setRenderBatchEnabled(true);
    REQUIRE(reference == draw(true, 0, 7, 1));
    gfx->setRenderBatchEnabled(false);
    surface.reset();
    gfx->setRenderBatchEnabled(true);
    REQUIRE(cache.stats().entries == 0);
    surface = std::make_unique<GAGCore::DrawableSurface>(64, 64);
    surface->drawFilledRect(0, 0, 64, 64, GAGCore::Color(73, 29, 171));
    reference = draw(false, 0, 7, 1);
    REQUIRE(reference == draw(true, 0, 7, 1));
    cache.clear();
    // A capture allocation failure must discard partially recorded geometry,
    // restore GL state, and leave the caller free to render normally.
    gfx->setClipRect();
    gfx->drawFilledRect(0, 0, 320, 240, 11, 23, 37);
    const auto beforeFailure = capture();
    REQUIRE_FALSE(cache.draw(key, frames, [&]
    {
        for (int i = 0; i < 4200; ++i) gfx->drawSurface(float(i % 8 * 32), 50.f, surface.get());
        throw std::bad_alloc();
    }));
    REQUIRE(beforeFailure == capture());
    REQUIRE(draw(false, 0, 7, 1) == draw(true, 0, 7, 1));
    cache.clear();
    REQUIRE(cache.stats().bytes == 0);
    REQUIRE(cache.stats().entries == 0);
}

TEST_CASE("map cache invalidates only referenced sources [display]")
{
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    glob2test::ToolkitScope toolkit;
    auto *gfx = GAGCore::Toolkit::initGraphic(320, 240,
        GAGCore::GraphicContext::USEGPU, "selective map invalidation");
    auto *batch = gfx->getRenderBatch();
    REQUIRE(batch);
    auto& cache = batch->geometryCache();
    std::vector<int> frames(1, 0);
    const GAGCore::MapGeometryCache::Key key{&frames, 0, 0, 0, 1, 1};
    GAGCore::DrawableSurface retained(32, 32);
    retained.drawFilledRect(0, 0, 32, 32, GAGCore::Color(61, 137, 211));
    gfx->drawSurface(0, 0, &retained); // Upload before the immutable capture.
    REQUIRE(cache.draw(key, frames, [&] { gfx->drawSurface(0, 0, &retained); }));
    REQUIRE(cache.stats().entries == 1);
    const auto builds = cache.stats().builds;
    const auto hits = cache.stats().hits;
    {
        GAGCore::DrawableSurface unrelated(32, 32);
        unrelated.drawFilledRect(0, 0, 32, 32, GAGCore::Color(191, 37, 83));
        gfx->drawSurface(100, 100, &unrelated);
        REQUIRE(cache.stats().entries == 1);
        unrelated.drawFilledRect(0, 0, 32, 32, GAGCore::Color(37, 83, 191));
        gfx->drawSurface(100, 100, &unrelated);
        REQUIRE(cache.stats().entries == 1);
    } // Deleting an unrelated source also leaves retained terrain intact.
    REQUIRE(cache.stats().entries == 1);
    REQUIRE(cache.draw(key, frames, [] { FAIL("unrelated mutation rebuilt map geometry"); }));
    REQUIRE(cache.stats().builds == builds);
    REQUIRE(cache.stats().hits == hits + 1);
    retained.drawFilledRect(0, 0, 32, 32, GAGCore::Color(137, 211, 61));
    gfx->drawSurface(0, 0, &retained);
    REQUIRE(cache.stats().entries == 0);
    REQUIRE(cache.draw(key, frames, [&] { gfx->drawSurface(0, 0, &retained); }));
    REQUIRE(cache.stats().builds == builds + 1);
    REQUIRE(glGetError() == GL_NO_ERROR);
}

TEST_CASE("map cache bounds frame builds while retaining hits and fallback pixels [display]")
{
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    glob2test::ToolkitScope toolkit;
    auto *gfx = GAGCore::Toolkit::initGraphic(320, 240,
        GAGCore::GraphicContext::USEGPU, "bounded geometry warming");
    auto *batch = gfx->getRenderBatch();
    REQUIRE(batch);
    auto& cache = batch->geometryCache();
    std::vector<int> frames(1, 0);
    GAGCore::DrawableSurface surface(16, 16);
    surface.drawFilledRect(0, 0, 16, 16, GAGCore::Color(71, 157, 223));
    gfx->drawSurface(0, 0, &surface);
    const GAGCore::MapGeometryCache::Key hot{&frames, 0, 1000, 0, 1, 1};
    REQUIRE(cache.draw(hot, frames, [&] { gfx->drawSurface(0, 0, &surface); }));
    const auto before = cache.stats();
    auto capture = [&]
    {
        GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
        std::vector<unsigned char> pixels(size_t(viewport[2]) * viewport[3] * 4);
        glReadPixels(0, 0, viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        REQUIRE(glGetError() == GL_NO_ERROR);
        return pixels;
    };
    gfx->setClipRect();
    gfx->drawFilledRect(0, 0, 320, 240, 17, 29, 41);
    for (int tile = 0; tile < 64; ++tile)
        gfx->drawSurface(8 + tile % 8 * 24, 8 + tile / 8 * 24, &surface);
    gfx->drawSurface(250, 200, &surface);
    glFinish();
    const auto reference = capture();
    gfx->drawFilledRect(0, 0, 320, 240, 17, 29, 41);
    {
        GAGCore::FrameDrawBatch frame(gfx);
        GAGCore::MapGeometryCache::Layer layer(cache);
        for (int tile = 0; tile < 64; ++tile)
        {
            const int x = 8 + tile % 8 * 24, y = 8 + tile / 8 * 24;
            if (!cache.draw({&frames, 0, tile, 0, 1, 1}, frames,
                    [&] { gfx->drawSurface(0, 0, &surface); }, -1, -1, float(x), float(y)))
            {
                layer.prepareFallback();
                gfx->drawSurface(x, y, &surface);
            }
        }
        // A hit still renders after either the attempt or soft-time limit.
        REQUIRE(cache.draw(hot, frames, [] { FAIL("frame budget rejected an existing hit"); },
            -1, -1, 250, 200));
    }
    glFinish();
    REQUIRE(reference == capture());
    const auto after = cache.stats();
    // The soft time limit may stop earlier on a busy host; sixteen is a hard cap.
    REQUIRE(after.builds > before.builds);
    REQUIRE(after.builds - before.builds <= 16);
    REQUIRE(after.deferred - before.deferred >= 48);
    REQUIRE(after.hits == before.hits + 1);
    REQUIRE(after.entries <= 17);
}
#endif
