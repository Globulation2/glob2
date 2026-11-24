// SPDX-License-Identifier: GPL-3.0-or-later
// The online screens speak to players, not to developers: queue names instead of
// queue ids, no ratings in unrated queues, positive rules, no seeds, no raw pairing
// strings and plurals that agree ("1 player"). UX review round 2: H-2, H-3, H-6, P-5,
// R-8, X-4, X-7, L-1.
#include "HeadlessGlobals.h"
#include "ProfileNeeds.h"
#include <StringTable.h>
#include <Toolkit.h>
#include "FrontendTheme.h"
#include "Engine.h"
#include "GameLoadScreen.h"
#include "GlobalContainer.h"
#include "LanRoom.h"
#include "PlatformRoom.h"
#include "RoomScreen.h"
#include "UIRecordingCanvas.h"
#include "OnlineUIFixtures.h"
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
	// Paragraphs can paint a phrase across multiple wrapped lines.
	std::string text;
	for (const auto &line : texts)
	{
		if (!text.empty()) text += ' ';
		text += line;
	}
	return text.find(needle) != std::string::npos;
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
	TEST_CASE("Bengali account avatar retains its complete first shaping cluster [display]")
	{
		Display display;
		GAGGUI::ScreenStack stack(*globalContainer->gfx);
		const auto texts = paintedTexts(OnlineUIFixtures::hubFixture(stack, [](OnlineHubScreen::Model &model) {
			model.displayName = "ক্ষিতিজ";
			model.accountKind = "registered";
		}));
		CHECK(std::find(texts.begin(), texts.end(), "ক্ষি") != texts.end());
		CHECK(std::find(texts.begin(), texts.end(), "ক্ষিতিজ") != texts.end());
	}
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

	TEST_CASE("generated map names in the online screens are translated" * glob2test::needs({"generators"}))
	{
		Strings strings;
		auto *table = GAGCore::Toolkit::getStringTable();
		const int english = table->getLang();
		CHECK(Glob2UI::generatorTitle("even-ground") == "Even Ground");
		REQUIRE(table->hasLanguage("de"));
		table->setLang(table->getLangCode("de"));
		const std::string german = Glob2UI::generatorTitle("even-ground");
		table->setLang(english);
		CHECK(german == table->getStringInLang("[Even Ground]", table->getLangCode("de")));
		CHECK(german != "Even Ground");
		// An id this build does not know still reads as words.
		CHECK(Glob2UI::generatorTitle("future-lands") == "Future Lands");
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
																			"https://app.glob2.online", "Bradley"));
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
																				  "https://app.glob2.online", "Bradley"));
		INFO(joined(rankedTexts));
		CHECK(anyContains(rankedTexts, "Opponents rated 1430 to 1630"));
		CHECK(anyContains(rankedTexts, "1 vs 1 ranked"));
	}

	TEST_CASE("the quick match screen lists only the queues of the client's kind [display]")
	{
		Display display;
		struct Restore
		{
			~Restore() { Online::setAssistedSource({}); }
		} restore;
		bool mcp = false;
		Online::setAssistedSource([&mcp] { return mcp; });
		auto queues = OnlineUIFixtures::queues();
		Online::QueueInfo assisted{"mcp-casual-1v1", "Agents 1 vs 1", "1v1", false, 30, 0, {"symmetric-arena"}};
		assisted.assisted = true;
		queues.push_back(assisted);
		GAGGUI::ScreenStack stack(*globalContainer->gfx);

		const auto plain = paintedTexts(std::make_unique<QuickMatchScreen>(stack, OnlineUIFixtures::model(9), queues,
																			"https://app.glob2.online", "Bradley"));
		INFO(joined(plain));
		CHECK(anyContains(plain, "1 vs 1 ranked"));
		CHECK_FALSE(anyContains(plain, "Agents 1 vs 1"));

		mcp = true;
		const auto withMcp = paintedTexts(std::make_unique<QuickMatchScreen>(stack, OnlineUIFixtures::model(10), queues,
																			  "https://app.glob2.online", "Bradley"));
		INFO(joined(withMcp));
		CHECK(anyContains(withMcp, "Agents 1 vs 1"));
		CHECK_FALSE(anyContains(withMcp, "1 vs 1 ranked"));
		CHECK_FALSE(anyContains(withMcp, "Casual 1 vs 1"));

		// Switching while the screen is open picks the other list on its next frame.
		mcp = false;
		const auto switched = paintedTexts(std::make_unique<QuickMatchScreen>(stack, OnlineUIFixtures::model(11), queues,
																			   "https://app.glob2.online", "Bradley"),
										   [&mcp](GAGGUI::Screen &) { mcp = true; });
		INFO(joined(switched));
		CHECK(anyContains(switched, "Agents 1 vs 1"));
		CHECK_FALSE(anyContains(switched, "1 vs 1 ranked"));
	}

	TEST_CASE("a client with MCP on a server without MCP queues is told why it cannot search [display]")
	{
		Display display;
		struct Restore
		{
			~Restore() { Online::setAssistedSource({}); }
		} restore;
		Online::setAssistedSource([] { return true; });
		GAGGUI::ScreenStack stack(*globalContainer->gfx);
		const auto search = paintedTexts(std::make_unique<QuickMatchScreen>(stack, OnlineUIFixtures::model(12),
																			 OnlineUIFixtures::queues(),
																			 "https://app.glob2.online", "Bradley"));
		INFO(joined(search));
		CHECK(anyContains(search, "no quick-match queues for players with the MCP server enabled"));
		CHECK_FALSE(anyContains(search, "1 vs 1 ranked"));

		const auto hub = paintedTexts(OnlineUIFixtures::hubFixture(stack, [](OnlineHubScreen::Model &model) {
			model.queues = Online::queuesFor(model.queues, true);
			model.assistedQueues = true;
		}));
		INFO(joined(hub));
		CHECK(anyContains(hub, "no quick-match queues for players with the MCP server enabled"));

		const auto plainHub = paintedTexts(OnlineUIFixtures::hubFixture(stack, [](OnlineHubScreen::Model &model) {
			model.queues = Online::Json::array();
		}));
		INFO(joined(plainHub));
		CHECK(anyContains(plainHub, "This server has no quick-match queues."));
		CHECK_FALSE(anyContains(plainHub, "MCP"));
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
		CHECK(anyContains(lan, "Pairing link"));
	}

	TEST_CASE("a LAN room tells players where to paste the host's pairing link [display]")
	{
		// The discovery list names hosts, not maps, and every guest (in the game or in a
		// browser) needs the pairing link: the room must not send them to a list on the
		// main menu, nor offer the link to browser players only.
		Display display;
		GAGGUI::ScreenStack stack(*globalContainer->gfx);
		const auto lan = paintedTexts(std::make_unique<RoomScreen>(stack, std::make_shared<OnlineUIFixtures::LanRoomFixture>()));
		INFO(joined(lan));
		CHECK(anyContains(lan, "Join a game"));
		CHECK(anyContains(lan, "pairing link"));
		CHECK_FALSE(anyContains(lan, "on the main menu"));
		CHECK_FALSE(anyContains(lan, "browser players"));
	}

	TEST_CASE("a room whose game is ready as it opens shows the game, not the room [display]")
	{
		// A guest rejoining a started LAN game opens the room with its launch already
		// queued. Handled in the room's constructor, the game's load screen went onto
		// the stack before the room itself, so the room covered it and the game never
		// loaded.
		Display display;
		Lan::LanHost::Options options;
		options.hostName = "Host";
		options.map = Engine().loadMapHeader("maps/balanced.map");
		options.network = false;
		auto room = Lan::LanRoom::host(std::move(options));
		room->update();
		REQUIRE(room->canStart());
		room->start(); // queues the launch
		GAGGUI::ScreenStack stack(*globalContainer->gfx);
		stack.push(std::make_unique<RoomScreen>(stack, room));
		const Uint32 tick = SDL_GetTicks();
		for (Uint32 frame = 0; frame < 3; ++frame)
			stack.frame(tick + frame * 40, {});
		CHECK(dynamic_cast<GameLoadScreen *>(stack.top()) != nullptr);
		CHECK(dynamic_cast<RoomScreen *>(stack.top()) == nullptr);
	}

	TEST_CASE("a LAN host puts AIs in open seats from the room [display]")
	{
		Display display;
		Lan::LanHost::Options options;
		options.hostName = "Host";
		options.map = Engine().loadMapHeader("maps/balanced.map");
		options.network = false;
		auto room = Lan::LanRoom::host(std::move(options));
		room->update();
		GAGGUI::ScreenStack stack(*globalContainer->gfx);
		auto owned = std::make_unique<RoomScreen>(stack, room);
		auto *screen = owned.get();
		stack.push(std::move(owned));
		const Uint32 tick = SDL_GetTicks();
		stack.frame(tick, {});
		stack.frame(tick + 40, {});
		REQUIRE(room->slots().size() >= 2);
		const RoomBackend::Slot open = room->slots()[1];
		REQUIRE(open.open);
		// The open seat offers its occupants; the host's own seat does not.
		CHECK(room->canSetOccupant(open));
		CHECK(screen->host().find("seat/1/controller") != nullptr);
		CHECK_FALSE(room->canSetOccupant(room->slots()[0]));

		room->setOccupant(1, RoomBackend::Occupant::AI, "castor");
		room->update();
		REQUIRE(room->slots()[1].ai);
		CHECK(room->slots()[1].aiId == "castor");
		room->setOccupant(1, RoomBackend::Occupant::AI, "nicowar");
		room->update();
		CHECK(room->slots()[1].aiId == "nicowar");
		// Opening the seat again removes the AI.
		room->setOccupant(1, RoomBackend::Occupant::Open);
		room->update();
		CHECK(room->slots()[1].open);
		// A LAN room has no closed seats to offer.
		CHECK_FALSE(room->canCloseSeats());
	}

	TEST_CASE("an unserved version blames the side that is out of date [display]")
	{
		Display display;
		auto paintedHub = [](OnlineHubScreen::Model::Outdated outdated) {
			GAGGUI::ScreenStack stack(*globalContainer->gfx);
			return paintedTexts(OnlineUIFixtures::hubFixture(stack, [&](OnlineHubScreen::Model &m) {
				m.link = OnlineHubScreen::Model::Link::UpdateRequired;
				m.outdated = outdated;
			}));
		};
		const auto server = paintedHub(OnlineHubScreen::Model::Outdated::Server);
		CHECK_MESSAGE(anyContains(server, "Server update pending"), joined(server));
		CHECK_MESSAGE(!anyContains(server, "Get the update"), joined(server));
		CHECK_MESSAGE(!anyContains(server, "newer version"), joined(server));
		const auto client = paintedHub(OnlineHubScreen::Model::Outdated::Client);
		CHECK_MESSAGE(anyContains(client, "Update required"), joined(client));
		CHECK_MESSAGE(anyContains(client, "Get the update"), joined(client));
		const auto unknown = paintedHub(OnlineHubScreen::Model::Outdated::Unknown);
		CHECK_MESSAGE(anyContains(unknown, "Different version"), joined(unknown));
		CHECK_MESSAGE(!anyContains(unknown, "newer version"), joined(unknown));
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
