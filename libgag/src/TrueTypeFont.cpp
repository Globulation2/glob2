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
#include <AssetLoader.h>
#include <StringTable.h>
#include <BidiText.h>
#include <limits>
#include <tuple>

#ifdef HAVE_FRIBIDI
#include <fribidi/fribidi.h>
#endif

#define MAX_CACHE_SIZE 128

namespace GAGCore
{
namespace
{
struct Codepoint { Uint32 value; std::string bytes; };
std::vector<Codepoint> codepoints(const std::string &text)
{
	std::vector<Codepoint> result;
	for (size_t i = 0; i < text.size();)
	{
		const auto c = static_cast<unsigned char>(text[i]);
		size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
		if (i + length > text.size()) length = 1;
		Uint32 value = length == 1 ? c : c & (0x7F >> length);
		for (size_t k = 1; k < length; ++k)
			value = (value << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
		result.push_back({value, text.substr(i, length)});
		i += length;
	}
	return result;
}

std::string scriptFor(Uint32 c)
{
	if (c >= 0x0900 && c <= 0x097F) return "Devanagari";
	if (c >= 0x0980 && c <= 0x09FF) return "Bengali";
	if (c >= 0x0A00 && c <= 0x0A7F) return "Gurmukhi";
	if (c >= 0x0A80 && c <= 0x0AFF) return "Gujarati";
	if (c >= 0x0B00 && c <= 0x0B7F) return "Oriya";
	if (c >= 0x0B80 && c <= 0x0BFF) return "Tamil";
	if (c >= 0x0C00 && c <= 0x0C7F) return "Telugu";
	if (c >= 0x0C80 && c <= 0x0CFF) return "Kannada";
	if (c >= 0x0D00 && c <= 0x0D7F) return "Malayalam";
	if (c >= 0x0E00 && c <= 0x0E7F) return "Thai";
	if ((c >= 0x1000 && c <= 0x109F) || (c >= 0xA9E0 && c <= 0xA9FF) ||
	    (c >= 0xAA60 && c <= 0xAA7F)) return "Myanmar";
	if ((c >= 0x1200 && c <= 0x139F) || (c >= 0x2D80 && c <= 0x2DDF) ||
	    (c >= 0xAB00 && c <= 0xAB2F)) return "Ethiopic";
	if ((c >= 0x0600 && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F) ||
	    (c >= 0x0870 && c <= 0x08FF) || (c >= 0xFB50 && c <= 0xFDFF) ||
	    (c >= 0xFE70 && c <= 0xFEFF)) return "Arabic";
	return {};
}

bool inheritsScript(Uint32 c)
{
	return c < 0x40 || (c >= 0x2000 && c <= 0x206F) ||
		(c >= 0x1CD0 && c <= 0x1CFF) || (c >= 0x0300 && c <= 0x036F) ||
		(c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
		(c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F);
}

// Convert cached glyph dimensions to logical units using the exact output
// transform. Rounded layout axes may have slightly different pixel densities.
std::pair<float, float> glyphDrawSize(float width, float height, float rasterScale, bool scaled,
                                    float requestedScale, float outputScaleX, float outputScaleY,
                                    int authoredWidth)
{
	if (!scaled) return {width, height};
	const float correction = rasterScale / requestedScale;
	// Hinted advances do not scale linearly. A line that fits the authored
	// layout can otherwise become several percent wider at high DPI. Preserve
	// its complete high-resolution raster, but draw opted-in new locales into the
	// same width used by wrapping, alignment and hit testing. Do not apply the
	// raster-size quantization correction to this already-logical width.
	width = authoredWidth >= 0 ? float(authoredWidth) : width * correction;
	height *= correction;
	width *= std::min(outputScaleX, outputScaleY) / outputScaleX;
	height *= std::min(outputScaleX, outputScaleY) / outputScaleY;
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
	clearScriptFonts();
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
        auto source = Toolkit::assets().requestBytes(filename);
        auto bytes = Toolkit::assets().wait(source);
        if (!bytes) return nullptr;
        SDL_IOStream *stream = SDL_IOFromConstMem(bytes->data(), bytes->size());
        if (!stream) return nullptr;
        using Source = AssetLoader::Handle<AssetLoader::Bytes>;
        auto *owned = new Source(std::move(source));
        if (!SDL_SetPointerPropertyWithCleanup(SDL_GetIOProperties(stream), "glob2.font-source", owned,
                [](void*, void *value) { delete static_cast<Source*>(value); }, nullptr)) {
            SDL_CloseIO(stream); return nullptr;
        }
        TTF_Font *result = TTF_OpenFontIO(stream, true, openSize);
        // shapeText already joins Arabic/Persian and puts every run in visual
        // order. HarfBuzz's default direction guessing would reorder that text
        // a second time, reversing both the letters and embedded Latin/numbers.
        // Set this on every opened size, including cached high-DPI rasters.
        if (result && !TTF_SetFontDirection(result, TTF_DIRECTION_LTR)) {
            TTF_CloseFont(result);
            return nullptr;
        }
        return result;
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
	clearScriptFonts();
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
	clearScriptFonts();
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

void TrueTypeFont::updateRenderScale(float explicitScale)
{
	if (!font)
		return;

	// Glyphs are rasterised once and then resampled with the rest of the frame, so on a
	// scaled screen they are the only part of the interface that cannot use the pixels it
	// is given. Rasterise them at the size they are actually drawn at instead, while every
	// reported metric keeps coming from the authored size: the layout does not move.
	const float wanted = explicitScale > 0 ? explicitScale : (_gc ? _gc->textRenderScale() : 1.0f);
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
	return visualOrder(text);
#endif
}

bool TrueTypeFont::needsScriptLayout(const std::string &text) const
{
	if (!activeScript.empty()) return true;
	for (const auto &c : codepoints(text))
	{
		const auto script = scriptFor(c.value);
		if (!script.empty() && (script != "Arabic" || useScriptArabic || !TTF_FontHasGlyph(font, c.value)))
			return true;
	}
	return false;
}

void TrueTypeFont::refreshScriptLanguage()
{
	const auto *strings = Toolkit::getStringTable();
	const std::string code = strings && strings->doesStringExist("[language-code]")
		? strings->getString("[language-code]") : std::string();
	std::string wanted;
	if (code == "hi" || code == "mr" || code == "ne" || code == "bho") wanted = "Devanagari";
	else if (code == "ur" || code == "pa-arab" || code == "ps" || code == "sd") wanted = "Arabic";
	else if (code == "bn") wanted = "Bengali";
	else if (code == "gu") wanted = "Gujarati";
	else if (code == "pa-guru") wanted = "Gurmukhi";
	else if (code == "ta") wanted = "Tamil";
	else if (code == "te") wanted = "Telugu";
	else if (code == "kn") wanted = "Kannada";
	else if (code == "ml") wanted = "Malayalam";
	else if (code == "or") wanted = "Oriya";
	else if (code == "th") wanted = "Thai";
	else if (code == "my") wanted = "Myanmar";
	else if (code == "am") wanted = "Ethiopic";
	// Languages sharing a script can have different OpenType locl features.
	// Keep the language on the authored font, including across hi/mr/ne or
	// ur/pa-arab/ps/sd switches that retain the same script family.
	const auto properties = TTF_GetFontProperties(font);
	const std::string previousLanguage = SDL_GetStringProperty(properties, "glob2.script-language", "");
	if (wanted != activeScript || code != previousLanguage)
	{
		SDL_SetStringProperty(properties, "glob2.script-language", code.c_str());
		activeScript = wanted;
		useScriptArabic = wanted == "Arabic";
		clearCache();
		clearMetrics();
	}
}

void TrueTypeFont::clearScriptFonts()
{
	// Fallbacks borrow the original fonts, so close these before base/raster fonts.
	for (const auto &entry : scriptFonts) TTF_CloseFont(entry.second);
	scriptFonts.clear();
}

TTF_Font *TrueTypeFont::scriptFont(const std::string &script, unsigned size, TTF_Font *fallback)
{
	int shapingVersion = 0;
	TTF_GetHarfBuzzVersion(&shapingVersion, nullptr, nullptr);
	if (!shapingVersion)
	{
		SDL_SetError("Script fonts require SDL3_ttf built with HarfBuzz");
		return nullptr;
	}
	const auto key = std::make_pair(script, size);
	auto found = scriptFonts.find(key);
	const char *language = SDL_GetStringProperty(TTF_GetFontProperties(font), "glob2.script-language", "");
	if (found != scriptFonts.end())
		return TTF_SetFontLanguage(found->second, *language ? language : nullptr) ? found->second : nullptr;
	TTF_Font *added = openFont("data/fonts/scripts/NotoSans" + script + "-Regular.ttf", size);
	if (!added) return nullptr;
	if (!TTF_AddFallbackFont(added, fallback) || !TTF_SetFontLanguage(added, *language ? language : nullptr))
	{
		TTF_CloseFont(added);
		return nullptr;
	}
	scriptFonts.emplace(key, added);
	return added;
}

std::pair<int, int> TrueTypeFont::layoutScriptText(const std::string &text, TTF_Font *base,
                                                unsigned size, SDL_Color color, SDL_Surface **surface)
{
	struct Run { std::string text, script; bool rtl; TTF_Font *font; int w, h, ascent; };
	std::vector<Run> runs;
	for (const auto &bidi : logicalBidiRuns(text))
	{
		std::vector<Run> logical;
		for (const auto &c : codepoints(bidi.text))
		{
			auto script = scriptFor(c.value);
			// Common punctuation/combining join controls stay with their script.
			if (script.empty() && !logical.empty() && inheritsScript(c.value))
				script = logical.back().script;
			if (logical.empty() || logical.back().script != script)
				logical.push_back({"", script, bidi.rtl, nullptr, 0, 0, 0});
			logical.back().text += c.bytes;
		}
		if (bidi.rtl) std::reverse(logical.begin(), logical.end());
		for (auto &run : logical) runs.push_back(std::move(run));
	}
	int width = 0, baseline = TTF_GetFontAscent(base), below = TTF_GetFontHeight(base) - baseline;
	if (!activeScript.empty())
	{
		TTF_Font *primary = scriptFont(activeScript, size, base);
		if (!primary) return {0, 0};
		TTF_SetFontStyle(primary, TTF_GetFontStyle(base));
		baseline = std::max(baseline, TTF_GetFontAscent(primary));
		// SDL_ttf's nominal height can be smaller than its hinted raster bounds:
		// the pinned Noto Devanagari/Gujarati probes need one extra pixel and
		// Gurmukhi needs two at the UI sizes. Reserve the rounding clearance in
		// the locale's default line box too, including Latin-only labels.
		below = std::max(below, TTF_GetFontHeight(primary) + 2 - TTF_GetFontAscent(primary));
	}
	for (auto &run : runs)
	{
		run.font = run.script.empty() ? base : scriptFont(run.script, size, base);
		if (!run.font) return {0, 0};
		TTF_SetFontStyle(run.font, TTF_GetFontStyle(base));
		if (!TTF_SetFontDirection(run.font, run.rtl ? TTF_DIRECTION_RTL : TTF_DIRECTION_LTR)) return {0, 0};
		const bool measured = TTF_GetStringSize(run.font, run.text.c_str(), 0, &run.w, &run.h);
		TTF_SetFontDirection(run.font, TTF_DIRECTION_LTR);
		if (!measured || run.w > std::numeric_limits<int>::max() - width) return {0, 0};
		run.ascent = TTF_GetFontAscent(run.font);
		baseline = std::max(baseline, run.ascent);
		below = std::max(below, run.h - run.ascent);
		width += run.w;
	}
	const int height = baseline + below;
	if (surface)
	{
		*surface = SDL_CreateSurface(std::max(1, width), std::max(1, height), SDL_PIXELFORMAT_RGBA32);
		if (!*surface) return {0, 0};
		SDL_FillSurfaceRect(*surface, nullptr, 0);
		int x = 0;
		for (const auto &run : runs)
		{
			TTF_SetFontDirection(run.font, run.rtl ? TTF_DIRECTION_RTL : TTF_DIRECTION_LTR);
			SDL_Surface *part = TTF_RenderText_Blended(run.font, run.text.c_str(), 0, color);
			TTF_SetFontDirection(run.font, TTF_DIRECTION_LTR);
			if (!part) { SDL_DestroySurface(*surface); *surface = nullptr; return {0, 0}; }
			// Preserve straight alpha while assembling the cached glyph surface.
			// Blending onto transparency here would multiply coverage into RGB
			// before the completed text is blended onto its real background.
			if (!SDL_SetSurfaceBlendMode(part, SDL_BLENDMODE_NONE))
			{ SDL_DestroySurface(part); SDL_DestroySurface(*surface); *surface = nullptr; return {0, 0}; }
			SDL_Rect dst{x, baseline - run.ascent, part->w, part->h};
			const bool copied = SDL_BlitSurface(part, nullptr, *surface, &dst);
			SDL_DestroySurface(part);
			if (!copied) { SDL_DestroySurface(*surface); *surface = nullptr; return {0, 0}; }
			x += run.w;
		}
	}
	return {width, height};
}

std::vector<std::size_t> TrueTypeFont::getStringClusterEnds(const std::string &text)
{
	if (text.empty()) return {};
	refreshScriptLanguage();
	struct Span { std::string text, script; size_t offset; };
	std::vector<Span> spans;
	size_t offset = 0;
	for (const auto &c : codepoints(text))
	{
		auto script = scriptFor(c.value);
		if (script.empty() && !spans.empty() && inheritsScript(c.value)) script = spans.back().script;
		if (spans.empty() || spans.back().script != script) spans.push_back({"", script, offset});
		spans.back().text += c.bytes;
		offset += c.bytes.size();
	}
	std::vector<std::size_t> ends;
	for (const auto &span : spans)
	{
		TTF_Font *selected = span.script.empty() ? font : scriptFont(span.script, baseSize, font);
		if (!selected) return {text.size()};
		TTF_SetFontStyle(selected, TTF_GetFontStyle(font));
		TTF_Text *layout = TTF_CreateText(nullptr, selected, span.text.c_str(), span.text.size());
		if (!layout) return {text.size()};
		bool valid = TTF_SetTextDirection(layout, span.script == "Arabic" ? TTF_DIRECTION_RTL : TTF_DIRECTION_LTR);
		for (size_t at = 0; valid && at < span.text.size();)
		{
			TTF_SubString cluster{};
			valid = at <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
				TTF_GetTextSubString(layout, static_cast<int>(at), &cluster) && cluster.offset >= 0 && cluster.length > 0;
			if (!valid) break;
			const size_t end = static_cast<size_t>(cluster.offset) + static_cast<size_t>(cluster.length);
			if (end <= at || end > span.text.size()) { valid = false; break; }
			ends.push_back(span.offset + end);
			at = end;
		}
		TTF_DestroyText(layout);
		if (!valid) return {text.size()};
	}
	ends.push_back(text.size());
	std::sort(ends.begin(), ends.end());
	ends.erase(std::unique(ends.begin(), ends.end()), ends.end());
	return ends;
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
	refreshScriptLanguage();
	if (text.empty())
		return activeScript.empty() ? std::make_pair(0, TTF_GetFontHeight(font))
			: layoutScriptText(text, font, baseSize, {255, 255, 255, 255}, nullptr);
	MetricsKey key{text, styleStack.top().shape};
	auto found = metricsCache.find(key);
	if (found != metricsCache.end())
	{
		metricsAge.splice(metricsAge.end(), metricsAge, found->second.age);
		return {found->second.w, found->second.h};
	}
	int w = 0, h = 0;
	if (needsScriptLayout(text))
		std::tie(w, h) = layoutScriptText(text, font, baseSize, {255, 255, 255, 255}, nullptr);
	else
	{
		const auto shaped = shapeText(text);
		if (!TTF_GetStringSize(font, shaped.c_str(), 0, &w, &h)) return {0, 0};
	}
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
		{
			const auto script = scriptFor(codepoint);
			TTF_Font *fallback = script.empty() ? nullptr : scriptFont(script, baseSize, font);
			if (!fallback || !TTF_FontHasGlyph(fallback, codepoint)) return false;
		}

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

std::unique_ptr<DrawableSurface> TrueTypeFont::rasterizeString(const std::string &text, float scale)
{
	const auto *data = getStringCached(text, true, scale);
	if (!data) return {};
	auto result = std::make_unique<DrawableSurface>(data->s->getSDLSurface());
	cleanupCache();
	return result;
}

std::pair<float, float> TrueTypeFont::getStringDrawSize(const std::string &text, float scale,
                                                     float outputScaleX, float outputScaleY)
{
	if (!(scale > 0.f) || !(outputScaleX > 0.f) || !(outputScaleY > 0.f) ||
		!std::isfinite(scale) || !std::isfinite(outputScaleX) || !std::isfinite(outputScaleY)) return {0.f, 0.f};
	const auto *data = getStringCached(text, true, scale);
	if (!data) return {0.f, 0.f};
	const auto size = glyphDrawSize(data->drawW, data->drawH, renderScale, true,
		scale, outputScaleX, outputScaleY, data->authoredDrawWidth ? data->w : -1);
	cleanupCache();
	return size;
}

const TrueTypeFont::CacheData *TrueTypeFont::getStringCached(const std::string text, bool scaled, float explicitScale)
{
	assert(font);
	assert(styleStack.size() > 0);
	refreshScriptLanguage();

	if (scaled)
		updateRenderScale(explicitScale);
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
		const bool scripts = needsScriptLayout(text);
		const std::string shaped = scripts ? std::string() : shapeText(text);
		SDL_Surface *temp = nullptr;
		if (scripts) layoutScriptText(text, scaled ? renderFont : font, key.rasterSize, c, &temp);
		else temp = TTF_RenderText_Blended(scaled ? renderFont : font, shaped.c_str(), 0, c);
		if (temp == NULL)
			return NULL;

		// create key
		CacheData data;
		// New locales opt into authored advances; preserve the existing Latin
		// geometry for legacy locales. Swahili and Bokmål use the original Latin font,
		// not the script-font/bidi composition path.
		const std::string language = SDL_GetStringProperty(
			TTF_GetFontProperties(font), "glob2.script-language", "");
		data.authoredDrawWidth = scripts || language == "sw" || language == "nb";
		data.lastAccessed = now;
		data.s = new DrawableSurface(temp);
		assert(data.s);
		SDL_DestroySurface(temp);
		// The raster may be finer than the layout; callers only ever see the authored size
		const float rasterScale = (scaled && renderScale > 0.0f) ? renderScale : 1.0f;
		data.drawW = data.s->getW() / rasterScale;
		data.drawH = data.s->getH() / rasterScale;
		if (scripts)
		{
			const auto measured = measureString(text);
			data.w = measured.first; data.h = measured.second;
		}
		else if (!scaled || !TTF_GetStringSize(font, shaped.c_str(), 0, &data.w, &data.h))
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
	const auto [dw, dh] = glyphDrawSize(data->drawW, data->drawH, renderScale, scaled,
		scaled ? _gc->textRenderScale() : 1.f,
		scaled && _gc->isNativeDesktop() ? float(_gc->getDrawableW()) / _gc->getW() : 1.f,
		scaled && _gc->isNativeDesktop() ? float(_gc->getDrawableH()) / _gc->getH() : 1.f,
		data->authoredDrawWidth ? data->w : -1);

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
	const auto [dw, dh] = glyphDrawSize(data->drawW, data->drawH, renderScale, scaled,
		scaled ? _gc->textRenderScale() : 1.f,
		scaled && _gc->isNativeDesktop() ? float(_gc->getDrawableW()) / _gc->getW() : 1.f,
		scaled && _gc->isNativeDesktop() ? float(_gc->getDrawableH()) / _gc->getH() : 1.f,
		data->authoredDrawWidth ? data->w : -1);

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
