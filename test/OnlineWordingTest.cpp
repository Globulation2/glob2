// SPDX-License-Identifier: GPL-3.0-or-later
// The online screens speak to players, not to developers: queue names instead of
// queue ids, no ratings in unrated queues, positive rules, no seeds, no raw pairing
// strings and plurals that agree ("1 player"). UX review round 2: H-2, H-3, H-6, P-5,
// R-8, X-4, X-7, L-1.
#include "EngineFixtures.h"
#include "FrontendTheme.h"
#include "GlobalContainer.h"
#include "PlatformRoom.h"
#include "RoomScreen.h"
#include "UIRecordingCanvas.h"
#include "test/OnlineUIFixtures.h"
#include "ui/OnlineUI.h"
#include <ScreenStack.h>
#include <ui/Canvas.h>
#include <ui/Presentation.h>
#include <ui/Screen.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace GAGGUI::ui;

namespace
{
struct Strings
{
	glob2test::HeadlessGlobals globals{glob2test::GlobalsOptions{.loadStrings = true}};
};

struct Display
{
	glob2test::HeadlessGlobals globals{glob2test::GlobalsOptions{
		.display = true, .loadStrings = true, .width = 1280, .height = 800,
		.screenFlags = GAGCore::GraphicContext::PORTABLEGPU | GAGCore::GraphicContext::RESIZABLE}};
};

// Every line of text a screen paints after two frames.
std::vector<std::string> paintedTexts(std::unique_ptr<GAGGUI::Screen> owned,
									  const std::function<void(GAGGUI::Screen &)> &adjust = {})
{
	GAGGUI::ScreenStack stack(*globalContainer->gfx);
	auto *screen = dynamic_cast<GAGGUI::ui::UIScreen *>(owned.get());
	REQUIRE(screen != nullptr);
	stack.push(std::move(owned));
	Uint32 tick = SDL_GetTicks();
	stack.frame(tick, {});
	if (adjust)
		adjust(*screen);
	stack.frame(tick + 40, {});
	stack.frame(tick + 80, {});
	auto &host = screen->host();
	const auto &p = host.presentation();
	ToolkitTextMeasurer measurer(screen->theme(), p.touch, p.textUnit);
	glob2test::RecordingCanvas canvas(p.viewport.size(), measurer);
	host.paint(canvas, 0);
	std::vector<std::string> texts;
	for (const auto &[at, text] : canvas.texts)
		texts.push_back(text);
	return texts;
}

bool anyContains(const std::vector<std::string> &texts, const std::string &needle)
{
	return std::any_of(texts.begin(), texts.end(), [&](const std::string &t) { return t.find(needle) != std::string::npos; });
}

std::string joined(const std::vector<std::string> &texts)
{
	std::string all;
	for (const auto &t : texts)
		all += t + " | ";
	return all;
}
} // namespace

TEST_SUITE("OnlineWording")
{
	TEST_CASE("queue names, durations and estimates read the same everywhere")
	{
		Strings strings;
		CHECK(Glob2UI::queueDisplayName("casual-1v1", "Casual 1v1") == "Casual 1v1");
		// An older server without the name still never shows the raw id.
		CHECK(Glob2UI::queueDisplayName("casual-1v1", "") == "Casual 1v1");
		CHECK(Glob2UI::clockText(65) == "1:05");
		CHECK(Glob2UI::aboutText(20) == "under a minute");
		CHECK(Glob2UI::aboutText(100) == "about 2 min");
		CHECK(Glob2UI::durationText(42) == "42 s");
		CHECK(Glob2UI::durationText(12 * 60 + 10) == "12 min");
		// Short matches keep their seconds, so results and Recent matches agree.
		CHECK(Glob2UI::durationText(102) == "1 min 42 s");
		CHECK(Glob2UI::durationText(180) == "3 min");
		// Relay regions read as places, never as raw ids.
		CHECK(Glob2UI::regionDisplayName("ca-central") == "Canada, central");
		CHECK(Glob2UI::regionDisplayName("eu-west") == "Europe, west");
		CHECK(Glob2UI::regionDisplayName("home-lab") == "Home, lab");
	}

	TEST_CASE("an unrated search shows no ratings; a ranked one explains its range [display]")
	{
		Display display;
		const auto now = Glob2UI::wallClockMs();
		auto casual = OnlineUIFixtures::queues()[2];
		auto status = OnlineUIFixtures::status(now);
		status.queueId = casual.id;
		auto &unrated = OnlineUIFixtures::model(7);
		unrated.presentSearching(casual, status, now - 65000);
		GAGGUI::ScreenStack stack(*globalContainer->gfx);
		const auto texts = paintedTexts(std::make_unique<QuickMatchScreen>(stack, unrated, OnlineUIFixtures::queues(),
																			"https://app.glob2online.com", "Bradley"));
		INFO(joined(texts));
		CHECK_FALSE(anyContains(texts, "rated 1430"));
		CHECK_FALSE(anyContains(texts, "1601"));
		CHECK(anyContains(texts, "AI opponent: Cortex"));
		CHECK(anyContains(texts, casual.name));
		CHECK(anyContains(texts, "1:05"));
		CHECK(anyContains(texts, "usual wait under a minute"));
		CHECK_FALSE(anyContains(texts, "Fair 128"));

		auto &ranked = OnlineUIFixtures::model(8);
		ranked.presentSearching(OnlineUIFixtures::queues()[0], OnlineUIFixtures::status(now), now - 42000);
		const auto rankedTexts = paintedTexts(std::make_unique<QuickMatchScreen>(stack, ranked, OnlineUIFixtures::queues(),
																				  "https://app.glob2online.com", "Bradley"));
		INFO(joined(rankedTexts));
		CHECK(anyContains(rankedTexts, "Opponents rated 1430 to 1630"));
		CHECK(anyContains(rankedTexts, "1 vs 1 ranked"));
	}

	TEST_CASE("the room reads positively and hides seeds and pairing strings [display]")
	{
		Display display;
		GAGGUI::ScreenStack stack(*globalContainer->gfx);
		auto room = [&](int tab) {
			auto screen = std::make_unique<RoomScreen>(stack, Online::PlatformRoom::preview(OnlineUIFixtures::roomState(), OnlineUIFixtures::HOST_ID, OnlineUIFixtures::roomChat()));
			screen->selectTab(tab);
			return paintedTexts(std::move(screen));
		};
		const auto seats = room(RoomScreen::PlayersTab);
		INFO(joined(seats));
		CHECK_FALSE(anyContains(seats, " people"));
		CHECK((anyContains(seats, "players ·") || anyContains(seats, "1 player ·")));

		const auto map = room(RoomScreen::MapTab);
		INFO(joined(map));
		CHECK_FALSE(anyContains(map, "Seed"));

		const auto rules = room(RoomScreen::RulesTab);
		INFO(joined(rules));
		for (const char *negative : {"No resource growth", "No hunger", "No permadeath", "No upgrades"})
			CHECK_FALSE(anyContains(rules, negative));
		CHECK(anyContains(rules, "Every other rule is as in Standard."));

		const auto lan = paintedTexts(std::make_unique<RoomScreen>(stack, std::make_shared<OnlineUIFixtures::LanRoomFixture>()));
		INFO(joined(lan));
		CHECK_FALSE(anyContains(lan, "ws://"));
		CHECK(anyContains(lan, "Link for browser players"));
	}

	TEST_CASE("menus read larger on big desktop windows that follow the desktop scale")
	{
		const auto at = [](int w, int h, bool touch = false) { return Presentation::forSurface(w, h, 1, touch); };
		CHECK(comfortScale(at(1920, 1080), true) == doctest::Approx(1.25));
		CHECK(comfortScale(at(2560, 1440), true) == doctest::Approx(1.5));
		CHECK(comfortScale(at(1280, 800), true) == doctest::Approx(1.0));
		// A chosen interface scale, and touch hosts, keep their sizes.
		CHECK(comfortScale(at(1920, 1080), false) == doctest::Approx(1.0));
		CHECK(comfortScale(at(1920, 1080, true), true) == doctest::Approx(1.0));
		auto p = at(1920, 1080);
		p.textUnit = 1;
		applyComfortScale(p, comfortScale(p, true));
		CHECK(p.pt(100) == 125);
		CHECK(p.textUnit == doctest::Approx(1.25));
	}

	TEST_CASE("a pairing link's fingerprint becomes a short code")
	{
		CHECK(Glob2UI::pairingCode("wss://192.168.1.5:7489/yog#sha256=ab12cd34ef") == "AB12 CD34");
		CHECK(Glob2UI::pairingCode("ws://192.168.1.20:7487 4F2A").empty());
	}
}
