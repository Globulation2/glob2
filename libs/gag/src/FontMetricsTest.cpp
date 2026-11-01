// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <TrueTypeFont.h>
#include <cmath>
#include <cstring>

using namespace GAGCore;

TEST_CASE("offscreen transformed text rasterizes at the requested resolution" *
		  doctest::test_suite("TextMetrics"))
{
	glob2test::ToolkitScope toolkit;
	struct TtfScope
	{
		TtfScope() { REQUIRE(TTF_Init()); }
		~TtfScope() { TTF_Quit(); }
	} ttf;
	const auto path = (glob2test::contentRoot() / "data/fonts/sans.ttf").string();
	TrueTypeFont font(path, 13);
	font.setStyle(Font::Style(Font::STYLE_NORMAL, 255, 239, 204));
	const std::string text = "Cancel Upgrade · مرحبا";
	const int width = font.getStringWidth(text), height = font.getStringHeight(text);
	for (const float scale : {1.f, 2.f, 3.f, 1.5f, 3.f})
	{
		auto bitmap = font.rasterizeString(text, scale);
		REQUIRE(bitmap);
		// Compare against an independently opened font at the physical size,
		// including shaping and every pixel, rather than an enlarged base bitmap.
		TrueTypeFont reference(path, unsigned(std::lround(13 * scale)));
		reference.setStyle(font.getStyle());
		auto expected = reference.rasterizeString(text, 1);
		REQUIRE(expected);
		CHECK(bitmap->getW() == expected->getW());
		CHECK(bitmap->getH() == expected->getH());
		// Existing Latin/legacy Arabic retains its raw physical raster geometry,
		// including fractional-size rounding. Only new script layout normalizes X.
		const auto drawn = font.getStringDrawSize(text, scale);
		CHECK(drawn.first == doctest::Approx(float(expected->getW()) / scale));
		CHECK(drawn.second == doctest::Approx(float(expected->getH()) / scale));
		const auto *actualPixels = bitmap->getSDLSurface();
		const auto *expectedPixels = expected->getSDLSurface();
		REQUIRE(actualPixels->format == expectedPixels->format);
		REQUIRE(actualPixels->w == expectedPixels->w);
		REQUIRE(actualPixels->h == expectedPixels->h);
		for (int y = 0; y < expectedPixels->h; ++y)
			CHECK(std::memcmp(static_cast<const Uint8 *>(actualPixels->pixels) + y * actualPixels->pitch,
				static_cast<const Uint8 *>(expectedPixels->pixels) + y * expectedPixels->pitch,
				expectedPixels->w * SDL_BYTESPERPIXEL(expectedPixels->format)) == 0);
		CHECK(font.getStringWidth(text) == width);
		CHECK(font.getStringHeight(text) == height);
	}
}

TEST_CASE("font metrics do not rasterize or evict rendered text" *
		  doctest::test_suite("TextMetrics"))
{
	glob2test::ToolkitScope toolkit;
	struct FontProbe : TrueTypeFont
	{
		FontProbe() : TrueTypeFont((glob2test::contentRoot() / "data/fonts/sans.ttf").string(), 13)
		{
		}
		using TrueTypeFont::getStringCached;
		using TrueTypeFont::metricsCache;
		using TrueTypeFont::metricsTextBytes;
		unsigned misses() const { return cacheMiss; }
		SDL_Surface *render(const std::string &text)
		{
			return TTF_RenderText_Blended(font, shapeText(text).c_str(), 0, {255, 255, 255, 255});
		}
	};
	struct TtfScope
	{
		TtfScope() { REQUIRE(TTF_Init()); }
		~TtfScope() { TTF_Quit(); }
	} ttf;
	FontProbe font;
	const auto *warm = font.getStringCached("Workers 123", false);
	REQUIRE(warm);
	const auto misses = font.misses();
	for (int i = 0; i < 5000; ++i)
	{
		CHECK(font.getStringWidth("Telemetry field " + std::to_string(i)) > 0);
		CHECK(font.getStringHeight("Telemetry field " + std::to_string(i)) > 0);
	}
	CHECK(font.misses() == misses);
	CHECK(font.metricsCache.size() == 1024);
	CHECK(font.metricsCache.count({"Telemetry field 0", Font::STYLE_NORMAL}) == 0);
	font.getStringWidth("Telemetry field 3976");
	font.getStringWidth("Telemetry field 5000");
	CHECK(font.metricsCache.count({"Telemetry field 3976", Font::STYLE_NORMAL}) == 1);
	CHECK(font.metricsCache.count({"Telemetry field 3977", Font::STYLE_NORMAL}) == 0);
	CHECK(font.getStringCached("Workers 123", false) == warm);
	CHECK(font.misses() == misses);
	REQUIRE(font.reload());
	for (int shape = 0; shape < 8; ++shape)
	{
		font.setStyle(Font::Style(Font::Shape(shape), 255, 255, 255));
		for (const auto *text :
			 {"Workers 123", "ToTo jÁ", "na · Updated at tick 128", "مرحبا", "a\nb"})
		{
			auto *surface = font.render(text);
			REQUIRE(surface);
			CHECK(font.getStringWidth(text) == surface->w);
			CHECK(font.getStringHeight(text) == surface->h);
			SDL_DestroySurface(surface);
		}
		const auto size = font.metricsCache.size();
		font.setStyle(Font::Style(Font::Shape(shape), 0, 0, 0));
		font.getStringWidth("Workers 123");
		CHECK(font.metricsCache.size() == size);
	}
	CHECK(font.getStringWidth("") == 0);
	CHECK(font.getStringHeight("") > 0);
	for (int i = 0; i < 40; ++i)
		font.getStringWidth(std::string(30000, 'a') + std::to_string(i));
	CHECK(font.metricsTextBytes <= 1024 * 1024);
	REQUIRE(font.reload());
	CHECK(font.metricsCache.empty());
	CHECK(font.metricsTextBytes == 0);
	font.getStringWidth("Workers 123");
	CHECK_FALSE(font.load("missing-font.ttf", 20));
	CHECK(font.metricsCache.size() == 1);
	REQUIRE(font.load((glob2test::contentRoot() / "data/fonts/sans.ttf").string(), 20));
	CHECK(font.metricsCache.empty());
}
