// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GraphicContextPrivate.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <assert.h>
#include <algorithm>
#include <string>
#include <valarray>
#include <SDL_image.h>

namespace GAGCore
{
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
	static T getMinPowerOfTwo(T t)
	{
		T v = 1;
		while (v < t)
			v *= 2;
		return v;
	}

	void DrawableSurface::allocateTexture(void)
	{
		#ifdef HAVE_OPENGL
		if (textureInfo)
			return;
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
		if (textureInfo)
		{
			return;
		}
		if (_gc->optionFlags & GraphicContext::USEGPU)
		{
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
}
