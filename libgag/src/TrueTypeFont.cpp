// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "TrueTypeFont.h"
#include "GraphicContextPrivate.h"
#include <Toolkit.h>
#include <SupportFunctions.h>
#include <FileManager.h>
#include <algorithm>
#include <assert.h>
#include <cmath>
#include <iostream>

#ifdef HAVE_FRIBIDI 
#include <fribidi/fribidi.h>
#endif

#define MAX_CACHE_SIZE 128

namespace GAGCore
{
	TrueTypeFont::TrueTypeFont()
	{
		init();
	}
	
	TrueTypeFont::TrueTypeFont(const std::string filename, unsigned size)
	{
		init();
		load(filename, size);
	}
	
	void TrueTypeFont::init(void)
	{
		font = NULL;
		renderFont = NULL;
		baseSize = 0;
		renderScale = 1.0f;
		textureGeneration = 0;
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
			clearCache();
			// close fonts
			if (renderFont && renderFont != font)
				TTF_CloseFont(renderFont);
			TTF_CloseFont(font);
		}
	}
	
	bool TrueTypeFont::load(const std::string filename, unsigned size)
	{
		SDL_RWops *fontStream = Toolkit::getFileManager()->open(filename, "rb");
		if (fontStream)
		{
			font = TTF_OpenFontRW(fontStream, 1, size);
			if (font)
			{
				fontFilename = filename;
				baseSize = size;
				renderFont = font;
				renderScale = 1.0f;
				setStyle(Style(STYLE_NORMAL, 255, 255, 255));
				return true;
			}
		}
		return false;
	}

	void TrueTypeFont::clearCache(void)
	{
		for (std::map<CacheKey, CacheData>::iterator it = cache.begin(); it != cache.end(); ++it)
			delete it->second.s;
		cache.clear();
		timeCache.clear();
	}

	void TrueTypeFont::updateRenderScale(void)
	{
		if (!font)
			return;

		// Glyphs are rasterised once and then resampled with the rest of the frame, so on a
		// scaled screen they are the only part of the interface that cannot use the pixels it
		// is given. Rasterise them at the size they are actually drawn at instead, while every
		// reported metric keeps coming from the authored size: the layout does not move.
		const float wanted = _gc ? _gc->textRenderScale() : 1.0f;
		const unsigned generation = _gc ? _gc->getGLContextGeneration() : 0;
		// A texture from a replaced GL context no longer names anything
		if (generation != textureGeneration)
		{
			clearCache();
			textureGeneration = generation;
		}
		// Font sizes are integers: only a scale that changes the raster size is worth reopening
		const unsigned wantedSize = std::max(1u, static_cast<unsigned>(std::lround(baseSize * wanted)));
		const unsigned currentSize = std::max(1u, static_cast<unsigned>(std::lround(baseSize * renderScale)));
		if (wantedSize == currentSize)
		{
			renderScale = wanted;
			return;
		}

		TTF_Font *replacement = NULL;
		if (wantedSize != baseSize)
		{
			if (SDL_RWops *fontStream = Toolkit::getFileManager()->open(fontFilename, "rb"))
				replacement = TTF_OpenFontRW(fontStream, 1, wantedSize);
			if (!replacement && verbose)
				std::cerr << "TrueTypeFont : cannot reopen " << fontFilename << " at size " << wantedSize
					<< ", keeping the authored raster" << std::endl;
		}

		clearCache();
		if (renderFont && renderFont != font)
			TTF_CloseFont(renderFont);
		// Falling back to the authored raster only costs sharpness, so a failed reopen is not fatal
		renderFont = replacement ? replacement : font;
		renderScale = replacement ? wanted : 1.0f;
		applyStyle();
	}

	void TrueTypeFont::applyStyle(void)
	{
		assert(font);
		assert(styleStack.size() > 0);
		TTF_SetFontStyle(font, styleStack.top().shape);
		if (renderFont && renderFont != font)
			TTF_SetFontStyle(renderFont, styleStack.top().shape);
	}

	bool TrueTypeFont::targetScalesText(const DrawableSurface *surface) const
	{
		// Only the screen carries a scaled GL viewport. An offscreen surface, for instance the
		// one an overlay dialog composes itself on, stores logical pixels and would have to
		// resample a raster made for the screen before anything reaches it.
		return renderFont != font && _gc != NULL && surface == static_cast<const DrawableSurface *>(_gc);
	}

	std::string TrueTypeFont::shapeText(const std::string &text) const
	{
#ifdef HAVE_FRIBIDI
		char *bidiStr = const_cast<TrueTypeFont *>(this)->getBIDIString(text);
		std::string shaped(bidiStr);
		delete []bidiStr;
		return shaped;
#else
		return text;
#endif
	}
	
	int TrueTypeFont::getStringWidth(const std::string string)
	{
		const CacheData *data = getStringCached(string, renderFont != font);
		int w;
		if (data)
		{
			w = data->w;
			cleanupCache();
		}
		else
			w = 0;
		return w;
	}
	
	int TrueTypeFont::getStringHeight(const std::string string)
	{
		int h;
		if (!string.empty())
		{
			const CacheData *data = getStringCached(string, renderFont != font);
			if (data)
			{
				h = data->h;
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

	bool TrueTypeFont::hasGlyphsFor(const std::string &utf8Text)
	{
		const unsigned char *s = reinterpret_cast<const unsigned char *>(utf8Text.c_str());
		while (*s)
		{
			Uint32 codepoint;
			int len;
			if ((*s & 0x80) == 0x00) { codepoint = *s; len = 1; }
			else if ((*s & 0xE0) == 0xC0) { codepoint = *s & 0x1F; len = 2; }
			else if ((*s & 0xF0) == 0xE0) { codepoint = *s & 0x0F; len = 3; }
			else if ((*s & 0xF8) == 0xF0) { codepoint = *s & 0x07; len = 4; }
			else { s++; continue; } // not a valid UTF-8 lead byte, skip it

			for (int i = 1; i < len && s[i]; i++)
				codepoint = (codepoint << 6) | (s[i] & 0x3F);

			if (!TTF_GlyphIsProvided32(font, codepoint))
				return false;

			s += len;
		}
		return true;
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
		applyStyle();
	}
	
	void TrueTypeFont::popStyle(void)
	{
		assert(font);
		
		if (styleStack.size() > 1)
		{
			styleStack.pop();
			applyStyle();
		}
	}
	
	Font::Style TrueTypeFont::getStyle(void) const
	{
		assert(font);
		
		return styleStack.top();
	}
	
	const TrueTypeFont::CacheData *TrueTypeFont::getStringCached(const std::string text, bool scaled)
	{
		assert(font);
		assert(styleStack.size()>0);
		
		updateRenderScale();
		scaled = scaled && (renderFont != font);

		CacheKey key;
		key.text = text;
		key.style = styleStack.top();
		key.scaled = scaled;
		
		std::map<CacheKey, CacheData>::iterator keyIt = cache.find(key);
		if (keyIt == cache.end())
		{
			// create bitmap
			SDL_Color c;
			c.r = styleStack.top().color.r;
			c.g = styleStack.top().color.g;
			c.b = styleStack.top().color.b;
			c.a = styleStack.top().color.a;
			const std::string shaped = shapeText(text);
			SDL_Surface *temp = TTF_RenderUTF8_Blended(scaled ? renderFont : font, shaped.c_str(), c);
			if (temp == NULL)
				return NULL;
			
			// create key
			CacheData data;
			data.lastAccessed = now;
			data.s = new DrawableSurface(temp);
			assert(data.s);
			SDL_FreeSurface(temp);
			// The raster may be finer than the layout; callers only ever see the authored size
			const float rasterScale = (scaled && renderScale > 0.0f) ? renderScale : 1.0f;
			data.drawW = data.s->getW() / rasterScale;
			data.drawH = data.s->getH() / rasterScale;
			if (!scaled || TTF_SizeUTF8(font, shaped.c_str(), &data.w, &data.h) != 0)
			{
				data.w = static_cast<int>(std::lround(data.drawW));
				data.h = static_cast<int>(std::lround(data.drawH));
			}
			
			// store in cache
			keyIt = cache.insert(std::make_pair(key, data)).first;
			timeCache[now] = keyIt;
			cacheMiss++;
		}
		else
		{
			// erase old time association
			timeCache.erase(keyIt->second.lastAccessed);
			// set new time
			keyIt->second.lastAccessed = now;
			// add new time association
			timeCache[now] = keyIt;
			cacheHit++;
		}
		now++;
		return &keyIt->second;
	}
#ifdef HAVE_FRIBIDI 
	char *TrueTypeFont::getBIDIString (const std::string text)
	{
		
		const char	*c_str = text.c_str();
		int		len = strlen(c_str);
		FriBidiChar	*bidi_logical = new FriBidiChar[len + 2];
		FriBidiChar	*bidi_visual = new FriBidiChar[len + 2];
		char		*utf8str = new char[4*len + 1];	//assume worst case here (all 4 Byte characters)
		FriBidiCharType	base_dir = FRIBIDI_TYPE_ON;
		int n;
		// fribidi_charset_to_unicode should take a const char*
		n = fribidi_charset_to_unicode (fribidi_parse_charset ((char*)"UTF-8"),const_cast<char*>(c_str), len, bidi_logical);
		fribidi_log2vis(bidi_logical, n, &base_dir, bidi_visual, NULL, NULL, NULL);
		n =  fribidi_remove_bidi_marks (bidi_visual, n, NULL, NULL, NULL);
		fribidi_unicode_to_charset (fribidi_parse_charset ((char*)"UTF-8"),bidi_visual, n, utf8str);
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
	
	void TrueTypeFont::drawString(DrawableSurface *surface, int x, int y, int w, const std::string text, Uint8 alpha)
	{
		// get
		const bool scaled = targetScalesText(surface);
		const CacheData *data = getStringCached(text, scaled);
		if (data == NULL)
			return;
		DrawableSurface *s = data->s;
		// Drawn at the raster's own size, from the same origin as before, so no glyph is
		// resampled. The reported width stays the authored one, which the raster matches to
		// within the rounding the font applies differently at every size.
		const float dw = data->drawW, dh = data->drawH;
		
		// render
		if (w)
		{
			int rx, ry, rw, rh;
			surface->getClipRect(&rx, &ry, &rw, &rh);
			int nrw = std::min(rw, x + w - rx);
			surface->setClipRect(rx, ry, nrw, rh);
			if (scaled)
				surface->drawSurface(static_cast<float>(x), static_cast<float>(y), dw, dh, s, alpha);
			else
				surface->drawSurface(x, y, s, alpha);
			surface->setClipRect(rx, ry, rw, rh);
			
		}
		else if (scaled)
			surface->drawSurface(static_cast<float>(x), static_cast<float>(y), dw, dh, s, alpha);
		else
			surface->drawSurface(x, y, s, alpha);
		
		// cleanup
		cleanupCache();
	}
	
	void TrueTypeFont::drawString(DrawableSurface *surface, float x, float y, float w, const std::string text, Uint8 alpha)
	{
		// get
		const bool scaled = targetScalesText(surface);
		const CacheData *data = getStringCached(text, scaled);
		if (data == NULL)
			return;
		DrawableSurface *s = data->s;
		const float dw = data->drawW, dh = data->drawH;
		
		// render
		if (w != 0.0f)
		{
			int rx, ry, rw, rh;
			surface->getClipRect(&rx, &ry, &rw, &rh);
			int nrw = std::min(rw, (int)x + (int)w - rx);
			surface->setClipRect(rx, ry, nrw, rh);
			if (scaled)
				surface->drawSurface(x, y, dw, dh, s, alpha);
			else
				surface->drawSurface(x, y, s, alpha);
			surface->setClipRect(rx, ry, rw, rh);
			
		}
		else if (scaled)
			surface->drawSurface(x, y, dw, dh, s, alpha);
		else
			surface->drawSurface(x, y, s, alpha);
		
		// cleanup
		cleanupCache();
	}
}
