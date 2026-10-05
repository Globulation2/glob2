// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <Toolkit.h>
#include <StringTable.h>
#include <FileManager.h>
#include <assert.h>
#include <iostream>
#include <stdexcept>
#include "TrueTypeFont.h"
#include <AssetLoader.h>
#include <memory>
#include <SpriteLoad.h>
#include <chrono>
#include <algorithm>

#include <GraphicContext.h>

namespace GAGCore
{
	namespace {
        std::unique_ptr<AssetLoader> assetLoader;
        std::map<std::string, std::unique_ptr<SpriteLoad>> loadingSprites;
        size_t completedSpriteLoads = 0, submittedSpriteLoads = 0;
    }
	AssetLoader &Toolkit::assets()
	{
		if (!assetLoader) assetLoader = std::make_unique<AssetLoader>(*fileManager, AssetLoader::Options::environment());
		return *assetLoader;
	}
	Toolkit::SpriteMap Toolkit::spriteMap;
	Toolkit::FontMap Toolkit::fontMap;
	GraphicContext *Toolkit::gc = NULL;
	FileManager *Toolkit::fileManager = NULL;
	StringTable *Toolkit::strings = NULL;
	
	void Toolkit::init(const char *gameName)
	{
		if (!fileManager)
		{
			fileManager = new FileManager(gameName);
			strings = new StringTable();
		}
		else
			assert(false);
	}
	
	GraphicContext *Toolkit::initGraphic(int w, int h, unsigned int flags, const std::string title, const std::string icon)
	{
		gc = new GraphicContext(w, h, flags, title, icon);
		return gc;
	}
	
	void Toolkit::close(void)
	{
        loadingSprites.clear();
		assetLoader.reset();
        completedSpriteLoads = submittedSpriteLoads = 0;
		for (SpriteMap::iterator it=spriteMap.begin(); it!=spriteMap.end(); ++it)
			delete (*it).second;
		spriteMap.clear();
		for (FontMap::iterator it=fontMap.begin(); it!=fontMap.end(); ++it)
			delete (*it).second;
		fontMap.clear();
		
		if (fileManager)
		{
			delete fileManager;
			fileManager = NULL;
			delete strings;
			strings = NULL;
		}
		
		if (gc)
		{
			delete gc;
			gc = NULL;
		}
	}
	
    void Toolkit::requestSprite(const std::string& name, bool variableAtlas)
    {
        if (spriteMap.contains(name) || loadingSprites.contains(name)) return;
        loadingSprites[name] = std::make_unique<SpriteLoad>(name, variableAtlas);
        ++submittedSpriteLoads;
    }
    Sprite *Toolkit::findSprite(const std::string& name)
    {
        auto found = spriteMap.find(name);
        return found == spriteMap.end() ? nullptr : found->second;
    }
    bool Toolkit::pollAssets(unsigned budgetMs)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
        auto remaining = [&] {
            return std::max(std::chrono::milliseconds::zero(),
                std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
        };
        if (remaining().count()) assets().poll(remaining());
        for (auto it = loadingSprites.begin(); it != loadingSprites.end() && remaining().count();) {
            if (it->second->pollUntil(deadline)) {
                auto sprite = it->second->take();
                if (sprite) spriteMap[it->first] = sprite.release();
                else std::cerr << it->second->error() << std::endl;
                ++completedSpriteLoads;
                it = loadingSprites.erase(it);
            } else ++it;
        }
        const bool reloaded = Sprite::pollHighResolutionUntil(deadline);
        return loadingSprites.empty() && reloaded;
    }
    unsigned Toolkit::assetProgress()
    {
        return submittedSpriteLoads ? unsigned(completedSpriteLoads * 100 / submittedSpriteLoads) : 100;
    }
    Sprite *Toolkit::getSprite(const std::string name)
    {
        if (auto *sprite = findSprite(name)) return sprite;
        requestSprite(name);
        while (loadingSprites.contains(name)) {
            pollAssets();
#ifndef __EMSCRIPTEN__
            SDL_Delay(1);
#endif
        }
        return findSprite(name);
    }

	void Toolkit::releaseSprite(const std::string name)
	{
		assert(name.size());
		SpriteMap::iterator it = spriteMap.find(name);
		assert(it!=spriteMap.end());
		delete (*it).second;
		spriteMap.erase(it);
	}
	
	void Toolkit::loadFont(const std::string filename, unsigned size, const std::string name)
	{
		assert(filename.size());
		assert(name.size());
		TrueTypeFont *ttf = new TrueTypeFont();
		if (ttf->load(filename, size))
		{
			Toolkit::fontMap[name] = ttf;
		}
		else
		{
			delete ttf;
			throw std::runtime_error("Cannot load font " + name + " from " + filename);
		}
	}
	
	void Toolkit::reloadFonts(void)
	{
		for (const auto &[name, font] : fontMap)
			if (auto *ttf = dynamic_cast<TrueTypeFont *>(font); ttf && !ttf->reload())
				std::cerr << "GAG : Can't reload font " << name << std::endl;
	}

	Font *Toolkit::getFont(const std::string name)
	{
		assert(name.size());
		if (fontMap.find(name) == fontMap.end())
		{
			std::cerr << "GAG : Font " << name << " does not exists" << std::endl;
			assert(false);
			return NULL;
		}
		return fontMap[name];
	}
	
	void Toolkit::releaseFont(const std::string name)
	{
		assert(name.size());
		FontMap::iterator it = fontMap.find(name);
		assert(it!=fontMap.end());
		delete (*it).second;
		fontMap.erase(it);
	}
}

