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
	TEST_CASE("text size applies to desktop and touch independently of interface scale")
	{
		glob2test::HeadlessGlobals globals;
		Glob2UI::Presentation presentation;
		for (int percent : {100,125,150})
		{
			globals->settings.mobileDialogTextPercent = percent;
			presentation.touch = false;
			CHECK(Glob2UI::frontendTextScale(presentation) == doctest::Approx(percent / 100.0));
			presentation.touch = true;
			CHECK(Glob2UI::frontendTextScale(presentation) == doctest::Approx(1.15 * percent / 100.0));
		}
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
		std::filesystem::remove(path);
	}
}
