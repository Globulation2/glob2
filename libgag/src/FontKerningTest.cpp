// SPDX-License-Identifier: GPL-3.0-or-later
// Kerned pairs must move the pen, not just one glyph. SDL3_ttf without HarfBuzz used
// to shift only the kerned glyph, which left a gap after it on the first screen every
// player sees: "Tu torial", "Yo u", "Wa rrush", "To ols", "Te ams".
#include "Glob2Test.h"
#include <FileManager.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <Toolkit.h>
#include <TrueTypeFont.h>
#include <BidiText.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using GAGCore::TrueTypeFont;

namespace
{
const char *const WORDS[] = {"Tutorial", "You", "Your delay", "Warrush", "Tools", "Teams", "Team 1",
							 "You left the match"};
const unsigned SIZES[] = {13, 16, 20, 26, 32};

struct TtfScope
{
	TtfScope() { GLOB2_REQUIRE(TTF_Init(), SDL_GetError()); }
	~TtfScope() { TTF_Quit(); }
};

std::string fontPath()
{
	return (glob2test::sourceRoot() / "data/fonts/sans.ttf").string();
}

// The widest run of blank columns between two inked columns, i.e. the widest gap
// inside the word. Spaces are measured the same way with and without kerning.
int widestInnerGap(TTF_Font *font, const char *text)
{
	SDL_Surface *raw = TTF_RenderText_Blended(font, text, 0, SDL_Color{255, 255, 255, 255});
	GLOB2_REQUIRE(raw != nullptr, SDL_GetError());
	SDL_Surface *surface = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(raw);
	GLOB2_REQUIRE(surface != nullptr, SDL_GetError());
	std::vector<bool> ink(surface->w, false);
	for (int y = 0; y < surface->h; ++y)
	{
		const Uint8 *row = static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch;
		for (int x = 0; x < surface->w; ++x)
			if (row[x * 4 + 3] > 64)
				ink[x] = true;
	}
	SDL_DestroySurface(surface);
	int widest = 0, run = 0;
	bool started = false;
	for (bool inked : ink)
	{
		if (inked)
		{
			if (started)
				widest = std::max(widest, run);
			started = true;
			run = 0;
		}
		else
			++run;
	}
	return widest;
}

int advance(TTF_Font *font, Uint32 ch)
{
	int minx, maxx, miny, maxy, value = 0;
	GLOB2_REQUIRE(TTF_GetGlyphMetrics(font, ch, &minx, &maxx, &miny, &maxy, &value), SDL_GetError());
	return value;
}

bool samePixels(SDL_Surface *first, SDL_Surface *second)
{
	if (first->w != second->w || first->h != second->h) return false;
	using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
	Surface a(SDL_ConvertSurface(first, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
	Surface b(SDL_ConvertSurface(second, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
	GLOB2_REQUIRE(a && b, SDL_GetError());
	for (int y = 0; y < a->h; ++y)
		if (std::memcmp(static_cast<const Uint8 *>(a->pixels) + y * a->pitch,
			static_cast<const Uint8 *>(b->pixels) + y * b->pitch, a->w * 4) != 0) return false;
	return true;
}
} // namespace

TEST_SUITE("FontKerning")
{
	TEST_CASE("Arabic and Persian visual-order text is not reordered again by SDL_ttf")
	{
		TtfScope ttf;
		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::getFileManager()->addDir(glob2test::sourceRoot().string());
		struct FontProbe : TrueTypeFont
		{
			using TrueTypeFont::TrueTypeFont;
			using TrueTypeFont::font;
			using TrueTypeFont::rasterFonts;
		};
		FontProbe font("data/fonts/sans.ttf", 20);
		font.setStyle(GAGCore::Font::Style(GAGCore::Font::STYLE_NORMAL, 255, 255, 255));
		// Logical Arabic, existing presentation-form Arabic, and Persian. Latin
		// and digits must retain their own order inside the RTL paragraph.
		const std::string samples[] = {"التمرير Mac 50", "ﺍﻟﺘﻤﺮﻳﺮ Mac 50", "پیمایش Mac 50"};
		for (bool reopened : {false, true})
		{
			if (reopened) REQUIRE(font.reload());
			CHECK(TTF_GetFontDirection(font.font) == TTF_DIRECTION_LTR);
			for (float scale : {1.f, 2.f, 1.5f})
				for (const auto &logical : samples)
				{
					CAPTURE(logical);
					CAPTURE(scale);
					CAPTURE(reopened);
					const auto visual = GAGCore::visualOrder(logical);
					REQUIRE(visual.find("Mac 50") != std::string::npos);
					TTF_Font *reference = TTF_OpenFont(fontPath().c_str(), 20 * scale);
					GLOB2_REQUIRE(reference, SDL_GetError());
					REQUIRE(TTF_SetFontDirection(reference, TTF_DIRECTION_LTR));
					TTF_SetFontKerning(reference, TTF_GetFontKerning(font.font));
					SDL_Surface *expected = TTF_RenderText_Blended(reference, visual.c_str(), 0, {255,255,255,255});
					GLOB2_REQUIRE(expected, SDL_GetError());
					auto actual = font.rasterizeString(logical, scale);
					REQUIRE(actual);
					CHECK(samePixels(actual->getSDLSurface(), expected));
					// With HarfBuzz this is the broken behavior: taking already
					// visual-order text as RTL produces visibly different pixels.
					int major = 0;
					TTF_GetHarfBuzzVersion(&major, nullptr, nullptr);
					if (major > 0)
					{
						REQUIRE(TTF_SetFontDirection(reference, TTF_DIRECTION_RTL));
						SDL_Surface *twice = TTF_RenderText_Blended(reference, visual.c_str(), 0, {255,255,255,255});
						GLOB2_REQUIRE(twice, SDL_GetError());
						CHECK_FALSE(samePixels(twice, expected));
						SDL_DestroySurface(twice);
					}
					SDL_DestroySurface(expected);
					TTF_CloseFont(reference);
				}
			for (const auto &[size, raster] : font.rasterFonts)
				CHECK(TTF_GetFontDirection(raster) == TTF_DIRECTION_LTR);
		}
	}

	TEST_CASE("the bundled SDL3_ttf moves the pen for kerned pairs")
	{
		// Guards scons/vcpkg-ports/sdl3-ttf/kerning-moves-pen.patch in every build that
		// uses the pinned prefix or the vcpkg overlay.
		TtfScope ttf;
		TTF_Font *kerned = TTF_OpenFont(fontPath().c_str(), 26);
		TTF_Font *unkerned = TTF_OpenFont(fontPath().c_str(), 26);
		GLOB2_REQUIRE(kerned && unkerned, SDL_GetError());
		TTF_SetFontKerning(unkerned, false);
		int kerning = 0;
		REQUIRE(TTF_GetGlyphKerning(kerned, 'T', 'o', &kerning));
		REQUIRE_MESSAGE(kerning < 0, "the game's font should kern T and o");
		CHECK(TrueTypeFont::kerningMovesPen(kerned, unkerned));
		TTF_CloseFont(kerned);
		TTF_CloseFont(unkerned);
	}

	TEST_CASE("pair advances equal the glyph advances plus kerning")
	{
		TtfScope ttf;
		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::getFileManager()->addDir(glob2test::sourceRoot().string());
		const char *const pairs[] = {"To", "Tu", "Te", "Yo", "Wa"};
		for (unsigned size : SIZES)
		{
			TTF_Font *font = TrueTypeFont::openFont("data/fonts/sans.ttf", size);
			GLOB2_REQUIRE(font != nullptr, "could not open data/fonts/sans.ttf");
			for (const char *pair : pairs)
			{
				CAPTURE(size);
				CAPTURE(pair);
				int kerning = 0;
				if (TTF_GetFontKerning(font))
					REQUIRE(TTF_GetGlyphKerning(font, Uint32(pair[0]), Uint32(pair[1]), &kerning));
				int measured = 0;
				size_t length = 0;
				REQUIRE(TTF_MeasureString(font, pair, 0, 0, &measured, &length));
				const int expected = advance(font, Uint32(pair[0])) + kerning + advance(font, Uint32(pair[1]));
				// FreeType rounds the pen once per glyph, so allow one pixel.
				CHECK(std::abs(measured - expected) <= 1);
			}
			TTF_CloseFont(font);
		}
	}

	TEST_CASE("kerned words have no gap wider than the same words unkerned")
	{
		TtfScope ttf;
		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::getFileManager()->addDir(glob2test::sourceRoot().string());
		for (unsigned size : SIZES)
		{
			TTF_Font *font = TrueTypeFont::openFont("data/fonts/sans.ttf", size);
			TTF_Font *reference = TTF_OpenFont(fontPath().c_str(), float(size));
			GLOB2_REQUIRE(font && reference, SDL_GetError());
			TTF_SetFontKerning(reference, false);
			for (const char *word : WORDS)
			{
				CAPTURE(size);
				CAPTURE(word);
				// Before the fix "Tutorial" at 26 px had a 6 px gap after "Tu" against 2 px
				// unkerned; kerning only ever tightens a pair.
				CHECK(widestInnerGap(font, word) <= widestInnerGap(reference, word));
			}
			TTF_CloseFont(font);
			TTF_CloseFont(reference);
		}
	}
}
