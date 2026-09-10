// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GraphicContextPrivate.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <SupportFunctions.h>
#include <assert.h>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include "SDL_ttf.h"
#include <SDL_image.h>

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
		#ifndef GLOB2_WEBGL2
		if (window) SDL_SetWindowMinimumSize(window, minW, minH);
		#endif
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
		if (watchingEvents) SDL_DelEventWatch(watchWindow, this);
		releaseFrameCache();
		freeOwnedSurface();
		if (context) SDL_GL_DeleteContext(context);
		if (window) SDL_DestroyWindow(window);
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

	void GraphicContext::freeOwnedSurface(void)
	{
		if (ownsSurface && sdlsurface)
			SDL_FreeSurface(sdlsurface);
		sdlsurface = NULL;
		ownsSurface = false;
	}

	float GraphicContext::drawableScale(void)
	{
		if (!drawableW || !sdlsurface)
			return 1.0f;
		return std::min(static_cast<float>(drawableW) / sdlsurface->w, static_cast<float>(drawableH) / sdlsurface->h);
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
		drawableW = windowW;
		drawableH = windowH;
		if (windowW <= 0 || windowH <= 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return;
		if ((optionFlags & RESIZABLE) && !(optionFlags & FULLSCREEN)
			&& (getW() != windowW || getH() != windowH))
		{
			SDL_Surface *resized = SDL_CreateRGBSurface(0, windowW, windowH, 32,
				0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
			if (!resized) return;
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
			setClipRect();
		}
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

    bool GraphicContext::toggleFullscreen()
    {
        if(!window)return false;
        const bool fullscreen=(optionFlags & FULLSCREEN)==0;
        if(SDL_SetWindowFullscreen(window,fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0)!=0)return false;
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
            case SDL_KEYDOWN:
                if(event->key.keysym.sym==SDLK_F11 && !event->key.repeat)_gc->toggleFullscreen();
                break;
			case SDL_MOUSEMOTION:
				_gc->windowToLogical(event->motion.x, event->motion.y);
				break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				_gc->windowToLogical(event->button.x, event->button.y);
				break;
			case SDL_WINDOWEVENT:
				#ifndef GLOB2_WEBGL2
				if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
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
        if (w == getW() && h == getH()) return true;
        const auto& format = *sdlsurface->format;
        SDL_Surface* replacement = SDL_CreateRGBSurface(0, w, h, 32,
            format.Rmask, format.Gmask, format.Bmask, format.Amask);
        if (!replacement) return false;
        // SDL may invalidate its borrowed window surface when changing size.
        freeOwnedSurface();
        SDL_SetWindowSize(window, w, h);
        sdlsurface = replacement;
        ownsSurface = true;
        SDL_GetWindowSize(window, &windowW, &windowH);
        drawableW = windowW; drawableH = windowH;
#ifdef HAVE_OPENGL
        if (optionFlags & USEGPU) {
            SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0, w, h, 0, -1, 1);
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

		// set flags
		optionFlags = flags;
		Uint32 sdlFlags = 0;
		if (flags & FULLSCREEN)
			// Desktop fullscreen, not exclusive: Wayland can't modeswitch to a non-native mode.
			sdlFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
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
		if (context) SDL_GL_DeleteContext(context);
		context = nullptr;
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
			#ifndef GLOB2_WEBGL2
			SDL_SetWindowMinimumSize(window, std::max(1, minW), std::max(1, minH));
			#endif
		// Own the drawing surface: SDL invalidates its window surface during resizing.
		sdlsurface = SDL_CreateRGBSurface(0, w, h, 32,
			0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
		ownsSurface = true;
		if (!sdlsurface)
		{
			fprintf(stderr, "Toolkit : can't get surface for %dx%d at 32 bpp\n", w, h);
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
				// Map the logical projection onto a centered, aspect-correct sub-rect of the
				// drawable so fullscreen scales without distorting circles into ellipses.
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
				glState.checkExtensions();
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
				glOrtho(0, w, h, 0, -1, 1);
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
			SDL_AddEventWatch(watchWindow, this);
			watchingEvents = true;
			return true;
		}
	}

	void GraphicContext::nextFrame(void)
	{
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
		SDL_Surface *toPrintSurface = NULL;

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
