/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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
#include <GraphicContext.h>
#include <assert.h>
#include <iostream>

Toolkit::SpriteMap Toolkit::spriteMap;
Toolkit::FontMap Toolkit::fontMap;
FileManager *Toolkit::fileManager = NULL;
StringTable *Toolkit::strings = NULL;
GraphicContext *Toolkit::gc = NULL;

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

void Toolkit::close(void)
{
	if (fileManager)
	{
		delete fileManager;
		fileManager=NULL;
		delete strings;
		strings=NULL;
	}
	for (SpriteMap::iterator it=spriteMap.begin(); it!=spriteMap.end(); ++it)
		delete (*it).second;
	spriteMap.clear();
	for (FontMap::iterator it=fontMap.begin(); it!=fontMap.end(); ++it)
		delete (*it).second;
	fontMap.clear();
}

Sprite *Toolkit::getSprite(const char *name)
{
	if (spriteMap.find(name) == spriteMap.end())
	{
		if (gc)
			gc->loadSprite(name, name);
		else
			return NULL;
	}
	//std::cout << "Sprite " << name << " loaded, " << spriteMap[std::string(name)]->getFrameCount() << std::endl;
	return spriteMap[std::string(name)];
}

void Toolkit::releaseSprite(const char *name)
{
	SpriteMap::iterator it = spriteMap.find(std::string(name));
	assert(it!=spriteMap.end());
	delete (*it).second;
	spriteMap.erase(it);
}

Font *Toolkit::getFont(const char *name)
{
	return fontMap[std::string(name)];
}

void Toolkit::releaseFont(const char *name)
{
	FontMap::iterator it = fontMap.find(std::string(name));
	assert(it!=fontMap.end());
	delete (*it).second;
	fontMap.erase(it);
}


