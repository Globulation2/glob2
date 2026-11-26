// SPDX-License-Identifier: GPL-3.0-or-later
// Portal mode on the screens (src/app/PortalPolicy.h): the main menu, Settings
// and the Discord panel offer only what the portal allows, and nothing changes
// without a portal.
#include "HeadlessGlobals.h"
#include "PortalPolicy.h"
#include "MainMenuScreen.h"
#include "SettingsScreen.h"
#include "ui/DiscordCommunity.h"

#include <algorithm>
#include <string>

namespace
{
// Installs a portal policy for one case and restores the previous one.
struct ScopedPortal
{
	PortalPolicy::Policy previous = PortalPolicy::current();
	explicit ScopedPortal(const char *description) { PortalPolicy::install(PortalPolicy::parse(description)); }
	~ScopedPortal() { PortalPolicy::install(previous); }
};

constexpr const char *NoLinksNoSignIn = R"({"name":"crazygames","externalLinks":false,"onlineSignIn":false})";
constexpr const char *SignInWithoutLinks = R"({"name":"test","externalLinks":false,"onlineSignIn":true})";
constexpr const char *Everything = R"({"name":"newgrounds","externalLinks":true,"onlineSignIn":true})";

Glob2UI::Presentation presentation(bool touch)
{
	Glob2UI::Presentation p;
	p.viewport = p.safe = p.dialog = touch ? GAGGUI::ui::Rect{0, 0, 400, 800} : GAGGUI::ui::Rect{0, 0, 1280, 800};
	p.touch = touch;
	p.hover = !touch;
	return p;
}

// The main menu's controls on a desktop and a phone layout ("More" included).
struct MenuKeys
{
	bool online = false, discord = false, campaign = false, tutorial = false, custom = false, quit = false;
};
MenuKeys menuKeys()
{
	MenuKeys keys;
	for (const bool touch : {false, true})
		for (const bool more : {false, true})
		{
			MainMenuScreen menu;
			if (more)
				menu.showMore(true);
			auto tree = menu.build(presentation(touch));
			REQUIRE(tree);
			auto has = [&](const std::string &key) { return tree->find(key) != nullptr; };
			keys.online |= has("menu/" + std::to_string(MainMenuScreen::PLAY_ONLINE));
			keys.discord |= has("menu/discord");
			keys.campaign |= has("menu/" + std::to_string(MainMenuScreen::CAMPAIGN));
			keys.tutorial |= has("menu/" + std::to_string(MainMenuScreen::TUTORIAL));
			keys.custom |= has("menu/" + std::to_string(MainMenuScreen::CUSTOM));
			keys.quit |= has("menu/" + std::to_string(MainMenuScreen::QUIT));
		}
	return keys;
}

bool showsCategory(SettingsScreen::Category category)
{
	SettingsScreen settings;
	const auto categories = settings.visibleCategories();
	return std::find(categories.begin(), categories.end(), category) != categories.end();
}
} // namespace

TEST_SUITE("PortalMode")
{
	TEST_CASE("without a portal the main menu keeps online play and Discord")
	{
		glob2test::HeadlessGlobals globals({.loadStrings = true});
		ScopedPortal portal("");
		const auto keys = menuKeys();
#if !defined(GLOB2_CHINA_RELEASE) && !defined(GLOB2_AMAZON_RELEASE)
		CHECK(keys.online);
#endif
		CHECK(keys.discord);
		CHECK(keys.custom);
		CHECK(keys.quit);
		CHECK(MainMenuScreen::offersQuit());
	}

	TEST_CASE("a portal without links or sign-in shows only offline play")
	{
		glob2test::HeadlessGlobals globals({.loadStrings = true});
		ScopedPortal portal(NoLinksNoSignIn);
		const auto keys = menuKeys();
		CHECK_FALSE(keys.online);
		CHECK_FALSE(keys.discord);
		CHECK(keys.custom);
		CHECK(keys.campaign);
		CHECK(keys.tutorial);
	}

	TEST_CASE("a portal never offers Quit, whatever it allows")
	{
		glob2test::HeadlessGlobals globals({.loadStrings = true});
		for (const char *description : {NoLinksNoSignIn, SignInWithoutLinks, Everything})
		{
			ScopedPortal portal(description);
			CHECK_FALSE(menuKeys().quit);
			CHECK_FALSE(MainMenuScreen::offersQuit());
		}
	}

	TEST_CASE("links and sign-in are switched one at a time")
	{
		glob2test::HeadlessGlobals globals({.loadStrings = true});
		{
			ScopedPortal portal(SignInWithoutLinks);
			const auto keys = menuKeys();
#if !defined(GLOB2_CHINA_RELEASE) && !defined(GLOB2_AMAZON_RELEASE)
			CHECK(keys.online);
#endif
			CHECK_FALSE(keys.discord);
		}
		{
			ScopedPortal portal(Everything);
			const auto keys = menuKeys();
#if !defined(GLOB2_CHINA_RELEASE) && !defined(GLOB2_AMAZON_RELEASE)
			CHECK(keys.online);
#endif
			CHECK(keys.discord);
		}
	}

	TEST_CASE("Settings drops the online account pages without sign-in")
	{
		glob2test::HeadlessGlobals globals({.loadStrings = true});
#if !defined(GLOB2_CHINA_RELEASE) && !defined(GLOB2_AMAZON_RELEASE)
		{
			ScopedPortal portal("");
			CHECK(showsCategory(SettingsScreen::Category::Online));
		}
		{
			ScopedPortal portal(SignInWithoutLinks);
			CHECK(showsCategory(SettingsScreen::Category::Online));
		}
#endif
		ScopedPortal portal(NoLinksNoSignIn);
		CHECK_FALSE(showsCategory(SettingsScreen::Category::Online));
		CHECK_FALSE(showsCategory(SettingsScreen::Category::HiveMind));
		CHECK(showsCategory(SettingsScreen::Category::Display));
		// Selecting a hidden page leaves Settings where it was.
		SettingsScreen settings;
		settings.selectCategory(SettingsScreen::Category::Online);
		CHECK(settings.category() != SettingsScreen::Category::Online);
	}

	TEST_CASE("the Discord panel does not open without external links")
	{
		glob2test::HeadlessGlobals globals({.loadStrings = true});
		{
			ScopedPortal portal(NoLinksNoSignIn);
			Glob2UI::DiscordCommunity community;
			community.open();
			CHECK_FALSE(community.visible);
		}
		Glob2UI::DiscordCommunity community;
		community.open();
		CHECK(community.visible);
	}
}
