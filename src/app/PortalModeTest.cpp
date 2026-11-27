// SPDX-License-Identifier: GPL-3.0-or-later
// Portal mode on the screens (src/app/PortalPolicy.h): the main menu, Settings
// and the Discord panel offer only what the portal allows, and nothing changes
// without a portal.
#include "HeadlessGlobals.h"
#include "PortalPolicy.h"
#include "MainMenuScreen.h"
#include "SettingsScreen.h"
#include "ui/DiscordCommunity.h"
#include "ProblemReportScreen.h"
#include "GameGUIDialog.h"
#include <ApplicationHost.h>
#include <ui/Host.h>
#include <ui/TextLayout.h>

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

// The five ad portals: no links out of the game, no online sign-in.
constexpr const char *AdPortals[] = {
	R"({"name":"crazygames","externalLinks":false,"onlineSignIn":false,"ads":true})",
	R"({"name":"poki","externalLinks":false,"onlineSignIn":false,"ads":true})",
	R"({"name":"gamedistribution","externalLinks":false,"onlineSignIn":false,"ads":true})",
	R"({"name":"gamepix","externalLinks":false,"onlineSignIn":false,"ads":true})",
	R"({"name":"yandex","externalLinks":false,"onlineSignIn":false,"ads":true})",
};

// Every entry the in-game menu can show, the pinned Return last.
constexpr const char *InGameMenuKeys[] = {"save", "load", "telemetry", "options", "settings",
										  "pause", "intro/restart", "quit", "return"};

// Lays the in-game menu (Save to Restart lesson, every entry it can have) out
// headlessly at one surface size, as UIDialog does without a window.
struct InGameMenuAt
{
	GAGGUI::ui::FixedTextMeasurer measurer{8, 16};
	InGameMainScreen menu{false, true, false};
	explicit InGameMenuAt(GAGGUI::ui::Presentation p)
	{
		menu.setTutorial(true);
		auto &host = menu.host();
		host.setMeasurer(&measurer);
		host.setPresentation(p);
		host.layoutIfNeeded();
	}
	GAGGUI::ui::Host &host() { return menu.host(); }
	GAGGUI::ui::Rect list() { return host().find("menu/scroll")->bounds; }
	bool shown(const char *key)
	{
		const auto r = host().bounds(key), area = list();
		return r.y >= area.y && r.bottom() <= area.bottom() && r.x >= area.x && r.right() <= area.right();
	}
	void wheel(float steps)
	{
		const auto at = list().center();
		SDL_Event motion{};
		motion.type = SDL_EVENT_MOUSE_MOTION;
		motion.motion.x = float(at.x);
		motion.motion.y = float(at.y);
		menu.eventLogical(motion);
		SDL_Event wheel{};
		wheel.type = SDL_EVENT_MOUSE_WHEEL;
		wheel.wheel.y = steps;
		menu.eventLogical(wheel);
		host().layoutIfNeeded();
	}
	// Wheels down until the entry is fully in the list; false if it never gets there.
	bool reach(const char *key, float steps)
	{
		for (int i = 0; i < 400 && !shown(key); ++i)
			wheel(steps);
		return shown(key);
	}
};

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

	TEST_CASE("the in-game menu offers AI telemetry outside portals only")
	{
		glob2test::HeadlessGlobals globals({.loadStrings = true});
		const auto p = GAGGUI::ui::Presentation::forSurface(1280, 800);
		{
			ScopedPortal portal("");
			CHECK(InGameMenuAt(p).host().find("telemetry") != nullptr);
		}
		for (const char *description : {NoLinksNoSignIn, Everything})
		{
			ScopedPortal portal(description);
			InGameMenuAt menu(p);
			CHECK(menu.host().find("telemetry") == nullptr);
			CHECK(menu.host().find("quit") != nullptr);
		}
	}

	// A CrazyGames desktop frame is 910x470 CSS pixels; the browser lays it out
	// on a 1162x600 surface. Every entry must be on screen or wheel-reachable,
	// and Return always on screen, there, in the frame at one pixel per point,
	// on phones in both orientations and in a frame too short for the list.
	TEST_CASE("every in-game menu entry is visible or wheel-reachable in short frames")
	{
		glob2test::HeadlessGlobals globals({.loadStrings = true});
		ScopedPortal portal("");
		struct Frame
		{
			const char *name;
			int w, h;
			bool touch;
		};
		for (const auto frame : {Frame{"portal surface", 1162, 600, false}, Frame{"portal css", 910, 470, false},
								 Frame{"phone landscape", 844, 390, true}, Frame{"phone portrait", 390, 844, true},
								 Frame{"very short", 640, 300, false}})
			for (const float steps : {-1.0f, -0.04f})
			{
				INFO(frame.name << " wheel " << steps);
				InGameMenuAt menu(GAGGUI::ui::Presentation::forSurface(frame.w, frame.h, 1, frame.touch));
				const auto viewport = GAGGUI::ui::Rect{0, 0, frame.w, frame.h};
				const auto back = menu.host().bounds("return");
				CHECK(back.y >= 0);
				CHECK(back.bottom() <= viewport.h);
				CHECK(menu.list().bottom() <= back.y);
				for (const char *key : InGameMenuKeys)
				{
					if (std::string(key) == "return" || !menu.host().find(key))
						continue;
					INFO(key);
					CHECK(menu.reach(key, steps));
				}
			}
		// Short desktop frames lay the list out in two columns, so the portal
		// frame needs no scrolling; the very short frame still scrolls.
		CHECK(InGameMainScreen::compactLayout(GAGGUI::ui::Presentation::forSurface(1162, 600)));
		CHECK_FALSE(InGameMainScreen::compactLayout(GAGGUI::ui::Presentation::forSurface(1280, 800)));
		CHECK(InGameMenuAt(GAGGUI::ui::Presentation::forSurface(1162, 600)).host().find("menu/scroll")->scrollMaximum() == 0);
		InGameMenuAt tight(GAGGUI::ui::Presentation::forSurface(640, 300));
		CHECK(tight.host().find("menu/scroll")->scrollMaximum() > 0);
		CHECK_FALSE(tight.shown("quit"));
		CHECK(tight.reach("quit", -1));
	}

	// Report a Problem stays inside the game in every ad portal: its screen
	// offers Save to file (a download) and no Send, and nothing on it opens a page.
	TEST_CASE("Report a problem stays in the game in portals without links or sign-in")
	{
		glob2test::HeadlessGlobals globals({.loadStrings = true});
		const std::string reportKey = "menu/" + std::to_string(MainMenuScreen::REPORT_PROBLEM);
		for (const char *description : AdPortals)
		{
			INFO(description);
			ScopedPortal portal(description);
			CHECK_FALSE(GAGCore::ApplicationHost::externalLinksAllowed());
			bool offered = false;
			for (const bool touch : {false, true})
				for (const bool more : {false, true})
				{
					MainMenuScreen menu;
					if (more)
						menu.showMore(true);
					auto tree = menu.build(presentation(touch));
					REQUIRE(tree);
					offered |= tree->find(reportKey) != nullptr;
				}
			CHECK(offered);
			for (const bool touch : {false, true})
			{
				ProblemReportScreen screen;
				auto tree = screen.build(presentation(touch));
				REQUIRE(tree);
				CHECK(tree->find("report.save-file") != nullptr);
				CHECK(tree->find("report.send") == nullptr);
				CHECK(tree->find("report.back") != nullptr);
			}
		}
	}
}
