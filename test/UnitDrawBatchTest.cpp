// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <GraphicContext.h>
#include <RenderBatch.h>
#include <Toolkit.h>
#include <algorithm>
#include <memory>
#include <vector>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif

#ifdef HAVE_OPENGL
namespace
{
std::vector<unsigned char> captureBatchPixels()
{
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    std::vector<unsigned char> pixels(size_t(viewport[2]) * viewport[3] * 4);
    glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGBA,
                GL_UNSIGNED_BYTE, pixels.data());
    REQUIRE(glGetError() == GL_NO_ERROR);
    return pixels;
}

void requireBatchParity(const std::vector<unsigned char>& reference,
                        const std::vector<unsigned char>& actual)
{
    REQUIRE(reference.size() == actual.size());
    int maximumDelta = 0;
    size_t changed = 0;
    for (size_t i = 0; i < reference.size(); ++i)
    {
        const int delta = std::abs(int(reference[i]) - int(actual[i]));
        maximumDelta = std::max(maximumDelta, delta);
        changed += delta != 0;
    }
    // Per-vertex HSV inputs can round differently from scalar uniforms. Permit
    // only sparse one-level differences; missing/reordered geometry fails this.
    REQUIRE(maximumDelta <= 1);
    REQUIRE(changed <= 100);
}
}

TEST_CASE("unit batches preserve overlays, clipping and texture lifetime [display]")
{
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    glob2test::ToolkitScope toolkit;
    auto *gfx = GAGCore::Toolkit::initGraphic(640, 480,
        GAGCore::GraphicContext::USEGPU, "unit batch regression");
    REQUIRE(gfx->hasUnitShader());
    struct RestoreNativeArt
    {
        ~RestoreNativeArt() { GAGCore::Sprite::setHighResolution(false); }
    } restoreNativeArt;
    GAGCore::Sprite::setHighResolution(true);
    GAGCore::Sprite units, icons;
    REQUIRE(units.load("data/gfx/unit"));
    REQUIRE(icons.load("data/gfx/ressource"));
    REQUIRE(GAGCore::Sprite::highResolutionStats().cpuBytes > 0);

    for (float zoom : {.0732421875f, .5f, 1.f, 1.37f, 4.f})
    {
        auto draw = [&](bool batched)
        {
            gfx->setClipRect();
            gfx->drawFilledRect(0, 0, 640, 480, 17, 23, 31);
            gfx->beginMapTransform(zoom, 11.25f, 9.5f, 7, 5, 540, 420);
            auto commands = [&]
            {
                // Cross the queue capacity repeatedly with different team
                // hues, translucent sprites, native/HD icons and original bars.
                for (int i = 0; i < 8400; ++i)
                {
                    const float x = i < 400 ? i % 80 - 100.f : float(i % 100) * 60;
                    const float y = i < 400 ? i % 57 - 70.f : float(i / 100) * 60;
                    GAGCore::Color team;
                    team.setHSV(i % 12 * 30.f, .8f, .9f);
                    units.setBaseColor(team);
                    gfx->drawSprite(x, y, &units, i % 300, i % 9 == 0 ? 127 : 255);
                    gfx->drawFilledRect(x + 1, y + 25, 31, 3, 0, 0, 0);
                    gfx->drawFilledRect(x + 2, y + 26, 20, 1, 78, 187, 78);
                    gfx->drawRect(x + 21, y + 25, 4, 3, 26, 62, 26);
                    gfx->drawSprite(x + 24, y, &icons, i % 48, i % 11 == 0 ? 127 : 255);
                    gfx->finishDrawingSprite(&icons, 255);
                    if (i % 1000 == 0)
                        gfx->drawCircle(int(x) + 16, int(y) + 16, 16, 0, 0, 255);
                }
                // Dirty upload and deletion must consume the old texture before
                // changing it. Reallocate immediately to exercise ID reuse.
                for (int repeat = 0; repeat < 4; ++repeat)
                {
                    auto surface = std::make_unique<GAGCore::DrawableSurface>(16, 16);
                    surface->drawFilledRect(0, 0, 16, 16, GAGCore::Color(255, 0, 0));
                    gfx->drawSurface(40, 40, surface.get());
                    surface->drawFilledRect(0, 0, 16, 16, GAGCore::Color(0, 255, 0));
                    gfx->drawSurface(45, 45, surface.get());
                }
                // A clip change flushes old commands and disables stale bounds.
                gfx->setClipRect(20, 20, 100, 100);
                gfx->drawFilledRect(0, 0, 30, 30, GAGCore::Color(80, 20, 180, 127));
            };
            if (batched)
            {
                GAGCore::UnitDrawBatch batch(gfx);
                commands();
            }
            else commands();
            gfx->endMapTransform();
            glFinish();
            return captureBatchPixels();
        };
        const auto reference = draw(false);
        requireBatchParity(reference, draw(true));
        const char *extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (extensions && std::string(extensions).find("GL_EXT_texture_array") != std::string::npos)
            REQUIRE(gfx->getRenderBatch()->textureBytes() > 0);
    }
}
TEST_CASE("unit batch outline bounds preserve all clip edges [display]")
{
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    glob2test::ToolkitScope toolkit;
    auto *gfx = GAGCore::Toolkit::initGraphic(640, 480,
        GAGCore::GraphicContext::USEGPU, "outline batch regression");
    for (float zoom : {.0732421875f, .5f, 1.f, 1.37f, 4.f})
        for (int side = 0; side < 4; ++side)
        {
            auto draw = [&](bool batched)
            {
                gfx->setClipRect();
                gfx->drawFilledRect(0, 0, 640, 480, 17, 23, 31);
                gfx->beginMapTransform(zoom, 0, 0, 0, 0, 640, 480);
                gfx->setClipRect(100, 100, 200, 200);
                auto commands = [&]
                {
                    // Subpixel distances outside each edge catch bounds that
                    // neglect the rasterized half-width of the requested line.
                    for (int i = 0; i < 40; ++i)
                    {
                        const float gap = -1 + i * .125f;
                        const float x = side == 0 ? (300 + gap) / zoom :
                                        side == 1 ? (100 - gap) / zoom - 10 : 160 / zoom;
                        const float y = side == 2 ? (300 + gap) / zoom :
                                        side == 3 ? (100 - gap) / zoom - 10 : 160 / zoom;
                        gfx->drawRect(x, y, 10.f, 10.f, GAGCore::Color(71, 113, 197, 127));
                    }
                };
                if (batched) { GAGCore::UnitDrawBatch batch(gfx); commands(); }
                else commands();
                gfx->endMapTransform();
                glFinish();
                return captureBatchPixels();
            };
            REQUIRE(draw(false) == draw(true));
        }
}
#endif
