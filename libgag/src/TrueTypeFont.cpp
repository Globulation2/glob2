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
#include <utility>

#ifdef HAVE_FRIBIDI
#include <fribidi/fribidi.h>
#endif

#define MAX_CACHE_SIZE 128

namespace GAGCore
{
namespace
{
// Convert cached glyph dimensions to logical units using the exact output
// transform. Rounded layout axes may have slightly different pixel densities.
std::pair<float, float> glyphDrawSize(float width, float height, float rasterScale, bool scaled)
{
	if (!scaled) return {width, height};
	const float correction = rasterScale / _gc->textRenderScale();
	width *= correction;
	height *= correction;
	if (_gc->isNativeDesktop())
	{
		const float sx = float(_gc->getDrawableW()) / _gc->getW();
		const float sy = float(_gc->getDrawableH()) / _gc->getH();
		width *= std::min(sx, sy) / sx;
		height *= std::min(sx, sy) / sy;
	}
	return {width, height};
}
}
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
						  TTF_GetFontHeight(font) <<*/
					" had " << cacheHit + cacheMiss << " requests, " << cacheHit << " hits ("
						  << static_cast<float>(cacheHit) / cacheTotal << "), " << cacheMiss
						  << " misses (" << static_cast<float>(cacheMiss) / cacheTotal << ")"
						  << std::endl;
		}
		// free cache
		clearCache();
		// close fonts
		for (const auto &[size, raster] : rasterFonts)
			TTF_CloseFont(raster);
		TTF_CloseFont(font);
	}
}

bool TrueTypeFont::kerningMovesPen(TTF_Font *kerned, TTF_Font *unkerned)
{
	// T and o kern strongly in the game's font (DejaVu Sans) and in most Latin fonts.
	int kerning = 0;
	if (!TTF_GetGlyphKerning(kerned, 'T', 'o', &kerning) || kerning == 0)
		return true;
	// The measured width ends at the pen. A correct layout moves the pen by the kerning;
	// the broken one moves only the glyph, so both widths come out the same.
	const char *const sample = "ToTo";
	int withKerning = 0, withoutKerning = 0;
	size_t length = 0;
	if (!TTF_MeasureString(kerned, sample, 0, 0, &withKerning, &length) ||
		!TTF_MeasureString(unkerned, sample, 0, 0, &withoutKerning, &length))
		return true;
	return withKerning != withoutKerning;
}

TTF_Font *TrueTypeFont::openFont(const std::string &filename, unsigned size)
{
	// -1 unknown, 0 kerning misplaces glyphs, 1 kerning works
	static int kerningWorks = -1;
	auto open = [&](unsigned openSize) -> TTF_Font *
	{
		SDL_IOStream *stream = Toolkit::getFileManager()->open(filename, "rb");
		return stream ? TTF_OpenFontIO(stream, 1, openSize) : NULL;
	};
	TTF_Font *opened = open(size);
	if (!opened)
		return NULL;
	if (kerningWorks < 0)
	{
		// Probe at a size where one kerning unit is several pixels.
		TTF_Font *kerned = open(32), *unkerned = open(32);
		if (kerned && unkerned)
		{
			TTF_SetFontKerning(unkerned, false);
			int kerning = 0;
			if (TTF_GetGlyphKerning(kerned, 'T', 'o', &kerning) && kerning != 0)
			{
				kerningWorks = kerningMovesPen(kerned, unkerned) ? 1 : 0;
				if (!kerningWorks)
					std::cerr << "TrueTypeFont: this SDL3_ttf misplaces kerned glyphs; drawing text without kerning" << std::endl;
			}
		}
		if (kerned)
			TTF_CloseFont(kerned);
		if (unkerned)
			TTF_CloseFont(unkerned);
	}
	if (kerningWorks == 0)
		TTF_SetFontKerning(opened, false);
	return opened;
}

bool TrueTypeFont::load(const std::string filename, unsigned size)
{
	TTF_Font *replacement = openFont(filename, size);
	if (!replacement)
		return false;
	clearCache();
	clearMetrics();
	for (const auto &[rasterSize, raster] : rasterFonts)
		TTF_CloseFont(raster);
	rasterFonts.clear();
	if (font)
		TTF_CloseFont(font);
	font = replacement;
	fontFilename = filename;
	baseSize = size;
	renderFont = font;
	renderScale = 1.0f;
	setStyle(Style(STYLE_NORMAL, 255, 255, 255));
	return true;
}

bool TrueTypeFont::reload(void)
{
	if (!font)
		return false;
	TTF_Font *replacement = openFont(fontFilename, baseSize);
	if (!replacement)
		return false;
	clearCache();
	clearMetrics();
	for (const auto &[size, raster] : rasterFonts)
		TTF_CloseFont(raster);
	rasterFonts.clear();
	TTF_CloseFont(font);
	font = replacement;
	renderFont = font;
	renderScale = 1.0f;
	applyStyle();
	return true;
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
	// Quantize once to the font's actual raster size; cached geometry must
	// not depend on which nearly identical transform first requested it.
	const unsigned wantedSize = std::max(1u, static_cast<unsigned>(std::lround(baseSize * wanted)));
	renderFont = font;
	renderScale = 1.0f;
	if (wantedSize != baseSize)
	{
		auto found = rasterFonts.find(wantedSize);
		if (found == rasterFonts.end())
		{
			TTF_Font *replacement = openFont(fontFilename, wantedSize);
			if (replacement)
				found = rasterFonts.emplace(wantedSize, replacement).first;
		}
		if (found != rasterFonts.end())
		{
			renderFont = found->second;
			renderScale = float(wantedSize) / baseSize;
		}
	}
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
	// Only the screen carries the drawable and local UI scales. An offscreen surface, for instance the
	// one an overlay dialog composes itself on, stores logical pixels and would have to
	// resample a raster made for the screen before anything reaches it.
	return _gc != NULL && surface == static_cast<const DrawableSurface *>(_gc);
}

std::string TrueTypeFont::shapeText(const std::string &text) const
{
#ifdef HAVE_FRIBIDI
	char *bidiStr = const_cast<TrueTypeFont *>(this)->getBIDIString(text);
	std::string shaped(bidiStr);
	delete[] bidiStr;
	return shaped;
#else
	return text;
#endif
}

void TrueTypeFont::clearMetrics()
{
	metricsCache.clear();
	metricsAge.clear();
	metricsTextBytes = 0;
}

std::pair<int, int> TrueTypeFont::measureString(const std::string &text)
{
	assert(font);
	assert(!styleStack.empty());
	if (text.empty())
		return {0, TTF_GetFontHeight(font)};
	MetricsKey key{text, styleStack.top().shape};
	auto found = metricsCache.find(key);
	if (found != metricsCache.end())
	{
		metricsAge.splice(metricsAge.end(), metricsAge, found->second.age);
		return {found->second.w, found->second.h};
	}
	int w = 0, h = 0;
	const auto shaped = shapeText(text);
	if (!TTF_GetStringSize(font, shaped.c_str(), 0, &w, &h))
		return {0, 0};
	constexpr size_t maximumEntries = 1024, maximumTextBytes = 1024 * 1024;
	// A single oversized diagnostic string must not grow the cache beyond its budget.
	if (text.size() > maximumTextBytes / 2)
		return {w, h};
	const size_t bytes = 2 * text.size();
	while (metricsCache.size() >= maximumEntries || metricsTextBytes + bytes > maximumTextBytes)
	{
		metricsTextBytes -= 2 * metricsAge.front().first.size();
		metricsCache.erase(metricsAge.front());
		metricsAge.pop_front();
	}
	metricsAge.push_back(key);
	metricsCache.emplace(std::move(key), MetricsData{w, h, std::prev(metricsAge.end())});
	metricsTextBytes += bytes;
	return {w, h};
}

int TrueTypeFont::getStringWidth(const std::string string)
{
	return measureString(string).first;
}

int TrueTypeFont::getStringHeight(const std::string string)
{
	return measureString(string).second;
}

bool TrueTypeFont::hasGlyphsFor(const std::string &utf8Text)
{
	const unsigned char *s = reinterpret_cast<const unsigned char *>(utf8Text.c_str());
	while (*s)
	{
		Uint32 codepoint;
		int len;
		if ((*s & 0x80) == 0x00)
		{
			codepoint = *s;
			len = 1;
		}
		else if ((*s & 0xE0) == 0xC0)
		{
			codepoint = *s & 0x1F;
			len = 2;
		}
		else if ((*s & 0xF0) == 0xE0)
		{
			codepoint = *s & 0x0F;
			len = 3;
		}
		else if ((*s & 0xF8) == 0xF0)
		{
			codepoint = *s & 0x07;
			len = 4;
		}
		else
		{
			s++;
			continue;
		} // not a valid UTF-8 lead byte, skip it

		for (int i = 1; i < len && s[i]; i++)
			codepoint = (codepoint << 6) | (s[i] & 0x3F);

		if (!TTF_FontHasGlyph(font, codepoint))
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
	assert(styleStack.size() > 0);

	if (scaled)
		updateRenderScale();
	scaled = scaled && (renderFont != font);

	CacheKey key;
	key.text = text;
	key.style = styleStack.top();
	key.rasterSize = scaled ? static_cast<unsigned>(std::lround(baseSize * renderScale)) : baseSize;

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
		SDL_Surface *temp = TTF_RenderText_Blended(scaled ? renderFont : font, shaped.c_str(), 0, c);
		if (temp == NULL)
			return NULL;

		// create key
		CacheData data;
		data.lastAccessed = now;
		data.s = new DrawableSurface(temp);
		assert(data.s);
		SDL_DestroySurface(temp);
		// The raster may be finer than the layout; callers only ever see the authored size
		const float rasterScale = (scaled && renderScale > 0.0f) ? renderScale : 1.0f;
		data.drawW = data.s->getW() / rasterScale;
		data.drawH = data.s->getH() / rasterScale;
		if (!scaled || !TTF_GetStringSize(font, shaped.c_str(), 0, &data.w, &data.h))
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
char *TrueTypeFont::getBIDIString(const std::string text)
{
	const char *c_str = text.c_str();
	int len = strlen(c_str);
	FriBidiChar *bidi_logical = new FriBidiChar[len + 2];
	FriBidiChar *bidi_visual = new FriBidiChar[len + 2];
	char *utf8str = new char[4 * len + 1]; //assume worst case here (all 4 Byte characters)
	FriBidiCharType base_dir = FRIBIDI_TYPE_ON;
	int n;
	// fribidi_charset_to_unicode should take a const char*
	n = fribidi_charset_to_unicode(fribidi_parse_charset((char *)"UTF-8"),
								   const_cast<char *>(c_str), len, bidi_logical);
	fribidi_log2vis(bidi_logical, n, &base_dir, bidi_visual, NULL, NULL, NULL);
	n = fribidi_remove_bidi_marks(bidi_visual, n, NULL, NULL, NULL);
	fribidi_unicode_to_charset(fribidi_parse_charset((char *)"UTF-8"), bidi_visual, n, utf8str);
	delete[] bidi_logical;
	delete[] bidi_visual;
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

void TrueTypeFont::drawString(DrawableSurface *surface, int x, int y, int w, const std::string text,
							  Uint8 alpha)
{
	// get
	const bool scaled = targetScalesText(surface);
	const CacheData *data = getStringCached(text, scaled);
	if (data == NULL)
		return;
	DrawableSurface *s = data->s;
	const auto [dw, dh] = glyphDrawSize(data->drawW, data->drawH, renderScale, scaled);

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

void TrueTypeFont::drawString(DrawableSurface *surface, float x, float y, float w,
							  const std::string text, Uint8 alpha)
{
	// get
	const bool scaled = targetScalesText(surface);
	const CacheData *data = getStringCached(text, scaled);
	if (data == NULL)
		return;
	DrawableSurface *s = data->s;
	const auto [dw, dh] = glyphDrawSize(data->drawW, data->drawH, renderScale, scaled);

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
} // namespace GAGCore
