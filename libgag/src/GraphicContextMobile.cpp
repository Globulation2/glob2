// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphicContextPrivate.h"
#include <InterfacePresentation.h>
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace GAGCore {
    double GraphicContext::logicalUnitsPerPoint() const
    {
        float density=1;
#ifdef __ANDROID__
        float dpi=160;
        if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window), &dpi, nullptr, nullptr)==0 && dpi>0) density=dpi/160;
#endif
        if (!windowW || !windowH || !sdlsurface) return 1;
        return density / std::min(double(windowW)/sdlsurface->w, double(windowH)/sdlsurface->h);
    }

    bool GraphicContext::setResponsiveViewport(bool enabled, int minimumWidth, int minimumHeight)
    {
        if (!renderer || !sdlsurface) return false;
        applyWindowMinimumSize();
        enabled = enabled && phonePresentationRequested();
#ifdef GLOB2_MOBILE
        if (!enabled) {
            enabled=true;
            minimumWidth=fixedLogicalW; minimumHeight=fixedLogicalH;
        }
#endif
        responsiveMinW=minimumWidth; responsiveMinH=minimumHeight;
        int width = fixedLogicalW, height = fixedLogicalH;
        if (enabled) {
            if (windowW<=0 || windowH<=0) return enabled;
            float density = 1;
#ifdef __ANDROID__
            float dpi = 160;
            if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window), &dpi, nullptr, nullptr) == 0 && dpi > 0)
                density = dpi / 160;
#endif
            width = std::max(1, static_cast<int>(windowW / density));
            height = std::max(1, static_cast<int>(windowH / density));
            // Retain enough room for legacy controls while extending the world
            // to the window's aspect ratio. This never stretches the artwork.
            const double expansion=std::max({1.0,double(minimumWidth)/width,double(minimumHeight)/height});
            width=static_cast<int>(std::ceil(width*expansion));
            height=static_cast<int>(std::ceil(height*expansion));
        }
        responsiveViewport = enabled;
        if (getW() == width && getH() == height) return enabled;
        SDL_Surface* replacement = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, sdlsurface->format->format);
        if (!replacement) throw std::runtime_error(SDL_GetError());
        renderer->logicalSize(width, height);
        freeOwnedSurface();
        sdlsurface = replacement;
        ownsSurface = true;
        setClipRect();
        return enabled;
    }

void GraphicContext::setUITransform(float scale, float x, float y, const SDL_Rect* bounds)
{
    if (renderer) renderer->transform(scale, x, y, bounds);
}
}
