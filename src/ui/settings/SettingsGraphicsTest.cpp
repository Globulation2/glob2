// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Settings.h"
#include "ui/FrontendUI.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <fstream>
#include <array>

namespace
{
const std::array<bool Settings::*, 8> effects = {
	&Settings::clouds, &Settings::cloudShadows, &Settings::buildingParticles,
	&Settings::fullMagicEffects, &Settings::translucentPanels, &Settings::translucentPathLines,
	&Settings::smoothProgressIndicators, &Settings::decorativeAnimations};
}
TEST_SUITE("SettingsGraphics")
{
    TEST_CASE("render ceiling defaults migrates validates and round trips")
    {
        glob2test::HeadlessGlobals globals;
        const std::string file = "render-fps-test.txt";
        const auto path = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / file;
        Settings settings;
        CHECK(settings.targetRenderFps == 60);
        for (int fps : Settings::RENDER_FPS_PRESETS) {
            settings.targetRenderFps = fps;
            settings.setGraphicsDetail(false);
            CHECK(settings.targetRenderFps == fps);
            REQUIRE(settings.save(file));
            Settings loaded; loaded.load(file);
            CHECK(loaded.targetRenderFps == fps);
        }
        for (const char *value : {"", "garbage", "60oops", "-1", "26", "9999999999999999999"}) {
            { std::ofstream out(path); out << "targetRenderFps=" << value << '\n'; }
            settings.targetRenderFps = 0;
            settings.load(file);
            CHECK(settings.targetRenderFps == 60);
        }
        { std::ofstream out(path); out << "optionFlags=129\n"; }
        settings.targetRenderFps = 0;
        settings.load(file);
        CHECK(settings.targetRenderFps == 60);
        std::filesystem::remove(path);
    }

    TEST_CASE("edge scrolling defaults migrate and independent modes round trip")
    {
        glob2test::HeadlessGlobals globals;
        const std::string file = "edge-scroll-test.txt";
        const auto path = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / file;
        { std::ofstream out(path); out << "scrollWheelEnabled=1\n"; }
        Settings legacy; legacy.load(file);
        CHECK(legacy.edgeScrollingEnabled(true));
        CHECK_FALSE(legacy.edgeScrollingEnabled(false));
        for (bool fullscreen : {false, true})
            for (bool windowed : {false, true})
            {
                legacy.edgeScrollFullscreen = fullscreen;
                legacy.edgeScrollWindowed = windowed;
                REQUIRE(legacy.save(file));
                Settings loaded; loaded.load(file);
                CHECK(loaded.edgeScrollingEnabled(true) == fullscreen);
                CHECK(loaded.edgeScrollingEnabled(false) == windowed);
            }
        std::filesystem::remove(path);
    }
	TEST_CASE("text size applies to desktop and touch independently of interface scale")
	{
		glob2test::HeadlessGlobals globals;
		const double touchBase = Glob2UI::frontendTheme().touchTextScale;
		CHECK(touchBase == doctest::Approx(1.15));
		for (int percent : {100,125,150})
		{
			globals->settings.setTextSizePercent(percent);
			// Desktop: authored pixels times the preference, whatever the scale.
			auto desktop = Glob2UI::Presentation::forSurface(800, 600, 1.5, false);
			Glob2UI::applyTextSize(desktop, touchBase);
			CHECK(desktop.textUnit == doctest::Approx(percent / 100.0));
			// Touch: points, so the same physical size on every logical surface.
			for (double unit : {1.0, 1.54, 2.05})
			{
				auto touch = Glob2UI::Presentation::forSurface(800, 600, unit, true);
				Glob2UI::applyTextSize(touch, touchBase);
				CHECK(touch.textUnit == doctest::Approx(unit * 1.15 * percent / 100.0));
				CHECK(touch.textPt(100) == touch.pt(100 * percent / 100.0));
			}
		}
		globals->settings.setTextSizePercent(100);
	}
	TEST_CASE("legacy preferences migrate and independent effects round trip")
	{
		glob2test::HeadlessGlobals globals;
		const std::string file = "graphics-test.txt";
		const auto path = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / file;
		for (bool reduced : {false, true})
		{
			{ std::ofstream out(path); out << "optionFlags=" << (128 + reduced) << '\n'; }
			Settings s; s.load(file);
			for (auto field : effects) CHECK(s.*field == !reduced);
			CHECK(s.optionFlags == 128);
			CHECK(s.showColonySkins);
			CHECK(s.save(file));
			Settings reloaded; reloaded.load(file);
			for (auto field : effects) CHECK(reloaded.*field == !reduced);
		}
		{ std::ofstream out(path); out << "optionFlags=129\nclouds=1\ntranslucentPanels=1\n"; }
		Settings mixed; mixed.load(file);
		CHECK(mixed.clouds); CHECK(!mixed.cloudShadows); CHECK(mixed.translucentPanels);
		CHECK(mixed.optionFlags == 128);
		mixed.setGraphicsDetail(true);
		for (auto field : effects) CHECK(mixed.*field);
		mixed.setGraphicsDetail(false);
		for (auto field : effects) CHECK(!(mixed.*field));
		CHECK(mixed.optionFlags == 128);
		for (size_t i = 0; i < effects.size(); ++i)
		{
			mixed.setGraphicsDetail(false); mixed.*effects[i] = true;
			REQUIRE(mixed.save(file));
			Settings s; s.load(file);
			for (size_t j = 0; j < effects.size(); ++j) CHECK(s.*effects[j] == (i == j));
		}
		{ std::ofstream out(path); out << "clouds=0\n"; }
		Settings defaults; defaults.load(file);
		CHECK(!defaults.clouds);
#ifndef __EMSCRIPTEN__
		CHECK(defaults.cloudShadows); CHECK(defaults.fullMagicEffects);
#endif
		defaults.showColonySkins = false;
        defaults.setGraphicsDetail(true);
        CHECK_FALSE(defaults.showColonySkins);
        REQUIRE(defaults.save(file));
        Settings hidden; hidden.load(file);
        CHECK_FALSE(hidden.showColonySkins);
		std::filesystem::remove(path);
	}
}
