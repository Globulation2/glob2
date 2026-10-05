// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <GestureScroll.h>
#include <GameplayRecording.h>
#include <PerformanceTelemetry.h>
#include "GraphicContextPrivate.h"
#include <SoftwareFramePresenter.h>
#include <UiScale.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <SupportFunctions.h>
#include <InterfacePresentation.h>
#include <BrowserTextInput.h>
#include <ApplicationHost.h>
#include <algorithm>
#include <assert.h>
#include <cstdlib>
#include <cstring>
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>

namespace GAGCore
{
	// Storage for the static globals declared in graphic_context_private.h.
	GraphicContext *_gc = NULL;
	SDL_PixelFormatDetails _glFormat = *SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_ARGB8888);
	const bool EXPERIMENTAL = false;

#ifdef HAVE_OPENGL
	GLState glState;

	void GLState::checkExtensions(void)
	{
		const char *glExtensions = (const char *)glGetString(GL_EXTENSIONS);
		isTextureSRectangle = (strstr(glExtensions, "GL_NV_texture_rectangle") != NULL);
		isTextureSRectangle = isTextureSRectangle || (strstr(glExtensions, "GL_EXT_texture_rectangle") != NULL);
		isTextureSRectangle = isTextureSRectangle || (strstr(glExtensions, "GL_ARB_texture_rectangle") != NULL);

		// A single normalized texture target supports both legacy atlases and HD mipmaps.
		isTextureSRectangle = false;
		hasS3TCCompression = (strstr(glExtensions, "GL_EXT_texture_compression_s3tc") != NULL)
			&& !std::getenv("GLOB2_DISABLE_S3TC");
		const char *glVendor = (const char *)glGetString(GL_VENDOR);
		if (strstr(glVendor, "ATI"))
			useATIWorkaround = true; // ugly temporary bug fix for bug 13823. We think it is an ATI driver bug

		if (verbose)
		{
			if (isTextureSRectangle)
			{
				std::cout << "Toolkit : GL_NV_texture_rectangle or GL_EXT_texture_rectangle extension present, optimal texture size will be used" << std::endl;
			} else {
				std::cout << "Toolkit : GL_NV_texture_rectangle or GL_EXT_texture_rectangle extension not present, power of two texture will be used" << std::endl;
			}
		}
	}

	bool GLState::doBlend(bool on)
	{
		if (_doBlend == on)
			return on;
		if (on)
			glEnable(GL_BLEND);
		else
			glDisable(GL_BLEND);
		_doBlend = on;
		return !on;
	}

	bool GLState::doTexture(bool on)
	{
		if (_doTexture == on)
			return on;
		GLenum cap;
		if (isTextureSRectangle)
			cap = GL_TEXTURE_RECTANGLE_NV;
		else
			cap = GL_TEXTURE_2D;

		if (on)
			glEnable(cap);
		else
			glDisable(cap);
		_doTexture = on;
		return !on;
	}

	void GLState::setTexture(int tex)
	{
		if (_texture == tex)
			return;

		if (isTextureSRectangle)
		{
			if (useATIWorkaround)
				glBindTexture(GL_TEXTURE_RECTANGLE_NV, 0);
			glBindTexture(GL_TEXTURE_RECTANGLE_NV, tex);
		}
		else
			glBindTexture(GL_TEXTURE_2D, tex);
		_texture = tex;
	}

	bool GLState::doScissor(bool on)
	{
		// The glIsEnabled function is quite expensive. That's why we have a _doScissor variable.
		if (_doScissor == on)
			return on;

		if (on)
			glEnable(GL_SCISSOR_TEST);
		else
			glDisable(GL_SCISSOR_TEST);
		_doScissor = on;
		return !on;
	}

	void GLState::blendFunc(GLenum sfactor, GLenum dfactor)
	{
		if ((sfactor == _sfactor) && (dfactor == _dfactor))
			return;

		glBlendFunc(sfactor, dfactor);

		_sfactor = sfactor;
		_dfactor = dfactor;
	}
#endif // HAVE_OPENGL

	// Color
	Uint32 Color::pack() const
	{
		return SDL_MapRGBA(&_glFormat, nullptr, r, g, b, a);
	}

	void Color::unpack(const Uint32 packedValue)
	{
		SDL_GetRGBA(packedValue, &_glFormat, nullptr, &r, &g, &b, &a);
	}

	void Color::getHSV(float *hue, float *sat, float *lum)
	{
		RGBtoHSV(static_cast<float>(r)/255.0f, static_cast<float>(g)/255.0f, static_cast<float>(b)/255.0f, hue, sat, lum);
	}

	void Color::setHSV(float hue, float sat, float lum)
	{
		float fr, fg, fb;
		HSVtoRGB(&fr, &fg, &fb, hue, sat, lum);
		r = static_cast<Uint8>(255.0f*fr);
		g = static_cast<Uint8>(255.0f*fg);
		b = static_cast<Uint8>(255.0f*fb);
	}

	Color Color::applyMultiplyAlpha(Uint8 _a) const
	{
		Color c;
		c.r = r;
		c.g = g;
		c.b = b;
		c.a = _a;
		return c;
	}

	// Predefined colors
	Color Color::black = Color(0, 0, 0);
	Color Color::white = Color(255, 255, 255);

	// GraphicContext lifecycle and window management

	void GraphicContext::setMinRes(int w, int h)
	{
		minW = w;
		minH = h;
		if (window) applyWindowMinimumSize();
	}

	// The minimum is a floor on the *logical* surface, which is the window divided
	// by the interface scale -- so the window's own minimum has to be the scaled
	// one. Without this, a window dragged to 640x480 at scale 1.75 lays the
	// interface out on 366x274, narrower than the 368px main menu panel.
	void GraphicContext::applyWindowMinimumSize(void)
	{
        if (window && (compactWindowAllowed || (renderer && phonePresentationRequested()))) { SDL_SetWindowMinimumSize(window,1,1); return; }
		if (!window) return;
		#ifdef GLOB2_WEBGL2
		return;
		#endif
		SDL_SetWindowMinimumSize(window,
			std::max(1, static_cast<int>(minW * uiScale + 0.5f)),
			std::max(1, static_cast<int>(minH * uiScale + 0.5f)));
	}

	VideoModes GraphicContext::listVideoModes() const
	{
		VideoModes modes;

        int displayCount = 0;
        SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);
        if (!displays) return modes;
        for (int i = 0; i < displayCount; ++i) {
            int count = 0;
            SDL_DisplayMode **available = SDL_GetFullscreenDisplayModes(displays[i], &count);
            if (!available) continue;
            for (int j = 0; j < count; ++j)
                if (available[j]->w >= minW && available[j]->h >= minH) modes.push_back(*available[j]);
            SDL_free(available);
        }
        SDL_free(displays);

		return modes;
	}

	GraphicContext::GraphicContext(int w, int h, Uint32 flags, const std::string title, const std::string icon):
		windowTitle(title),
		appIcon(icon)
	{
		// some assert on the universe's structure
		assert(sizeof(Color) == 4);

		minW = minH = 0;
		sdlsurface = NULL;
		optionFlags = DEFAULT;

		// Request native Mac momentum before Cocoa registers application defaults.
		SDL_SetHint(SDL_HINT_MAC_SCROLL_MOMENTUM, "1");
		// Load the SDL library
		if ( !SDL_Init(SDL_INIT_VIDEO) )
		{
			fprintf(stderr, "Toolkit : Initialisation Error : %s\n", SDL_GetError());
			exit(1);
		}
		else
		{
			if (verbose)
				fprintf(stderr, "Toolkit : Initialized : Graphic Context created\n");
		}


        if (!(flags & NOAUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
            SDL_Log("Audio unavailable: %s", SDL_GetError());
		if (!TTF_Init()) {
			SDL_Log("Font initialization failed: %s", SDL_GetError());
			exit(1);
		}

		///If setting the given resolution fails, default to 800x600
		if(!setRes(w, h, flags))
		{
			fprintf(stderr, "Toolkit : Can't set screen resolution, resetting to default of 800x600\n");
			if (!setRes(800,600,flags)) {
				fprintf(stderr, "Toolkit : Initial window could not be created, quitting.\n");
				exit(1);
			}
		}
	}

	GraphicContext::~GraphicContext(void)
	{
		// must run before SDL_Quit(): ~CursorManager() runs too late, after this
		// destructor's body, and SDL_DestroyCursor() after SDL_Quit() is undefined
		cursorManager.releaseNativeCursor();
		renderer = nullptr;
        portableRenderer.reset();
		if (watchingEvents) SDL_RemoveEventWatch(watchWindow, this);
        watchingEvents = false;
		releaseFrameCache();
		freeOwnedSurface();
#ifdef HAVE_OPENGL
		if (context) { destroySkinRenderer(); destroyUnitShader(); }
#endif
		if (context) SDL_GL_DestroyContext(context);
		removeMacScrollMonitor();
		if (window) SDL_DestroyWindow(window);
		window = nullptr;
		_gc = nullptr;
		TTF_Quit();
		SDL_Quit();

		if (verbose)
			fprintf(stderr, "Toolkit : Graphic Context destroyed\n");
	}

	bool GraphicContext::isScalingActive(void)
	{
		return windowW && sdlsurface && (windowW != getW() || windowH != getH());
	}

	float GraphicContext::textRenderScale(void)
	{
        // GPU and native software backends rasterize glyphs at output density.
        // Include the local UI transform used by enlarged touch controls.
        // Unscaled software surfaces retain their authored raster size.
        if (!renderer && !(optionFlags & USEGPU)) return 1.0f;
        const float outputScale = (softwareTransform || offscreenPass) ? 1.0f : drawableScale();
        const float scale = outputScale * (uiTransformActive ? uiTransformScale : 1.0f);
        if (std::fabs(scale - 1.0f) < 0.01f) return 1.0f;
        return std::clamp(scale, 0.25f, 8.0f);
	}

	float GraphicContext::requestedUiScale = 0.0f;

	namespace
	{
		float scaleFromEnv(const char *name)
		{
			const char *value = getenv(name);
			if (!value || !*value)
				return 0.0f;
			const float scale = static_cast<float>(atof(value));
			return (scale >= 0.5f && scale <= 8.0f) ? scale : 0.0f;
		}


	}

	void GraphicContext::refreshDesktopScale()
	{
		desktopDisplay = SDL_GetDisplayForWindow(window);
		desktopSystemScale = windowUiScale(SDL_GetWindowDisplayScale(window),
			SDL_GetWindowPixelDensity(window), 0);
	}

	float GraphicContext::querySystemUiScale()
	{
		if (!_gc || !_gc->window) return 1.0f;
		return windowUiScale(SDL_GetWindowDisplayScale(_gc->window),
			SDL_GetWindowPixelDensity(_gc->window), 0);
	}

    float GraphicContext::effectiveUiScale(float preferred)
    {
        return windowUiScale(querySystemUiScale(), 1.0f, preferred,
                             scaleFromEnv("GLOB2_UI_SCALE"));
    }

	void GraphicContext::freeOwnedSurface(void)
	{
        if (softwareTransform)
        {
            if (softwareRasterizer) softwareRasterizer->flush();
            renderer = nullptr;
            softwareTransform = false;
            mapTransformActive = uiTransformActive = false;
            mapScale = 1;
        }
        // Both native-display and transformed passes borrow this owner.
        // Invalidate the active pointer before destroying its target/backend.
        if (renderer == softwareRasterizer.get()) renderer = nullptr;
        softwareRasterizer.reset();
        // Retain the last completed image across a resize until the replacement
        // framebuffer completes its first frame. No retention allocation is needed.
        if (watchingEvents && softwarePresenter)
        {
            if (auto* completed = softwarePresenter->takeCompleted())
            {
                SDL_DestroySurface(frameCache.surface);
                frameCache.surface = completed;
                frameCache.valid = true;
            }
        }
        softwarePresenter.reset();
		if (ownsSurface && sdlsurface)
			SDL_DestroySurface(sdlsurface);
		sdlsurface = NULL;
		ownsSurface = false;
	}

	float GraphicContext::drawableScale(void)
	{
		if (!drawableW || !sdlsurface)
			return 1.0f;
		return std::min(static_cast<float>(drawableW) / getW(), static_cast<float>(drawableH) / getH());
	}

	float GraphicContext::rasterScale(void)
	{
		// An offscreen pass rasterises into its own target, whose pixels have
		// nothing to do with the window the logical surface is stretched into.
		if (renderTargetScale > 0.0f)
			return renderTargetScale;
		return drawableScale();
	}

	void GraphicContext::glLetterbox(float &scale, int &offX, int &offY)
	{
		scale = drawableScale();
		if (nativeDesktop) { offX=offY=0; return; }
		offX = (drawableW - static_cast<int>(sdlsurface->w * scale + 0.5f)) / 2;
		offY = (drawableH - static_cast<int>(sdlsurface->h * scale + 0.5f)) / 2;
	}

	void GraphicContext::applyGLViewport(void)
	{
		#ifdef HAVE_OPENGL
		float scale;
		int offX, offY;
		glLetterbox(scale, offX, offY);
		if (nativeDesktop) glViewport(0, 0, drawableW, drawableH);
		else glViewport(offX, offY, static_cast<int>(sdlsurface->w * scale + 0.5f), static_cast<int>(sdlsurface->h * scale + 0.5f));
		#endif
	}

	void GraphicContext::updateWindowSize(void)
	{
		if (!window || !sdlsurface) return;
		if (nativeDesktop) { refreshNativeWindow(); return; }
		SDL_GetWindowSize(window, &windowW, &windowH);
        const float newScale = effectiveUiScale(requestedUiScale);
        const bool scaleChanged = newScale != wantedUiScale;
        if (scaleChanged) {
            wantedUiScale = uiScale = newScale;
            applyWindowMinimumSize();
        }
		drawableW = windowW;
		drawableH = windowH;
        if (responsiveViewport) {
            if (renderer) renderer->outputSize(drawableW, drawableH);
            setResponsiveViewport(true, responsiveMinW, responsiveMinH);
            return;
        }
		if (windowW <= 0 || windowH <= 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return;
		// A resizable window keeps the interface at its scale: the logical surface
		// follows the window divided by uiScale, not the window itself.
		const int logicalW = std::max(1, static_cast<int>(windowW / uiScale + 0.5f));
		const int logicalH = std::max(1, static_cast<int>(windowH / uiScale + 0.5f));
		if (((optionFlags & RESIZABLE) || (optionFlags & FULLSCREEN) || scaleChanged)
			&& (getW() != logicalW || getH() != logicalH))
		{
			SDL_Surface *resized = SDL_CreateSurface(logicalW, logicalH, SDL_PIXELFORMAT_ARGB8888);
			if (!resized) return;
			if (!(optionFlags & FULLSCREEN)) {
				requestedW = windowW;
				requestedH = windowH;
			}
			freeOwnedSurface();
			sdlsurface = resized;
			ownsSurface = true;
			#ifdef HAVE_OPENGL
			if (optionFlags & USEGPU)
			{
				glMatrixMode(GL_PROJECTION);
				glLoadIdentity();
				glOrtho(0, getW(), getH(), 0, -1, 1);
				glMatrixMode(GL_MODELVIEW);
				glLoadIdentity();
			}
			#endif
			if (renderer) renderer->logicalSize(logicalW, logicalH);
			setClipRect();
		}
		if (renderer) renderer->outputSize(drawableW, drawableH);
		#ifdef HAVE_OPENGL
		if (optionFlags & USEGPU)
		{
			SDL_GetWindowSizeInPixels(window, &drawableW, &drawableH);
			applyGLViewport();
		}
		#endif
	}

	void GraphicContext::windowToLogical(float &x, float &y)
	{
		if (nativeDesktop && windowW > 0 && windowH > 0)
		{
			x = std::clamp(x * getW() / windowW, 0.0f, float(getW()-1));
			y = std::clamp(y * getH() / windowH, 0.0f, float(getH()-1));
			return;
		}
		if (!isScalingActive())
			return;
		// letterboxed the same way as the GL viewport / software blit, but in
		// window points rather than drawable pixels or window-surface pixels
		float scale = std::min(static_cast<float>(windowW) / sdlsurface->w, static_cast<float>(windowH) / sdlsurface->h);
		const float offX = (windowW - sdlsurface->w * scale) / 2.0f;
		const float offY = (windowH - sdlsurface->h * scale) / 2.0f;
		x = (x - offX) / scale;
		y = (y - offY) / scale;
		if (x < 0)
			x = 0;
		else if (x >= sdlsurface->w)
			x = sdlsurface->w - 1;
		if (y < 0)
			y = 0;
		else if (y >= sdlsurface->h)
			y = sdlsurface->h - 1;
	}

    namespace {
        bool confirmedFullscreen(SDL_Window *window, bool fullscreen)
        {
            // Cocoa can complete fullscreen-space changes in the event pump.
            const Uint64 deadline = SDL_GetTicks() + 3000;
            while (bool(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != fullscreen &&
                   SDL_GetTicks() < deadline) {
                SDL_PumpEvents();
                SDL_Delay(10);
            }
            return bool(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == fullscreen;
        }
    }

    bool GraphicContext::setFullscreen(bool fullscreen)
    {
        if (!window) return false;
        const bool previous = bool(optionFlags & FULLSCREEN);
        if (previous == fullscreen) return true;
        if (!previous) SDL_GetWindowSize(window, &requestedW, &requestedH);
        const bool previousResizable = (SDL_GetWindowFlags(window) & SDL_WINDOW_RESIZABLE) ? true : false;
        // Cocoa fullscreen spaces require a resizable window. This temporary
        // window style does not change the saved windowed resize preference.
        if (nativeDesktop && fullscreen) SDL_SetWindowResizable(window, true);
        if (!SDL_SetWindowFullscreen(window, fullscreen)) {
            SDL_SetWindowResizable(window, previousResizable);
            return false;
        }
        bool applied = !nativeDesktop || confirmedFullscreen(window, fullscreen);
        if (applied) {
            if (fullscreen) optionFlags |= FULLSCREEN; else optionFlags &= ~FULLSCREEN;
            if (!fullscreen) {
                SDL_SetWindowResizable(window, (optionFlags & RESIZABLE) ? true : false);
                SDL_SetWindowSize(window, requestedW, requestedH);
            }
            // Fullscreen and its restoration resize can outlive the flag change.
            // Read and persist dimensions only after both requests have settled.
            if (nativeDesktop) applied = SDL_SyncWindow(window) && refreshNativeWindow();
        }
        if (!applied) {
            if (previous) optionFlags |= FULLSCREEN; else optionFlags &= ~FULLSCREEN;
            SDL_SetWindowResizable(window, previousResizable);
            SDL_SetWindowFullscreen(window, previous);
            updateWindowSize();
            return false;
        }
        if (!nativeDesktop) updateWindowSize();
        return true;
    }

    bool GraphicContext::toggleFullscreen()
    {
        return setFullscreen(!(optionFlags & FULLSCREEN));
    }

    bool GraphicContext::setUiScale(float scale)
    {
        const float previous = preferredUiScale;
        preferredUiScale = scale;
        if (nativeDesktop)
        {
            if (!refreshNativeWindow()) { preferredUiScale = previous; return false; }
        }
        else
        {
            const float previousScale=uiScale, previousWanted=wantedUiScale;
            uiScale=wantedUiScale=effectiveUiScale(scale);
            try
            {
                if (responsiveViewport) setResponsiveViewport(true,responsiveMinW,responsiveMinH);
                else if (!resizeViewport(windowW,windowH)) throw std::runtime_error(SDL_GetError());
            }
            catch (const std::exception &error)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Interface scale: %s",error.what());
                uiScale=previousScale; wantedUiScale=previousWanted; preferredUiScale=previous;
                return false;
            }
        }
        requestedUiScale = scale;
        return true;
    }

	void GraphicContext::translateMouseEvent(SDL_Event *event)
	{
		if (auto sample = scrollGesture(*event))
		{
			if (_gc && !sample->logical)
			{
				float x = float(sample->x), y = float(sample->y);
				_gc->windowToLogical(x, y);
				sample->x = x; sample->y = y;
				// Deltas use the inverse window transform, without its letterbox
				// translation. Layout points are not window points at UI scale > 1.
				if (_gc->nativeDesktop && _gc->windowW > 0 && _gc->windowH > 0)
				{
					sample->dx *= double(_gc->getW()) / _gc->windowW;
					sample->dy *= double(_gc->getH()) / _gc->windowH;
				}
				else if (_gc->isScalingActive())
				{
					const double scale = std::min(double(_gc->windowW) / _gc->sdlsurface->w,
						double(_gc->windowH) / _gc->sdlsurface->h);
					sample->dx /= scale;
					sample->dy /= scale;
				}
				sample->logical = true;
				*event = gestureScrollEvent(*sample);
			}
			return;
		}
		if (!_gc)
			return;
		switch (event->type)
		{
            case SDL_EVENT_KEY_DOWN:
                if(event->key.key==SDLK_F11 && !event->key.repeat)_gc->toggleFullscreen();
                break;
            case SDL_EVENT_RENDER_DEVICE_RESET:
            case SDL_EVENT_RENDER_TARGETS_RESET:
                if (_gc->renderer) _gc->renderer->reset();
                break;
			case SDL_EVENT_MOUSE_MOTION:
                // SDL3 keeps all renderer events in window coordinates. Convert
                // positions and deltas once at this shared input boundary.
                if (_gc->windowW > 0 && _gc->windowH > 0)
                {
                    if (_gc->nativeDesktop) {
                        event->motion.xrel *= float(_gc->getW()) / _gc->windowW;
                        event->motion.yrel *= float(_gc->getH()) / _gc->windowH;
                    } else if (_gc->isScalingActive()) {
                        const float scale = std::min(float(_gc->windowW) / _gc->getW(),
                            float(_gc->windowH) / _gc->getH());
                        event->motion.xrel /= scale;
                        event->motion.yrel /= scale;
                    }
                }
                _gc->windowToLogical(event->motion.x, event->motion.y);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                _gc->windowToLogical(event->button.x, event->button.y);
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                _gc->windowToLogical(event->wheel.mouse_x, event->wheel.mouse_y);
                break;
			case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
				#ifndef GLOB2_WEBGL2
				if (event->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) _gc->desktopDisplay = -1;
				if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED || event->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
					_gc->updateWindowSize();
				#endif
				break;
			default:
				break;
		}
	}

    void GraphicContext::translateMouseCoordinates(float &x, float &y)
    {
        if (_gc) _gc->windowToLogical(x, y);
    }

	void GraphicContext::translateMouseCoordinates(int &x, int &y)
	{
		if (_gc)
		{
			float sx = x, sy = y;
			_gc->windowToLogical(sx, sy);
			x = sx;
			y = sy;
		}
	}

    bool GraphicContext::resizeViewport(int w, int h)
    {
        if (!window || !sdlsurface || w <= 0 || h <= 0) return false;
        if (nativeDesktop) { SDL_SetWindowSize(window,w,h); return refreshNativeWindow(); }
        const int logicalW = std::max(1, static_cast<int>(w / uiScale + 0.5f));
        const int logicalH = std::max(1, static_cast<int>(h / uiScale + 0.5f));
        if (w == windowW && h == windowH && logicalW == getW() && logicalH == getH()) return true;
        SDL_Surface* replacement = SDL_CreateSurface(logicalW, logicalH, SDL_PIXELFORMAT_ARGB8888);
        if (!replacement) return false;
        // This API updates presentation immediately; SDL3 resize requests can
        // otherwise still be pending when we read the window geometry below.
        if (!SDL_SetWindowSize(window, w, h) || !SDL_SyncWindow(window)) {
            SDL_DestroySurface(replacement);
            return false;
        }
        // SDL may invalidate its borrowed window surface when changing size.
        freeOwnedSurface();
        requestedW = w;
        requestedH = h;
        sdlsurface = replacement;
        ownsSurface = true;
        SDL_GetWindowSize(window, &windowW, &windowH);
        drawableW = windowW; drawableH = windowH;
#ifdef HAVE_OPENGL
        if (optionFlags & USEGPU) {
            SDL_GetWindowSizeInPixels(window, &drawableW, &drawableH);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0, logicalW, logicalH, 0, -1, 1);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            applyGLViewport();
        }
#endif
        if (renderer) { renderer->logicalSize(logicalW,logicalH); renderer->outputSize(drawableW,drawableH); }
        setClipRect();
        return true;
    }

	bool GraphicContext::setRes(int w, int h, Uint32 flags)
	{
        resetRenderPacing();
		// check dimension
		if (minW && (w < minW))
		{
			if (verbose)
				fprintf(stderr, "Toolkit : Screen width %d is too small, set to min %d\n", w, minW);
			w = minW;
		}
		if (minH && (h < minH))
		{
			if (verbose)
				fprintf(stderr, "Toolkit : Screen height %d is too small, set to min %d\n", h, minH);
			h = minH;
		}

		requestedW = w;
		requestedH = h;
		preferredUiScale = requestedUiScale;
		desktopLogicalW = desktopLogicalH = 0;
		nativeSoftware = false;
		desktopDisplay = -1;
		// The window keeps the requested size while the interface is laid out on a
		// smaller logical surface that is scaled back up to fill it. Widgets, fonts and
		// the map all keep their pixel sizes, so every screen grows by the same factor
		// without touching any of the layout constants they are written in.
		wantedUiScale = uiScale = effectiveUiScale(requestedUiScale);
		// Widget layouts are authored against 640x480, and setMinRes() has not run yet
		// on the context's first setRes(), so hold that floor here regardless. Only the
		// scale is reduced: a window genuinely smaller than the floor keeps scale 1.
		const int floorW = std::max(minW, 640), floorH = std::max(minH, 480);
		// One factor for both axes, so scaling never changes the aspect ratio.
		if (w < static_cast<int>(floorW * uiScale))
			uiScale = static_cast<float>(w) / floorW;
		if (h < static_cast<int>(floorH * uiScale))
			uiScale = static_cast<float>(h) / floorH;
		uiScale = std::max(std::min(1.0f, wantedUiScale), uiScale);
		int logicalW = std::max(1, static_cast<int>(w / uiScale + 0.5f));
		int logicalH = std::max(1, static_cast<int>(h / uiScale + 0.5f));

		// set flags
        const char* selectedRenderer = SDL_getenv_unsafe("GLOB2_RENDERER");
        if (selectedRenderer && std::string(selectedRenderer) == "sdl") flags |= PORTABLEGPU;
#ifdef GLOB2_MOBILE
        // Android's SDL3 renderer requires an active Java Activity even with
        // dummy video. Keep native shell tools on the existing software path.
        const char *videoDriver = SDL_GetCurrentVideoDriver();
        if (!videoDriver || std::string(videoDriver) != "dummy") flags |= PORTABLEGPU;
        flags |= RESIZABLE;
#endif
        if (flags & PORTABLEGPU) flags &= ~USEGPU;
#if !defined(GLOB2_MOBILE) && !defined(__EMSCRIPTEN__)
        // The portable backend retains the mobile/browser viewport contract.
        nativeDesktop = !(flags & PORTABLEGPU);
#endif
		optionFlags = flags;
        fixedLogicalW=logicalW; fixedLogicalH=logicalH; responsiveViewport=false;
		SDL_WindowFlags sdlFlags = (flags & LOWPIXELDENSITY) ? 0 : SDL_WINDOW_HIGH_PIXEL_DENSITY;
		if (flags & FULLSCREEN)
			// Desktop fullscreen, not exclusive: Wayland can't modeswitch to a non-native mode.
			sdlFlags |= SDL_WINDOW_FULLSCREEN;
		if ((flags & RESIZABLE) || (nativeDesktop && (flags & FULLSCREEN)))
			sdlFlags |= SDL_WINDOW_RESIZABLE;
		#ifdef HAVE_OPENGL
		if (flags & USEGPU)
		{
			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
			SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
#ifdef GLOB2_WEBGL2
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
			sdlFlags |= SDL_WINDOW_OPENGL;
		}
		#else
		// remove GL from options
		optionFlags &= ~USEGPU;
		#endif

		// if window exists, delete it
		if (watchingEvents) SDL_RemoveEventWatch(watchWindow, this);
		watchingEvents = false;
		releaseFrameCache();
#ifdef HAVE_OPENGL
		if (context) { destroySkinRenderer(); destroyUnitShader(); }
#endif
		if (context) SDL_GL_DestroyContext(context);
		context = nullptr;
		renderer = nullptr;
        portableRenderer.reset();
		freeOwnedSurface();
		if (window) {
			removeMacScrollMonitor();
			SDL_DestroyWindow(window);
			window = nullptr;
		}
		// create the new window and the surface
		window = SDL_CreateWindow(windowTitle.c_str(), w, h, sdlFlags);
		if (!window)
		{
			fprintf(stderr, "Toolkit : can't create window %dx%d\n", w, h);
			fprintf(stderr, "Toolkit : %s\n", SDL_GetError());
			return false;
		}
		// With SDL_WINDOW_FULLSCREEN the window keeps the desktop size,
		// so the actual pixel size can differ from the requested logical w x h.
		SDL_GetWindowSize(window, &windowW, &windowH);
        const float newScale = effectiveUiScale(requestedUiScale);
        if (newScale != wantedUiScale) {
            wantedUiScale = uiScale = newScale;
            applyWindowMinimumSize();
        }
        // The new window can be on a different display from the old context.
        // Allocate its initial surface using that display's scale even when the
        // window is fixed-size and updateWindowSize will preserve its geometry.
        logicalW = std::max(1, static_cast<int>(w / uiScale + 0.5f));
        logicalH = std::max(1, static_cast<int>(h / uiScale + 0.5f));
        fixedLogicalW = logicalW; fixedLogicalH = logicalH;
		drawableW = windowW;
		drawableH = windowH;
		if (flags & PORTABLEGPU) {
            portableRenderer = makeSDLRenderBackend(window, logicalW, logicalH);
            renderer = portableRenderer.get();
            if (!renderer) {
                std::cerr << "Cannot initialize portable renderer: " << SDL_GetError() << std::endl;
                return false;
            }
            renderer->outputSize(drawableW, drawableH);
        }
		applyWindowMinimumSize();
		// Own the drawing surface: SDL invalidates its window surface during resizing.
		sdlsurface = SDL_CreateSurface(logicalW, logicalH, SDL_PIXELFORMAT_ARGB8888);
		ownsSurface = true;
		if (!sdlsurface)
		{
			fprintf(stderr, "Toolkit : can't get surface for %dx%d at 32 bpp\n", logicalW, logicalH);
			fprintf(stderr, "Toolkit : %s\n", SDL_GetError());
			return false;
		}
		{
			_gc = this;
			// Use the effective flags: software-only builds clear USEGPU above.
			if (optionFlags & USEGPU)
			{
				context = SDL_GL_CreateContext(window);
				if (!context || !SDL_GL_MakeCurrent(window, context))
				{
					fprintf(stderr, "OpenGL context failed: %s\n", SDL_GetError());
					if (context) SDL_GL_DestroyContext(context);
					context = nullptr;
					return false;
				}
				++glContextGeneration;
                ApplicationHost::initializeOpenGLContext();
				#ifdef HAVE_OPENGL
				// The new context starts at the GL defaults, so a cache still describing the
				// replaced one would skip the enables the next draw call needs.
				glState.resetCache();
				// Map logical coordinates onto native drawable pixels. The legacy mobile
				// viewport contract still retains its aspect-correct sub-rectangle.
				// The drawable is in pixels; on HiDPI it is larger than the window points mouse events use.
				SDL_GetWindowSizeInPixels(window, &drawableW, &drawableH);
				applyGLViewport();
				#endif
			}
            _glFormat = *SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_ARGB8888);

			#ifdef HAVE_OPENGL
			if (optionFlags & USEGPU)
			{
				glState.checkExtensions();
				// A failed compile/link logs once and leaves hasUnitShader() false;
				// callers fall back to the CPU team-color cache for this context's
				// lifetime.
				createUnitShader();
			}
			#endif // HAVE_OPENGL

			// setup title and icon
			if (!appIcon.empty())
			{
				SDL_Surface *iconSurface = IMG_Load(appIcon.c_str());
				SDL_SetWindowIcon(window, iconSurface);
				SDL_DestroySurface(iconSurface);
			}

			updateWindowSize();
			setClipRect();
			if (flags & CUSTOMCURSOR)
				// cursorManager installs its cursors as native ones (see
				// CursorManager::update()), so the system cursor stays on
				cursorManager.load();
			else
				cursorManager.releaseNativeCursor();
			SDL_ShowCursor();

			if (verbose)
				fprintf(stderr,
					(flags & FULLSCREEN)
					?"Toolkit : Screen set to %dx%d at 32 bpp in fullscreen\n"
					:"Toolkit : Screen set to %dx%d at 32 bpp in window\n",
					w, h);

			#ifdef HAVE_OPENGL
			if (optionFlags & USEGPU)
			{
				glMatrixMode(GL_PROJECTION);
				glLoadIdentity();
				glOrtho(0, getW(), getH(), 0, -1, 1);
				glMatrixMode(GL_MODELVIEW);
				glLoadIdentity();
				glGetIntegerv(GL_MAX_TEXTURE_SIZE, &frameCache.maximumTextureSize);
				glEnable(GL_LINE_SMOOTH);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				glState.doTexture(true);
				glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}
			#endif

            if (nativeDesktop && (flags & FULLSCREEN) && !confirmedFullscreen(window, true)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Fullscreen was not applied; restoring windowed mode");
                SDL_SetWindowFullscreen(window,0);
                SDL_SetWindowResizable(window,(flags & RESIZABLE) ? true : false);
                SDL_SetWindowSize(window,requestedW,requestedH);
                optionFlags &= ~FULLSCREEN;
            }
			installMacScrollMonitor(window);
			eventThread = SDL_GetCurrentThreadID();
			if (nativeDesktop && !refreshNativeWindow()) return false;
			if (!renderer || nativeSoftware) {
				SDL_AddEventWatch(watchWindow, this);
				watchingEvents = true;
			}
			return true;
		}
	}

	void GraphicContext::captureRecordingFrame()
	{
		auto &capture = Recording::recorder();
		if (!Recording::supported())
			return;
		const auto state = capture.status().state;
		const char *suffix =
			state == Recording::State::Starting || state == Recording::State::Recording
				? " — Recording"
			: state == Recording::State::Finalizing ? " — Finalizing recording"
			: state == Recording::State::Failed ? " — Recording failed (see recording controls/log)"
												: "";
		const auto title = windowTitle + suffix;
		if (window && title != SDL_GetWindowTitle(window))
			SDL_SetWindowTitle(window, title.c_str());
		if (!capture.wantsFrame())
			return;
		PERF_SCOPE_TIME(RecordingCapture);
		try
		{
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(nullptr,
																			   SDL_DestroySurface);
			if (renderer)
			{
				renderer->flush();
				pixels.reset(renderer->capture());
			}
#if defined(HAVE_OPENGL)
			else if (optionFlags & USEGPU)
			{
				GLint viewport[4], alignment, rowLength;
				glGetIntegerv(GL_VIEWPORT, viewport);
				if (viewport[2] <= 0 || viewport[3] <= 0)
					return;
				pixels.reset(SDL_CreateSurface(viewport[2], viewport[3], SDL_PIXELFORMAT_RGBA32));
				if (!pixels)
					throw std::runtime_error(SDL_GetError());
				glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
#if !defined(GLOB2_WEBGL2)
				glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
#endif
				glPixelStorei(GL_PACK_ALIGNMENT, 1);
#if !defined(GLOB2_WEBGL2)
				glPixelStorei(GL_PACK_ROW_LENGTH, pixels->pitch / 4);
#endif
				glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGBA,
							 GL_UNSIGNED_BYTE, pixels->pixels);
				glPixelStorei(GL_PACK_ALIGNMENT, alignment);
#if !defined(GLOB2_WEBGL2)
				glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
#endif
			}
#endif
			else
			{ capture.frame(*sdlsurface); return; }
			if (!pixels)
				throw std::runtime_error(SDL_GetError());
			capture.frame(*pixels, !renderer && (optionFlags & USEGPU));
		}
		catch (const std::exception &error)
		{
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Recording frame capture: %s", error.what());
			capture.abort(error.what());
		}
	}

	void GraphicContext::nextFrame(void)
	{
        endBrowserTextFrame();
		DrawableSurface::nextFrame();
		if (sdlsurface)
		{
			if (optionFlags & CUSTOMCURSOR)
			{
				float mx, my;
				unsigned b = SDL_GetMouseState(&mx, &my);
				windowToLogical(mx, my);
				cursorManager.nextTypeFromMouse(this, mx, my, b != 0);
				// Cocoa cursor images are sized in window points; Retina already
				// supplies the backing-pixel scale. Applying it here doubles the cursor.
				float cursorScale = drawableScale();
				const char *videoDriver = SDL_GetCurrentVideoDriver();
				if (videoDriver && std::strcmp(videoDriver, "cocoa") == 0 && windowW && windowH)
					cursorScale = std::min(float(windowW) / getW(), float(windowH) / getH());
				cursorManager.update(cursorScale);
			}

			captureRecordingFrame();

			// A transformed software pass may end before nextFrame. Keep the
			// request independent of the borrowed active-backend pointer.
			if (!pendingScreenshot.empty())
			{
				std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
					renderer ? renderer->capture()
							 : SDL_ConvertSurface(sdlsurface, SDL_PIXELFORMAT_RGBA32),
					SDL_DestroySurface);
				bool saved = false;
				if (pixels)
					for (size_t i = 0; i < Toolkit::getFileManager()->getDirCount(); ++i)
					{
						auto path = Toolkit::getFileManager()->getDir(i) + DIR_SEPARATOR_S +
									pendingScreenshot;
						if (SDL_SaveBMP(pixels.get(), path.c_str()))
						{
							saved = true;
							break;
						}
					}
				if (!saved)
					std::cerr << "Cannot save screenshot: " << SDL_GetError() << std::endl;
				pendingScreenshot.clear();
			}
			if (renderer && !nativeSoftware)
			{
				if (renderer == portableRenderer.get())
				{
					renderer->present();
					return;
				}
				renderer->flush();
			}
#ifdef HAVE_OPENGL
			if (optionFlags & USEGPU)
				Sprite::checkAllSpritesDrawn();
#endif
			if (renderer && nativeSoftware) renderer->flush();
			cacheFrame();
			if (optionFlags & USEGPU)
				swapBuffers();
			else
				presentLastFrame();
		}
	}

	void GraphicContext::printScreen(const std::string filename)
	{
		PERF_SCOPE_TIME(Screenshot);
		SDL_Surface *toPrintSurface = NULL;
		if (renderer)
		{
			pendingScreenshot = filename;
			return;
		}

	// Fetch the surface to print
#ifdef HAVE_OPENGL
		std::unique_ptr<DrawableSurface> toPrint = nullptr;
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			toPrint = std::make_unique<DrawableSurface>(getW(), getH());
			glFlush();
			toPrint->drawSurface(0, 0, this);
			toPrintSurface = toPrint->sdlsurface;
		}
		else
#endif
			toPrintSurface = sdlsurface;

		// Print it using virtual filesystem
		if (toPrintSurface)
		{
			for (size_t i = 0; i < Toolkit::getFileManager()->getDirCount(); i++)
			{
				std::string fullFileName = Toolkit::getFileManager()->getDir(i) + DIR_SEPARATOR_S + filename;
				if (SDL_SaveBMP(toPrintSurface, fullFileName.c_str()))
					break;
			}
		}
	}
}

namespace GAGCore
{
void GraphicContext::drawToSurface(SDL_Surface* surface, float scale, const std::function<void()>& draw)
{
	if (!surface || !(scale > 0) || offscreenPass)
		throw std::invalid_argument("Invalid or nested offscreen pass");
	if (renderBatch) renderBatch->barrier();
	Sprite::flushBatches(this);
	if (renderer) renderer->flush();
	auto backend = makeSoftwareRenderBackend(surface);
	if (!backend) throw std::runtime_error("Cannot create offscreen backend");
	auto savedBatch = std::move(renderBatch);
	const auto savedRenderer = renderer;
	const auto savedSurface = sdlsurface;
	const auto savedClip = clipRect;
	const auto savedFlags = optionFlags;
	const auto savedNative = nativeDesktop;
	const auto savedSoftware = nativeSoftware;
	const auto savedTransform = softwareTransform;
	const auto savedMap = mapTransformActive;
	const auto savedUI = uiTransformActive;
	const auto savedScale = mapScale;
	const auto savedTargetScale = renderTargetScale;
	const auto savedUiScale = uiScale;
	const auto savedMapGeometry = std::make_tuple(mapTranslateX, mapTranslateY,
		mapCopyTranslateX, mapCopyTranslateY, mapClipX, mapClipY, mapClipW, mapClipH, overlayScale, periodicCopy);
	const auto savedUIGeometry = std::make_tuple(uiTransformScale, uiTransformX, uiTransformY, uiBounds, uiSavedClip);
	const auto restore = [&] {
		renderer = savedRenderer; sdlsurface = savedSurface; clipRect = savedClip;
		optionFlags = savedFlags; nativeDesktop = savedNative; nativeSoftware = savedSoftware;
		softwareTransform = savedTransform; mapTransformActive = savedMap; uiTransformActive = savedUI;
		mapScale = savedScale; renderTargetScale = savedTargetScale; uiScale = savedUiScale;
		std::tie(mapTranslateX, mapTranslateY, mapCopyTranslateX, mapCopyTranslateY,
			mapClipX, mapClipY, mapClipW, mapClipH, overlayScale, periodicCopy) = savedMapGeometry;
		std::tie(uiTransformScale, uiTransformX, uiTransformY, uiBounds, uiSavedClip) = savedUIGeometry;
		renderBatch = std::move(savedBatch); offscreenPass = false;
	};
	sdlsurface = surface; renderer = backend.get(); optionFlags = 0;
	nativeDesktop = nativeSoftware = softwareTransform = uiTransformActive = false;
	mapTransformActive = true;
	mapClipX = mapClipY = 0; mapClipW = surface->w; mapClipH = surface->h;
	offscreenPass = true; uiScale = 1; renderTargetScale = 1; mapScale = scale;
	mapTranslateX = mapTranslateY = mapCopyTranslateX = mapCopyTranslateY = 0;
	periodicCopy = false;
	clipRect = {0, 0, surface->w, surface->h};
	try
	{
		backend->clip(&clipRect);
		backend->transform(scale, 0, 0, &clipRect);
		draw(); backend->flush();
	}
	catch (...) { restore(); throw; }
	restore();
}
}
