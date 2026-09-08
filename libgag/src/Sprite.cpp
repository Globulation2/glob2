// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GraphicContextPrivate.h"
#include <math.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <assert.h>
#include <SDL_image.h>
#include <algorithm>
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
			image->texMultX = atlas->texMultX;
			image->texMultY = atlas->texMultY;
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
		return getColoredSurface(rotated[index]);
	}

	DrawableSurface *Sprite::getColoredSurface(RotatedImage *image)
	{
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
            if(sprite->highResolutionAtlas)stats.cpuBytes+=1024*1024*4;
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
        if(fileName!="data/gfx/terrain" || experimentImages.size()<16)return;
        if(std::none_of(experimentImages.begin(),experimentImages.begin()+16,[](auto p){return p!=nullptr;}))return;
        auto reject=[&]()
        {
            for(int i=0;i<16;++i){delete experimentImages[i];experimentImages[i]=nullptr;}
            std::cerr<<"High-resolution terrain atlas rejected; using original terrain"<<std::endl;
        };
        for(int i=0;i<16;++i)if(!experimentImages[i]){reject();return;}
        const char *overrideDir=std::getenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        std::string directory=overrideDir?overrideDir:"data/highres/v1";
        std::vector<std::unique_ptr<DrawableSurface>> levels;
        for(int mip=0;mip<4;++mip)
        {
            auto rw=Toolkit::getFileManager()->open((directory+"/terrain-atlas-mip"+std::to_string(mip)+".png").c_str(),"rb");
            if(!rw){reject();return;}
            auto s=IMG_Load_RW(rw,1);if(!s){reject();return;}
            if(s->w!=(1024>>mip)||s->h!=(1024>>mip)){SDL_FreeSurface(s);reject();return;}
            levels.emplace_back(new DrawableSurface(s));SDL_FreeSurface(s);
        }
        // The atlas must correspond to this pack's validated frame layers.
        for(int i=0;i<16;++i)for(int y=0;y<128;++y)
        {
            auto source=static_cast<unsigned char*>(experimentImages[i]->sdlsurface->pixels)+y*experimentImages[i]->sdlsurface->pitch;
            auto packed=static_cast<unsigned char*>(levels[0]->sdlsurface->pixels)+((i/4)*256+64+y)*levels[0]->sdlsurface->pitch+((i%4)*256+64)*4;
            if(std::memcmp(source,packed,128*4)!=0){reject();return;}
        }
        auto batch=std::make_unique<Sprite>();
        auto atlas=std::move(levels[0]);atlas->uploadToTexture();
        glBindTexture(GL_TEXTURE_2D,atlas->texture);
        for(int mip=1;mip<4;++mip)
            glTexImage2D(GL_TEXTURE_2D,mip,GL_RGBA,1024>>mip,1024>>mip,0,GL_BGRA,GL_UNSIGNED_BYTE,levels[mip]->sdlsurface->pixels);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,3);
        const size_t mipBytes=(512*512+256*256+128*128)*4;
        atlas->gpuBytes+=mipBytes;glState.allocatedTextureBytes+=mipBytes;
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenBuffers(1,&batch->vbo);glGenBuffers(1,&batch->texCoordBuffer);
        for(int i=0;i<16;++i)
        {
            auto surface=experimentImages[i];surface->freeGPUTexture();
            surface->textureInfo=TextureInfo{batch.get(),(i%4)*256+64,(i/4)*256+64,128,128};
            surface->texMultX=surface->texMultY=1.f/1024;
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

	DrawableSurface *Sprite::getDrawSurface(unsigned index, bool teamColor, bool experiment)
	{
		if (teamColor)
		{
			if (experiment && experimentRotated[index])
				return getColoredSurface(experimentRotated[index]);
			return rotated[index] ? getRotatedSurface(index) : nullptr;
		}
		return experiment && experimentImages[index] ? experimentImages[index] : images[index];
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
