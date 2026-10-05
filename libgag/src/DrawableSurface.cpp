// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GraphicContextPrivate.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <assert.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <valarray>
#include <SDL3_image/SDL_image.h>
#include <SurfaceRaster.h>
#include <stdexcept>
#include <AssetLoader.h>
#ifdef GLOB2_WEBGL2
#include <set>
#endif

namespace GAGCore
{
#ifdef GLOB2_WEBGL2
    namespace { std::set<DrawableSurface*> gpuSurfaces; }
#endif
    std::uint64_t DrawableSurface::nextSurfaceIdentity()
    {
        static std::atomic<std::uint64_t> sequence{0};
        return sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    }
	SDL_Surface *DrawableSurface::convertForUpload(SDL_Surface *source)
	{
		// Color::pack/unpack and software drawing use _glFormat. A 32-bit
		// display is not necessarily BGRA (the browser uses RGBA), so loaded
		// and cloned sprites must use the same format as generated surfaces.
		// WebGL converts to RGBA separately at the texture upload boundary.
		SDL_Surface *dest = SDL_ConvertSurface(source, SDL_PIXELFORMAT_ARGB8888);
		assert(dest);
		return dest;
	}

    bool DrawableSurface::hasOpaquePixels()
    {
        if (opacityRevision != pixelRevision)
        {
            opaquePixels = SurfaceRaster::opaque(sdlsurface);
            opacityRevision = pixelRevision;
        }
        return opaquePixels;
    }

	// Drawable surface
	DrawableSurface::DrawableSurface(const std::string &imageFileName)
	{
		sdlsurface = NULL;
		if (!loadImage(imageFileName))
			setRes(0, 0);
		allocateTexture();
		prepareTexture();
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
		// beurk, const cast here because SDL API sucks
		sdlsurface = convertForUpload(const_cast<SDL_Surface *>(sourceSurface));
		assert(sdlsurface);
		setClipRect();
		allocateTexture();
		markPixelsChanged();
	}

    DrawableSurface::DrawableSurface(SDL_Surface *prepared, AdoptPixels, bool allocateGPU)
    {
        assert(prepared && prepared->format == SDL_PIXELFORMAT_ARGB8888);
        sdlsurface = prepared;
        setClipRect(); if (allocateGPU) allocateTexture(); markPixelsChanged();
    }

    std::unique_ptr<DrawableSurface> DrawableSurface::fromAssetImage(const AssetImage& image,
            bool exclusive, bool allocateGPU)
    {
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
            exclusive ? image.releaseSurface() : SDL_DuplicateSurface(image.surface), SDL_DestroySurface);
        if (!pixels) throw std::runtime_error(SDL_GetError());
        auto result = std::make_unique<DrawableSurface>(pixels.get(), AdoptPixels{}, allocateGPU);
        pixels.release();
        result->adoptUploadPreparation(image, exclusive);
        return result;
    }

    void DrawableSurface::adoptUploadPreparation(const AssetImage& image, bool exclusive)
    {
        if (exclusive) { preparedUploadPixels = std::move(image.uploadPixels); preparedMips = std::move(image.mips); }
        else { preparedUploadPixels = image.uploadPixels; preparedMips = image.mips; }
        preparedUploadRevision = pixelRevision;
    }
    void DrawableSurface::prepareTexture()
    {
        assert(!_gc || SDL_GetCurrentThreadID() == _gc->eventThread);
        if (_gc && _gc->portableRenderer) _gc->portableRenderer->prepareTexture(this, sdlsurface, pixelRevision);
        if (glUploadedRevision != pixelRevision) uploadToTexture();
    }

    size_t DrawableSurface::allocatedTextureBytes()
    {
#ifdef HAVE_OPENGL
        return glState.allocatedTextureBytes;
#else
        return 0;
#endif
    }

	DrawableSurface *DrawableSurface::clone(void)
	{
		auto copy=new DrawableSurface(sdlsurface);
		copy->highResolutionSampling=highResolutionSampling;
		return copy;
	}

	DrawableSurface::~DrawableSurface(void)
	{
		if (_gc && _gc->portableRenderer) _gc->portableRenderer->forget(this);
        if (_gc && _gc->softwareRasterizer) _gc->softwareRasterizer->forget(this);
		SDL_DestroySurface(sdlsurface);
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
		if (_gc && (_gc->optionFlags & GraphicContext::USEGPU))
		{
			glGenTextures(1, reinterpret_cast<GLuint*>(&texture));
			glState.allocatedTextureCount++;
#ifdef GLOB2_WEBGL2
            gpuSurfaces.insert(this);
#endif
			initTextureSize();
		}
		#endif
	}

	void DrawableSurface::initTextureSize(void)
	{
		if (!textureInfo && _gc && _gc->renderBatch)
			_gc->renderBatch->textureChanged(texture);
        if (!texture || textureInfo) return;
		#ifdef HAVE_OPENGL
		if (_gc && (_gc->optionFlags & GraphicContext::USEGPU))
		{
			// only power of two textures are supported
			if (!glState.isTextureSRectangle)
			{
				// TODO : if anyone has a better way to do it, please tell :-)
				glState.setTexture(texture);
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);

				int w = getMinPowerOfTwo(sdlsurface->w);
				int h = getMinPowerOfTwo(sdlsurface->h);
				glState.allocatedTextureBytes-=gpuBytes;gpuBytes=w*h*4;glState.allocatedTextureBytes+=gpuBytes;
				std::valarray<char> zeroBuffer((char)0, w * h * 4);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &zeroBuffer[0]);

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
		if (!textureInfo && _gc && _gc->renderBatch)
			_gc->renderBatch->textureChanged(texture);
		#ifdef HAVE_OPENGL
		if (textureInfo)
		{
			return;
		}
		if (_gc && (_gc->optionFlags & GraphicContext::USEGPU))
		{
			glState.setTexture(texture);

			void *pixelsPtr;
			GLenum pixelFormat;
#if defined(GLOB2_WEBGL2)
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(nullptr, SDL_DestroySurface);
#elif SDL_BYTEORDER == SDL_BIG_ENDIAN
            std::valarray<Uint32> tempPixels;
#endif
            if (preparedUploadRevision == pixelRevision && !preparedUploadPixels.empty()) {
                pixelsPtr = preparedUploadPixels.data(); pixelFormat = GL_RGBA;
            } else {
			#if defined(GLOB2_WEBGL2)
            rgba.reset(SDL_ConvertSurface(sdlsurface, SDL_PIXELFORMAT_RGBA32));
            if (!rgba) return;
            pixelsPtr = rgba->pixels;
            pixelFormat = GL_RGBA;
            #elif SDL_BYTEORDER == SDL_BIG_ENDIAN
			tempPixels.resize(sdlsurface->w * sdlsurface->h);
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
            }
			if (glState.isTextureSRectangle)
			{
				glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGBA, sdlsurface->w, sdlsurface->h, 0, pixelFormat, GL_UNSIGNED_BYTE, pixelsPtr);
				// Magnify with nearest-neighbour so a scaled fullscreen looks like the
				// software renderer's block scaling instead of a bilinear blur.
				glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			}
			else
			{
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sdlsurface->w, sdlsurface->h, pixelFormat, GL_UNSIGNED_BYTE, pixelsPtr);
                if(highResolutionSampling)
                {
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
                    AssetImage fallback(nullptr);
                    const auto *levels = &preparedMips;
                    if (preparedUploadRevision != pixelRevision || preparedMips.empty()) {
                        fallback.surface = SDL_DuplicateSurface(sdlsurface);
                        if (!fallback.surface) throw std::runtime_error(SDL_GetError());
                        fallback.prepareUpload(true);
                        levels = &fallback.mips;
                    }
                    glState.allocatedTextureBytes-=gpuBytes;gpuBytes=0;
                    // S3TC/DXT5 stores one 16-byte block per 4x4 pixel tile, a fixed
                    // 4:1 ratio versus RGBA8 regardless of encoder; letting the driver
                    // compress during upload needs no offline tool or vendored encoder.
                    // Block compression is undefined below 4x4, so the chain stops
                    // there instead of continuing to the 2x2/1x1 tail.
                    // WebGL2 only accepts pre-compressed data through
                    // compressedTexImage2D. Desktop GL may compress RGBA on
                    // upload, but WebGL2 rejects that internal format here.
#ifdef GLOB2_WEBGL2
                    const bool compress=false;
#else
                    const bool compress=glState.hasS3TCCompression;
#endif
                    const GLenum internalFormat=compress?GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:GL_RGBA;
                    for (size_t mip = 0; mip < levels->size(); ++mip) {
                        const auto &level = (*levels)[mip];
                        const int w = level.width, h = level.height;
                        if (compress && (w < 4 || h < 4)) { glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, int(mip) - 1); break; }
                        glTexImage2D(GL_TEXTURE_2D, int(mip), internalFormat, w, h, 0, pixelFormat, GL_UNSIGNED_BYTE, level.pixels.data());
                        const size_t bytes = compress ? size_t((w + 3) / 4) * ((h + 3) / 4) * 16 : size_t(w) * h * 4;
                        gpuBytes += bytes; glState.allocatedTextureBytes += bytes;
                    }

                }
			}
		}
		#endif
		glUploadedRevision = pixelRevision;
        std::vector<unsigned char>().swap(preparedUploadPixels);
        std::vector<AssetImage::Mip>().swap(preparedMips);
	}

	void DrawableSurface::freeGPUTexture(void)
	{
		if (!textureInfo && _gc && _gc->renderBatch)
			_gc->renderBatch->textureChanged(texture);
#ifdef GLOB2_WEBGL2
        gpuSurfaces.erase(this);
#endif
        if (!texture) return;
		#ifdef HAVE_OPENGL
		if (_gc && texture && (_gc->optionFlags & GraphicContext::USEGPU))
		{
			glDeleteTextures(1, reinterpret_cast<const GLuint*>(&texture));
			glState.allocatedTextureCount--;
			glState.allocatedTextureBytes-=gpuBytes;gpuBytes=0;
			if(glState._texture==static_cast<GLint>(texture))glState._texture=-1;
			texture=0;

			// The next line causes a desynchronization between _doScissors and glIsEnabled(GL_SCISSOR_TEST),
			// which causes the setClipRect() functions to not reset the clipping the way it should,  so many
			// things don't get drawn properly and the game appears to "blink". Commenting it out didn't cause
			// any other problems.  If you think glState should be reset,  feel free to do so,  but also call
			// functions like glDisable() as required.

			//glState.resetCache();
		}
		#endif
	}

	void DrawableSurface::setRes(int w, int h)
	{
		if (sdlsurface)
			SDL_DestroySurface(sdlsurface);

		sdlsurface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
		if (!sdlsurface) throw std::runtime_error(SDL_GetError());
		setClipRect();
		initTextureSize();
		markPixelsChanged();
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

		SDL_SetSurfaceClipRect(sdlsurface, &clipRect);
	}

	void DrawableSurface::setClipRect(void)
	{
		assert(sdlsurface);

		clipRect.x = 0;
		clipRect.y = 0;
		clipRect.w = static_cast<Uint16>(sdlsurface->w);
		clipRect.h = static_cast<Uint16>(sdlsurface->h);

		SDL_SetSurfaceClipRect(sdlsurface, &clipRect);
	}

    bool DrawableSurface::loadImage(const std::string name)
    {
        if (name.empty()) return false;
        auto &loader = Toolkit::assets();
        auto request = loader.requestImage(name);
        if (!loader.wait(request)) return false;
        auto image = request.take();
        const bool exclusive = bool(image);
        if (!image) image = request.get();
        SDL_Surface *prepared = exclusive ? image->releaseSurface() : SDL_DuplicateSurface(image->surface);
        if (!prepared) return false;
        SDL_DestroySurface(sdlsurface);
        sdlsurface = prepared;
        setClipRect(); markPixelsChanged();
        adoptUploadPreparation(*image, exclusive);
        initTextureSize();
        if (texture || (_gc && _gc->portableRenderer)) prepareTexture();
        return true;
    }

	void DrawableSurface::shiftHSV(float hue, float sat, float lum)
	{
		Uint32 *mem = (Uint32 *)sdlsurface->pixels;
		for (size_t i = 0; i < static_cast<size_t>(sdlsurface->w * sdlsurface->h); i++)
		{
			// get values
			float h, s, v;
			Color c;
			// Sprite surfaces retain alpha even when the software window has none.
			SDL_GetRGBA(*mem, SDL_GetPixelFormatDetails(sdlsurface->format), SDL_GetSurfacePalette(sdlsurface), &c.r, &c.g, &c.b, &c.a);
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
			*mem = SDL_MapSurfaceRGBA(sdlsurface, c.r, c.g, c.b, c.a);
			mem++;
		}
		markPixelsChanged();
	}
}

#ifdef GLOB2_WEBGL2
namespace GAGCore {
void GraphicContext::restoreBrowserContext()
{
    if (!_gc || !(_gc->optionFlags & USEGPU)) return;
    _gc->resetRenderPacing();
    // GL objects owned outside libgag, such as the torus overview's, belong to
    // the lost context; a new generation tells them to recreate, not reuse.
    ++_gc->glContextGeneration;
    // Names from the lost context cannot be reused or deleted in the new one.
    _gc->skinResources = {};
    glState.resetCache();
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, _gc->getW(), _gc->getH(), 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    _gc->applyGLViewport();
    // CPU surfaces, including sprite atlases, remain the source of truth.
    // Rebuild GPU objects without touching game, camera, or UI state.
    glState.allocatedTextureCount = 0;
    for (auto* surface : gpuSurfaces) {
        glDeleteTextures(1, &surface->texture);
        surface->texture = 0;
        if (surface->textureInfo) continue;
        glGenTextures(1, &surface->texture);
        ++glState.allocatedTextureCount;
        surface->initTextureSize();
        surface->uploadToTexture();
    }
    _gc->setClipRect();
}
}
#endif
