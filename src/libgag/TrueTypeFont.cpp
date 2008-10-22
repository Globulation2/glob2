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

#include "TrueTypeFont.h"
#include <Toolkit.h>
#include <SupportFunctions.h>
#include <FileManager.h>
#include <assert.h>
#include <iostream>

#ifdef HAVE_FRIBIDI 
#include <fribidi/fribidi.h>
#endif

using namespace std;
#define MAX_CACHE_SIZE 128

namespace GAGCore
{
	TrueTypeFont::TrueTypeFont()
	{
		init();
	}
	
	TrueTypeFont::TrueTypeFont(const char *filename, unsigned size)
	{
		init();
		load(filename, size);
	}
	
	void TrueTypeFont::init(void)
	{
		font = NULL;
		now = 0;
		cacheHit = 0;
		cacheMiss = 0;
	}
	
	TrueTypeFont::~TrueTypeFont()
	{
		if (font)
		{
			// display stats
			float cacheTotal = static_cast<float>(cacheHit + cacheMiss);
			if (cacheTotal > 0)
			{
				if (verbose)
					std::cout << "TrueTypeFont : font" <<
						/*TTF_FontFaceFamilyName(font) << ", " <<
						  TTF_FontFaceStyleName(font) << ", " <<
						  TTF_FontHeight(font) <<*/ " had " <<
						cacheHit + cacheMiss << " requests, " <<
						cacheHit << " hits (" << static_cast<float>(cacheHit)/cacheTotal << "), " <<
						cacheMiss << " misses (" << static_cast<float>(cacheMiss)/cacheTotal << ")" << std::endl;
			}
			// free cache
			for (std::map<CacheKey, CacheData>::iterator it = cache.begin(); it != cache.end(); ++it)
				delete it->second.s;
			// close font
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
	
	int TrueTypeFont::getStringWidth(const char *string)
	{
		DrawableSurface *s = getStringCached(string);
		int w;
		if (s)
		{
			w = s->getW();
			cleanupCache();
		}
		else
			w = 0;
		return w;
	}
	
	int TrueTypeFont::getStringHeight(const char *string)
	{
		int h;
		if (string)
		{
			DrawableSurface *s = getStringCached(string);
			if (s)
			{
				h = s->getH();
				cleanupCache();
			}
			else
				h = 0;
		}
		else
		{
			h = TTF_FontHeight(font);
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
	
	DrawableSurface *TrueTypeFont::getStringCached(const char *text)
	{
		assert(text);
		assert(font);
		assert(styleStack.size()>0);
		
		CacheKey key;
		key.text = text;
		key.style = styleStack.top();
		
		CacheData data;
		DrawableSurface *s;
		
		std::map<CacheKey, CacheData>::iterator keyIt = cache.find(key);
		if (keyIt == cache.end())
		{
			// create bitmap
			SDL_Color c;
			c.r = styleStack.top().color.r;
			c.g = styleStack.top().color.g;
			c.b = styleStack.top().color.b;
			c.unused = styleStack.top().color.a;
#ifdef HAVE_FRIBIDI 
			char *bidiStr = getBIDIString(text);
			SDL_Surface *temp = TTF_RenderUTF8_Blended(font, bidiStr, c);
			delete []bidiStr;
#else		
			SDL_Surface *temp = TTF_RenderUTF8_Blended(font, text, c);
#endif
			if (temp == NULL)
				return NULL;
			
			// create key
			data.lastAccessed = now;
			data.s = s = new DrawableSurface(temp);
			assert(s);
			SDL_FreeSurface(temp);
			
			// store in cache
			cache[key] = data;
			timeCache[now] = cache.find(key);
			cacheMiss++;
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
			cacheHit++;
		}
		now++;
		return s;
	}
#ifdef HAVE_FRIBIDI 
	char *TrueTypeFont::getBIDIString (const char *text)
	{
		char		*c_str = (char*) text;
		int		len = strlen(c_str);
		FriBidiChar	*bidi_logical = new FriBidiChar[len + 2];
		FriBidiChar	*bidi_visual = new FriBidiChar[len + 2];
		char		*utf8str = new char[4*len + 1];	//assume worst case here (all 4 Byte characters)
		FriBidiCharType	base_dir = FRIBIDI_TYPE_ON;
		int n;
		n = fribidi_charset_to_unicode (fribidi_parse_charset ("UTF-8"),c_str, len, bidi_logical);
		fribidi_log2vis(bidi_logical, n, &base_dir, bidi_visual, NULL, NULL, NULL);
		n =  fribidi_remove_bidi_marks (bidi_visual, n, NULL, NULL, NULL);
		fribidi_unicode_to_charset (fribidi_parse_charset ("UTF-8"),bidi_visual, n, utf8str);
		delete []bidi_logical;
		delete []bidi_visual;
		return utf8str;	
	}
#endif	
	void TrueTypeFont::cleanupCache(void)
	{
		// when cache is too big, remove the first element
		if (cache.size() >= MAX_CACHE_SIZE)
		{
			delete timeCache.begin()->second->second.s;
			cache.erase(timeCache.begin()->second);
			timeCache.erase(timeCache.begin());
		}
	}
	
	void TrueTypeFont::drawString(DrawableSurface *surface, int x, int y, int w, const char *text, Uint8 alpha)
	{
		// get
		DrawableSurface *s = getStringCached(text);
		if (s == NULL)
			return;
		
		// render
		if (w)
		{
			int rx, ry, rw, rh;
			surface->getClipRect(&rx, &ry, &rw, &rh);
			int nrw = std::min(rw, x + w - rx);
			surface->setClipRect(rx, ry, nrw, rh);
			surface->drawSurface(x, y, s, alpha);
			surface->setClipRect(rx, ry, rw, rh);
			
		}
		else
			surface->drawSurface(x, y, s, alpha);
		
		// cleanup
		cleanupCache();
	}
	
	void TrueTypeFont::drawString(DrawableSurface *surface, float x, float y, float w, const char *text, Uint8 alpha)
	{
		// get
		DrawableSurface *s = getStringCached(text);
		if (s == NULL)
			return;
		
		// render
		if (w != 0.0f)
		{
			int rx, ry, rw, rh;
			surface->getClipRect(&rx, &ry, &rw, &rh);
			int nrw = std::min(rw, (int)x + (int)w - rx);
			surface->setClipRect(rx, ry, nrw, rh);
			surface->drawSurface(x, y, s, alpha);
			surface->setClipRect(rx, ry, rw, rh);
			
		}
		else
			surface->drawSurface(x, y, s, alpha);
		
		// cleanup
		cleanupCache();
	}
}
