// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "GraphicContextPrivate.h"
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
#include "SDL_ttf.h"
#include <SDL_image.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#endif

namespace GAGCore
{
	// Storage for the static globals declared in graphic_context_private.h.
	GraphicContext *_gc = NULL;
	SDL_PixelFormat _glFormat;
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
		return SDL_MapRGBA(&_glFormat, r, g, b, a);
	}

	void Color::unpack(const Uint32 packedValue)
	{
		SDL_GetRGBA(packedValue, &_glFormat, &r, &g, &b, &a);
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

		// Iterate display
		const int displayCount = SDL_GetNumVideoDisplays();
		if (displayCount < 1)
		{
			std::cerr << "SDL_GetNumVideoDisplays failed: " << SDL_GetError() << std::endl;
			return modes;
		}

		// For each display, iterate modes
		for (int i = 0; i < displayCount; i++)
		{
			const int modeCount = SDL_GetNumDisplayModes(i);
			if (modeCount < 0)
			{
				std::cerr << "SDL_GetNumDisplayModes failed: " << SDL_GetError() << std::endl;
				continue;
			}
			for (int j = 0; j < modeCount; j++)
			{
				SDL_DisplayMode mode;
				if (SDL_GetDisplayMode(i, j, &mode)) {
					std::cerr << "SDL_GetDisplayMode failed: " << SDL_GetError() << std::endl;
					continue;
				}
				if (mode.w < minW || mode.h < minH)
				{
					continue;
				}
				modes.push_back(mode);
			}
		}

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

#ifdef _WIN32
		SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
		// Load the SDL library
		if ( SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO|SDL_INIT_TIMER)<0 )
		{
			fprintf(stderr, "Toolkit : Initialisation Error : %s\n", SDL_GetError());
			exit(1);
		}
		else
		{
			if (verbose)
				fprintf(stderr, "Toolkit : Initialized : Graphic Context created\n");
		}

		#ifdef _WIN32
		SDL_version version;
		SDL_GetVersion(&version);
		if (SDL_VERSIONNUM(version.major, version.minor, version.patch) < SDL_VERSIONNUM(2, 30, 0))
		{
			fprintf(stderr, "Glob2 requires SDL 2.30 or newer for window resizing on Windows.\n");
			exit(1);
		}
		#endif
		TTF_Init();

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
		// destructor's body, and SDL_FreeCursor() after SDL_Quit() is undefined
		cursorManager.releaseNativeCursor();
		renderer.reset();
		if (watchingEvents) SDL_DelEventWatch(watchWindow, this);
		releaseFrameCache();
		freeOwnedSurface();
#ifdef HAVE_OPENGL
		if (context) destroyUnitShader();
#endif
		if (context) SDL_GL_DeleteContext(context);
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

		// Xft.dpi in the X resource database is where GTK, Qt and Xwayland all read the
		// desktop's scale from, and the only place it is exposed as the fraction the user
		// picked rather than the panel's physical DPI. libX11 is loaded on demand so a
		// build without X11 simply reports no scale.
		float scaleFromXResources(void)
		{
			#ifndef _WIN32
			void *lib = dlopen("libX11.so.6", RTLD_LAZY);
			if (!lib)
				return 0.0f;
			auto openDisplay = reinterpret_cast<void *(*)(const char *)>(dlsym(lib, "XOpenDisplay"));
			auto resourceString = reinterpret_cast<char *(*)(void *)>(dlsym(lib, "XResourceManagerString"));
			auto closeDisplay = reinterpret_cast<int (*)(void *)>(dlsym(lib, "XCloseDisplay"));
			float scale = 0.0f;
			if (openDisplay && resourceString && closeDisplay)
			{
				if (void *display = openDisplay(NULL))
				{
					if (const char *database = resourceString(display))
						if (const char *entry = strstr(database, "Xft.dpi:"))
						{
							const float dpi = static_cast<float>(atof(entry + strlen("Xft.dpi:")));
							if (dpi >= 48.0f && dpi <= 768.0f)
								scale = dpi / 96.0f;
						}
					closeDisplay(display);
				}
			}
			dlclose(lib);
			return scale;
			#else
			return 0.0f;
			#endif
		}
	}

	void GraphicContext::refreshDesktopScale()
	{
		const int display = SDL_GetWindowDisplayIndex(window);
#ifdef _WIN32
		SDL_SysWMinfo info{};
		SDL_VERSION(&info.version);
		if (SDL_GetWindowWMInfo(window, &info))
		{
			using GetDpi = UINT (WINAPI *)(HWND);
			auto getDpi = reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
			if (getDpi) desktopSystemScale = std::max(1.0f, getDpi(info.info.win.window) / 96.0f);
		}
#else
		if (display != desktopDisplay)
		{
#ifdef __APPLE__
			// Cocoa coordinates are already points; backing density only affects rasterization.
			desktopSystemScale = 1.0f;
#else
			int pointsW,pointsH,pixelsW=0,pixelsH=0;
			SDL_GetWindowSize(window,&pointsW,&pointsH);
#ifdef HAVE_OPENGL
			if (optionFlags & USEGPU) SDL_GL_GetDrawableSize(window,&pixelsW,&pixelsH);
			else
#endif
			if (auto *target=SDL_GetWindowSurface(window)) { pixelsW=target->w; pixelsH=target->h; }
			const float density=pointsW>0 && pointsH>0 && pixelsW>0 && pixelsH>0
				? std::min(float(pixelsW)/pointsW,float(pixelsH)/pointsH) : 1.0f;
			const float desktopScale=scaleFromXResources();
			// Without a desktop preference, drawable density supplies the scale.
			// Dividing it out keeps window points from counting density twice.
			desktopSystemScale=std::max(1.0f,(desktopScale>0?desktopScale:density)/density);
#endif
		}
#endif
		desktopDisplay = display;
	}

	float GraphicContext::querySystemUiScale(void)
	{
		return _gc && _gc->nativeDesktop ? _gc->desktopSystemScale : scaleFromXResources();
	}

	float GraphicContext::effectiveUiScale(float preferred)
	{
		float scale = scaleFromEnv("GLOB2_UI_SCALE");
		if (!scale)
			scale = preferred;
		if (!scale)
			scale = querySystemUiScale();
		// Below 1 the interface would be drawn smaller than the window can show.
		return std::max(1.0f, std::min(scale ? scale : 1.0f, 4.0f));
	}

	void GraphicContext::freeOwnedSurface(void)
	{
        softwareRasterizer.reset();
		if (ownsSurface && sdlsurface)
			SDL_FreeSurface(sdlsurface);
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
		if ((optionFlags & RESIZABLE) && !(optionFlags & FULLSCREEN)
			&& (getW() != logicalW || getH() != logicalH))
		{
			SDL_Surface *resized = SDL_CreateRGBSurface(0, logicalW, logicalH, 32,
				0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
			if (!resized) return;
			requestedW = windowW;
			requestedH = windowH;
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
			SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);
			applyGLViewport();
		}
		#endif
	}

	void GraphicContext::windowToLogical(Sint32 &x, Sint32 &y)
	{
		if (nativeDesktop && windowW > 0 && windowH > 0)
		{
			x = std::clamp(int(double(x) * getW() / windowW), 0, getW()-1);
			y = std::clamp(int(double(y) * getH() / windowH), 0, getH()-1);
			return;
		}
		if (!isScalingActive())
			return;
		// letterboxed the same way as the GL viewport / software blit, but in
		// window points rather than drawable pixels or window-surface pixels
		float scale = std::min(static_cast<float>(windowW) / sdlsurface->w, static_cast<float>(windowH) / sdlsurface->h);
		int offX = static_cast<int>((windowW - sdlsurface->w * scale) / 2.0f);
		int offY = static_cast<int>((windowH - sdlsurface->h * scale) / 2.0f);
		x = static_cast<Sint32>((x - offX) / scale);
		y = static_cast<Sint32>((y - offY) / scale);
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
            const Uint32 deadline = SDL_GetTicks() + 3000;
            while (bool(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != fullscreen &&
                   !SDL_TICKS_PASSED(SDL_GetTicks(), deadline)) {
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
        const SDL_bool previousResizable = (SDL_GetWindowFlags(window) & SDL_WINDOW_RESIZABLE) ? SDL_TRUE : SDL_FALSE;
        // Cocoa fullscreen spaces require a resizable window. This temporary
        // window style does not change the saved windowed resize preference.
        if (nativeDesktop && fullscreen) SDL_SetWindowResizable(window, SDL_TRUE);
        if (SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
            SDL_SetWindowResizable(window, previousResizable);
            return false;
        }
        bool applied = !nativeDesktop || confirmedFullscreen(window, fullscreen);
        if (applied) {
            if (fullscreen) optionFlags |= FULLSCREEN; else optionFlags &= ~FULLSCREEN;
            if (!fullscreen) {
                SDL_SetWindowResizable(window, (optionFlags & RESIZABLE) ? SDL_TRUE : SDL_FALSE);
                SDL_SetWindowSize(window, requestedW, requestedH);
            }
            if (nativeDesktop) applied = refreshNativeWindow();
        }
        if (!applied) {
            if (previous) optionFlags |= FULLSCREEN; else optionFlags &= ~FULLSCREEN;
            SDL_SetWindowResizable(window, previousResizable);
            SDL_SetWindowFullscreen(window, previous ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
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
		if (!_gc)
			return;
		switch (event->type)
		{
            case SDL_KEYDOWN:
                if(event->key.keysym.sym==SDLK_F11 && !event->key.repeat)_gc->toggleFullscreen();
                break;
            case SDL_RENDER_DEVICE_RESET:
            case SDL_RENDER_TARGETS_RESET:
                if (_gc->renderer) _gc->renderer->reset();
                break;
			case SDL_MOUSEMOTION:
                // SDL's renderer event watch already maps polled events to its
                // logical size. Raw SDL_GetMouseState coordinates still need
                // translateMouseCoordinates below.
				if (!_gc->renderer || _gc->nativeSoftware)
                {
                    if (_gc->nativeDesktop && _gc->windowW>0 && _gc->windowH>0)
                    {
                        // Preserve subpixel motion between integer SDL events, so
                        // slow editor dragging also follows the whole-view scale.
                        struct RelativeState {
                            GraphicContext *context=nullptr;
                            Uint32 window=0;
                            int pointsW=0,pointsH=0,logicalW=0,logicalH=0;
                            double x=0,y=0;
                        };
                        static RelativeState relative;
                        const Uint32 id=SDL_GetWindowID(_gc->window);
                        if (relative.context!=_gc || relative.window!=id ||
                            relative.pointsW!=_gc->windowW || relative.pointsH!=_gc->windowH ||
                            relative.logicalW!=_gc->getW() || relative.logicalH!=_gc->getH())
                            relative={_gc,id,_gc->windowW,_gc->windowH,_gc->getW(),_gc->getH(),0,0};
                        auto delta=[](int value,int logical,int points,double &remainder) {
                            const double mapped=remainder+double(value)*logical/points;
                            const int result=int(std::lround(mapped));
                            remainder=mapped-result; return result;
                        };
                        event->motion.xrel=delta(event->motion.xrel,relative.logicalW,relative.pointsW,relative.x);
                        event->motion.yrel=delta(event->motion.yrel,relative.logicalH,relative.pointsH,relative.y);
                    }
                    _gc->windowToLogical(event->motion.x, event->motion.y);
                }
				break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				if (!_gc->renderer || _gc->nativeSoftware) _gc->windowToLogical(event->button.x, event->button.y);
				break;
			case SDL_DISPLAYEVENT:
				_gc->desktopDisplay = -1;
				_gc->updateWindowSize();
				break;
			case SDL_WINDOWEVENT:
				#ifndef GLOB2_WEBGL2
				if (event->window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) _gc->desktopDisplay = -1;
				if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED || event->window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED)
					_gc->updateWindowSize();
				#endif
				break;
			default:
				break;
		}
	}

	void GraphicContext::translateMouseCoordinates(int &x, int &y)
	{
		if (_gc)
		{
			Sint32 sx = x, sy = y;
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
        const auto& format = *sdlsurface->format;
        SDL_Surface* replacement = SDL_CreateRGBSurface(0, logicalW, logicalH, 32,
            format.Rmask, format.Gmask, format.Bmask, format.Amask);
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
            SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);
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
#if !defined(GLOB2_MOBILE) && !defined(__EMSCRIPTEN__)
        // The portable backend retains the mobile/browser viewport contract.
        nativeDesktop = !(flags & PORTABLEGPU);
#endif
		optionFlags = flags;
        fixedLogicalW=logicalW; fixedLogicalH=logicalH; responsiveViewport=false;
		Uint32 sdlFlags = (nativeDesktop || (flags & PORTABLEGPU)) ? SDL_WINDOW_ALLOW_HIGHDPI : 0;
		if (flags & FULLSCREEN)
			// Desktop fullscreen, not exclusive: Wayland can't modeswitch to a non-native mode.
			sdlFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
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
			sdlFlags |= SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
		}
		#else
		// remove GL from options
		optionFlags &= ~USEGPU;
		#endif

		// if window exists, delete it
		if (watchingEvents) SDL_DelEventWatch(watchWindow, this);
		watchingEvents = false;
		releaseFrameCache();
#ifdef HAVE_OPENGL
		if (context) destroyUnitShader();
#endif
		if (context) SDL_GL_DeleteContext(context);
		context = nullptr;
		renderer.reset();
		freeOwnedSurface();
		if (window) {
			SDL_DestroyWindow(window);
			window = nullptr;
		}
		// create the new window and the surface
		window = SDL_CreateWindow(windowTitle.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, sdlFlags);
		if (!window)
		{
			fprintf(stderr, "Toolkit : can't create window %dx%d\n", w, h);
			fprintf(stderr, "Toolkit : %s\n", SDL_GetError());
			return false;
		}
		// With SDL_WINDOW_FULLSCREEN_DESKTOP the window keeps the desktop size,
		// so the actual pixel size can differ from the requested logical w x h.
		SDL_GetWindowSize(window, &windowW, &windowH);
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
		sdlsurface = SDL_CreateRGBSurface(0, logicalW, logicalH, 32,
			0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
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
				if (!context || SDL_GL_MakeCurrent(window, context) != 0)
				{
					fprintf(stderr, "OpenGL context failed: %s\n", SDL_GetError());
					if (context) SDL_GL_DeleteContext(context);
					context = nullptr;
					return false;
				}
				++glContextGeneration;
				#ifdef HAVE_OPENGL
				// The new context starts at the GL defaults, so a cache still describing the
				// replaced one would skip the enables the next draw call needs.
				glState.resetCache();
				// Map logical coordinates onto native drawable pixels. The legacy mobile
				// viewport contract still retains its aspect-correct sub-rectangle.
				// The drawable is in pixels; on HiDPI it is larger than the window points mouse events use.
				SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);
				applyGLViewport();
				#endif
			}
			// set _glFormat
			if ((optionFlags & USEGPU) && (_gc->sdlsurface->format->BitsPerPixel != 32))
			{
				_glFormat.palette = NULL;
				_glFormat.BitsPerPixel = 32;
				_glFormat.BytesPerPixel = 4;
				#if SDL_BYTEORDER == SDL_BIG_ENDIAN
				_glFormat.Rmask = 0x000000ff;
				_glFormat.Rshift = 0;
				_glFormat.Gmask = 0x0000ff00;
				_glFormat.Gshift = 8;
				_glFormat.Bmask = 0x00ff0000;
				_glFormat.Bshift = 16;
				#else
				_glFormat.Rmask = 0x00ff0000;
				_glFormat.Rshift = 16;
				_glFormat.Gmask = 0x0000ff00;
				_glFormat.Gshift = 8;
				_glFormat.Bmask = 0x000000ff;
				_glFormat.Bshift = 0;
				#endif
				_glFormat.Amask = 0xff000000;
				_glFormat.Ashift = 24;
				_glFormat.Rloss = 0;
				_glFormat.Gloss = 0;
				_glFormat.Bloss = 0;
				_glFormat.Aloss = 0;
			}
			else
			{
				memcpy(&_glFormat, _gc->sdlsurface->format, sizeof(SDL_PixelFormat));
				unsigned alphaPos(24);
				if ((_glFormat.Rshift == 24) || (_glFormat.Gshift == 24) || (_glFormat.Bshift == 24))
					alphaPos = 0;
				_glFormat.Amask = 0xff << alphaPos;
				_glFormat.Ashift = alphaPos;
				_glFormat.Aloss = 0;
			}

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
				SDL_FreeSurface(iconSurface);
			}

			setClipRect();
			if (flags & CUSTOMCURSOR)
				// cursorManager installs its cursors as native ones (see
				// CursorManager::update()), so the system cursor stays on
				cursorManager.load();
			else
				cursorManager.releaseNativeCursor();
			SDL_ShowCursor(SDL_ENABLE);

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

            if (nativeDesktop && (flags & FULLSCREEN) && !confirmedFullscreen(window, true)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Fullscreen was not applied; restoring windowed mode");
                SDL_SetWindowFullscreen(window,0);
                SDL_SetWindowResizable(window,(flags & RESIZABLE) ? SDL_TRUE : SDL_FALSE);
                SDL_SetWindowSize(window,requestedW,requestedH);
                optionFlags &= ~FULLSCREEN;
            }
			eventThread = SDL_ThreadID();
			if (nativeDesktop && !refreshNativeWindow()) return false;
			if (!renderer || nativeSoftware) {
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
				int mx, my;
				unsigned b = SDL_GetMouseState(&mx, &my);
				translateMouseCoordinates(mx, my);
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
                    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> pixels(renderer->capture(), SDL_FreeSurface);
                    bool saved=false;
                    for (size_t i=0;i<Toolkit::getFileManager()->getDirCount();++i) {
                        auto path=Toolkit::getFileManager()->getDir(i)+DIR_SEPARATOR_S+pendingScreenshot;
                        if(SDL_SaveBMP(pixels.get(),path.c_str())==0) {saved=true;break;}
                    }
                    if(!saved) std::cerr << "Cannot save screenshot: " << SDL_GetError() << std::endl;
                    pendingScreenshot.clear();
                }
                if (!nativeSoftware) { renderer->present(); return; }
                renderer->flush();
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
				if (SDL_SaveBMP(toPrintSurface, fullFileName.c_str()) == 0)
					break;
			}
		}
	}
}
