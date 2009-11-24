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

#ifndef __TRUETYPE_FONT_H
#define __TRUETYPE_FONT_H

#include <GAGSys.h>
#include "GraphicContext.h"
#include "SDL_ttf.h"
#include <stack>
#include <map>
#include <string>

struct SDL_Surface;

namespace GAGCore
{
	class DrawableSurface;
	
	//! An implementation of Font using SDL_TTF
	class TrueTypeFont:public Font
	{
		static const bool verbose = false;
	public:
		TrueTypeFont();
		TrueTypeFont(const std::string filename, unsigned size);
		virtual ~TrueTypeFont();
		bool load(const std::string filename, unsigned size);
		
		//! Get the width of string with shape. Update cache
		int getStringWidth(const char *string);
		//! Get the height of string with shape. If string is NULL, return base value, else update cache
		int getStringHeight(const char *string);
		
		// Style and color
		virtual void setStyle(Style style);
		virtual Style getStyle(void) const;
		
	protected:
		//! Init internal variables
		void init(void);
		virtual void drawString(DrawableSurface *surface, int x, int y, int w, const std::string text, Uint8 alpha);
		virtual void drawString(DrawableSurface *surface, float x, float y, float w, const std::string text, Uint8 alpha);
		virtual void pushStyle(Style style);
		virtual void popStyle(void);
		
		//! If text is cached, returns its surface. If it is not, create, cache and return surface
		DrawableSurface *getStringCached(const std::string text);
		//! If cache is too big, remove old entry
		void cleanupCache(void);
#ifdef HAVE_FRIBIDI 
		char *getBIDIString (const std::string text);
#endif		
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
			DrawableSurface *s;
			unsigned lastAccessed;
		};
		
		unsigned now;
		std::map<CacheKey, CacheData> cache;
		std::map<unsigned, std::map<CacheKey, CacheData>::iterator> timeCache;
		//! number of cache hit
		unsigned cacheHit;
		//! number of cache miss
		unsigned cacheMiss;
	};
}

#endif
