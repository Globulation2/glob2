// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "MipReduction.h"
#include "GraphicContextPrivate.h"
#include <RenderDevice.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <assert.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <valarray>
#include <SDL3_image/SDL_image.h>
#include <SurfaceRaster.h>
#include <stdexcept>
#include <AssetLoader.h>
#include <ApplicationHost.h>
#include <set>

namespace GAGCore
{
    namespace { std::set<DrawableSurface*> liveSurfaces; }
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
        if (sdlsurface && opacityRevision != pixelRevision)
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
        liveSurfaces.insert(this);
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

    std::unique_ptr<DrawableSurface> DrawableSurface::fromReducedLevel(const AssetImage& image, unsigned level,
            int width, int height)
    {
        if (level >= image.mips.size()) throw std::invalid_argument("No such prepared mip level");
        const auto &mip = image.mips[level];
        if (width <= 0 || height <= 0 || width > mip.width || height > mip.height ||
            mip.pixels.size() < size_t(mip.width) * mip.height * 4)
            throw std::invalid_argument("Reduced level outside its prepared mip");
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
            SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        if (!pixels) throw std::runtime_error(SDL_GetError());
        // Levels hold the decoded surface's byte order, or RGBA where the
        // workers prepared RGBA upload pixels (AssetImage::prepareUpload). The
        // content is the level's top-left corner: the chain pads to powers of two.
        const auto format = image.uploadPixels.empty() ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_RGBA32;
        if (!SDL_ConvertPixels(width, height, format, mip.pixels.data(), mip.width * 4,
                               SDL_PIXELFORMAT_ARGB8888, pixels->pixels, pixels->pitch))
            throw std::runtime_error(SDL_GetError());
        auto result = std::make_unique<DrawableSurface>(pixels.get(), AdoptPixels{}, true);
        pixels.release();
        return result;
    }

    void DrawableSurface::adoptUploadPreparation(const AssetImage& image, bool exclusive)
    {
        if (exclusive) { preparedUploadPixels = std::move(image.uploadPixels); preparedMips = std::move(image.mips); }
        else { preparedUploadPixels = image.uploadPixels; preparedMips = image.mips; }
        preparedCompression = image.compression;
        preparedUploadRevision = pixelRevision;
    }
    void DrawableSurface::prepareTexture()
    {
        assert(!_gc || SDL_GetCurrentThreadID() == _gc->eventThread);
        if (_gc && _gc->portableRenderer) _gc->portableRenderer->prepareTexture(this, sdlsurface, pixelRevision, highResolutionSampling);
        // Device adapters consume the authored chain on draw and need it again
        // after switching or recovery. Legacy upload cleanup must not discard
        // that restoration source when there is no legacy GL allocation.
        if ((!_gc || !_gc->presentationDevice) && glUploadedRevision != pixelRevision) uploadToTexture();
    }

    size_t DrawableSurface::allocatedTextureBytes()
    {
#ifdef HAVE_OPENGL
        return glState.allocatedTextureBytes;
#else
        return 0;
#endif
    }

    DrawableSurface::ResidentPixels DrawableSurface::residentPixels()
    {
        ResidentPixels total;
        for (const auto *surface : liveSurfaces)
        {
            if (surface->sdlsurface && surface->sdlsurface->pixels)
            {
                ++total.surfaces;
                total.surfaceBytes += size_t(surface->sdlsurface->pitch) * surface->sdlsurface->h;
            }
            total.preparedBytes += surface->preparedUploadPixels.capacity();
            for (const auto &mip : surface->preparedMips)
                total.preparedBytes += mip.pixels.capacity() + mip.compressed.capacity();
        }
        return total;
    }

    bool DrawableSurface::releasePixels()
    {
#if defined(HAVE_OPENGL)
        // Device and software renderers read the pixels on draw; an atlas frame
        // has no texture of its own. After a WebGL context loss the browser's
        // legacy GL drops released frames to native and reloads them
        // (restoreBrowserContext), as desktop does after a context rebuild.
        if (!sdlsurface || this == _gc || textureInfo || !texture || glUploadedRevision != pixelRevision ||
            !_gc || !(_gc->optionFlags & GraphicContext::USEGPU) || _gc->renderDevice() ||
            _gc->backendSwitchStatus().active != RendererBackend::LegacyGL)
            return false;
        hasOpaquePixels(); // Answered from the pixels one last time, then from the cache.
        releasedW = sdlsurface->w;
        releasedH = sdlsurface->h;
        SDL_DestroySurface(sdlsurface);
        sdlsurface = nullptr;
        std::vector<unsigned char>().swap(preparedUploadPixels);
        std::vector<AssetImage::Mip>().swap(preparedMips);
        return true;
#else
        return false;
#endif
    }

    bool DrawableSurface::releasePortablePixels()
    {
        if (!sdlsurface || this == _gc || textureInfo || !_gc || !_gc->portableRenderer ||
            _gc->renderer != _gc->portableRenderer.get() ||
            !_gc->portableRenderer->hasTexture(this, contentRevision()))
            return false;
        hasOpaquePixels(); // Answered from the pixels one last time, then from the cache.
        releasedW = sdlsurface->w;
        releasedH = sdlsurface->h;
        SDL_DestroySurface(sdlsurface);
        sdlsurface = nullptr;
        std::vector<unsigned char>().swap(preparedUploadPixels);
        std::vector<AssetImage::Mip>().swap(preparedMips);
        releasedOnPortable = true;
        return true;
    }

    bool DrawableSurface::portableTextureLost() const
    {
        return releasedOnPortable && !(_gc && _gc->portableRenderer && _gc->renderer == _gc->portableRenderer.get() &&
            _gc->portableRenderer->hasTexture(this, contentRevision()));
    }

    bool DrawableSurface::releaseDevicePixels()
    {
        // Releasing frees a few large blocks; spreading them keeps frames even.
        constexpr unsigned ReleasesPerFrame = 16;
        if (!sdlsurface || this == _gc || textureInfo || !_gc || !_gc->deviceRenderer ||
            _gc->renderer != _gc->deviceRenderer.get() || _gc->offscreenPass ||
            _gc->deviceReleasesThisFrame >= ReleasesPerFrame ||
            !retainDeviceTexture(*_gc->deviceRenderer, this, contentRevision()))
            return false;
        ++_gc->deviceReleasesThisFrame;
        hasOpaquePixels(); // Answered from the pixels one last time, then from the cache.
        releasedW = sdlsurface->w;
        releasedH = sdlsurface->h;
        SDL_DestroySurface(sdlsurface);
        sdlsurface = nullptr;
        std::vector<unsigned char>().swap(preparedUploadPixels);
        std::vector<AssetImage::Mip>().swap(preparedMips);
        releasedOnDevice = true;
        return true;
    }

    bool DrawableSurface::deviceHoldsPixels() const
    {
        if (!_gc || !_gc->deviceRenderer || _gc->renderer != _gc->deviceRenderer.get()) return true;
        return _gc->deviceRenderer->hasTexture(this, contentRevision());
    }

    bool DrawableSurface::takeDeviceUpload()
    {
        constexpr unsigned UploadsPerFrame = 8;
        if (!_gc || !_gc->deviceRenderer || _gc->renderer != _gc->deviceRenderer.get() || _gc->offscreenPass) return true;
        if (_gc->deviceUploadsThisFrame >= UploadsPerFrame) return false;
        ++_gc->deviceUploadsThisFrame;
        return true;
    }

    bool DrawableSurface::prepareDeviceTexture()
    {
        // Plain nearest-sampled surfaces only: linear and mipmapped ones admit
        // other texture descriptors when they draw.
        if (!sdlsurface || !_gc || !_gc->deviceRenderer || _gc->renderer != _gc->deviceRenderer.get() ||
            _gc->offscreenPass || highResolutionSampling || singleLevelSampling || spriteSource)
            return false;
        if (deviceHoldsPixels()) return true;
        if (!takeDeviceUpload()) return false;
        // The budget class and alpha a draw admits it with (GraphicContext::drawSurface).
        auto &backend = *_gc->deviceRenderer;
        backend.textureBudgetClass(deviceBudgetClass);
        backend.prepareTexture(this, sdlsurface, pixelRevision);
        backend.textureBudgetClass(TextureDescriptor::BudgetClass::General);
        return deviceHoldsPixels();
    }

    bool DrawableSurface::deviceBacked() const
    {
        return releasedOnDevice && !sdlsurface && _gc && _gc->deviceRenderer &&
            _gc->deviceRenderer->hasTexture(this, pixelRevision);
    }

	DrawableSurface *DrawableSurface::clone(void)
	{
		assert(sdlsurface);
		auto copy=new DrawableSurface(sdlsurface);
		copy->highResolutionSampling=highResolutionSampling;
		return copy;
	}

	DrawableSurface::~DrawableSurface(void)
	{
        liveSurfaces.erase(this);
		if (_gc && _gc->portableRenderer) _gc->portableRenderer->forget(this);
        if (_gc && _gc->softwareRasterizer) _gc->softwareRasterizer->forget(this);
        if (_gc && _gc->deviceRenderer) _gc->deviceRenderer->forget(this);
        // An offscreen pass draws through its own backend, which none of the above own. Its texture
        // cache is keyed by this address and revision, so a surface later allocated at the same
        // address (every streamed terrain page of a whole-map render) would draw this one's pixels.
        if (_gc && _gc->offscreenPass && _gc->renderer) _gc->renderer->forget(this);
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

	void DrawableSurface::allocateTexture(bool initializeStorage)
	{
        liveSurfaces.insert(this);
		#ifdef HAVE_OPENGL
		if (textureInfo)
			return;
		if (_gc && _gc->context && (_gc->optionFlags & GraphicContext::USEGPU))
		{
			glGenTextures(1, reinterpret_cast<GLuint*>(&texture));
			glState.allocatedTextureCount++;
			if (initializeStorage) initTextureSize();
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
#ifdef GLOB2_WEBGL2
                // WebGL zero-initializes storage for a null pixels argument.
                // Avoid a second texture-sized allocation in the bounded Wasm heap.
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
				// Value initialization preserves zeroed native texture padding without
				// an instrumented per-byte valarray construction loop.
				std::unique_ptr<char[]> zeroBuffer(new char[std::size_t(w) * h * 4]());
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, zeroBuffer.get());
#endif

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
		if (!sdlsurface) return; // Released pixels: the texture is the only copy.
		if (!textureInfo && _gc && _gc->renderBatch)
			_gc->renderBatch->textureChanged(texture);
		#ifdef HAVE_OPENGL
		if (textureInfo)
		{
			return;
		}
		if (_gc && (_gc->optionFlags & GraphicContext::USEGPU))
		{
            // Restoration and live activation both discard absent GL names.
            if (!texture) allocateTexture();
			glState.setTexture(texture);

			void *pixelsPtr;
			GLenum pixelFormat;
#if defined(GLOB2_WEBGL2)
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(nullptr, SDL_DestroySurface);
#elif SDL_BYTEORDER == SDL_BIG_ENDIAN
            std::valarray<Uint32> tempPixels;
#endif
            if (highResolutionSampling && !glState.isTextureSRectangle) {
                // The HD branch uploads its prepared mip chain, including
                // level zero. Converting this surface here would create an
                // unused full-size RGBA copy on every terrain animation update.
                pixelsPtr = nullptr;
#if defined(GLOB2_WEBGL2) || SDL_BYTEORDER == SDL_BIG_ENDIAN
                pixelFormat = GL_RGBA;
#else
                pixelFormat = GL_BGRA;
#endif
            } else if (preparedUploadRevision == pixelRevision && !preparedUploadPixels.empty()) {
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
                if(highResolutionSampling)
                {
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
                    // A render device may have kept only premultiplied blocks,
                    // which this context never uploads: prepare the chain again.
                    if (!preparedMips.empty() && preparedMips.front().pixels.empty())
                        std::vector<AssetImage::Mip>().swap(preparedMips);
                    if ((preparedUploadRevision != pixelRevision || preparedMips.empty()) &&
                        sdlsurface->w > 0 && sdlsurface->h > 0 &&
                        sdlsurface->pitch == sdlsurface->w * 4 &&
                        !(sdlsurface->w & (sdlsurface->w - 1)) && !(sdlsurface->h & (sdlsurface->h - 1))) {
#ifdef GLOB2_WEBGL2
                        const bool generated = ApplicationHost::prepareOpenGLMipmaps(
                            texture, sdlsurface->pixels, sdlsurface->w, sdlsurface->h);
#else
                        const bool generated = prepareNativeMipmaps(texture, sdlsurface);
#endif
                        if (generated) {
                            glState.allocatedTextureBytes -= gpuBytes; gpuBytes = 0;
                            int w = sdlsurface->w, h = sdlsurface->h;
                            for (;;) {
                                gpuBytes += size_t(w) * h * 4;
                                if (w == 1 && h == 1) break;
                                w = std::max(1, w / 2); h = std::max(1, h / 2);
                            }
                            glState.allocatedTextureBytes += gpuBytes;
                            glUploadedRevision = pixelRevision;
                            std::vector<unsigned char>().swap(preparedUploadPixels);
                            std::vector<AssetImage::Mip>().swap(preparedMips);
                            return;
                        }
                    }
#if !defined(GLOB2_WEBGL2) && SDL_BYTEORDER == SDL_LIL_ENDIAN
                    if ((preparedUploadRevision != pixelRevision || preparedMips.empty()) &&
                        sdlsurface->format == SDL_PIXELFORMAT_ARGB8888 &&
                        sdlsurface->w > 0 && sdlsurface->h > 0 &&
                        sdlsurface->pitch == sdlsurface->w * 4 &&
                        !(sdlsurface->w & (sdlsurface->w - 1)) && !(sdlsurface->h & (sdlsurface->h - 1))) {
                        // Generated pages can upload their existing pixels and
                        // reuse the first reduced level for the rest of the chain.
                        int w = sdlsurface->w, h = sdlsurface->h;
                        const auto scratchBytes = size_t(std::max(1, w / 2)) * std::max(1, h / 2) * 4;
                        auto scratch = std::make_unique_for_overwrite<unsigned char[]>(scratchBytes);
                        const auto *pixels = static_cast<const unsigned char *>(sdlsurface->pixels);
                        size_t bytes = 0;
                        int level = 0;
                        for (;;) {
                            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
                            bytes += size_t(w) * h * 4;
                            if (w == 1 && h == 1) break;
                            downsampleMip(pixels, w, h, scratch.get());
                            pixels = scratch.get();
                            w = std::max(1, w / 2);
                            h = std::max(1, h / 2);
                            ++level;
                        }
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level);
                        glState.allocatedTextureBytes -= gpuBytes;
                        gpuBytes = bytes;
                        glState.allocatedTextureBytes += gpuBytes;
                        glUploadedRevision = pixelRevision;
                        std::vector<unsigned char>().swap(preparedUploadPixels);
                        std::vector<AssetImage::Mip>().swap(preparedMips);
                        return;
                    }
#endif
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
                    // Authored artwork has worker-prepared mip levels. Generated
                    // or edited pixels use the fallback chain and can change on
                    // every animation phase; driver-side DXT encoding stalls the
                    // render thread for those updates.
                    // Only blocks a worker prepared: the driver never encodes on
                    // this thread. Anything else uploads uncompressed.
                    // And only in the format this context decodes: artwork decoded
                    // before the context existed may hold another.
                    const bool compress=glState.blockCompression != AssetImage::BlockCompression::Uncompressed &&
                        preparedCompression == glState.blockCompression &&
                        preparedUploadRevision == pixelRevision && !preparedMips.empty() &&
                        !preparedMips.front().compressed.empty();
#endif
                    const GLenum internalFormat=compress?GLenum(glState.blockCompressionFormat):GL_RGBA;
                    // A detail band leaves out the largest levels: the texture's
                    // level zero is a smaller prepared level, sampled through the
                    // same normalized coordinates. Blocks need 4x4 levels.
                    size_t first = std::min<size_t>(uploadSkipLevels, levels->empty() ? 0 : levels->size() - 1);
                    while (compress && first > 0 && ((*levels)[first].width < 4 || (*levels)[first].height < 4)) --first;
                    // An HD sprite frame samples its band's level alone.
                    const size_t end = singleLevelSampling ? first + 1 : levels->size();
                    if (singleLevelSampling) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
                    for (size_t mip = first; mip < end; ++mip) {
                        const auto &level = (*levels)[mip];
                        const int w = level.width, h = level.height, glLevel = int(mip - first);
                        if (compress && (w < 4 || h < 4)) { glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, glLevel - 1); break; }
                        if (compress)
                            glCompressedTexImage2D(GL_TEXTURE_2D, glLevel, internalFormat, w, h, 0,
                                GLsizei(level.compressed.size()), level.compressed.data());
                        else
                            glTexImage2D(GL_TEXTURE_2D, glLevel, internalFormat, w, h, 0, pixelFormat, GL_UNSIGNED_BYTE, level.pixels.data());
                        const size_t bytes = compress ? size_t((w + 3) / 4) * ((h + 3) / 4) * 16 : size_t(w) * h * 4;
                        gpuBytes += bytes; glState.allocatedTextureBytes += bytes;
                    }

                }
                else
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sdlsurface->w, sdlsurface->h, pixelFormat, GL_UNSIGNED_BYTE, pixelsPtr);
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
        if (!texture) return;
		#ifdef HAVE_OPENGL
		if (_gc && texture && (_gc->optionFlags & GraphicContext::USEGPU))
		{
			glDeleteTextures(1, reinterpret_cast<const GLuint*>(&texture));
			glState.allocatedTextureCount--;
			glState.allocatedTextureBytes-=gpuBytes;gpuBytes=0;
			if(glState._texture==static_cast<GLint>(texture))glState._texture=-1;
			texture=0;
			// Nothing is uploaded now: a later draw uploads the pixels again.
			glUploadedRevision=0;

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
    if (_gc && _gc->presentationDevice) {
        try { _gc->recoverPresentationDevice(); }
        catch (const std::exception &error) { _gc->handleDeviceFailure(error.what()); }
        return;
    }
#ifdef HAVE_OPENGL
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
    glState.allocatedTextureBytes = 0;
    for (auto* surface : liveSurfaces) {
        surface->texture = 0;
        surface->gpuBytes = 0;
        surface->glUploadedRevision = 0;
    }
    // HD frames whose pixels were released after upload had only their
    // textures: draw native and reload them on the asset workers.
    Sprite::discardUnbackedHighResolution();
    _gc->setClipRect();
#endif
}
}
#endif

namespace GAGCore {
void GraphicContext::releaseSurfaceTextures()
{
#ifdef HAVE_OPENGL
    for (auto *surface : liveSurfaces)
        if (surface != _gc) surface->freeGPUTexture();
    // Frames whose pixels were released had only their textures.
    Sprite::discardUnbackedHighResolution();
#endif
}
void GraphicContext::restoreSurfaceTextures()
{
#ifdef HAVE_OPENGL
    for (auto *surface : liveSurfaces) {
        if (surface == _gc || surface->textureInfo || !surface->sdlsurface ||
            surface->getW() <= 0 || surface->getH() <= 0) continue;
        surface->allocateTexture();
        surface->uploadToTexture();
    }
#endif
}
}
