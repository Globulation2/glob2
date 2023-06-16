/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <GraphicContext.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <SupportFunctions.h>
#include "EventListener.h"
#include <assert.h>
#include <string>
#include <sstream>
#include <iostream>
#include "SDL_ttf.h"
#include <SDL_image.h>
#include <math.h>
#include <string.h>
#include <valarray>
#include <cstdlib>
#include <memory>
#include <mutex>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_OPENGL
	#if defined(__APPLE__) || defined(OPENGL_HEADER_DIRECTORY_OPENGL)
		#include <OpenGL/gl.h>
		#include <OpenGL/glext.h>
		#include <OpenGL/glu.h>
		#define GL_TEXTURE_RECTANGLE_NV GL_TEXTURE_RECTANGLE_EXT
	#else
		#include <GL/gl.h>
		#include <GL/glu.h>
	#endif
#endif

#ifdef WIN32
	#include <GL/glext.h>
#endif

//extern "C" { SDL_PixelFormat *SDL_AllocFormat(int bpp, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask); }

namespace GAGCore
{
	// static local pointer to the actual graphic context
	static GraphicContext *_gc = NULL;
	static SDL_PixelFormat _glFormat;
	//EXPERIMENTAL is a bit buggy and "not EXPERIMENTAL" is bugfree but slow
	//when rendering clouds or other density layers (GraphicContext::drawAlphaMap).
	static const bool EXPERIMENTAL=false;

	// Color
	Uint32 Color::pack() const
	{
		//return (SDL_MapRGB(&_glFormat, r, g, b) & 0x00ffffff) | (a << 24);
		return SDL_MapRGBA(&_glFormat, r, g, b, a);
	}

	void  Color::unpack(const Uint32 packedValue)
	{
		//SDL_GetRGB(packedValue, &_glFormat, &r, &g, &b);
		//a = packedValue >> 24;
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
	Color Color::white = Color(255,255,255);

	#ifdef HAVE_OPENGL
	// Cache for GL state, call gl only if necessary. GL optimisations
	static struct GLState
	{
		static const bool verbose = false;
		bool _doBlend;
		bool _doTexture;
		bool _doScissor;
		GLint _texture;
		GLenum _sfactor, _dfactor;
		bool isTextureSRectangle;
		bool useATIWorkaround;
		unsigned alocatedTextureCount;

		GLState(void)
		{
			resetCache();
			isTextureSRectangle = false;
			useATIWorkaround = false;
			alocatedTextureCount = 0;
		}

		void resetCache(void)
		{
			_doBlend = false;
			_doTexture = false;
			_doScissor = false;
			_texture = -1;
			_sfactor = 0xffffffff;
			_dfactor = 0xffffffff;
		}

		void checkExtensions(void)
		{
			const char *glExtensions = (const char *)glGetString(GL_EXTENSIONS);
			isTextureSRectangle = (strstr(glExtensions, "GL_NV_texture_rectangle") != NULL);
			isTextureSRectangle = isTextureSRectangle || (strstr(glExtensions, "GL_EXT_texture_rectangle") != NULL);
			isTextureSRectangle = isTextureSRectangle || (strstr(glExtensions, "GL_ARB_texture_rectangle") != NULL);

			const char *glVendor= (const char *)glGetString(GL_VENDOR);
			if(strstr(glVendor,"ATI"))
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

		bool doBlend(bool on)
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

		bool doTexture(bool on)
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

		void setTexture(int tex)
		{
			if (_texture == tex)
				return;

			if (isTextureSRectangle)
			{
				if(useATIWorkaround)
					glBindTexture(GL_TEXTURE_RECTANGLE_NV, 0);
				glBindTexture(GL_TEXTURE_RECTANGLE_NV, tex);
			}
			else
				glBindTexture(GL_TEXTURE_2D, tex);
			_texture = tex;
		}

		bool doScissor(bool on)
		{
			// The glIsEnabled is function is quite expensive. That's why we have a _doScissor variable.
			// I'm quite sure that this assert should never fail, so I've outcommented it, partially
			// because we don't do #define NDEBUG in most of our releases (so far).
			
			//assert(_doScissor == glIsEnabled(GL_SCISSOR_TEST));
			
			if (_doScissor == on)
				return on;

			if (on)
				glEnable(GL_SCISSOR_TEST);
			else
				glDisable(GL_SCISSOR_TEST);
			_doScissor = on;
			return !on;
		}

		void blendFunc(GLenum sfactor, GLenum dfactor)
		{
			if ((sfactor == _sfactor) && (dfactor == _dfactor))
				return;

			glBlendFunc(sfactor, dfactor);

			_sfactor = sfactor;
			_dfactor = dfactor;
		}
	} glState;
	#endif

	SDL_Surface *DrawableSurface::convertForUpload(SDL_Surface *source)
	{
		SDL_Surface *dest;
		if (_gc->sdlsurface->format->BitsPerPixel == 32)
		{
			dest = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_BGRA32, 0);
		}
		else
		{
			dest = SDL_ConvertSurface(source, &_glFormat, 0);
		}
		assert(dest);
		return dest;
	}

	// Drawable surface
	DrawableSurface::DrawableSurface(const std::string &imageFileName)
	{
		sdlsurface = NULL;
		if (!loadImage(imageFileName))
			setRes(0, 0);
		allocateTexture();
	}

	DrawableSurface::DrawableSurface(int w, int h)
	{
		sdlsurface = NULL;
		setRes(w, h);
		allocateTexture();
	}

	DrawableSurface::DrawableSurface(const SDL_Surface *sourceSurface)
	{
		assert(sourceSurface);
		// beurk, const cast here becasue SDL API sucks
		sdlsurface = convertForUpload(const_cast<SDL_Surface *>(sourceSurface));
		assert(sdlsurface);
		setClipRect();
		allocateTexture();
		dirty = true;
	}

	DrawableSurface *DrawableSurface::clone(void)
	{
		return new DrawableSurface(sdlsurface);
	}

	DrawableSurface::~DrawableSurface(void)
	{
		SDL_FreeSurface(sdlsurface);
		freeGPUTexture();
	}

	template<typename T>
	T getMinPowerOfTwo(T t)
	{
		T v = 1;
		while (v < t)
			v *= 2;
		return v;
	}

	void DrawableSurface::allocateTexture(void)
	{
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			glGenTextures(1, reinterpret_cast<GLuint*>(&texture));
			glState.alocatedTextureCount++;
			initTextureSize();
		}
		#endif
	}

	void DrawableSurface::initTextureSize(void)
	{
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			// only power of two textures are supported
			if (!glState.isTextureSRectangle)
			{
				// TODO : if anyone has a better way to do it, please tell :-)
				glState.setTexture(texture);
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);

				int w = getMinPowerOfTwo(sdlsurface->w);
				int h = getMinPowerOfTwo(sdlsurface->h);
				std::valarray<char> zeroBuffer((char)0, w * h * 4);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, &zeroBuffer[0]);

				texMultX = 1.0f / static_cast<float>(w);
				texMultY = 1.0f / static_cast<float>(h);
			}
			else
			{
				texMultX = 1.0f;
				texMultY = 1.0f;
			}
		}
		#endif
	}

	void DrawableSurface::uploadToTexture(void)
	{
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			std::unique_lock<std::recursive_mutex> lock(EventListener::renderMutex);
			EventListener::ensureContext();
			glState.setTexture(texture);

			void *pixelsPtr;
			GLenum pixelFormat;
			#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			std::valarray<Uint32> tempPixels(sdlsurface->w * sdlsurface->h);
			Uint32 *sourcePtr = static_cast<Uint32 *>(sdlsurface->pixels);
			for (size_t i=0; i<tempPixels.size(); i++)
			{
				tempPixels[i] = ((*sourcePtr) << 8) | ((*sourcePtr) >> 24);
				sourcePtr++;
			}
			pixelsPtr = &tempPixels[0];
			pixelFormat = GL_RGBA;
			#else
			pixelsPtr = sdlsurface->pixels;
			pixelFormat = GL_BGRA;
			#endif
			if (glState.isTextureSRectangle)
			{
				glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGBA, sdlsurface->w, sdlsurface->h, 0, pixelFormat, GL_UNSIGNED_BYTE, pixelsPtr);
			}
			else
			{
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sdlsurface->w, sdlsurface->h, pixelFormat, GL_UNSIGNED_BYTE, pixelsPtr);
			}
		}
		#endif
		dirty = false;
	}

	void DrawableSurface::freeGPUTexture(void)
	{
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			glDeleteTextures(1, reinterpret_cast<const GLuint*>(&texture));
			glState.alocatedTextureCount--;
			
			// The next line causes a desynchronization between _doScissors and glIsEnabled(GL_SCISSOR_TEST),
			// which causes the setClipRect() functions to not reset the clipping the way it should,  so many
			// things don't get drawn properly and the game appears to "blink". Outcommenting it didn't cause
			// any other problems.  If you think glState should be reset,  feel free to do so,  but also call
			// functions like glDisable() as required.
			
			//glState.resetCache();
		}
		#endif
	}

	void DrawableSurface::setRes(int w, int h)
	{
		if (sdlsurface)
			SDL_FreeSurface(sdlsurface);

		sdlsurface = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32, _glFormat.Rmask, _glFormat.Gmask, _glFormat.Bmask, _glFormat.Amask);
		assert(sdlsurface);
		setClipRect();
		initTextureSize();
		dirty = true;
	}

	void DrawableSurface::getClipRect(int *x, int *y, int *w, int *h)
	{
		assert(x);
		assert(y);
		assert(w);
		assert(h);

		*x = clipRect.x;
		*y = clipRect.y;
		*w = clipRect.w;
		*h = clipRect.h;
	}

	void DrawableSurface::setClipRect(int x, int y, int w, int h)
	{
		assert(sdlsurface);

		clipRect.x = static_cast<Sint16>(x);
		clipRect.y = static_cast<Sint16>(y);
		clipRect.w = static_cast<Uint16>(w);
		clipRect.h = static_cast<Uint16>(h);

		SDL_SetClipRect(sdlsurface, &clipRect);
	}

	void DrawableSurface::setClipRect(void)
	{
		assert(sdlsurface);

		clipRect.x = 0;
		clipRect.y = 0;
		clipRect.w = static_cast<Uint16>(sdlsurface->w);
		clipRect.h = static_cast<Uint16>(sdlsurface->h);

		SDL_SetClipRect(sdlsurface, &clipRect);
	}

	bool DrawableSurface::loadImage(const std::string name)
	{
		if (name.size())
		{
			SDL_RWops *imageStream;
			if ((imageStream = Toolkit::getFileManager()->open(name, "rb")) != NULL)
			{
				SDL_Surface *loadedSurface;
				loadedSurface = IMG_Load_RW(imageStream, 0);
				SDL_RWclose(imageStream);
				if (loadedSurface)
				{
					if (sdlsurface)
						SDL_FreeSurface(sdlsurface);
					sdlsurface = convertForUpload(loadedSurface);
					SDL_FreeSurface(loadedSurface);
					setClipRect();
					dirty = true;
					return true;
				}
			}
		}
		return false;
	}

	void DrawableSurface::shiftHSV(float hue, float sat, float lum)
	{
		Uint32 *mem = (Uint32 *)sdlsurface->pixels;
		for (size_t i = 0; i < static_cast<size_t>(sdlsurface->w * sdlsurface->h); i++)
		{
			// get values
			float h, s, v;
			Color c;
			c.unpack(*mem);
			c.getHSV(&h, &s, &v);

			// shift
			h += hue;
			s += sat;
			v += lum;

			// wrap and saturate
			if (h >= 360.0f)
				h -= 360.0f;
			if (h < 0.0f)
				h += 360.0f;
			s = std::max(s, 0.0f);
			s = std::min(s, 1.0f);
			v = std::max(v, 0.0f);
			v = std::min(v, 1.0f);

			// set values
			c.setHSV(h, s, v);
			*mem = c.pack();
			mem++;
		}
		dirty = true;
	}

	void DrawableSurface::drawPixel(int x, int y, const Color& color)
	{
		// clip
		if ((x<clipRect.x) || (x>=clipRect.x+clipRect.w) || (y<clipRect.y) || (y>=clipRect.y+clipRect.h))
			return;

		// draw
		if (color.a == Color::ALPHA_OPAQUE)
		{
			*(((Uint32 *)sdlsurface->pixels) + y*(sdlsurface->pitch>>2) + x) = color.pack();
		}
		else
		{
			Uint32 a = color.a;
			Uint32 na = 255 - a;
			Uint32 colorValue = color.applyAlpha(Color::ALPHA_OPAQUE).pack();
			Uint32 colorPreMult0 = (colorValue & 0x00FF00FF) * a;
			Uint32 colorPreMult1 = ((colorValue >> 8) & 0x00FF00FF) * a;

			Uint32 *mem = ((Uint32 *)sdlsurface->pixels) + y*(sdlsurface->pitch>>2) + x;

			Uint32 surfaceValue = *mem;
			Uint32 surfacePreMult0 = (surfaceValue & 0x00FF00FF) * na;
			Uint32 surfacePreMult1 = ((surfaceValue >> 8) & 0x00FF00FF) * na;

			surfacePreMult0 += colorPreMult0;
			surfacePreMult1 += colorPreMult1;

			*mem = ((surfacePreMult0 >> 8) & 0x00FF00FF) | (surfacePreMult1 & 0xFF00FF00);
		}
		dirty = true;
	}

	void DrawableSurface::drawPixel(float x, float y, const Color& color)
	{
		drawPixel(static_cast<int>(x), static_cast<int>(y), color);
	}

	// compat
	void DrawableSurface::drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawPixel(x, y, Color(r, g, b, a));
	}

	void DrawableSurface::drawRect(int x, int y, int w, int h, const Color& color)
	{
		_drawHorzLine(x, y, w, color);
		_drawHorzLine(x, y+h-1, w, color);
		_drawVertLine(x, y, h, color);
		_drawVertLine(x+w-1, y, h, color);
	}

	void DrawableSurface::drawRect(float x, float y, float w, float h, const Color& color)
	{
		drawRect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), color);
	}

	// compat
	void DrawableSurface::drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawRect(x, y, w, h, Color(r, g, b, a));
	}

	void DrawableSurface::drawFilledRect(int x, int y, int w, int h, const Color& color)
	{
		// clip
		if (x < clipRect.x)
		{
			w -= clipRect.x - x;
			x = clipRect.x;
		}
		if (y < 0)
		{
			h -= clipRect.y - y;
			y = clipRect.y;
		}
		if (x + w >= clipRect.x + clipRect.w)
		{
			w = clipRect.x + clipRect.w - x;
		}
		if (y + h >= clipRect.y + clipRect.h)
		{
			h = clipRect.y + clipRect.h - y;
		}
		if ((w <= 0) || (h <= 0))
			return;

		// draw
		if (color.a == Color::ALPHA_OPAQUE)
		{
			Uint32 colorValue = color.pack();
			for (int dy = y; dy < y + h; dy++)
			{
				Uint32 *mem = ((Uint32 *)sdlsurface->pixels) + dy*(sdlsurface->pitch>>2) + x;
				int dw = w;
				do
				{
					*mem++ = colorValue;
				}
				while (--dw);
			}
		}
		else
		{
			Uint32 a = color.a;
			Uint32 na = 255 - a;
			Uint32 colorValue = color.applyAlpha(Color::ALPHA_OPAQUE).pack();
			Uint32 colorPreMult0 = (colorValue & 0x00FF00FF) * a;
			Uint32 colorPreMult1 = ((colorValue >> 8) & 0x00FF00FF) * a;

			for (int dy = y; dy < y + h; dy++)
			{
				Uint32 *mem = ((Uint32 *)sdlsurface->pixels) + dy*(sdlsurface->pitch>>2) + x;
				int dw = w;
				do
				{
					Uint32 surfaceValue = *mem;
					Uint32 surfacePreMult0 = (surfaceValue & 0x00FF00FF) * na;
					Uint32 surfacePreMult1 = ((surfaceValue >> 8) & 0x00FF00FF) * na;
					surfacePreMult0 += colorPreMult0;
					surfacePreMult1 += colorPreMult1;
					*mem++ = ((surfacePreMult0 >> 8) & 0x00FF00FF) | (surfacePreMult1 & 0xFF00FF00);
				}
				while (--dw);
			}
		}
		dirty = true;
	}

	void DrawableSurface::drawFilledRect(float x, float y, float w, float h, const Color& color)
	{
		drawFilledRect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), color);
	}

	void DrawableSurface::drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawFilledRect(x, y, w, h, Color(r, g, b, a));
	}

	void DrawableSurface::_drawVertLine(int x, int y, int l, const Color& color)
	{
		// clip
		// be sure we have to draw something
		if ((x < clipRect.x) || (x >= clipRect.x + clipRect.w))
			return;

		// set l positiv
		if (l < 0)
		{
			y += l;
			l = -l;
		}

		// clip on y at top
		if (y < clipRect.y)
		{
			l -= clipRect.y - y;
			y = clipRect.y;
		}

		// clip on y at bottom
		if (y + l >= clipRect.y + clipRect.h)
		{
			l = clipRect.y + clipRect.h - y;
		}

		// again, be sure we have to draw something
		if (l <= 0)
			return;

		// draw
		int increment = sdlsurface->pitch >> 2;
		Uint32 *mem = ((Uint32 *)sdlsurface->pixels) + y*increment + x;
		if (color.a == Color::ALPHA_OPAQUE)
		{
			Uint32 colorValue = color.pack();

			do
			{
				*mem = colorValue;
				mem += increment;
			}
			while (--l);
		}
		else
		{
			Uint32 a = color.a;
			Uint32 na = 255 - a;
			Uint32 colorValue = color.applyAlpha(Color::ALPHA_OPAQUE).pack();
			Uint32 colorPreMult0 = (colorValue & 0x00FF00FF) * a;
			Uint32 colorPreMult1 = ((colorValue >> 8) & 0x00FF00FF) * a;

			do
			{
				Uint32 surfaceValue = *mem;
				Uint32 surfacePreMult0 = (surfaceValue & 0x00FF00FF) * na;
				Uint32 surfacePreMult1 = ((surfaceValue >> 8) & 0x00FF00FF) * na;
				surfacePreMult0 += colorPreMult0;
				surfacePreMult1 += colorPreMult1;
				*mem = ((surfacePreMult0 >> 8) & 0x00FF00FF) | (surfacePreMult1 & 0xFF00FF00);
				mem += increment;
			}
			while (--l);
		}
		dirty = true;
	}

	void DrawableSurface::_drawHorzLine(int x, int y, int l, const Color& color)
	{
		// clip
		// be sure we have to draw something
		if ((y < clipRect.y) || (y >= clipRect.y + clipRect.h))
			return;

		// set l positiv
		if (l < 0)
		{
			x += l;
			l = -l;
		}

		// clip on x at left
		if (x < clipRect.x)
		{
			l -= clipRect.x - x;
			x = clipRect.x;
		}

		// clip on x at right
		if ( x + l >= clipRect.x + clipRect.w)
		{
			l = clipRect.x + clipRect.w - x;
		}

		// again, be sure we have to draw something
		if (l <= 0)
			return;

		// draw
		Uint32 *mem = ((Uint32 *)sdlsurface->pixels) + y*(sdlsurface->pitch >> 2) + x;
		if (color.a == Color::ALPHA_OPAQUE)
		{
			Uint32 colorValue = color.pack();

			do
			{
				*mem++ = colorValue;
			}
			while (--l);
		}
		else
		{
			Uint32 a = color.a;
			Uint32 na = 255 - a;
			Uint32 colorValue = color.applyAlpha(Color::ALPHA_OPAQUE).pack();
			Uint32 colorPreMult0 = (colorValue & 0x00FF00FF) * a;
			Uint32 colorPreMult1 = ((colorValue >> 8) & 0x00FF00FF) * a;

			do
			{
				Uint32 surfaceValue = *mem;
				Uint32 surfacePreMult0 = (surfaceValue & 0x00FF00FF) * na;
				Uint32 surfacePreMult1 = ((surfaceValue >> 8) & 0x00FF00FF) * na;
				surfacePreMult0 += colorPreMult0;
				surfacePreMult1 += colorPreMult1;
				*mem++ = ((surfacePreMult0 >> 8) & 0x00FF00FF) | (surfacePreMult1 & 0xFF00FF00);
			}
			while (--l);
		}
		dirty = true;
	}

	void DrawableSurface::drawLine(int x1, int y1, int x2, int y2, const Color& _color)
	{
		// we want to modify the color
		Color color = _color;

		// compute deltas
		int dx = x2 - x1;
		if (dx == 0)
		{
			_drawVertLine(x1, y1, y2-y1, color);
			return;
		}
		int dy = y2 - y1;
		if (dy == 0)
		{
			_drawHorzLine(x1, y1, x2-x1, color);
			return;
		}

		// clip
		int test = 1;
		// Y clipping
		if (dy < 0)
		{
			test = -test;
			std::swap(x1, x2);
			std::swap(y1, y2);
			dx = -dx;
			dy = -dy;
		}

		// the 2 points are Y-sorted. (y1 <= y2)
		if (y2 < clipRect.y)
			return;
		if (y1 >= clipRect.y + clipRect.h)
			return;
		if (y1 < clipRect.y)
		{
			x1 = x2 - ( (y2 - clipRect.y)*(x2-x1) ) / (y2-y1);
			y1 = clipRect.y;
		}
		if (y1 == y2)
		{
			_drawHorzLine(x1, y1, x2-x1, color);
			return;
		}
		if (y2 >= clipRect.y + clipRect.h)
		{
			x2 = x1 - ( (y1 - (clipRect.y + clipRect.h))*(x1-x2) ) / (y1-y2);
			y2 = (clipRect.y + clipRect.h - 1);
		}
		if (x1 == x2)
		{
			_drawVertLine(x1, y1, y2-y1, color);
			return;
		}

		// X clipping
		if (dx < 0)
		{
			test = -test;
			std::swap(x1, x2);
			std::swap(y1, y2);
			dx = -dx;
			dy = -dy;
		}
		// the 2 points are X-sorted. (x1 <= x2)
		if (x2 < clipRect.x)
			return;
		if (x1 >= clipRect.x + clipRect.w)
			return;
		if (x1 < clipRect.x)
		{
			y1 = y2 - ( (x2 - clipRect.x)*(y2-y1) ) / (x2-x1);
			x1 = clipRect.x;
		}
		if (x1 == x2)
		{
			_drawVertLine(x1, y1, y2-y1, color);
			return;
		}
		if (x2 >= clipRect.x + clipRect.w)
		{
			y2 = y1 - ( (x1 - (clipRect.x + clipRect.w))*(y1-y2) ) / (x1-x2);
			x2 = (clipRect.x + clipRect.w - 1);
		}

		// last return case
		if (x1 >= (clipRect.x + clipRect.w) || y1 >= (clipRect.y + clipRect.h) || (x2 < clipRect.x) || (y2 < clipRect.y))
			return;

		// recompute deltas after clipping
		dx = x2-x1;
		dy = y2-y1;

		// setup variable to draw alpha in the right direction
		#define Sgn(x) (x>0 ? (x == 0 ? 0 : 1) : (x==0 ? 0 : -1))
		Sint32 littleincx;
		Sint32 littleincy;
		Sint32 bigincx;
		Sint32 bigincy;
		Sint32 alphadecx;
		Sint32 alphadecy;
		if (abs(dx) > abs(dy))
		{
			littleincx = 1;
			littleincy = 0;
			bigincx = 1;
			bigincy = Sgn(dy);
			alphadecx = 0;
			alphadecy = Sgn(dy);
		}
		else
		{
			// we swap x and y meaning
			test = -test;
			std::swap(dx, dy);
			littleincx = 0;
			littleincy = 1;
			bigincx = Sgn(dx);
			bigincy = 1;
			alphadecx = 1;
			alphadecy = 0;
		}

		if (dx < 0)
		{
			dx = -dx;
			littleincx = 0;
			littleincy = -littleincy;
			bigincx = -bigincx;
			bigincy = -bigincy;
			alphadecy = -alphadecy;
		}

		// compute initial position
		int px, py;
		px = x1;
		py = y1;

		// variable initialisation for bresenham algo
		if (dx == 0)
			return;
		if (dy == 0)
			return;
		const int FIXED = 8;
		const int I = 255; // number of degree of alpha
		const int Ibits = 8;
		int m = (abs(dy) << (Ibits+FIXED)) / abs(dx);
		int w = (I << FIXED) - m;
		int e = 1 << (FIXED-1);

		// first point
		color.a = I - (e >> FIXED);
		drawPixel(px, py, color);

		// main loop
		int x = dx+1;
		if (x <= 0)
			return;
		while (--x)
		{
			if (e < w)
			{
				px+=littleincx;
				py+=littleincy;
				e+= m;
			}
			else
			{
				px+=bigincx;
				py+=bigincy;
				e-= w;
			}
			color.a = I - (e >> FIXED);
			drawPixel(px, py, color);
			color.a = e >> FIXED;
			drawPixel(px + alphadecx, py + alphadecy, color);
		}
	}

	void DrawableSurface::drawLine(float x1, float y1, float x2, float y2, const Color& color)
	{
		drawRect(static_cast<int>(x1), static_cast<int>(y1), static_cast<int>(x2), static_cast<int>(y2), color);
	}

	void DrawableSurface::drawVertLine(int x, int y, int l, const Color& color)
	{
		 _drawVertLine(x, y, l, color);
	}
	
	void DrawableSurface::drawHorzLine(int x, int y, int l, const Color& color)
	{
		_drawHorzLine(x, y, l, color);
	}
	
	// compat
	void DrawableSurface::drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		 _drawVertLine(x, y, l, Color(r, g, b, a));
	}
	// compat
	void DrawableSurface::drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		_drawHorzLine(x, y, l, Color(r, g, b, a));
	}
	// compat
	void DrawableSurface::drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawLine(x1, y1, x2, y2, Color(r, g, b, a));
	}

	void DrawableSurface::drawCircle(int x, int y, int radius, const Color& _color)
	{
		// we want to modify the color
		Color color = _color;

		// clip
		if ((x+radius < clipRect.x) || (x-radius >= clipRect.x+clipRect.w) || (y+radius < clipRect.y) || (y-radius >= clipRect.y+clipRect.h))
			return;

		// draw
		int dx, dy, d;
		int rdx, rdy;
		int i;
		color.a >>= 2;
		for (i=0; i<3; i++)
		{
			dx = 0;
			dy = (radius<<1) + i;
			d = 0;

			do
			{
				rdx = (dx>>1);
				rdy = (dy>>1);
				drawPixel(x+rdx, y+rdy, color);
				drawPixel(x+rdx, y-rdy, color);
				drawPixel(x-rdx, y+rdy, color);
				drawPixel(x-rdx, y-rdy, color);
				drawPixel(x+rdy, y+rdx, color);
				drawPixel(x+rdy, y-rdx, color);
				drawPixel(x-rdy, y+rdx, color);
				drawPixel(x-rdy, y-rdx, color);
				dx++;
				if (d >= 0)
				{
					dy--;
					d += ((dx-dy)<<1)+2;
				}
				else
				{
					d += (dx<<1) +1;
				}
			}
			while (dx <= dy);
		}
	}

	void DrawableSurface::drawCircle(float x, float y, float radius, const Color& color)
	{
		drawCircle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(radius), color);
	}

	// compat
	void DrawableSurface::drawCircle(int x, int y, int radius, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawCircle(x, y, radius, Color(r, g, b, a));
	}

	void DrawableSurface::drawSurface(int x, int y, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, surface, 0, 0, surface->getW(), surface->getH(), alpha);
	}

	void DrawableSurface::drawSurface(float x, float y, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, surface, 0, 0, surface->getW(), surface->getH(), alpha);
	}

	void DrawableSurface::drawSurface(int x, int y, int w, int h, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, w, h, surface, 0, 0, surface->getW(), surface->getH(), alpha);
	}

	void DrawableSurface::drawSurface(float x, float y, float w, float h, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, w, h, surface, 0, 0, surface->getW(), surface->getH(), alpha);
	}

	void DrawableSurface::drawSurface(int x, int y, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
		if (alpha == Color::ALPHA_OPAQUE)
		{
			#ifdef HAVE_OPENGL
			if ((surface == _gc) && (_gc->getOptionFlags() & GraphicContext::USEGPU))
			{
				if ((x == 0) && (y == 0) && (sdlsurface->w == sw) && (sdlsurface->h == sh))
				{
					std::valarray<unsigned> tempPixels(sw*sh);
					#if SDL_BYTEORDER == SDL_BIG_ENDIAN
					glReadPixels(sx, sy, sdlsurface->w, sdlsurface->h, GL_RGBA, GL_UNSIGNED_BYTE, &tempPixels[0]);
					#else
					glReadPixels(sx, sy, sdlsurface->w, sdlsurface->h, GL_BGRA, GL_UNSIGNED_BYTE, &tempPixels[0]);
					#endif
					for (int y = 0; y<sh; y++)
					{
						unsigned *srcPtr = (unsigned *)&tempPixels[y*sw];
						unsigned *destPtr = &(((unsigned *)sdlsurface->pixels)[(sh-y-1)*sw]);
						for (int x = 0; x<sw; x++)
						#if SDL_BYTEORDER == SDL_BIG_ENDIAN
						{
							*destPtr++ = (*srcPtr >> 8) | (*srcPtr << 24);
							srcPtr++;
						}
						#else
							*destPtr++ = *srcPtr++;
						#endif
					}

				}
				else
				{
					std::cerr << "Partial blitting to from framebuffer in GL is forbidden" << std::endl;
					assert(false);
				}
			}
			else
			{
			#endif // HAVE_OPENGL
				// well, we *hope* SDL is faster than a handmade code
				SDL_Rect sr, dr;
				sr.x = static_cast<Sint16>(sx);
				sr.y = static_cast<Sint16>(sy);
				sr.w = static_cast<Uint16>(sw);
				sr.h = static_cast<Uint16>(sh);
				dr.x = static_cast<Sint16>(x);
				dr.y = static_cast<Sint16>(y);
				dr.w = static_cast<Uint16>(sw);
				dr.h = static_cast<Uint16>(sh);
				SDL_BlitSurface(surface->sdlsurface, &sr, sdlsurface, &dr);
			#ifdef HAVE_OPENGL
			}
			#endif // HAVE_OPENGL
		}
		else
		{
			if ((surface == _gc) && (_gc->getOptionFlags() & GraphicContext::USEGPU))
			{
				std::cerr << "Blitting with alphablending from framebuffer in GL is forbidden" << std::endl;
				assert(false);
			}

			// check we assume the source rect is within the source surface
			assert((sx >= 0) && (sx < surface->getW()));
			assert((sy >= 0) && (sy < surface->getH()));
			assert((sw > 0) && (sx + sw <= surface->getW()));
			assert((sh > 0) && (sy + sh <= surface->getH()));

			// clip
			if (x < clipRect.x)
			{
				int diff = clipRect.x - x;
				sw -= diff;
				sx += diff;
				x = clipRect.x;
			}
			if (y < 0)
			{
				int diff = clipRect.y - y;
				sh -= diff;
				sy += diff;
				y = clipRect.y;
			}
			if (x + sw >= clipRect.x + clipRect.w)
			{
				sw = clipRect.x + clipRect.w - x;
			}
			if (y + sh >= clipRect.y + clipRect.h)
			{
				sh = clipRect.y + clipRect.h - y;
			}
			if ((sw <= 0) || (sh <= 0))
				return;

			// draw
			#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			Uint32 alphaShift = 0;
			#else
			Uint32 alphaShift = 24;
			#endif
			for (int dy = 0; dy < sh; dy++)
			{
				Uint32 *memSrc = ((Uint32 *)surface->sdlsurface->pixels) + (sy + dy)*(surface->sdlsurface->pitch>>2) + sx;
				Uint32 *memDest = ((Uint32 *)sdlsurface->pixels) + (y + dy)*(sdlsurface->pitch>>2) + x;
				int dw = sw;
				do
				{
					Uint32 srcValue = *memSrc++;
					Uint32 srcAlpha = (((srcValue >> alphaShift) & 0xFF) * alpha) >> 8;
					Uint32 destAlpha = 255 - srcAlpha;
					Uint32 srcPreMult0 =  (srcValue & 0x00FF00FF) * srcAlpha;
					Uint32 srcPreMult1 = ((srcValue >> 8) & 0x00FF00FF) * srcAlpha;

					Uint32 destValue = *memDest;
					Uint32 destPreMult0 =  (destValue & 0x00FF00FF) * destAlpha;
					Uint32 destPreMult1 = ((destValue >> 8) & 0x00FF00FF) * destAlpha;

					destPreMult0 += srcPreMult0;
					destPreMult1 += srcPreMult1;

					*memDest++ = ((destPreMult0 >> 8) & 0x00FF00FF) | (destPreMult1 & 0xFF00FF00);
				}
				while (--dw);
			}
		}
		dirty = true;
	}

	void DrawableSurface::drawSurface(float x, float y, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
		drawSurface(static_cast<int>(x), static_cast<int>(y), surface, sx, sy, sw, sh, alpha);
	}

	void DrawableSurface::drawSurface(int x, int y, int w, int h, DrawableSurface *surface, int sx, int sy, int sw, int sh,  Uint8 alpha)
	{
		// TODO : Implement
	}

	void DrawableSurface::drawSurface(float x, float y, float w, float h, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
		drawSurface(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), surface, sx, sy, sw, sh, alpha);
	}

	void DrawableSurface::drawSprite(int x, int y, Sprite *sprite, unsigned index,  Uint8 alpha)
	{
		// check bounds
		assert(sprite);
		if (!sprite->checkBound(index))
			return;

		// draw background
		if (sprite->images[index])
			drawSurface(x, y, sprite->images[index], alpha);

		// draw rotation
		if (sprite->rotated[index])
			drawSurface(x, y, sprite->getRotatedSurface(index), alpha);
	}

	void DrawableSurface::drawSprite(float x, float y, Sprite *sprite, unsigned index,  Uint8 alpha)
	{
		// check bounds
		assert(sprite);
		if (!sprite->checkBound(index))
			return;

		// draw background
		if (sprite->images[index])
			drawSurface(x, y, sprite->images[index], alpha);

		// draw rotation
		if (sprite->rotated[index])
			drawSurface(x, y, sprite->getRotatedSurface(index), alpha);
	}

	void DrawableSurface::drawSprite(int x, int y, int w, int h, Sprite *sprite, unsigned index, Uint8 alpha)
	{
		// check bounds
		assert(sprite);
		if (!sprite->checkBound(index))
			return;

		// draw background
		if (sprite->images[index])
			drawSurface(x, y, w, h, sprite->images[index], alpha);

		// draw rotation
		if (sprite->rotated[index])
			drawSurface(x, y, w, h, sprite->getRotatedSurface(index), alpha);
	}

	void DrawableSurface::drawSprite(float x, float y, float w, float h, Sprite *sprite, unsigned index, Uint8 alpha)
	{
		// check bounds
		assert(sprite);
		if (!sprite->checkBound(index))
			return;

		// draw background
		if (sprite->images[index])
			drawSurface(x, y, w, h, sprite->images[index], alpha);

		// draw rotation
		if (sprite->rotated[index])
			drawSurface(x, y, w, h, sprite->getRotatedSurface(index), alpha);
	}

	void DrawableSurface::drawString(int x, int y, Font *font, const std::string &msg, int w, Uint8 alpha)
	{
		std::string output(msg);
		std::string::size_type pos = output.find('\n', 0);
		if(pos != std::string::npos)
			output = output.substr(0, pos);

		pos = output.find('\r', 0);
		if(pos != std::string::npos)
			output = output.substr(0, pos);

		font->drawString(this, x, y, w, output, alpha);

		///////////// The following code is for translation textshots ////////////
		if(!translationPicturesDirectory.empty())
		{
			for(std::map<std::string, std::string>::iterator i=texts.begin(); i!=texts.end(); ++i)
			{
				if(output.find(i->first)!=std::string::npos)
				{
					int width=font->getStringWidth(i->first.c_str());
					int height=font->getStringHeight(i->first.c_str());
					int startx=font->getStringWidth(output.substr(0, output.find(i->first)).c_str());
					drawSquares.push_back(boost::make_tuple(SRectangle(x+startx, y, width, height), i->second, this));
					wroteTexts.insert(i->second);
					texts.erase(i);
					break;
				}
			}
		}
	}

	void DrawableSurface::drawString(float x, float y, Font *font, const std::string &msg, float w, Uint8 alpha)
	{
		std::string output(msg);
		std::string::size_type pos = output.find('\n', 0);
		if(pos != std::string::npos)
			output = output.substr(0, pos);

		pos = output.find('\r', 0);
		if(pos != std::string::npos)
			output = output.substr(0, pos);

		///////////// The following code is for translation textshots ////////////
		if(!translationPicturesDirectory.empty())
		{
			for(std::map<std::string, std::string>::iterator i=texts.begin(); i!=texts.end(); ++i)
			{
				if(output.find(i->first)!=std::string::npos)
				{
					int width=font->getStringWidth(i->first.c_str());
					int height=font->getStringHeight(i->first.c_str());
					int startx=font->getStringWidth(output.substr(0, output.find(i->first)).c_str());
					drawSquares.push_back(boost::make_tuple(SRectangle(int(x+startx), int(y), width, height), i->second, this));
					wroteTexts.insert(i->second);
					texts.erase(i);
					break;
				}
			}
		}
		font->drawString(this, x, y, w, output, alpha);

	}

	void DrawableSurface::drawAlphaMap(const std::valarray<float> &map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color)
	{
		assert(mapW * mapH <= static_cast<int>(map.size()));

		for (int dy=0; dy < mapH-1; dy++)
			for (int dx=0; dx < mapW-1; dx++)
				drawFilledRect(x + dx * cellW, y + dy * cellH, cellW, cellH, color.applyMultiplyAlpha((Uint8)(255.0f * map[mapW * dy + dx])));
	}

	void DrawableSurface::drawAlphaMap(const std::valarray<unsigned char> &map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color)
	{
		assert(mapW * mapH <= static_cast<int>(map.size()));

		for (int dy=0; dy < mapH-1; dy++)
			for (int dx=0; dx < mapW-1; dx++)
				drawFilledRect(x + dx * cellW, y + dy * cellH, cellW, cellH, color.applyMultiplyAlpha(map[mapW * dy + dx]));
	}

	// compat
	void DrawableSurface::drawString(int x, int y, Font *font, int i)
	{
		std::stringstream str;
		str << i;
		this->drawString(x, y, font, str.str());
	}

	//This code is for the textshot code
	std::map<std::string, std::string> DrawableSurface::texts;
	std::set<std::string> DrawableSurface::wroteTexts;
	std::vector<boost::tuple<DrawableSurface::SRectangle, std::string, GAGCore::DrawableSurface*> > DrawableSurface::drawSquares;
	std::string DrawableSurface::translationPicturesDirectory;

	void DrawableSurface::flushTextPictures()
	{
		using namespace GAGCore;
		for(std::vector<boost::tuple<SRectangle, std::string, DrawableSurface*> >::iterator i=drawSquares.begin(); i!=drawSquares.end();)
		{
			DrawableSurface toPrint(i->get<2>()->getW(), i->get<2>()->getH());
			toPrint.drawSurface(0, 0, i->get<2>());
			int x=i->get<0>().x;
			int y=i->get<0>().y;
			int width=i->get<0>().w;
			int height=i->get<0>().h;

			toPrint.drawRect(x-2, y-2, width+4, height+4, Color(255, 126, 21));
			toPrint.drawRect(x-3, y-3, width+6, height+6, Color(255, 126, 21));
			toPrint.drawCircle(x+width/2, y+height/2, std::max(width+4, height+4)/2+4, Color(255, 126, 21));
			toPrint.drawCircle(x+width/2, y+height/2, std::max(width+4, height+4)/2+5, Color(255, 126, 21));
			toPrint.drawCircle(x+width/2, y+height/2, std::max(width+4, height+4)/2+6, Color(255, 126, 21));

			// Print it using virtual filesystem
			for (size_t i2 = 0; i2 < Toolkit::getFileManager()->getDirCount(); i2++)
			{
				std::string fullFileName = translationPicturesDirectory + DIR_SEPARATOR_S + "text-" + i->get<1>();
				if (SDL_SaveBMP(toPrint.sdlsurface, (fullFileName+".bmp").c_str()) == 0)
				{
					break;
				}
			}
			i=drawSquares.erase(i);
		}
	}

	void DrawableSurface::printFinishingText()
	{
		if(!texts.empty())
			std::cout<<"The following requested translation texts where never drawn to the screen, or too mangled to be detected:"<<std::endl;
		for(std::map<std::string, std::string>::iterator i=texts.begin(); i!=texts.end(); ++i)
		{
			std::cout<<"\t"<<i->second<<std::endl;
		}
	}

	// here begin the Graphic Context part
	void GraphicContext::setClipRect(int x, int y, int w, int h)
	{
		DrawableSurface::setClipRect(x, y, w, h);
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			glState.doScissor(true);
			glScissor(clipRect.x, getH() - clipRect.y - clipRect.h, clipRect.w, clipRect.h);
		}
		#endif
	}

	void GraphicContext::setClipRect(void)
	{
		DrawableSurface::setClipRect();
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
			glState.doScissor(false);
		#endif
	}

	// drawing, reimplementation for GL

	void GraphicContext::drawPixel(int x, int y, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			GraphicContext::drawPixel(static_cast<float>(x), static_cast<float>(y), color);
		else
		#endif
			DrawableSurface::drawPixel(x, y, color);
	}

	void GraphicContext::drawPixel(float x, float y, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawFilledRect(x, y, 1.0f, 1.0f, color);
		else
		#endif
			DrawableSurface::drawPixel(static_cast<int>(x), static_cast<int>(y), color);
	}


	void GraphicContext::drawRect(int x, int y, int w, int h, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			GraphicContext::drawRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), color);
		else
		#endif
			DrawableSurface::drawRect(x, y, w, h, color);
	}

	void GraphicContext::drawRect(float x, float y, float w, float h, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
		{
			// state change
			glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glState.doBlend(true);
			glState.doTexture(false);

			// draw
			glBegin(GL_LINES);
			if (color.a < 255)
				glColor4ub(color.r, color.g, color.b, color.a);
			else
				glColor3ub(color.r, color.g, color.b);
			glVertex2f(x, y);     glVertex2f(x+w, y);
			glVertex2f(x+w, y);   glVertex2f(x+w, y+h);
			glVertex2f(x+w, y+h); glVertex2f(x, y+h);
			glVertex2f(x, y+h);   glVertex2f(x, y);
			glEnd();
		}
		else
		#endif
			DrawableSurface::drawRect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), color);
	}


	void GraphicContext::drawFilledRect(int x, int y, int w, int h, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			GraphicContext::drawFilledRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), color);
		else
		#endif
			DrawableSurface::drawFilledRect(x, y, w, h, color);
	}

	void GraphicContext::drawFilledRect(float x, float y, float w, float h, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
		{
			// state change
			if (color.a < 255)
				glState.doBlend(true);
			else
				glState.doBlend(false);
			glState.doTexture(false);

			// draw
			glBegin(GL_QUADS);
			if (color.a < 255)
				glColor4ub(color.r, color.g, color.b, color.a);
			else
				glColor3ub(color.r, color.g, color.b);
			glVertex2f(x, y);
			glVertex2f(x+w, y);
			glVertex2f(x+w, y+h);
			glVertex2f(x, y+h);
			glEnd();
		}
		else
		#endif
			DrawableSurface::drawFilledRect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), color);
	}


	void GraphicContext::drawLine(int x1, int y1, int x2, int y2, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			GraphicContext::drawLine(static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(x2), static_cast<float>(y2), color);
		else
		#endif
			DrawableSurface::drawLine(x1, y1, x2, y2, color);
	}

	void GraphicContext::drawLine(float x1, float y1, float x2, float y2, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
		{
			// state change
			glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glState.doBlend(true);
			glState.doTexture(false);

			// draw
			glBegin(GL_LINES);
			if (color.a < 255)
				glColor4ub(color.r, color.g, color.b, color.a);
			else
				glColor3ub(color.r, color.g, color.b);
			glVertex2f(x1, y1);
			glVertex2f(x2, y2);
			glEnd();
		}
		else
		#endif
			DrawableSurface::drawLine(static_cast<int>(x1), static_cast<int>(y1), static_cast<int>(x2), static_cast<int>(y2), color);
	}


	void GraphicContext::drawCircle(int x, int y, int radius, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawCircle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(radius), color);
		else
		#endif
			DrawableSurface::drawCircle(x, y, radius, color);
	}

	void GraphicContext::drawCircle(float x, float y, float radius, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
		{
			glState.doBlend(true);
			glState.doTexture(false);
			glLineWidth(2);

			double tot = radius;
			double fx = x;
			double fy = y;
			double fray = radius;

			glBegin(GL_LINES);
			if (color.a < 255)
				glColor4ub(color.r, color.g, color.b, color.a);
			else
				glColor3ub(color.r, color.g, color.b);
			for (int i=0; i<tot; i++)
			{
				double angle0 = (2*M_PI*(double)i)/((double)tot);
				double angle1 = (2*M_PI*(double)(i+1))/((double)tot);
				glVertex2d(fx+fray*sin(angle0), fy+fray*cos(angle0));
				glVertex2d(fx+fray*sin(angle1), fy+fray*cos(angle1));
			}
			glEnd();
			glLineWidth(1);
		}
		else
		#endif
			DrawableSurface::drawCircle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(radius), color);
	}

	void GraphicContext::drawSurface(int x, int y, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, surface, 0, 0, surface->getW(), surface->getH(), alpha);
	}

	void GraphicContext::drawSurface(float x, float y, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, surface, 0, 0, surface->getW(), surface->getH(), alpha);
	}

	void GraphicContext::drawSurface(int x, int y, int w, int h, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, w, h, surface, 0, 0, surface->getW(), surface->getH(), alpha);
	}

	void GraphicContext::drawSurface(float x, float y, float w, float h, DrawableSurface *surface, Uint8 alpha)
	{
		drawSurface(x, y, w, h, surface, 0, 0, surface->getW(), surface->getH(), alpha);
	}

	void GraphicContext::drawSurface(int x, int y, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
			drawSurface(x, y, sw, sh, surface, sx, sy, sw, sh, alpha);
		else
		#endif
			DrawableSurface::drawSurface(x, y, surface, sx, sy, sw, sh, alpha);
	}

	void GraphicContext::drawSurface(float x, float y, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
			drawSurface(x, y, static_cast<float>(sw), static_cast<float>(sh), surface, sx, sy, sw, sh, alpha);
		else
		#endif
			DrawableSurface::drawSurface(static_cast<int>(x), static_cast<int>(y), surface, sx, sy, sw, sh, alpha);
	}

	void GraphicContext::drawSurface(int x, int y, int w, int h, DrawableSurface *surface, int sx, int sy, int sw, int sh,  Uint8 alpha)
	{
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
			GraphicContext::drawSurface(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), surface, sx, sy, sw, sh, alpha);
		else
		#endif
			DrawableSurface::drawSurface(x, y, w, h, surface, sx, sy, sw, sh, alpha);
	}

	void GraphicContext::drawSurface(float x, float y, float w, float h, DrawableSurface *surface, int sx, int sy, int sw, int sh, Uint8 alpha)
	{
		#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			if (!resizeTimer && EventListener::instance()->isResizing())
				resizeTimer++;
			// upload
			if (surface->dirty || resizeTimer)
				surface->uploadToTexture();

			// state change
			glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glState.doBlend(true);
			glState.doTexture(true);
			glColor4ub(255, 255, 255, alpha);

			// draw
			glState.setTexture(surface->texture);
			glBegin(GL_QUADS);
			glTexCoord2f(static_cast<float>(sx) * surface->texMultX, static_cast<float>(sy) * surface->texMultY);
			glVertex2f(x, y);
			glTexCoord2f(static_cast<float>(sx + sw) * surface->texMultX, static_cast<float>(sy) * surface->texMultY);
			glVertex2f(x+w, y);
			glTexCoord2f(static_cast<float>(sx + sw) * surface->texMultX, static_cast<float>(sy + sh) * surface->texMultY);
			glVertex2f(x+w, y+h);
			glTexCoord2f(static_cast<float>(sx) * surface->texMultX, static_cast<float>(sy + sh) * surface->texMultY);
			glVertex2f(x, y+h);
			glEnd();
		}
		else
		#endif
			DrawableSurface::drawSurface(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), surface, sx, sy, sw, sh, alpha);
	}

	void GraphicContext::drawAlphaMap(const std::valarray<float> &map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color)
	{
	#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			assert(mapW * mapH <= static_cast<int>(map.size()));
			float fr = 255.0f*(float)color.r;
			float fg = 255.0f*(float)color.g;
			float fb = 255.0f*(float)color.b;
			if (EXPERIMENTAL) {
				GLuint texture[1];
				GLboolean old_blend;                //var to store blend state
				glGetBooleanv(GL_BLEND,&old_blend); //store blend state
				glEnable(GL_BLEND);                 //enable blend
				GLboolean old_texture_2d;
				glGetBooleanv(GL_TEXTURE_2D,&old_texture_2d);
				glEnable(GL_TEXTURE_2D);
				std::valarray<GLfloat> image(mapW*mapH);
				for (int i=0; i<mapH; i++)
					for (int j=0; j<mapW;j++)
						image[i*mapW+j]=map[mapW*i+j];
				glColor4ub(fr, fg, fb, 255);
				glGenTextures(1, &texture[0]);
				glBindTexture(GL_TEXTURE_2D, texture[0]);
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA,mapW,mapH, 0, GL_ALPHA, GL_UNSIGNED_BYTE, &image[0]);
				glBindTexture( GL_TEXTURE_2D, texture[0] );
				glBegin(GL_QUADS);
				glTexCoord2f( 1.0f, 0.0f ); glVertex2f(x+mapW*cellW,y+0);
				glTexCoord2f( 0.0f, 0.0f ); glVertex2f(x+0         ,y+0);
				glTexCoord2f( 0.0f, 1.0f ); glVertex2f(x+0         ,y+mapH*cellH);
				glTexCoord2f( 1.0f, 1.0f ); glVertex2f(x+mapW*cellW,y+mapH*cellH);
				glEnd( );
				if(!old_blend)
					glDisable(GL_BLEND);
				if(!old_texture_2d)
					glDisable(GL_TEXTURE_2D);
			} else {
				glState.doBlend(true);
				glState.doTexture(false);
				for (int dy=0; dy < mapH-1; dy++)
				{
					int midy = y + dy * cellH + cellH/2;
					for (int dx=0; dx < mapW-1; dx++)
					{
						glBegin(GL_TRIANGLE_FAN);
						//This interpolates to find the center color, then fans out to the four corners.
						int midx = x + dx * cellW + cellW/2;
						float mid_top_alpha = (map[mapW * dy + dx] + map[mapW * dy + dx + 1])/2;
						float mid_bottom_alpha = (map[mapW * (dy + 1) + dx] + map[mapW * (dy + 1) + dx + 1])/2;
						glColor4f(fr, color.g, color.b, (mid_top_alpha + mid_bottom_alpha) / 2);
						glVertex2f(midx, midy);
						//Touch each of the four corners
						glColor4f(fr, fg, fb, map[mapW * dy + dx]);
						glVertex2f(x + dx * cellW, y + dy * cellH);
						glColor4f(fr, fg, fb, map[mapW * (dy + 1) + dx]);
						glVertex2f(x + dx * cellW, y + (dy + 1) * cellH);

						glColor4f(fr, fg, fb, map[mapW * (dy + 1) + dx + 1]);
						glVertex2f(x + (dx+1) * cellW, y + (dy + 1) * cellH);
						glColor4f(fr, fg, fb, map[mapW * dy + dx + 1]);
						glVertex2f(x + (dx+1) * cellW, y + dy * cellH);

						glColor4f(fr, fg, fb, map[mapW * dy + dx]);
						glVertex2f(x + dx * cellW, y + dy * cellH);
						glEnd();
					}
				}
			}
		}
		else
	#endif
			DrawableSurface::drawAlphaMap(map, mapW, mapH, x, y, cellW, cellH, color);
	}

	void GraphicContext::drawAlphaMap(const std::valarray<unsigned char> &map, int mapW, int mapH, int x, int y, int cellW, int cellH, const Color &color)
	{
	#ifdef HAVE_OPENGL
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
			assert(mapW * mapH <= static_cast<int>(map.size()));
			if(EXPERIMENTAL) {
				glPushMatrix();
				glEnable(GL_BLEND);
				glEnable(GL_TEXTURE_2D);
/*				glState.resetCache();
				bool oldBlend=glState.doBlend(true);
				bool oldTexture=glState.doTexture(true);*/
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				std::valarray<GLubyte> image(mapW*mapH);
				for (int i=0; i<mapH; i++)
					for (int j=0; j<mapW;j++)
						image[i*mapW+j]=map[mapW*i+j];
				glColor4ub(color.r, color.g, color.b, color.a);
				GLuint texture[1];
				glGenTextures(1, &texture[0]);
				glBindTexture(GL_TEXTURE_2D, texture[0]);
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA,mapW,mapH, 0, GL_ALPHA, GL_UNSIGNED_BYTE, &image[0]);
				glBegin(GL_QUADS);
					glTexCoord2f( 1.0f, 0.0f ); glVertex2f(x+mapW*cellW,y+0);
					glTexCoord2f( 0.0f, 0.0f ); glVertex2f(x+0         ,y+0);
					glTexCoord2f( 0.0f, 1.0f ); glVertex2f(x+0         ,y+mapH*cellH);
					glTexCoord2f( 1.0f, 1.0f ); glVertex2f(x+mapW*cellW,y+mapH*cellH);
				glEnd( );
				glPopMatrix();
				//uploadToTexture();
//				glState.doBlend(oldBlend);
//				glState.doTexture(oldTexture);
			} else {
				glState.doBlend(true);
				glState.doTexture(false);
				for (int dy=0; dy < mapH-1; dy++)
				{
					int midy = y + dy * cellH + cellH/2;
					for (int dx=0; dx < mapW-1; dx++)
					{

						glBegin(GL_TRIANGLE_FAN);
						//This interpolates to find the center color, then fans out to the four corners.
						int midx = x + dx * cellW + cellW/2;
						int mid_top_alpha = (map[mapW * dy + dx] + map[mapW * dy + dx + 1])/2;
						int mid_bottom_alpha = (map[mapW * (dy + 1) + dx] + map[mapW * (dy + 1) + dx + 1])/2;
						glColor4ub(color.r, color.g, color.b, (mid_top_alpha + mid_bottom_alpha) / 2);
						glVertex2f(midx, midy);
						//Touch each of the four corners
						glColor4ub(color.r, color.g, color.b, map[mapW * dy + dx]);
						glVertex2f(x + dx * cellW, y + dy * cellH);
						glColor4ub(color.r, color.g, color.b, map[mapW * (dy + 1) + dx]);
						glVertex2f(x + dx * cellW, y + (dy + 1) * cellH);

						glColor4ub(color.r, color.g, color.b, map[mapW * (dy + 1) + dx + 1]);
						glVertex2f(x + (dx+1) * cellW, y + (dy + 1) * cellH);
						glColor4ub(color.r, color.g, color.b, map[mapW * dy + dx + 1]);
						glVertex2f(x + (dx+1) * cellW, y + dy * cellH);

						glColor4ub(color.r, color.g, color.b, map[mapW * dy + dx]);
						glVertex2f(x + dx * cellW, y + dy * cellH);
						glEnd();
					}
				}
			}
		}
		else
	#endif
			DrawableSurface::drawAlphaMap(map, mapW, mapH, x, y, cellW, cellH, color);
	}

	// compat... this is there because it sems gcc is not able to do function overloading with several levels of inheritance
	void GraphicContext::drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawPixel(x, y, Color(r, g, b, a));
	}

	void GraphicContext::drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawRect(x, y, w, h, Color(r, g, b, a));
	}

	void GraphicContext::drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawFilledRect(x, y, w, h, Color(r, g, b, a));
	}

	void GraphicContext::drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawLine(x1, y1, x2, y2, Color(r, g, b, a));
	}

	void GraphicContext::drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawLine(x, y, x, y+l, Color(r, g, b, a));
		else
		#endif
			 _drawVertLine(x, y, l, Color(r, g, b, a));
	}
	
	void GraphicContext::drawVertLine(int x, int y, int l, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawLine(x, y, x, y+l, color);
		else
		#endif
			 _drawVertLine(x, y, l, color);
	}

	void GraphicContext::drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawLine(x, y, x+l, y, Color(r, g, b, a));
		else
		#endif
			_drawHorzLine(x, y, l, Color(r, g, b, a));
	}
	
	void GraphicContext::drawHorzLine(int x, int y, int l, const Color& color)
	{
		#ifdef HAVE_OPENGL
		if (optionFlags & GraphicContext::USEGPU)
			drawLine(x, y, x+l, y, color);
		else
		#endif
			_drawHorzLine(x, y, l, color);
	}

	void GraphicContext::drawCircle(int x, int y, int radius, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		drawCircle(x, y, radius, Color(r, g, b, a));
	}

	void GraphicContext::setMinRes(int w, int h)
	{
		minW = w;
		minH = h;
		SDL_SetWindowMinimumSize(window, minW, minH);
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
		appIcon(icon),
		resizeTimer(0)
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
		TTF_Quit();
		SDL_Quit();
		sdlsurface = NULL;

		if (verbose)
			fprintf(stderr, "Toolkit : Graphic Context destroyed\n");
	}

	GraphicContext* GraphicContext::instance()
	{
		return _gc;
	}

	std::mutex m;
	void GraphicContext::createGLContext()
	{
		// enable GL context
		if (optionFlags & USEGPU)
		{
			std::lock_guard<std::mutex> l(m);
			if (!context)
			    context = SDL_GL_CreateContext(window);
			if (!context)
			    throw "no context";
			SDL_GL_MakeCurrent(window, context);
		}
	}
	void GraphicContext::unsetContext()
	{
		if (optionFlags & USEGPU)
		{
			SDL_GL_MakeCurrent(window, nullptr);
		}
	}
	bool GraphicContext::resChanged()
	{
		int w, h;
		SDL_GetWindowSize(window, &w, &h);
		return prevW != w || prevH != h;
	}
	SDL_Rect GraphicContext::getRes()
	{
		int w, h;
		SDL_Rect r;
		SDL_GetWindowSize(window, &w, &h);
		r = {0, 0, w, h};
		return r;
	}
	void GraphicContext::resetMatrices()
	{
		// https://gamedev.stackexchange.com/questions/62691/opengl-resize-problem
		int w = getW(), h = getH();
		glViewport(0, 0, w, h);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
	}

	SDL_Surface* GraphicContext::getOrCreateSurface(int w, int h, Uint32 flags) {
		std::unique_lock<std::recursive_mutex> lock(EventListener::renderMutex);
		if (flags & USEGPU)
		{
			if (sdlsurface)
				SDL_FreeSurface(sdlsurface);
			// Can't use SDL_GetWindowSurface with OpenGL; the documentation forbids it.
			sdlsurface = SDL_CreateRGBSurface(0, w, h, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
		}
		else
		{
			sdlsurface = SDL_GetWindowSurface(window);
		}
		return sdlsurface;
	}

	bool GraphicContext::setRes(int w, int h, Uint32 flags)
	{
		static bool isLoading = true;
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

		prevW = w;
		prevH = h;

		// set flags
		optionFlags = flags;
		Uint32 sdlFlags = 0;
		if (flags & FULLSCREEN)
			sdlFlags |= SDL_WINDOW_FULLSCREEN;
		// FIXME: window resize is broken
		if (flags & RESIZABLE && !isLoading)
		{
			sdlFlags |= SDL_WINDOW_RESIZABLE;
		}
		else
		{
			isLoading = false;
		}
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

		// if window exists, resize it
		if (window) {
			SDL_SetWindowSize(window, w, h);
			SDL_SetWindowResizable(window, SDL_TRUE);
			getOrCreateSurface(w, h, flags);
#ifdef HAVE_OPENGL
			if (flags & USEGPU)
			{
				resetMatrices();
			}
#endif
			setClipRect(0, 0, w, h);
			//nextFrame();
		}
		else {
			// create the new window and the surface
			window = SDL_CreateWindow(windowTitle.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, sdlFlags);
			sdlsurface = window != nullptr ? getOrCreateSurface(w, h, flags) : nullptr;
#ifdef HAVE_OPENGL
			// enable GL context
			if (flags & USEGPU)
			{
				if (!context)
					createGLContext();
				resetMatrices();
			}
#endif
		}

		// check surface
		if (!sdlsurface)
		{
			fprintf(stderr, "Toolkit : can't set screen to %dx%d at 32 bpp\n", w, h);
			fprintf(stderr, "Toolkit : %s\n", SDL_GetError());
			return false;
		}
		else
		{
			_gc = this;
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
				//_glFormat.colorkey = 0;
				//_glFormat.alpha = 255;
				//_glFormat = *SDL_AllocFormat(32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
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
				SDL_GL_SwapWindow(window);
				//fprintf(stderr, "%d allocated GPU textures\n", glState.alocatedTextureCount);
			}
			else
			#endif
			{
				SDL_UpdateWindowSurface(window);
			}
			if (resizeTimer)
				resizeTimer--;
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

	// Font stuff

	int Font::getStringWidth(const int i)
	{
		std::ostringstream temp;
		temp << i;
		return getStringWidth(temp.str());
	}

	int Font::getStringWidth(const std::string string, int len)
	{
		std::string temp;
		temp.append(string.c_str(), len);
		return getStringWidth(temp);
	}

	int Font::getStringHeight(const std::string string, int len)
	{
		std::string temp;
		temp.append(string.c_str(), len);
		return getStringHeight(temp);
	}

	int Font::getStringHeight(const int i)
	{
		std::ostringstream temp;
		temp << i;
		return getStringHeight(temp.str());
	}
}
