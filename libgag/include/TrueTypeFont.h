// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

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
		int getStringWidth(const std::string string);
		//! Get the height of string with shape. If string is NULL, return base value, else update cache
		int getStringHeight(const std::string string);
		//! Whether every character of this UTF-8 string has a glyph in the loaded font
		virtual bool hasGlyphsFor(const std::string &utf8Text);

		// Style and color
		virtual void setStyle(Style style);
		virtual Style getStyle(void) const;
		
	protected:
		struct CacheKey
		{
			std::string text;
			Style style;
			//! Whether this entry holds the raster made for the screen, which only the screen can use
			bool scaled;

			bool operator<(const CacheKey &o) const
			{
				if (text != o.text) return (text < o.text);
				if (style < o.style) return true;
				if (o.style < style) return false;
				return (scaled < o.scaled);
			}
		};

		struct CacheData
		{
			DrawableSurface *s;
			unsigned lastAccessed;
			//! Size in logical pixels, as the authored font size measures it
			int w, h;
			//! The raster's own size in logical pixels. Hinting rounds every size differently,
			//! so this is close to but not exactly w x h, and drawing at it keeps one raster
			//! pixel on one screen pixel instead of stretching glyphs by a few percent.
			float drawW, drawH;
		};

		//! Init internal variables
		void init(void);
		virtual void drawString(DrawableSurface *surface, int x, int y, int w, const std::string text, Uint8 alpha);
		virtual void drawString(DrawableSurface *surface, float x, float y, float w, const std::string text, Uint8 alpha);
		virtual void pushStyle(Style style);
		virtual void popStyle(void);

		//! If text is cached, returns its entry. If it is not, create, cache and return it.
		//! scaled asks for the raster made for a magnifying target; the reported size is the
		//! authored one either way.
		const CacheData *getStringCached(const std::string text, bool scaled);
		//! Whether text drawn on this target is resampled on its way to the screen
		bool targetScalesText(const DrawableSurface *surface) const;
		//! If cache is too big, remove old entry
		void cleanupCache(void);
		//! Drop every rendered string, for instance when their raster or textures went stale
		void clearCache(void);
		//! Reopen the rasterisation font when the screen started scaling text differently
		void updateRenderScale(void);
		//! Apply the current style to both the metrics and the rasterisation font
		void applyStyle(void);
		//! The text as it is handed to SDL_ttf: reordered where fribidi is available
		std::string shapeText(const std::string &text) const;
#ifdef HAVE_FRIBIDI
		char *getBIDIString (const std::string text);
#endif
	protected:
		//! Metrics at the size the layout was authored in. Every width and height reported
		//! to callers comes from here, so a finer raster never moves a widget.
		TTF_Font *font;
		//! Rasterisation at size * renderScale; the same object as font while renderScale is 1
		TTF_Font *renderFont;
		//! Where font was loaded from, so the raster can be reopened at another size
		std::string fontFilename;
		//! The size the layout was authored in
		unsigned baseSize;
		//! Drawable pixels per logical pixel the cached rasters were made for
		float renderScale;
		//! GL context the cached textures belong to
		unsigned textureGeneration;
		std::stack<Style> styleStack;

		unsigned now;
		std::map<CacheKey, CacheData> cache;
		std::map<unsigned, std::map<CacheKey, CacheData>::iterator> timeCache;
		//! number of cache hit
		unsigned cacheHit;
		//! number of cache miss
		unsigned cacheMiss;
	};
}
