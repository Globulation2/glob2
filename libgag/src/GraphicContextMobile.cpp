// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphicContextPrivate.h"
#include <InterfacePresentation.h>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <cassert>
#include <HostViewport.h>
#include <ApplicationHost.h>
namespace GAGCore {
    double GraphicContext::logicalUnitsPerPoint() const
    {
        float density=1;
#ifdef __ANDROID__
        float dpi=160;
        if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window), &dpi, nullptr, nullptr)==0 && dpi>0) density=dpi/160;
#endif
        if (!windowW || !windowH || !sdlsurface) return 1;
        return density*uiScale / std::min(double(windowW)/sdlsurface->w, double(windowH)/sdlsurface->h);
    }

    bool GraphicContext::refreshPresentation()
    {
        if (window) SDL_GetWindowSize(window,&windowW,&windowH);
        // Responsive screens can scroll/reflow below the legacy 640x480 floor.
        // Honor the chosen scale before resolving fit, including first launch.
        if (compactWindowAllowed) uiScale=wantedUiScale;
        const auto old=presentationState;
        ViewportMetrics metrics;
        metrics.width=windowW; metrics.height=windowH;
        metrics.userScale=uiScale;
        metrics.safe=mobileSafeInsets(this);
        metrics.keyboardInset=mobileKeyboardInset(this);
        InputCapabilities input;
        for (int i=0;i<SDL_GetNumTouchDevices();++i)
            input.touch=input.touch || SDL_GetTouchDeviceType(SDL_GetTouchDevice(i))==SDL_TOUCH_DEVICE_DIRECT;
#if defined(__ANDROID__) || defined(__IPHONEOS__)
        input.touch=true;input.pointer=mobilePointerAvailable();input.hover=input.pointer;
#endif
#ifdef __ANDROID__
        float dpi=160;
        if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window),&dpi,nullptr,nullptr)==0 && dpi>0) {
            const double density=dpi/160;
            metrics.width/=density;metrics.height/=density;
        }
#endif
        ApplicationHost::presentationMetrics(metrics,input);
        updatePresentation(metrics,input);
        return old.layout!=presentationState.layout || old.touch!=presentationState.touch ||
            old.usable.x!=presentationState.usable.x || old.usable.y!=presentationState.usable.y ||
            old.usable.w!=presentationState.usable.w || old.usable.h!=presentationState.usable.h;
    }

    bool GraphicContext::setResponsiveViewport(bool enabled, int minimumWidth, int minimumHeight)
    {
        if (!sdlsurface) return false;
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
        if (!renderer && !enabled) { responsiveViewport=false; return false; }
        if (enabled) {
            if (windowW<=0 || windowH<=0) return enabled;
            float density = 1;
#ifdef __ANDROID__
            float dpi = 160;
            if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window), &dpi, nullptr, nullptr) == 0 && dpi > 0)
                density = dpi / 160;
#endif
            width = std::max(1, static_cast<int>(windowW / (density*uiScale)));
            height = std::max(1, static_cast<int>(windowH / (density*uiScale)));
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
        if (renderer) renderer->logicalSize(width, height);
        freeOwnedSurface();
        sdlsurface = replacement;
        ownsSurface = true;
#ifdef HAVE_OPENGL
        if (optionFlags & USEGPU) {
            Sprite::flushBatches(this);
            glMatrixMode(GL_PROJECTION); glLoadIdentity();
            glOrtho(0,width,height,0,-1,1);
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();
            SDL_GL_GetDrawableSize(window,&drawableW,&drawableH);
            applyGLViewport();
        }
#endif
        setClipRect();
        return enabled;
    }

void GraphicContext::beginSoftwareTransform()
{
    if (renderer || (optionFlags & USEGPU)) return;
    if (!softwareRasterizer) softwareRasterizer=makeSoftwareRenderBackend(sdlsurface);
    renderer=std::move(softwareRasterizer);softwareTransform=true;
}
void GraphicContext::endSoftwareTransform()
{
    if (!softwareTransform) return;
    renderer->flush();softwareRasterizer=std::move(renderer);softwareTransform=false;
}

void GraphicContext::setUITransform(float scale, float x, float y, const SDL_Rect* bounds)
{
    const bool reset=scale==1 && x==0 && y==0 && !bounds;
    if (uiTransformActive) {
        if (renderer) renderer->transform(1,0,0,nullptr);
#ifdef HAVE_OPENGL
        else if (optionFlags & USEGPU) { Sprite::flushBatches(this); glPopMatrix(); }
#endif
        endSoftwareTransform();
        uiTransformActive=false;
        setClipRect(uiSavedClip.x,uiSavedClip.y,uiSavedClip.w,uiSavedClip.h);
    }
    if (reset) return;
    assert(!mapTransformActive);
    uiSavedClip=clipRect;
    const SDL_Rect requested=bounds ? *bounds : SDL_Rect{0,0,getW(),getH()};
    SDL_IntersectRect(&uiSavedClip,&requested,&uiBounds);
    uiTransformScale=scale; uiTransformX=x; uiTransformY=y;
    beginSoftwareTransform();
    if (renderer) renderer->transform(scale,x,y,&uiBounds);
#ifdef HAVE_OPENGL
    else if (optionFlags & USEGPU) {
        Sprite::flushBatches(this);
        glPushMatrix(); glTranslatef(x,y,0); glScalef(scale,scale,1);
        setClipRect(uiBounds.x,uiBounds.y,uiBounds.w,uiBounds.h);
    }
#endif
    uiTransformActive=true;
}
}
