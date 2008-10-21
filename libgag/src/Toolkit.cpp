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

#include <Toolkit.h>
#include <StringTable.h>
#include <FileManager.h>
#include <assert.h>
#include <iostream>
#ifndef DX9_BACKEND	// TODO:Die!
#include "TrueTypeFont.h"
#endif

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
	GraphicContext *Toolkit::initGraphic(int w, int h, unsigned int flags, const char *title, const char *icon)
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
	Sprite *Toolkit::getSprite(const char *name)
	{
		assert(name);
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
	
	Sprite *Toolkit::getSprite(const std::string &name)
	{
		return getSprite(name.c_str());
	}
	
	void Toolkit::releaseSprite(const char *name)
	{
		assert(name);
		SpriteMap::iterator it = spriteMap.find(std::string(name));
		assert(it!=spriteMap.end());
		delete (*it).second;
		spriteMap.erase(it);
	}
	
	void Toolkit::releaseSprite(const std::string &name)
	{
		return releaseSprite(name.c_str());
	}
	
	void Toolkit::loadFont(const char *filename, unsigned size, const char *name)
	{
		assert(filename);
		assert(name);
		TrueTypeFont *ttf = new TrueTypeFont();
		if (ttf->load(filename, size))
		{
			Toolkit::fontMap[std::string(name)] = ttf;
		}
		else
		{
			delete ttf;
			std::cerr << "GAG : Can't load font " << name << " with size " << size << " from " << filename << std::endl;
		}
	}
	
	Font *Toolkit::getFont(const char *name)
	{
		assert(name);
		if (fontMap.find(name) == fontMap.end())
		{
			std::cerr << "GAG : Font " << name << " does not exists" << std::endl;
			assert(false);
			return NULL;
		}
		return fontMap[std::string(name)];
	}
	
	void Toolkit::releaseFont(const char *name)
	{
		assert(name);
		FontMap::iterator it = fontMap.find(std::string(name));
		assert(it!=fontMap.end());
		delete (*it).second;
		fontMap.erase(it);
	}
	#endif
}


