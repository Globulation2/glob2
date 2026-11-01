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
#include <StringTable.h>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using GAGCore::TrueTypeFont;

namespace
{
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
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
	TEST_CASE("script run composition preserves straight-alpha antialiasing")
	{
		TtfScope ttf;
		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::getFileManager()->addDir(glob2test::sourceRoot().string());
		TrueTypeFont font("data/fonts/sans.ttf", 20);
		font.setStyle(GAGCore::Font::Style(GAGCore::Font::STYLE_NORMAL, 173, 211, 239));
		auto raster = font.rasterizeString("क्षत्रिय Glob2", 1.f);
		REQUIRE(raster);
		Surface rgba(SDL_ConvertSurface(raster->getSDLSurface(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
		REQUIRE(rgba);
		bool antialiased = false;
		for (int y = 0; y < rgba->h; ++y)
			for (int x = 0; x < rgba->w; ++x)
			{
				const auto *pixel = static_cast<const Uint8 *>(rgba->pixels) + y * rgba->pitch + x * 4;
				if (pixel[3] > 0 && pixel[3] < 255)
				{
					antialiased = true;
					CHECK(pixel[0] == 173); CHECK(pixel[1] == 211); CHECK(pixel[2] == 239);
				}
			}
		CHECK(antialiased);
	}

	TEST_CASE("shaping clusters keep conjuncts and combining marks together")
	{
		TtfScope ttf;
		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::getFileManager()->addDir(glob2test::sourceRoot().string());
		TrueTypeFont font("data/fonts/sans.ttf", 20);
		struct Sample { const char *text; std::vector<size_t> ends; };
		const Sample samples[] = {
			{"क्षिX", {12, 13}}, {"क्षत्रिX", {9, 21, 22}},
			{"က္ကX", {9, 10}}, {"ก้X", {6, 7}}, {"o\u0323\u0301X", {5, 6}},
		};
		for (const auto &sample : samples)
		{
			CAPTURE(sample.text);
			const auto ends = font.getStringClusterEnds(sample.text);
			CHECK(ends == sample.ends);
		}
		CHECK(font.getStringClusterEnds("").empty());
	}

	TEST_CASE("language changes refresh default line height and Arabic shaping caches")
	{
		TtfScope ttf;
		glob2test::ToolkitScope toolkit;
		glob2test::TempDir files("script-language");
		GAGCore::Toolkit::getFileManager()->addDir(glob2test::sourceRoot().string());
		GAGCore::Toolkit::getFileManager()->addDir(files.path.string());
		std::ofstream(files.path / "probe.keys.txt") << "[language-code]\n";
		std::ofstream listing(files.path / "probe.list.txt");
		listing << "probe.keys.txt\n";
		for (const char *code : {"en", "hi", "ur", "sw", "fil", "nb", "gu", "pa-guru", "ar", "fa"})
		{
			listing << "probe." << code << ".txt\n";
			std::ofstream(files.path / (std::string("probe.") + code + ".txt")) << "[language-code]\n" << code << '\n';
		}
		listing.close();
		auto *strings = GAGCore::Toolkit::getStringTable();
		REQUIRE(strings->load("probe.list.txt"));
		for (unsigned size : {10u, 13u, 20u})
		{
			TrueTypeFont font("data/fonts/sans.ttf", size);
			const int originalHeight = font.getStringHeight("");
			auto original = font.rasterizeString("Menu", 1.f);
			REQUIRE(original);
			for (const char *code : {"hi", "ur", "sw", "fil", "nb", "gu", "pa-guru"})
			{
				strings->setLang(strings->getLangCode(code));
				const int lineHeight = font.getStringHeight("");
				CHECK(lineHeight >= originalHeight);
				CHECK(font.getStringHeight("Menu") == lineHeight);
				auto menu = font.rasterizeString("Menu", 1.f);
				REQUIRE(menu);
				CHECK(menu->getH() == lineHeight);
				CAPTURE(code); CAPTURE(size);
				const char *sample = std::string(code) == "hi" ? "क्षत्रिय खिलाड़ी" :
					std::string(code) == "gu" ? "ગુજરાતી ખેલાડી" :
					std::string(code) == "pa-guru" ? "ਪੰਜਾਬੀ ਖਿਡਾਰੀ" :
					std::string(code) == "nb" ? "Det gjennomsnittlige angrepsnivået til koloniens krigere, der angrepshastighet og angrepsstyrke teller likt, på skalaen fra 1" :
					std::string(code) == "fil" ? "Nagtatayo at nagsasanay habang pinapanatili ang paglago na nakatali sa" :
					std::string(code) == "sw" ? "Nina haki ya kushiriki rekodi hizi na michoro hii kwa leseni iliyochaguliwa." : "کھلاڑی 12 / 100";
				CHECK(font.getStringHeight(sample) <= lineHeight);
				// Include a Latin-only label in a script locale: it uses the same
				// composed layout and must obey the authored logical advance too.
				for (const char *text : {sample, "Menu", "V: Onyesha maeneo yanayoonekana"})
					for (float scale : {1.f, 1.1f, 1.5f, 2.f, 3.f})
					{
						const int width = font.getStringWidth(text);
						const auto drawn = font.getStringDrawSize(text, scale);
						CHECK(drawn.first == doctest::Approx(width));
						const auto axes = font.getStringDrawSize(text, scale, 2.1f, 2.f);
						CHECK(axes.first * 2.1f == doctest::Approx(width * 2.f));
						CHECK(axes.second * 2.f == doctest::Approx(drawn.second * 2.f));
						const auto otherAxes = font.getStringDrawSize(text, scale, 2.f, 2.1f);
						CHECK(otherAxes.first == doctest::Approx(width));
						CHECK(otherAxes.second * 2.1f == doctest::Approx(drawn.second * 2.f));
					}
				REQUIRE(font.reload());
				CHECK(font.getStringHeight("") == lineHeight);
			}
			for (const char *code : {"en", "ar", "fa"})
			{
				strings->setLang(strings->getLangCode(code));
				CHECK(font.getStringHeight("") == originalHeight);
				auto menu = font.rasterizeString("Menu", 1.f);
				REQUIRE(menu);
				CHECK(samePixels(original->getSDLSurface(), menu->getSDLSurface()));
				for (float scale : {1.1f, 2.f})
				{
					auto raw = font.rasterizeString("Menu", scale);
					REQUIRE(raw);
					CHECK(font.getStringDrawSize("Menu", scale).first == doctest::Approx(raw->getW() / scale));
				}
			}
		}
	}

	TEST_CASE("high-DPI Hindi wrapping width is preserved without cropping the raw raster")
	{
		TtfScope ttf;
		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::getFileManager()->addDir(glob2test::sourceRoot().string());
		TrueTypeFont font("data/fonts/sans.ttf", 13);
		// A real 390px production preview line. Its pinned hinted raster at 26px
		// was 754px wide even though its authored advance is only 362px.
		const std::string text = "माउस के विंडो के किनारे पर पहुँचने पर खेल और संपादक के नक्शों को खिसकाएँ।";
		const int width = font.getStringWidth(text);
		REQUIRE(width <= 366);
		auto raw = font.rasterizeString(text, 2.f);
		REQUIRE(raw);
		TrueTypeFont physical("data/fonts/sans.ttf", 26);
		auto reference = physical.rasterizeString(text, 1.f);
		REQUIRE(reference);
		CHECK(samePixels(raw->getSDLSurface(), reference->getSDLSurface()));
		const auto drawn = font.getStringDrawSize(text, 2.f);
		CHECK(drawn.first == doctest::Approx(width));
		CHECK(drawn.first * 2.f <= 732.f);
		CHECK(font.getStringWidth(text) == width);
		// Querying destination geometry does not crop or replace the cached raster.
		auto unchanged = font.rasterizeString(text, 2.f);
		REQUIRE(unchanged);
		CHECK(samePixels(raw->getSDLSurface(), unchanged->getSDLSurface()));
	}

	TEST_CASE("script fonts shape clusters and preserve measured raster sizes after reload")
	{
		TtfScope ttf;
		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::getFileManager()->addDir(glob2test::sourceRoot().string());
		const char *samples[] = {"क्षत्रिय खिलाड़ी", "বাংলা শ্রমিক", "ગુજરાતી ખેલાડી", "ਪੰਜਾਬੀ ਖਿਡਾਰੀ",
			"தமிழ் வீரர்", "తెలుగు క్రీడాకారుడు", "ಕನ್ನಡ ಆಟಗಾರ", "മലയാളം കളിക്കാരൻ", "ଓଡ଼ିଆ ଖେଳାଳି",
			"ไทย ผู้เล่น", "မြန်မာ ကစားသမား", "አማርኛ ተጫዋች", "کھلاڑی 12 / 100 Glob2", "پښتو لوبغاړی", "سنڌي رانديگر"};
		for (unsigned size : {10u, 13u, 20u})
		{
			TrueTypeFont font("data/fonts/sans.ttf", size);
			for (bool reloaded : {false, true})
			{
				if (reloaded) REQUIRE(font.reload());
				for (const auto *sample : samples)
				{
					CAPTURE(sample); CAPTURE(size); CAPTURE(reloaded);
					CHECK(font.hasGlyphsFor(sample));
					const int width = font.getStringWidth(sample), height = font.getStringHeight(sample);
					REQUIRE(width > 0); REQUIRE(height > 0);
					auto raster = font.rasterizeString(sample, 1.f);
					REQUIRE(raster);
					CHECK(raster->getW() == width); CHECK(raster->getH() == height);
					auto scaled = font.rasterizeString(sample, 2.f);
					REQUIRE(scaled);
					CHECK(font.getStringWidth(sample) == width); CHECK(font.getStringHeight(sample) == height);
				}
			}
		}
	}

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
