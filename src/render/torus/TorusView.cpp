// SPDX-License-Identifier: GPL-3.0-or-later

#include "TorusView.h"
#include "GlobalContainer.h"
#include "TorusGeometry.h"
#include <GraphicContext.h>
#include <algorithm>
#include <cmath>

#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#if defined(HAVE_OPENGL)
#define GLOB2_TORUS_OPENGL
#endif

namespace
{
float clamp(float x, float a, float b) { return std::max(a, std::min(b, x)); }
}

TorusView::TorusView()
    : target(false), amount(0), travelU(0), travelV(0), baseViewportX(0), baseViewportY(0),
      worldW(0), worldH(0),
      lastFrame(0), clouds(&globalContainer->settings), cloudTexture(0), framebuffer(0),
      material(0), meshBuffer(0), cloudBuffer(0), indexBuffer(0), meshKey{}, failed(false), originX(0),
      originY(0), focusU(0.5f), focusV(0.5f)
{
}
TorusView::~TorusView() { releaseMaterials(); }

void TorusView::reset()
{
    releaseMaterials();
    target = wholeRing = moving = pointerHeld = panHeld = failed = false;
    lastMove = 0;
    amount = travelU = travelV = cameraU = cameraV = 0;
    worldW = worldH = 0;
    lastFrame = 0;
}

bool TorusView::available() const
{
#ifdef GLOB2_TORUS_OPENGL
    if (!globalContainer->gfx ||
        !(globalContainer->gfx->getOptionFlags() & GAGCore::GraphicContext::USEGPU) ||
        !SDL_GL_GetCurrentContext())
        return false;
    if (failed && graphicsGeneration == globalContainer->gfx->getGLContextGeneration())
        return false;
    int major = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
#ifdef __APPLE__
    return major >= 2 && SDL_GL_ExtensionSupported("GL_EXT_framebuffer_object");
#else
    return major >= 2 && (major >= 3 || SDL_GL_ExtensionSupported("GL_ARB_framebuffer_object"));
#endif
#else
    return false;
#endif
}
void TorusView::toggle()
{
    if (available())
    {
        target = !target;
        if (target)
            wholeRing = false;
        moving = pointerHeld = false;
        lastFrame = SDL_GetTicks();
    }
}
void TorusView::notifyMove()
{
    // The default 2D path does not query the GPU or allocate overview resources.
    if (!globalContainer->settings.automaticTorus || target || pointerHeld || !available())
        return;
    Uint32 now = SDL_GetTicks();
    if (!active())
    {
        wholeRing = true;
        lastFrame = now;
    }
    lastMove = now;
    moving = true;
}
void TorusView::setViewport(int x, int y)
{
    if (!worldW || !worldH || !active())
        return;
    auto current =
        TorusGeometry::destination(baseViewportX, baseViewportY, travelU, travelV, worldW, worldH);
    travelU += float(TorusGeometry::wrappedDelta(current.x, x, worldW)) / worldW;
    travelV += float(TorusGeometry::wrappedDelta(current.y, y, worldH)) / worldH;
    travelU -= std::floor(travelU);
    travelV -= std::floor(travelV);
}
void TorusView::rebaseViewport(int x, int y)
{
    if (!worldW || !worldH || !active())
        return;
    auto current =
        TorusGeometry::destination(baseViewportX, baseViewportY, travelU, travelV, worldW, worldH);
    baseViewportX = (baseViewportX + TorusGeometry::wrappedDelta(current.x, x, worldW)) & (worldW - 1);
    baseViewportY = (baseViewportY + TorusGeometry::wrappedDelta(current.y, y, worldH)) & (worldH - 1);
}

bool TorusView::pick(int x, int y, int &px, int &py) const
{
    if (!active() || !worldW || !worldH || x < 0 || y < 16 || x >= pickWidth || y >= pickHeight)
        return false;
    if (x != cachedPickX || y != cachedPickY)
    {
        cachedPick = TorusPicking::Hit{};
        cachedPickFound = TorusPicking::mesh(vertices, meshColumns, meshRows, x, y, cachedPick);
        cachedPickX = x;
        cachedPickY = y;
    }
    if (!cachedPickFound)
        return false;
    px = TorusPicking::worldPixel(pickU + cachedPick.u, originX, worldW);
    py = TorusPicking::worldPixel(pickV - cachedPick.v, originY, worldH);
    return true;
}
