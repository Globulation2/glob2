// SPDX-License-Identifier: GPL-3.0-or-later
// Device-adaptive preferences: Auto resolution, the migration of profiles that
// predate Auto, the explicit-value-wins rule, the session step-down and the
// settings and new-game screens that show and use the Auto values.
#include "HeadlessGlobals.h"
#include "CustomGamePreferences.h"
#include "CustomGameScreen.h"
#include "DeviceAdaptationSession.h"
#include "DeviceProfile.h"
#include "FrontendTheme.h"
#include "GlobalContainer.h"
#include "Settings.h"
#include "SettingsScreen.h"
#include "ScopedEnvironment.h"
#include "ui/FrontendUI.h"
#include <FileManager.h>
#include <ScreenStack.h>
#include <Toolkit.h>
#include <SDL3/SDL_events.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>

namespace
{
DeviceProfile device(std::uint64_t ramMiB, bool mobile = false)
{
	DeviceInputs inputs;
	inputs.systemRamMiB = ramMiB;
	inputs.cores = 8;
	inputs.mobile = mobile;
	return DeviceProfile::classify(inputs);
}

// Replaces the process profile for one scope.
struct ScopedDevice
{
	DeviceProfile previous;
	explicit ScopedDevice(DeviceProfile profile) : previous(DeviceProfile::replace(std::move(profile))) {}
	~ScopedDevice() { DeviceProfile::replace(std::move(previous)); }
};

std::filesystem::path profileFile(const std::string &name)
{
	return std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / name;
}

Settings loadText(const std::string &name, const std::string &text)
{
	{
		std::ofstream out(profileFile(name));
		out << text;
	}
	Settings settings;
	settings.load(name);
	return settings;
}

std::string readText(const std::string &name)
{
	std::ifstream in(profileFile(name));
	return {std::istreambuf_iterator<char>(in), {}};
}

using Choice = Settings::AdaptiveChoice;

struct NativeSettings : SettingsScreen
{
	NativeSettings() { beginExecution(globalContainer->gfx); }
	Row row(const std::string &id)
	{
		for (const auto &r : rows())
			if (r.id == id)
				return r;
		return {};
	}
};
} // namespace

TEST_SUITE("AdaptiveSettings")
{
	TEST_CASE("profiles from before Auto migrate; explicit values win over the device [writes-preferences]")
	{
		glob2test::HeadlessGlobals globals;
		const std::string name = "adaptive-preferences.txt";
		{
			ScopedDevice low(device(2048));
			// The old default was written unconditionally: 1 is not a choice.
			auto legacyOn = loadText(name, "highResolutionArtwork=1\n");
			CHECK(legacyOn.highResolutionArtworkChoice == Choice::Auto);
			CHECK_FALSE(legacyOn.highResolutionArtwork); // Auto on a low-tier device
			// 0 departed from the old default, so it stays an explicit Off.
			auto legacyOff = loadText(name, "highResolutionArtwork=0\n");
			CHECK(legacyOff.highResolutionArtworkChoice == Choice::Off);
			// No artwork key at all (or no file) is Auto.
			CHECK(loadText(name, "username=player\n").highResolutionArtworkChoice == Choice::Auto);
			// The new key wins over the legacy one; an unknown value falls back to it.
			auto explicitOn = loadText(name, "highResolutionArtworkChoice=on\nhighResolutionArtwork=1\n");
			CHECK(explicitOn.highResolutionArtworkChoice == Choice::On);
			CHECK(explicitOn.highResolutionArtwork); // never overridden on a low tier
			CHECK(loadText(name, "highResolutionArtworkChoice=maybe\nhighResolutionArtwork=0\n")
					  .highResolutionArtworkChoice == Choice::Off);
		}
		{
			ScopedDevice high(device(16384));
			auto legacyOn = loadText(name, "highResolutionArtwork=1\n");
			CHECK(legacyOn.highResolutionArtwork); // high tier: unchanged for existing players
			auto explicitOff = loadText(name, "highResolutionArtworkChoice=off\n");
			CHECK_FALSE(explicitOff.highResolutionArtwork);
		}
		// Saving writes the choice, and a legacy value older builds read as before.
		ScopedDevice low(device(2048));
		for (const auto [choice, text, legacy] : {std::tuple{Choice::Auto, "auto", 1}, std::tuple{Choice::Off, "off", 0},
												  std::tuple{Choice::On, "on", 1}})
		{
			Settings settings;
			settings.setHighResolutionArtworkChoice(choice);
			REQUIRE(settings.save(name));
			const auto saved = readText(name);
			CHECK(saved.find(std::string("highResolutionArtworkChoice=") + text + "\n") != std::string::npos);
			CHECK(saved.find("highResolutionArtwork=" + std::to_string(legacy) + "\n") != std::string::npos);
			Settings loaded;
			loaded.load(name);
			CHECK(loaded.highResolutionArtworkChoice == choice);
		}
		// A round trip through an older build drops the new key: Auto comes back as Auto.
		CHECK(loadText(name, "highResolutionArtwork=1\n").highResolutionArtworkChoice == Choice::Auto);
		std::filesystem::remove(profileFile(name));
	}

	TEST_CASE("a step-down holds Auto off for the session and never touches an explicit choice")
	{
		glob2test::HeadlessGlobals globals;
		ScopedDevice high(device(16384));
		Settings settings;
		CHECK(settings.highResolutionArtwork);
		settings.highResolutionArtworkSteppedDown = true;
		settings.resolveAdaptiveDefaults();
		CHECK_FALSE(settings.highResolutionArtwork);
		settings.setHighResolutionArtworkChoice(Choice::On);
		CHECK(settings.highResolutionArtwork);
		settings.setHighResolutionArtworkChoice(Choice::Auto);
		CHECK_FALSE(settings.highResolutionArtwork);
	}

	TEST_CASE("settings show Auto's value and reason; the session steps down and offers to keep it [display][writes-preferences]")
	{
		glob2test::ScopedEnvironment desktop("GLOB2_MOBILE_UI", "0");
		glob2test::GlobalsOptions options{.display = true, .loadStrings = true, .width = 1000, .height = 700,
										  .screenFlags = Uint32(GAGCore::GraphicContext::USEGPU)};
		options.beforeLoad = [](GlobalContainer &globals) { globals.settings.language = "en"; };
		glob2test::HeadlessGlobals globals(options);
		auto &s = globalContainer->settings;
		const bool filtering = globalContainer->gfx->rendererCapabilities().textureFiltering;
		FrontendTheme theme;
		FrontendScope frontend;
		{
			ScopedDevice low(device(2048));
			s.setHighResolutionArtworkChoice(Choice::Auto);
			NativeSettings screen;
			screen.selectCategory(SettingsScreen::Category::Display);
			const auto row = screen.row("graphics.artwork");
			REQUIRE(row.kind == SettingsScreen::Kind::Choice);
			CHECK(row.choices.size() == 3);
			CHECK(row.number == 0);
			if (filtering)
				CHECK(row.help == "Automatic: original on this device (2 GB RAM). Applies on the next game or editor load.");
			screen.done();
		}
		if (filtering)
		{
			ScopedDevice high(device(16384));
			s.setHighResolutionArtworkChoice(Choice::Auto);
			REQUIRE(s.highResolutionArtwork);
			NativeSettings screen;
			screen.selectCategory(SettingsScreen::Category::Display);
			CHECK(screen.row("graphics.artwork").help ==
				  "Automatic: high resolution on this device (16 GB RAM). Applies on the next game or editor load.");
			// Explicit values show the plain help and never resolve against the device.
			REQUIRE(screen.changeSetting("graphics.artwork", 1));
			CHECK(s.highResolutionArtworkChoice == Choice::Off);
			CHECK(screen.row("graphics.artwork").help == "Apply artwork on the next game or editor load.");
			REQUIRE(screen.changeSetting("graphics.artwork", 0));
			CHECK(s.highResolutionArtwork);

			// One memory warning (iOS's SDL_EVENT_LOW_MEMORY) steps Auto down at once.
			SDL_Event warning{};
			warning.type = SDL_EVENT_LOW_MEMORY;
			DeviceAdaptation::serviceFrame({warning});
#ifndef __ANDROID__
			CHECK(DeviceAdaptation::session().steppedDown());
			CHECK(s.highResolutionArtworkSteppedDown);
			CHECK_FALSE(s.highResolutionArtwork);
			CHECK(s.highResolutionArtworkChoice == Choice::Auto); // nothing saved yet
			screen.selectCategory(SettingsScreen::Category::Display);
			CHECK(screen.row("graphics.artwork").help ==
				  "Automatic: original for the rest of this session because the device ran low on memory.");
			// The offer: keep it as the default.
			REQUIRE(screen.row("graphics.artwork.keep").kind == SettingsScreen::Kind::Button);
			screen.activateSetting("graphics.artwork.keep");
			CHECK(s.highResolutionArtworkChoice == Choice::Off);
			screen.done();
			Settings saved;
			saved.load();
			CHECK(saved.highResolutionArtworkChoice == Choice::Off);
#endif
		}
		s.setHighResolutionArtworkChoice(Choice::Auto);
		REQUIRE(s.save());
	}

	TEST_CASE("a new custom game starts at the device's map size; a saved lobby keeps its own [display][writes-preferences]")
	{
		glob2test::GlobalsOptions options{.display = true, .loadStrings = true, .width = 1000, .height = 700,
										  .screenFlags = Uint32(GAGCore::GraphicContext::USEGPU)};
		glob2test::HeadlessGlobals globals(options);
		FrontendTheme theme;
		FrontendScope frontend;
		GAGGUI::ScreenStack stack(*globalContainer->gfx);
		GAGCore::Toolkit::getFileManager()->remove(CustomGamePreferences::filename);
		{
			ScopedDevice low(device(2048, true));
			CustomGameScreen screen(stack);
			CHECK(screen.setup.generator.wDec == 7);
			CHECK(screen.setup.generator.hDec == 7);
			// The player can still choose 256×256; Reset returns to the device default.
			screen.setup.generator.wDec = screen.setup.generator.hDec = 8;
			screen.resetParameters();
			CHECK(screen.setup.generator.wDec == 7);
		}
		// Each screen saves its lobby on the way out; start again without one.
		GAGCore::Toolkit::getFileManager()->remove(CustomGamePreferences::filename);
		{
			ScopedDevice mid(device(4096, false));
			CustomGameScreen screen(stack);
			CHECK(screen.setup.generator.wDec == 8); // desktop mid tier keeps 256
		}
		GAGCore::Toolkit::getFileManager()->remove(CustomGamePreferences::filename);
		{
			ScopedDevice mid(device(4096, true));
			CustomGameScreen screen(stack);
			CHECK(screen.setup.generator.wDec == 7); // phones from the mid tier down
		}
		// A saved lobby is the player's own setup: its size is not adapted.
		CustomGamePreferences saved;
		saved.setup.generator.wDec = saved.setup.generator.hDec = 8;
		REQUIRE(saved.save(*GAGCore::Toolkit::getFileManager()));
		{
			ScopedDevice low(device(2048, true));
			CustomGameScreen screen(stack);
			CHECK(screen.setup.generator.wDec == 8);
		}
		GAGCore::Toolkit::getFileManager()->remove(CustomGamePreferences::filename);
	}
}
