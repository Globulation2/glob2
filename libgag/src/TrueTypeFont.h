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

#ifndef __TRUETYPE_FONT_H
#define __TRUETYPE_FONT_H

#include <GAGSys.h>
#include "GraphicContext.h"
#include "SDL_ttf.h"
#include <stack>
#include <map>
#include <string>

class SDL_Surface;

class TrueTypeFont:public Font
{
public:
	TrueTypeFont();
	TrueTypeFont(const char *filename, unsigned size);
	virtual ~TrueTypeFont();
	bool load(const char *filename, unsigned size);
	
	int getStringWidth(const char *string) const;
	int getStringHeight(const char *string) const;
	
	// Style and color
	virtual void setStyle(Style style);
	virtual void pushStyle(Style style);
	virtual void popStyle(void);
	virtual Style getStyle(void) const;
	
protected:
	virtual void drawString(SDL_Surface *Surface, int x, int y, int w, const char *text, SDL_Rect *clip=NULL);
	
protected:
	TTF_Font *font;
	std::stack<Style> styleStack;
	
	struct CacheKey
	{
		std::string text;
		Style style;
		
		bool operator<(const CacheKey &o) const { if (text == o.text) return (style < o.style); else return (text < o.text);  }
	};
	
	struct CacheData
	{
		SDL_Surface *s;
		unsigned lastAccessed;
	};
	
	unsigned now;
	std::map<CacheKey, CacheData> cache;
	std::map<unsigned, std::map<CacheKey, CacheData>::iterator> timeCache;
};

#endif
