// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GraphicContextPrivate.h"
#include <math.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <assert.h>
#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstring>

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
	static std::set<Sprite*> loadedSprites;
	static bool highResolutionEnabled = false;
    static std::string packDirectory, packText;
    static bool packRead=false;
    static bool readPack(const std::string &directory)
    {
        if(packRead && packDirectory==directory)return !packText.empty();
        packRead=true;packDirectory=directory;packText.clear();
        auto input=Toolkit::getFileManager()->open((directory+"/frames.txt").c_str(),"rb");
        if(!input){std::cerr<<"High-resolution pack unavailable: "<<directory<<std::endl;return false;}
        const auto size=SDL_RWsize(input);
        if(size<=0||size>1024*1024){SDL_RWclose(input);std::cerr<<"Invalid high-resolution manifest size"<<std::endl;return false;}
        std::string text(static_cast<size_t>(size),'\0');
        const auto count=SDL_RWread(input,text.data(),1,text.size());SDL_RWclose(input);
        if(count!=text.size())return false;
        std::istringstream header(text);std::string magic;int version=0;header>>magic>>version;
        if(magic!="GLOB2_HIGHRES"||version!=1){std::cerr<<"Unsupported high-resolution pack"<<std::endl;return false;}
        packText=std::move(text);return true;
    }

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
		loadedSprites.insert(this);
		
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
			loadExperimentFrame(frameName.str(), frameNameRot.str());
	
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
		
		createHighResolutionAtlas();
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
	bool Sprite::createTextureAtlas(bool allowVariableSizes)
	{
#ifdef HAVE_OPENGL
		if (!Toolkit::gc || !(Toolkit::gc->getOptionFlags() & GraphicContext::USEGPU))
			return false;
		if (atlas)
			return true;
		if (images.empty())
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
		// The normal automatic path retains its equal-size requirement. Resource
		// rendering explicitly opts into padded cells for variable-size frames.
		for (auto image : images)
		{
			if (!image)
				return false; // One of the images is null
			if (!tileWidth || !tileHeight)
			{
				tileWidth = image->getW();
				tileHeight = image->getH();
			}
			if (!allowVariableSizes && (image->getW() != tileWidth || image->getH() != tileHeight))
				return false;
			tileWidth = std::max(tileWidth, image->getW());
			tileHeight = std::max(tileHeight, image->getH());
		}
		const int padding = allowVariableSizes ? 1 : 0;
		tileWidth += 2 * padding;
		tileHeight += 2 * padding;
		int sheetWidth = tileWidth * (static_cast<int>(sqrt(numImages)) + 1);
		int sheetHeight = tileHeight * (static_cast<int>(sqrt(numImages)) + 1);
		if (sheetWidth > maxTextureSize || sheetHeight > maxTextureSize)
		{
			std::cerr << "Warning: Sprite sheet " << fileName << " with size " << sheetWidth << "x" << sheetHeight
				<< " exceeds your graphics card's maximum texture size of " << maxTextureSize << std::endl;
			return false; // We can't continue, falling back to glBegin/glEnd rendering.
		}
		std::unique_ptr<DrawableSurface> atlas = make_unique<DrawableSurface>(sheetWidth, sheetHeight);
		int x = padding, y = padding;
		for (auto image: images)
		{
			const int width = image->getW(), height = image->getH();
			if (padding)
			{
				// Copy straight RGBA and extrude the border so linear filtering
				// matches each standalone texture, without bleeding adjacent frames.
				SDL_BlendMode blend;
				SDL_GetSurfaceBlendMode(image->sdlsurface, &blend);
				SDL_SetSurfaceBlendMode(image->sdlsurface, SDL_BLENDMODE_NONE);
				int sx[3] = {0, 0, width - 1}, sy[3] = {0, 0, height - 1};
				int dx[3] = {x - 1, x, x + width}, dy[3] = {y - 1, y, y + height};
				int widths[3] = {1, width, 1}, heights[3] = {1, height, 1};
				for (int row = 0; row < 3; ++row)
					for (int col = 0; col < 3; ++col)
					{
						SDL_Rect src = {sx[col], sy[row], widths[col], heights[row]};
						SDL_Rect dest = {dx[col], dy[row], widths[col], heights[row]};
						SDL_BlitSurface(image->sdlsurface, &src, atlas->sdlsurface, &dest);
					}
				SDL_SetSurfaceBlendMode(image->sdlsurface, blend);
			}
			else
				atlas->drawSurface(x, y, image);
			TextureInfo info = { this, x, y, width, height };
			image->textureInfo = info;
			image->texMultX = atlas->texMultX;
			image->texMultY = atlas->texMultY;
			x += tileWidth;
			if (tileWidth + x - padding > sheetWidth) {
				x = padding;
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
		return getColoredSurface(rotated[index]);
	}

	DrawableSurface *Sprite::getColoredSurface(RotatedImage *image)
	{
		if (compositeOnly)
		{
			// Sharp UI draws must not repopulate a second retained team-color cache.
			float baseHue, hue, sat, lum;
			Color(51,255,153).getHSV(&baseHue, &sat, &lum);
			actColor.getHSV(&hue, &sat, &lum);
			transientColor.reset(image->orig->clone());
			transientColor->shiftHSV(hue-baseHue, 0, 0);
			return transientColor.get();
		}
		RotatedImage::RotationMap::const_iterator it = image->rotationMap.find(actColor);
		DrawableSurface *ds;
		if (it == image->rotationMap.end())
		{
			// compute hue shift
			float baseHue, actHue, lum, sat;
			float hueShift;
			Color(51, 255, 153).getHSV(&baseHue, &sat, &lum);
			actColor.getHSV(&actHue, &sat, &lum);
			hueShift = actHue - baseHue;
			
			// rotate image
			ds = image->orig->clone();
			ds->shiftHSV(hueShift, 0.0f, 0.0f);
			
			// write back
			image->rotationMap[actColor] = ds;
		}
		else
		{
			ds = it->second;
		}
		return ds;
	}

    Sprite::HighResolutionStats Sprite::highResolutionStats()
    {
        HighResolutionStats stats;
        for(auto sprite:loadedSprites)
        {
            for(auto s:sprite->experimentImages)if(s)stats.cpuBytes+=s->getW()*s->getH()*4;
            for(auto r:sprite->experimentRotated)if(r)
            {
                stats.cpuBytes+=r->orig->getW()*r->orig->getH()*4;
                for(auto entry:r->rotationMap){stats.cpuBytes+=entry.second->getW()*entry.second->getH()*4;++stats.coloredFrames;}
            }
#ifdef HAVE_OPENGL
            if(sprite->highResolutionAtlas)stats.cpuBytes+=sprite->highResolutionAtlas->atlas->sdlsurface->w*sprite->highResolutionAtlas->atlas->sdlsurface->h*4;
#endif
        }
        return stats;
    }

	void Sprite::setHighResolution(bool enabled)
	{
		highResolutionEnabled = enabled;
		packRead=false;packText.clear();
		for (auto sprite : loadedSprites) sprite->reloadHighResolution();
	}

	void Sprite::reloadHighResolution()
	{
		// Entries are unbounded while assets are unchanged; a pack reload invalidates them.
		compositeCache.clear();
		transientColor.reset();
		compositeBytes = compositeHits = compositeMisses = 0;
		highResolutionAtlas.reset();
		for (auto p : experimentImages) delete p;
		for (auto p : experimentRotated) delete p;
		experimentImages.clear(); experimentRotated.clear();
		for (size_t i=0;i<images.size();++i)
			loadExperimentFrame(fileName+std::to_string(i)+".png",fileName+std::to_string(i)+"r.png");
		createHighResolutionAtlas();
	}

    void Sprite::flushBatches(GraphicContext *gc)
    {
        for(auto sprite:loadedSprites)gc->finishDrawingSprite(sprite,255);
    }
    void Sprite::createHighResolutionAtlas()
    {
#ifdef HAVE_OPENGL
        const bool resources=fileName=="data/gfx/ressource";
        if(!resources && fileName!="data/gfx/terrain")return;
        int count=0;
        while(count<static_cast<int>(experimentImages.size()) && experimentImages[count])++count;
        const int columns=resources?8:(count>16?16:4), rows=resources?9:(count>16?17:4);
        if(count!=(resources?65:16) && !(count==272 && !resources))return;
        const int border=resources?32:64, atlasW=columns*256, atlasH=rows*256;
        const std::string prefix=resources?"ressource":"terrain";
        if(experimentImages.size()<static_cast<size_t>(count))return;
        if(std::none_of(experimentImages.begin(),experimentImages.begin()+count,[](auto p){return p!=nullptr;}))return;
        auto reject=[&]()
        {
            for(int i=0;i<count;++i){delete experimentImages[i];experimentImages[i]=nullptr;}
            std::cerr<<"High-resolution atlas rejected; using original frames"<<std::endl;
        };
        for(int i=0;i<count;++i)if(!experimentImages[i]){reject();return;}
        GLint maxSize=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxSize);
        if(atlasW>maxSize || atlasH>maxSize){reject();return;}
        const char *overrideDir=std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        std::string directory=overrideDir?overrideDir:"data/highres/v1";
        std::vector<std::unique_ptr<DrawableSurface>> levels;
        for(int mip=0;mip<4;++mip)
        {
            auto rw=Toolkit::getFileManager()->open((directory+"/"+prefix+"-atlas-mip"+std::to_string(mip)+".png").c_str(),"rb");
            if(!rw){reject();return;}
            auto s=IMG_Load_RW(rw,1);if(!s){reject();return;}
            if(s->w!=(atlasW>>mip)||s->h!=(atlasH>>mip)){SDL_FreeSurface(s);reject();return;}
            levels.emplace_back(new DrawableSurface(s));SDL_FreeSurface(s);
        }
        // The atlas must correspond to this pack's validated frame layers.
        for(int i=0;i<count;++i)for(int y=0;y<experimentImages[i]->getH();++y)
        {
            auto source=static_cast<unsigned char*>(experimentImages[i]->sdlsurface->pixels)+y*experimentImages[i]->sdlsurface->pitch;
            auto packed=static_cast<unsigned char*>(levels[0]->sdlsurface->pixels)+((i/columns)*256+border+y)*levels[0]->sdlsurface->pitch+((i%columns)*256+border)*4;
            if(std::memcmp(source,packed,experimentImages[i]->getW()*4)!=0){reject();return;}
        }
        auto batch=std::make_unique<Sprite>();
        auto atlas=std::move(levels[0]);atlas->uploadToTexture();
        glBindTexture(GL_TEXTURE_2D,atlas->texture);
        // DrawableSurface's legacy allocator rounds up to powers of two. These
        // prepacked mip levels use exact dimensions, so redefine level zero too.
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,atlasW,atlasH,0,GL_BGRA,GL_UNSIGNED_BYTE,atlas->sdlsurface->pixels);
        glState.allocatedTextureBytes-=atlas->gpuBytes;
        atlas->gpuBytes=atlasW*atlasH*4;glState.allocatedTextureBytes+=atlas->gpuBytes;
        atlas->texMultX=1.f/atlasW;atlas->texMultY=1.f/atlasH;

        for(int mip=1;mip<4;++mip)
            glTexImage2D(GL_TEXTURE_2D,mip,GL_RGBA,atlasW>>mip,atlasH>>mip,0,GL_BGRA,GL_UNSIGNED_BYTE,levels[mip]->sdlsurface->pixels);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,3);
        const size_t mipBytes=((atlasW/2)*(atlasH/2)+(atlasW/4)*(atlasH/4)+(atlasW/8)*(atlasH/8))*4;
        atlas->gpuBytes+=mipBytes;glState.allocatedTextureBytes+=mipBytes;
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenBuffers(1,&batch->vbo);glGenBuffers(1,&batch->texCoordBuffer);
        for(int i=0;i<count;++i)
        {
            auto surface=experimentImages[i];surface->freeGPUTexture();
            surface->textureInfo=TextureInfo{batch.get(),(i%columns)*256+border,(i/columns)*256+border,surface->getW(),surface->getH()};
            surface->texMultX=1.f/atlasW;surface->texMultY=1.f/atlasH;
        }
        batch->atlas=std::move(atlas);highResolutionAtlas=std::move(batch);
#endif
    }

	void Sprite::loadExperimentFrame(const std::string &frameName, const std::string &rotatedName)
	{
		const size_t index=experimentImages.size();
		experimentImages.push_back(nullptr); experimentRotated.push_back(nullptr);
		const char *overrideDir=std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
		if ((!highResolutionEnabled && !overrideDir) || !Toolkit::gc || !(Toolkit::gc->getOptionFlags() & GraphicContext::USEGPU)) return;
		std::string directory=overrideDir ? overrideDir : "data/highres/v1";
        if(!readPack(directory))return;
        std::istringstream stream(packText);std::string magic,id,base,team;int version,w,h,scale;
        stream>>magic>>version;
		std::string wanted=frameName.substr(frameName.find_last_of('/')+1);wanted.resize(wanted.size()-4);
		while(stream>>id>>w>>h>>scale>>base>>team)
		{
			if(id!=wanted)continue;
			if(w!=getW(index)||h!=getH(index)||scale!=4){std::cerr<<"High-resolution dimensions rejected: "<<id<<std::endl;return;}
			auto load=[&](const std::string &name,DrawableSurface *original)->DrawableSurface*
			{
				if(name=="-")return nullptr;
				if(name.find_first_of("/\\:")!=std::string::npos || name.find("..")!=std::string::npos)return nullptr;
				SDL_RWops *rw=Toolkit::getFileManager()->open((directory+"/"+name).c_str(),"rb");
				if(!rw)return nullptr;
				SDL_Surface *surface=IMG_Load_RW(rw,1);if(!surface)return nullptr;
				int lw=original?original->getW():w,lh=original?original->getH():h;
				if(surface->w!=lw*scale||surface->h!=lh*scale){SDL_FreeSurface(surface);return nullptr;}
				auto result=new DrawableSurface(surface);result->highResolutionSampling=true;SDL_FreeSurface(surface);return result;
			};
			auto normal=load(base,images[index]);
			auto colored=load(team,rotated[index]?rotated[index]->orig:nullptr);
			if((base!="-"&&!normal)||(team!="-"&&!colored)||(images[index]&&base=="-")||(rotated[index]&&team=="-"))
			{delete normal;delete colored;std::cerr<<"High-resolution frame rejected: "<<id<<std::endl;return;}
			experimentImages.back()=normal;
			if(colored)experimentRotated.back()=new RotatedImage(colored);
			return;
		}
	}

	DrawableSurface *Sprite::prepareDrawSurface(unsigned index, bool teamColor, bool experiment)
	{
		DrawableSurface *surface;
		if (teamColor)
			surface = experiment && experimentRotated[index] ? getColoredSurface(experimentRotated[index])
				: (rotated[index] ? getRotatedSurface(index) : nullptr);
		else
			surface = experiment && experimentImages[index] ? experimentImages[index] : images[index];
#ifdef HAVE_OPENGL
		if (surface && experiment)
		{
			// Preserve painter order when a pack mixes HD frames and original artwork.
			const Sprite *batch = surface->textureInfo ? surface->textureInfo->sprite : nullptr;
			if (batch != this && !vertices.empty())
				Toolkit::gc->finishDrawingSprite(this, 255);
			if (highResolutionAtlas && batch != highResolutionAtlas.get())
				Toolkit::gc->finishDrawingSprite(highResolutionAtlas.get(), 255);
		}
#endif
		return surface;
	}
	
	DrawableSurface *Sprite::getCachedComposite(const std::vector<std::pair<int, int>> &frames)
	{
		assert(!frames.empty());
		// Switch this sprite to final-image caching without keeping source recolors.
		if (!compositeOnly)
		{
			for (const auto *layers : {&rotated, &experimentRotated})
				for (auto layer : *layers)
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
		// A shutter uses one resolution throughout, including partial-pack fallback.
		bool highResolution = true;
		for (const auto &frame : frames)
		{
			assert(checkBound(frame.first));
			const auto index = frame.first;
			highResolution = highResolution
				&& (!images[index] || experimentImages[index])
				&& (!rotated[index] || experimentRotated[index]);
		}
		const int scale = highResolution ? 4 : 1;
		const int width = getW(frames.front().first) * scale;
		const int height = getH(frames.front().first) * scale;
		// Accumulate premultiplied RGBA so the result can be drawn over any background.
		std::vector<double> pixels(width * height * 4, 0);
		float baseHue, hue, sat, lum;
		Color(51, 255, 153).getHSV(&baseHue, &sat, &lum);
		actColor.getHSV(&hue, &sat, &lum);
		for (auto frame : frames)
		{
			assert(checkBound(frame.first));
			assert(getW(frame.first) * scale == width && getH(frame.first) * scale == height);
			assert(frame.second >= 0 && frame.second <= 255);
			for (int layer = 0; layer < 2; ++layer)
			{
				DrawableSurface *source = highResolution ? experimentImages[frame.first] : images[frame.first];
				if (layer == 1)
				{
					if (!rotated[frame.first])
						continue;
					source = highResolution ? experimentRotated[frame.first]->orig : rotated[frame.first]->orig;
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
		result->highResolutionSampling = highResolution;
		// Include CPU pixels and GPU allocation (rectangle or padded power-of-two).
		int texW = 1, texH = 1;
		while (texW < width)
			texW *= 2;
		while (texH < height)
			texH *= 2;
		size_t bytes = raw->pitch * height;
		if (Toolkit::gc->getOptionFlags() & GraphicContext::USEGPU)
		{
			bytes += (result->texMultX == 1.0f ? width * height : texW * texH) * 4;
			if (highResolution && result->texMultX != 1.0f)
				while (texW > 1 || texH > 1)
				{
					texW = std::max(1, texW / 2); texH = std::max(1, texH / 2);
					bytes += texW * texH * 4;
				}
		}
		compositeBytes += bytes;
		auto inserted = compositeCache.emplace(std::move(key), std::move(result));
		return inserted.first->second.get();
	}

	Sprite::~Sprite()
	{
        loadedSprites.erase(this);
#ifdef HAVE_OPENGL
        if(vbo)glDeleteBuffers(1,&vbo);
        if(texCoordBuffer)glDeleteBuffers(1,&texCoordBuffer);
#endif
		for (auto image : experimentImages)
			delete image;
		for (auto image : experimentRotated)
			delete image;
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
