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
		freeDummySurface();
		TTF_Quit();
		SDL_Quit();

		if (verbose)
			fprintf(stderr, "Toolkit : Graphic Context destroyed\n");
	}

	void GraphicContext::freeDummySurface(void)
	{
		// SDL owns the window surface used outside GPU mode; the GPU-mode dummy is ours
		if (sdlsurface && (optionFlags & USEGPU))
			SDL_FreeSurface(sdlsurface);
		sdlsurface = NULL;
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

		// releases the previous mode's surface, so it has to run while optionFlags
		// still describes that mode
		freeDummySurface();

		// set flags
		optionFlags = flags;
		Uint32 sdlFlags = 0;
		if (flags & FULLSCREEN)
			// Desktop fullscreen, not exclusive: Wayland can't modeswitch to a non-native mode.
			sdlFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
		// FIXME: window resize is broken
		// if (flags & RESIZABLE)
		// 	sdlFlags |= SDL_WINDOW_RESIZABLE;
		#ifdef HAVE_OPENGL
		if (flags & USEGPU)
		{
			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
			sdlFlags |= SDL_WINDOW_OPENGL;
		}
		#else
		// remove GL from options
		optionFlags &= ~USEGPU;
		#endif

		// if window exists, delete it
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
		// SDL_GetWindowSurface is incompatible with SDL_WINDOW_OPENGL;
		// in GPU mode, create a small dummy surface so format-dependent code works.
		if (optionFlags & USEGPU)
		{
			sdlsurface = SDL_CreateRGBSurface(0, w, h, 32,
				0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
		}
		else
		{
			sdlsurface = SDL_GetWindowSurface(window);
		}
		if (!sdlsurface)
		{
			fprintf(stderr, "Toolkit : can't get surface for %dx%d at 32 bpp\n", w, h);
			fprintf(stderr, "Toolkit : %s\n", SDL_GetError());
			return false;
		}
		{
			_gc = this;
			// enable GL context
			if (flags & USEGPU)
			{
				SDL_GLContext context = SDL_GL_CreateContext(window);
				SDL_GL_MakeCurrent(window, context);
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
			{
				// disable system cursor
				SDL_ShowCursor(SDL_DISABLE);
				// load custom cursors
				cursorManager.load();
			}
			else
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
				gluOrtho2D(0, w, h, 0);
				glEnable(GL_LINE_SMOOTH);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				glState.doTexture(true);
				glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}
			#endif

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
				cursorManager.nextTypeFromMouse(this, mx, my, b != 0);
				setClipRect();
				cursorManager.draw(this, mx, my);
			}


			#ifdef HAVE_OPENGL
			if (optionFlags & GraphicContext::USEGPU)
			{
				Sprite::checkAllSpritesDrawn();
				SDL_GL_SwapWindow(window);
			}
			else
			#endif
			{
				SDL_UpdateWindowSurface(window);
			}
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
