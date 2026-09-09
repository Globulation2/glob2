// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <GraphicContext.h>
#include <math.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <assert.h>
#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

#if __cplusplus >= 201402L
#include <memory>
using std::make_unique;
#else
#if BOOST_VERSION >= 107500
#include <boost/smart_ptr/make_unique.hpp>
#elif BOOST_VERSION >= 106300
#include <boost/make_unique.hpp>
#elif BOOST_VERSION >= 105700
#include <boost/move/make_unique.hpp>
#else
#error "Can't make_unique when there's no Boost and C++ standard is earlier than C++14"
#endif
using boost::make_unique;
#endif // __cplusplus

#define GL_GLEXT_PROTOTYPES
#ifdef HAVE_OPENGL
#if defined(__APPLE__) || defined(OPENGL_HEADER_DIRECTORY_OPENGL)
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/glu.h>
#define GL_TEXTURE_RECTANGLE_NV GL_TEXTURE_RECTANGLE_EXT
#else
#include <epoxy/gl.h>
#ifdef _WIN32
#include <epoxy/wgl.h>
#else
#include <epoxy/glx.h>
#endif // _WIN32
#endif // defined(__APPLE__)
#endif // ifdef HAVE_OPENGL


namespace GAGCore
{
	Sprite::RotatedImage::~RotatedImage()
	{
		delete orig;
		for (RotationMap::iterator it = rotationMap.begin(); it != rotationMap.end(); ++it)
		{
			delete it->second;
		}
	}
	
	bool Sprite::load(const std::string filename)
	{
		SDL_RWops *frameStream;
		SDL_RWops *rotatedStream;
		unsigned i = 0;
		
		this->fileName = filename;
		
		while (true)
		{
			std::ostringstream frameName;
			frameName << filename << i << ".png";
			frameStream = Toolkit::getFileManager()->open(frameName.str().c_str(), "rb");
	
			std::ostringstream frameNameRot;
			frameNameRot << filename << i << "r.png";
			rotatedStream = Toolkit::getFileManager()->open(frameNameRot.str().c_str(), "rb");
	
			if (!((frameStream) || (rotatedStream)))
				break;
	
			loadFrame(frameStream, rotatedStream);
	
			if (frameStream)
				SDL_RWclose(frameStream);
			if (rotatedStream)
				SDL_RWclose(rotatedStream);
			i++;
		}
		// TODO: How to cache rotated images?
		if (std::any_of(images.begin(), images.end(), [](DrawableSurface *s) {return s != nullptr; }) &&
			std::all_of(rotated.begin(), rotated.end(), [](RotatedImage *s) {return s == nullptr; }))
		{
			createTextureAtlas();
		}
		
		return getFrameCount() > 0;
	}

#ifdef DEBUG_SPRITE_NOT_DRAWN
	std::vector<Sprite*> Sprite::sprites;
#endif

	void Sprite::checkAllSpritesDrawn()
	{
#ifdef DEBUG_SPRITE_NOT_DRAWN
		for (const Sprite* sprite : sprites)
			if (sprite->vertices.size() || sprite->texCoords.size())
			{
				std::cout << "Warning: Sprite " << sprite->fileName << " has not been drawn" << std::endl;
			}
#endif
	}

	// Create texture atlas for images array
	// Using a sprite sheet lets us efficiently drawn terrain and water with a few calls
	// to glDrawArrays, rather than 272 individual calls to glBegin...glEnd.
	bool Sprite::createTextureAtlas()
	{
#ifdef HAVE_OPENGL
		if (!Toolkit::gc || !(Toolkit::gc->getOptionFlags() & GraphicContext::USEGPU))
			return false;
#ifdef DEBUG_SPRITE_NOT_DRAWN
		sprites.push_back(this);
#endif
		size_t numImages = images.size();
		int tileWidth = 0, tileHeight = 0;
		static int maxTextureSize = 0;
		if (!maxTextureSize)
		{
			glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
			assert(maxTextureSize);
		}
		// Check all tiles have the same size
		for (auto image : images)
		{
			if (!image)
				return false; // One of the images is null
			if (!tileWidth || !tileHeight)
			{
				tileWidth = image->getW();
				tileHeight = image->getH();
			}
			if (image->getW() != tileWidth || image->getH() != tileHeight)
				return false; // One of them has a different size
		}
		int sheetWidth = tileWidth * (static_cast<int>(sqrt(numImages)) + 1);
		int sheetHeight = tileHeight * (static_cast<int>(sqrt(numImages)) + 1);
		if (sheetWidth > maxTextureSize || sheetHeight > maxTextureSize)
		{
			std::cerr << "Warning: Sprite sheet " << fileName << " with size " << sheetWidth << "x" << sheetHeight
				<< " exceeds your graphics card's maximum texture size of " << maxTextureSize << std::endl;
			return false; // We can't continue, falling back to glBegin/glEnd rendering.
		}
		std::unique_ptr<DrawableSurface> atlas = make_unique<DrawableSurface>(sheetWidth, sheetHeight);
		int x = 0, y = 0;
		for (auto image: images)
		{
			atlas->drawSurface(x, y, image);
			TextureInfo info = { this, x, y, tileWidth, tileHeight };
			image->textureInfo = info;
			image->texMultX = 1.f;
			image->texMultY = 1.f;
			x += tileWidth;
			if (tileWidth + x > sheetWidth) {
				x = 0;
				y += tileHeight;
			}
		}
		atlas->uploadToTexture();
		this->atlas = std::move(atlas);
		glGenBuffers(1, &vbo);
		glGenBuffers(1, &texCoordBuffer);
		return true; // Success
#else
		return false;
#endif
	}
	
	DrawableSurface *Sprite::getRotatedSurface(int index)
	{
		if (compositeOnly)
		{
			// Sharp UI draws must not repopulate a second retained team-color cache.
			float baseHue, hue, sat, lum;
			Color(51,255,153).getHSV(&baseHue, &sat, &lum);
			actColor.getHSV(&hue, &sat, &lum);
			transientColor.reset(rotated[index]->orig->clone());
			transientColor->shiftHSV(hue-baseHue, 0, 0);
			return transientColor.get();
		}
		RotatedImage::RotationMap::const_iterator it = rotated[index]->rotationMap.find(actColor);
		DrawableSurface *ds;
		if (it == rotated[index]->rotationMap.end())
		{
			// compute hue shift
			float baseHue, actHue, lum, sat;
			float hueShift;
			Color(51, 255, 153).getHSV(&baseHue, &sat, &lum);
			actColor.getHSV(&actHue, &sat, &lum);
			hueShift = actHue - baseHue;
			
			// rotate image
			ds = rotated[index]->orig->clone();
			ds->shiftHSV(hueShift, 0.0f, 0.0f);
			
			// write back
			rotated[index]->rotationMap[actColor] = ds;
		}
		else
		{
			ds = it->second;
		}
		return ds;
	}

	DrawableSurface *Sprite::getCachedComposite(const std::vector<std::pair<int, int>> &frames)
	{
		assert(!frames.empty());
		// Switch this sprite to final-image caching without keeping source recolors.
		if (!compositeOnly)
		{
			for (auto layer : rotated)
			{
				if (!layer)
					continue;
				for (auto &cached : layer->rotationMap)
					delete cached.second;
				layer->rotationMap.clear();
			}
			compositeOnly = true;
		}
		CompositeKey key{actColor, frames};
		auto found = compositeCache.find(key);
		if (found != compositeCache.end())
		{
			++compositeHits;
			return found->second.get();
		}
		++compositeMisses;
		const int width = getW(frames.front().first), height = getH(frames.front().first);
		// Accumulate premultiplied RGBA so the result can be drawn over any background.
		std::vector<double> pixels(width * height * 4, 0);
		float baseHue, hue, sat, lum;
		Color(51, 255, 153).getHSV(&baseHue, &sat, &lum);
		actColor.getHSV(&hue, &sat, &lum);
		for (auto frame : frames)
		{
			assert(checkBound(frame.first));
			assert(getW(frame.first) == width && getH(frame.first) == height);
			assert(frame.second >= 0 && frame.second <= 255);
			for (int layer = 0; layer < 2; ++layer)
			{
				DrawableSurface *source = images[frame.first];
				if (layer == 1)
				{
					if (!rotated[frame.first])
						continue;
					source = rotated[frame.first]->orig;
				}
				if (!source)
					continue;
				auto raw = source->getSDLSurface();
				assert(raw->format->BytesPerPixel == 4);
				SDL_LockSurface(raw);
				for (int y = 0; y < height; ++y)
					for (int x = 0; x < width; ++x)
					{
						Uint8 r, g, b, a;
						SDL_GetRGBA(reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(raw->pixels) +
						                                       y * raw->pitch)[x],
						            raw->format, &r, &g, &b, &a);
						if (layer == 1)
						{
							Color color(r, g, b, a);
							float h, s, v;
							color.getHSV(&h, &s, &v);
							h += hue - baseHue;
							if (h >= 360)
								h -= 360;
							if (h < 0)
								h += 360;
							color.setHSV(h, s, v);
							r = color.r;
							g = color.g;
							b = color.b;
						}
						double alpha = double(a) * frame.second / (255.0 * 255.0);
						auto dest = &pixels[(y * width + x) * 4];
						dest[0] = r * alpha + dest[0] * (1 - alpha);
						dest[1] = g * alpha + dest[1] * (1 - alpha);
						dest[2] = b * alpha + dest[2] * (1 - alpha);
						dest[3] = alpha + dest[3] * (1 - alpha);
					}
				SDL_UnlockSurface(raw);
			}
		}
		std::unique_ptr<DrawableSurface> result(new DrawableSurface(width, height));
		auto raw = result->getSDLSurface();
		SDL_LockSurface(raw);
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x)
			{
				auto pixel = &pixels[(y * width + x) * 4];
				auto byte = [](double v)
				{ return static_cast<Uint8>(std::min(255.0, std::max(0.0, std::round(v)))); };
				double a = pixel[3];
				reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(raw->pixels) + y * raw->pitch)[x] =
				    SDL_MapRGBA(raw->format, a ? byte(pixel[0] / a) : 0, a ? byte(pixel[1] / a) : 0,
					            a ? byte(pixel[2] / a) : 0, byte(a * 255));
			}
		SDL_UnlockSurface(raw);
		result->dirty = true;
		// Include CPU pixels and GPU allocation (rectangle or padded power-of-two).
		int texW = 1, texH = 1;
		while (texW < width)
			texW *= 2;
		while (texH < height)
			texH *= 2;
		size_t bytes = raw->pitch * height;
		if (Toolkit::gc->getOptionFlags() & GraphicContext::USEGPU)
			bytes += (result->texMultX == 1.0f ? width * height : texW * texH) * 4;
		compositeBytes += bytes;
		auto inserted = compositeCache.emplace(std::move(key), std::move(result));
		return inserted.first->second.get();
	}

	Sprite::~Sprite()
	{
		for (std::vector <DrawableSurface *>::iterator imagesIt = images.begin(); imagesIt != images.end(); ++imagesIt)
		{
			if (*imagesIt)
				delete (*imagesIt);
		}
		for (std::vector <RotatedImage *>::iterator rotatedIt=rotated.begin(); rotatedIt!=rotated.end(); ++rotatedIt)
		{
			if (*rotatedIt)
				delete (*rotatedIt);
		}
	}
	
	void Sprite::loadFrame(SDL_RWops *frameStream, SDL_RWops *rotatedStream)
	{
		if (frameStream)
		{
			SDL_Surface *sprite = IMG_Load_RW(frameStream, 0);
			assert(sprite);
			images.push_back(new DrawableSurface(sprite));
			SDL_FreeSurface(sprite);
		}
		else
			images.push_back(NULL);
	
		if (rotatedStream)
		{
			SDL_Surface *sprite = IMG_Load_RW(rotatedStream, 0);
			assert(sprite);
			rotated.push_back(new RotatedImage(new DrawableSurface(sprite)));
			SDL_FreeSurface(sprite);
		}
		else
			rotated.push_back(NULL);
	}
	
	int Sprite::getW(int index)
	{
		if (!checkBound(index))
			return 0;
		if (images[index])
			return images[index]->getW();
		else if (rotated[index])
			return rotated[index]->orig->getW();
		else
			return 0;
	}
	
	int Sprite::getH(int index)
	{
		if (!checkBound(index))
			return 0;
		if (images[index])
			return images[index]->getH();
		else if (rotated[index])
			return rotated[index]->orig->getH();
		else
			return 0;
	}
	
	int Sprite::getFrameCount(void)
	{
		return std::max(images.size(), rotated.size());
	}
	
	bool Sprite::checkBound(int index)
	{
		if ((index < 0) || (index >= getFrameCount()))
		{
			Toolkit::SpriteMap::const_iterator it = Toolkit::spriteMap.begin();
			while (it != Toolkit::spriteMap.end())
			{
				if (it->second == this)
				{
					std::cerr << "GAG : Sprite " << fileName << " ::checkBound(" << index << ") : error : out of bound access for " << it->first << std::endl;
					assert(false);
					return false;
				}
				++it;
			}
			std::cerr << "GAG : Sprite " << fileName << " ::checkBound(" << index << ") : error : sprite is not in the sprite server" << std::endl;
			assert(false);
			return false;
		}
		else
			return true;
	}
}
