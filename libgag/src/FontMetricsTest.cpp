// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <TrueTypeFont.h>

using namespace GAGCore;

TEST_CASE("font metrics do not rasterize or evict rendered text" *
		  doctest::test_suite("TextMetrics"))
{
	glob2test::ToolkitScope toolkit;
	struct FontProbe : TrueTypeFont
	{
		FontProbe() : TrueTypeFont((glob2test::sourceRoot() / "data/fonts/sans.ttf").string(), 13)
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
	REQUIRE(font.load((glob2test::sourceRoot() / "data/fonts/sans.ttf").string(), 20));
	CHECK(font.metricsCache.empty());
}
