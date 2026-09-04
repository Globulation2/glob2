// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <Toolkit.h>
#include <StringTable.h>
#include <FileManager.h>
#include <assert.h>
#include <iostream>
#include "TrueTypeFont.h"

#ifndef YOG_SERVER_ONLY
#include <GraphicContext.h>
#endif

namespace GAGCore
{
	#ifndef YOG_SERVER_ONLY
	Toolkit::SpriteMap Toolkit::spriteMap;
	Toolkit::FontMap Toolkit::fontMap;
	GraphicContext *Toolkit::gc = NULL;
	#endif
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
	
	#ifndef YOG_SERVER_ONLY
	GraphicContext *Toolkit::initGraphic(int w, int h, unsigned int flags, const std::string title, const std::string icon)
	{
		gc = new GraphicContext(w, h, flags, title, icon);
		return gc;
	}
	#endif
	
	void Toolkit::close(void)
	{
		#ifndef YOG_SERVER_ONLY
		for (SpriteMap::iterator it=spriteMap.begin(); it!=spriteMap.end(); ++it)
			delete (*it).second;
		spriteMap.clear();
		for (FontMap::iterator it=fontMap.begin(); it!=fontMap.end(); ++it)
			delete (*it).second;
		fontMap.clear();
		#endif
		
		if (fileManager)
		{
			delete fileManager;
			fileManager = NULL;
			delete strings;
			strings = NULL;
		}
		
		#ifndef YOG_SERVER_ONLY
		if (gc)
		{
			delete gc;
			gc = NULL;
		}
		#endif
	}
	
		#ifndef YOG_SERVER_ONLY
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
			std::cerr << "GAG : Can't load font " << name << " with size " << size << " from " << filename << std::endl;
		}
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
	#endif
}


