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

#include "TrueTypeFont.h"
#include <Toolkit.h>
#include <SupportFunctions.h>
#include <FileManager.h>
#include <assert.h>
#include <iostream>

#define MAX_CACHE_SIZE 128

namespace GAGCore
{
	TrueTypeFont::TrueTypeFont()
	{
		font = NULL;
		now = 0;
	}
	
	TrueTypeFont::TrueTypeFont(const char *filename, unsigned size)
	{
		load(filename, size);
	}
	
	TrueTypeFont::~TrueTypeFont()
	{
		if (font)
		{
			for (std::map<CacheKey, CacheData>::iterator it = cache.begin(); it != cache.end(); ++it)
				SDL_FreeSurface(it->second.s);
			TTF_CloseFont(font);
		}
	}
	
	bool TrueTypeFont::load(const char *filename, unsigned size)
	{
		SDL_RWops *fontStream = Toolkit::getFileManager()->open(filename, "rb");
		if (fontStream)
		{
			font = TTF_OpenFontRW(fontStream, 1, size);
			if (font)
			{
				setStyle(Style(STYLE_NORMAL, 255, 255, 255));
				return true;
			}
		}
		return false;
	}
	
	int TrueTypeFont::getStringWidth(const char *string, Shape shape)
	{
		pushStyle(Style(shape, styleStack.top().r, styleStack.top().g, styleStack.top().b, styleStack.top().a));
		SDL_Surface *s = getStringCached(string);
		int w;
		if (s)
		{
			w = s->w;
			cleanupCache();
		}
		else
			w = 0;
		popStyle();
		return w;
	}
	
	int TrueTypeFont::getStringHeight(const char *string, Shape shape)
	{
		int h;
		if (string)
		{
			pushStyle(Style(shape, styleStack.top().r, styleStack.top().g, styleStack.top().b, styleStack.top().a));
			SDL_Surface *s = getStringCached(string);
			if (s)
			{
				h = s->h;
				cleanupCache();
			}
			else
				h = 0;
			popStyle();
		}
		else
		{
			int oldShape = TTF_GetFontStyle(font);
			TTF_SetFontStyle(font, shape);
			h = TTF_FontHeight(font);
			TTF_SetFontStyle(font, oldShape);
		}
		return h;
	}
	
	void TrueTypeFont::setStyle(Style style)
	{
		assert(font);
		
		while (styleStack.size() > 0)
			styleStack.pop();
		pushStyle(style);
	}
	#include <iostream>
	void TrueTypeFont::pushStyle(Style style)
	{
		assert(font);
		
		styleStack.push(style);
		TTF_SetFontStyle(font, style.shape);
	}
	
	void TrueTypeFont::popStyle(void)
	{
		assert(font);
		
		if (styleStack.size() > 1)
		{
			styleStack.pop();
			TTF_SetFontStyle(font, styleStack.top().shape);
		}
	}
	
	Font::Style TrueTypeFont::getStyle(void) const
	{
		assert(font);
		
		return styleStack.top();
	}
	
	SDL_Surface *TrueTypeFont::getStringCached(const char *text)
	{
		assert(text);
		assert(font);
		assert(styleStack.size()>0);
		
		CacheKey key;
		key.text = text;
		key.style = styleStack.top();
		
		CacheData data;
		SDL_Surface *s;
		
		std::map<CacheKey, CacheData>::iterator keyIt = cache.find(key);
		if (keyIt == cache.end())
		{
			// create bitmap
			SDL_Color c;
			c.r = styleStack.top().r;
			c.g = styleStack.top().g;
			c.b = styleStack.top().b;
			c.unused = styleStack.top().a;
			
			SDL_Surface *temp = TTF_RenderUTF8_Blended(font, text, c);
			if (temp == NULL)
				return NULL;
			
			// create key
			data.lastAccessed = now;
			data.s = s = SDL_DisplayFormatAlpha(temp);
			assert(s);
			SDL_FreeSurface(temp);
			
			// store in cache
			cache[key] = data;
			timeCache[now] = cache.find(key);
			//std::cout << "String cache size for " << this << " is now " << cache.size() << std::endl;
		}
		else
		{
			// get surface
			s = keyIt->second.s;
			// erase old time association
			timeCache.erase(keyIt->second.lastAccessed);
			// set new time
			keyIt->second.lastAccessed = now;
			// add new time association
			timeCache[now] = keyIt;
		}
		now++;
		return s;
	}
	
	void TrueTypeFont::cleanupCache(void)
	{
		// when cache is too big, remove the first element
		if (cache.size() >= MAX_CACHE_SIZE)
		{
			SDL_FreeSurface(timeCache.begin()->second->second.s);
			cache.erase(timeCache.begin()->second);
			timeCache.erase(timeCache.begin());
		}
	}
	
	void TrueTypeFont::drawString(SDL_Surface *Surface, int x, int y, int w, const char *text, SDL_Rect *clip)
	{
		SDL_Surface *s = getStringCached(text);
		
		// render
		if (s)
		{
			SDL_Rect sr;
			sr.x = 0;
			sr.y = 0;
			sr.w = static_cast<Uint16>(s->w);
			sr.h = static_cast<Uint16>(s->h);
		
			SDL_Rect r;
			r.x = static_cast<Sint16>(x);
			r.y = static_cast<Sint16>(y);
			if (w)
				r.w = static_cast<Uint16>(w);
			else
				r.w = static_cast<Uint16>(s->w);
			r.h = static_cast<Uint16>(s->h);
		
			SDL_Rect oc;
			if (clip)
			{
				SDL_GetClipRect(Surface, &oc);
				sdcRects(&sr, &r, *clip);
				SDL_SetClipRect(Surface, &r);
			}
		
			SDL_BlitSurface(s, &sr, Surface, &r);
		
			if (clip)
			{
				SDL_SetClipRect(Surface, &oc);
			}
			
			cleanupCache();
		}
	}
}
