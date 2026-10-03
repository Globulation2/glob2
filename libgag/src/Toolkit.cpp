// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <Toolkit.h>
#include <StringTable.h>
#include <FileManager.h>
#include <assert.h>
#include <iostream>
#include <stdexcept>
#include "TrueTypeFont.h"

#include <GraphicContext.h>

namespace GAGCore
{
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
	
	Sprite *Toolkit::getSprite(const std::string name)
	{
		assert(name.size());
		if (spriteMap.find(name) == spriteMap.end())
		{
			Sprite *sprite = new Sprite();
			if (sprite->load(name))
			{
				spriteMap[std::string(name)] = sprite;
			}
			else
			{
				delete sprite;
				std::cerr << "GAG : Can't load sprite " << name << std::endl;
				return NULL;
			}
		}
		return spriteMap[std::string(name)];
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


