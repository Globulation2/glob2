// SPDX-License-Identifier: GPL-3.0-or-later

#include "TorusView.h"
#include "GlobalContainer.h"
#include "TorusGeometry.h"
#include <GraphicContext.h>
#include <algorithm>
#include <cmath>

namespace
{
float clamp(float x, float a, float b) { return std::max(a, std::min(b, x)); }
}

TorusView::TorusView()
    : target(false), amount(0), zoom(1), travelU(0), travelV(0), baseViewportX(0), baseViewportY(0),
      worldW(0), worldH(0), atlasW(0), atlasH(0),
      lastFrame(0), clouds(&globalContainer->settings), texture(0), cloudTexture(0), framebuffer(0),
      material(0), meshBuffer(0), cloudBuffer(0), indexBuffer(0), meshKey{}, failed(false), originX(0),
      originY(0), focusU(0.5f), focusV(0.5f)
{
}
TorusView::~TorusView() { releaseResources(); }

void TorusView::reset()
{
    releaseResources();
    target = moving = held = pointerHeld = failed = false;
    lastMove = 0;
    amount = travelU = travelV = cameraU = cameraV = 0;
    worldW = worldH = 0;
    lastFrame = 0;
    resetCamera();
}

bool TorusView::available() const
{
#ifdef HAVE_OPENGL
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
        if (!active())
            resetCamera();
        target = !target;
        lastFrame = SDL_GetTicks();
    }
}
void TorusView::resetCamera() { zoom = cameraZoom = 1; }
void TorusView::setHeld(bool down)
{
    if (down && !available())
        return;
    if (down && !active())
    {
        resetCamera();
        lastFrame = SDL_GetTicks();
    }
    held = down;
}

void TorusView::notifyMove()
{
    if (!available())
        return;
    if (!active())
    {
        resetCamera();
        lastFrame = SDL_GetTicks();
    }
    lastMove = SDL_GetTicks();
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
bool TorusView::event(const SDL_Event &e, int width)
{
    if (!active())
        return false;
    // The middle button pans through the ordinary 2D path in every mode, so the
    // ring moves at the speed the flat map does; only the wheel is handled here.
    if (target && e.type == SDL_MOUSEWHEEL)
    {
        int x, y;
        SDL_GetMouseState(&x, &y);
        if (x >= 0 && x < width && y >= 16 && y < globalContainer->gfx->getH())
        {
            int direction = e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
            zoom = clamp(zoom * std::pow(1.12f, float(direction)), 0.4f, 2.0f);
            if (direction > 0 && zoom >= 2.0f)
                toggle();
            return true;
        }
    }
    return false;
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
