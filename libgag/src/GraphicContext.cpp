// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "GraphicContextPrivate.h"
#include <UiScale.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <SupportFunctions.h>
#include <InterfacePresentation.h>
#include <BrowserTextInput.h>
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


        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
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
		renderer.reset();
		if (watchingEvents) SDL_RemoveEventWatch(watchWindow, this);
		releaseFrameCache();
		freeOwnedSurface();
#ifdef HAVE_OPENGL
		if (context) destroyUnitShader();
#endif
		if (context) SDL_GL_DestroyContext(context);
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
		return windowW && sdlsurface && (windowW != sdlsurface->w || windowH != sdlsurface->h);
	}

	float GraphicContext::textRenderScale(void)
	{
        // Both GPU backends draw glyph textures directly to the output. Include
        // the local UI transform: touch controls can enlarge text independently
        // of the window's logical-to-drawable scale. Pure software composition
        // still cannot preserve more pixels than its destination surface.
        if (!renderer && !(optionFlags & USEGPU)) return 1.0f;
        const float outputScale = softwareTransform ? 1.0f : drawableScale();
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

    float GraphicContext::querySystemUiScale(void)
    {
        if (!_gc || !_gc->window) return 1.0f;
        const float display = SDL_GetWindowDisplayScale(_gc->window);
        const float density = SDL_GetWindowPixelDensity(_gc->window);
        return windowUiScale(display, density, 0);
    }

    float GraphicContext::effectiveUiScale(float preferred)
    {
        return windowUiScale(querySystemUiScale(), 1.0f, preferred,
                             scaleFromEnv("GLOB2_UI_SCALE"));
    }

	void GraphicContext::freeOwnedSurface(void)
	{
        softwareRasterizer.reset();
		if (ownsSurface && sdlsurface)
			SDL_DestroySurface(sdlsurface);
		sdlsurface = NULL;
		ownsSurface = false;
	}

	float GraphicContext::drawableScale(void)
	{
		if (!drawableW || !sdlsurface)
			return 1.0f;
		return std::min(static_cast<float>(drawableW) / sdlsurface->w, static_cast<float>(drawableH) / sdlsurface->h);
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
		offX = (drawableW - static_cast<int>(sdlsurface->w * scale + 0.5f)) / 2;
		offY = (drawableH - static_cast<int>(sdlsurface->h * scale + 0.5f)) / 2;
	}

	void GraphicContext::applyGLViewport(void)
	{
		#ifdef HAVE_OPENGL
		float scale;
		int offX, offY;
		glLetterbox(scale, offX, offY);
		glViewport(offX, offY, static_cast<int>(sdlsurface->w * scale + 0.5f), static_cast<int>(sdlsurface->h * scale + 0.5f));
		#endif
	}

	void GraphicContext::updateWindowSize(void)
	{
		if (!window || !sdlsurface) return;
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

    bool GraphicContext::toggleFullscreen()
    {
        if(!window)return false;
        const bool fullscreen=(optionFlags & FULLSCREEN)==0;
        if(!SDL_SetWindowFullscreen(window,fullscreen))return false;
        if(fullscreen)optionFlags|=FULLSCREEN;else optionFlags&=~FULLSCREEN;
        updateWindowSize();
        return true;
    }

	void GraphicContext::translateMouseEvent(SDL_Event *event)
	{
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
                // SDL3 leaves coordinates in window space. Convert once here
                // for every rendering backend.
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
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
				#ifndef GLOB2_WEBGL2
				if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
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
        const int logicalW = std::max(1, static_cast<int>(w / uiScale + 0.5f));
        const int logicalH = std::max(1, static_cast<int>(h / uiScale + 0.5f));
        if (w == windowW && h == windowH && logicalW == getW() && logicalH == getH()) return true;
        const auto format = sdlsurface->format;
        SDL_Surface* replacement = SDL_CreateSurface(logicalW, logicalH, SDL_PIXELFORMAT_ARGB8888);
        if (!replacement) return false;
        // SDL may invalidate its borrowed window surface when changing size.
        freeOwnedSurface();
        SDL_SetWindowSize(window, w, h);
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
        setClipRect();
        return true;
    }

	bool GraphicContext::setRes(int w, int h, Uint32 flags)
	{
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
		uiScale = std::max(1.0f, uiScale);
		const int logicalW = std::max(1, static_cast<int>(w / uiScale + 0.5f));
		const int logicalH = std::max(1, static_cast<int>(h / uiScale + 0.5f));

		// set flags
        const char* selectedRenderer = SDL_getenv("GLOB2_RENDERER");
        if (selectedRenderer && std::string(selectedRenderer) == "sdl") flags |= PORTABLEGPU;
#ifdef GLOB2_MOBILE
        flags |= PORTABLEGPU | RESIZABLE;
#endif
        if (flags & PORTABLEGPU) flags &= ~USEGPU;
		optionFlags = flags;
        fixedLogicalW=logicalW; fixedLogicalH=logicalH; responsiveViewport=false;
		SDL_WindowFlags sdlFlags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
		if (flags & FULLSCREEN)
			// Desktop fullscreen, not exclusive: Wayland can't modeswitch to a non-native mode.
			sdlFlags |= SDL_WINDOW_FULLSCREEN;
		if ((flags & RESIZABLE) && !(flags & FULLSCREEN))
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
			sdlFlags |= SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
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
		if (context) destroyUnitShader();
#endif
		if (context) SDL_GL_DestroyContext(context);
		context = nullptr;
		renderer.reset();
		freeOwnedSurface();
		if (window) {
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
		drawableW = windowW;
		drawableH = windowH;
		if (flags & PORTABLEGPU) {
            renderer = makeSDLRenderBackend(window, logicalW, logicalH);
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
				#ifdef HAVE_OPENGL
				// The new context starts at the GL defaults, so a cache still describing the
				// replaced one would skip the enables the next draw call needs.
				glState.resetCache();
				// Map the logical projection onto a centered, aspect-correct sub-rect of the
				// drawable so fullscreen scales without distorting circles into ellipses.
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
				glOrtho(0, logicalW, logicalH, 0, -1, 1);
				glMatrixMode(GL_MODELVIEW);
				glLoadIdentity();
				glGetIntegerv(GL_MAX_TEXTURE_SIZE, &frameCache.maximumTextureSize);
				glEnable(GL_LINE_SMOOTH);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				glState.doTexture(true);
				glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}
			#endif

			eventThread = SDL_ThreadID();
			if (!renderer) {
				SDL_AddEventWatch(watchWindow, this);
				watchingEvents = true;
			}
			return true;
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


            if (renderer) {
                if (!pendingScreenshot.empty()) {
                    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(renderer->capture(), SDL_DestroySurface);
                    bool saved=false;
                    for (size_t i=0;i<Toolkit::getFileManager()->getDirCount();++i) {
                        auto path=Toolkit::getFileManager()->getDir(i)+DIR_SEPARATOR_S+pendingScreenshot;
                        if(SDL_SaveBMP(pixels.get(),path.c_str())) {saved=true;break;}
                    }
                    if(!saved) std::cerr << "Cannot save screenshot: " << SDL_GetError() << std::endl;
                    pendingScreenshot.clear();
                }
                renderer->present(); return;
            }
			#ifdef HAVE_OPENGL
			if (optionFlags & USEGPU) Sprite::checkAllSpritesDrawn();
			#endif
			cacheFrame();
			if (optionFlags & USEGPU) swapBuffers();
			else presentLastFrame();
		}
	}

	void GraphicContext::printScreen(const std::string filename)
	{
		PERF_SCOPE_TIME(Screenshot);
		SDL_Surface *toPrintSurface = NULL;
        if (renderer) { pendingScreenshot=filename; return; }

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
