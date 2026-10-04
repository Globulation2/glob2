// SPDX-License-Identifier: GPL-3.0-or-later
// Renders every declarative screen at phone, tablet and desktop viewports with
// platform gutters and text scales, checking the framework invariants and saving
// captures for review. Run with SDL_VIDEODRIVER=dummy and a disposable profile.
#include <Environment.h>
#include "EngineFixtures.h"
#include "UIRecordingCanvas.h"
#include <InterfacePresentation.h>
#include <vector>
#include <string>
#include <memory>
#include <utility>
#include <cstdlib>
#include <cmath>
#include <exception>
#include "GlobalContainer.h"
#include "CampaignEditor.h"
#include "CampaignMainMenu.h"
#include "CampaignMenuScreen.h"
#include "CampaignSelectorScreen.h"
#include "ChooseMapScreen.h"
#include "CreditScreen.h"
#include "CustomGameOtherOptions.h"
#include "EditorMainMenu.h"
#include "Engine.h"
#include "LANFindScreen.h"
#include "LanRoom.h"
#include "LANMenuScreen.h"
#include "MainMenuScreen.h"
#include "MessageScreen.h"
#include "SettingsScreen.h"
#include "CustomGameScreen.h"
#include "LandscapePickerScreen.h"
#include "NewMapScreen.h"
#include "StartQualityScreen.h"
#include "AINames.h"
#include "GeneratorRegistry.h"
#include "GenerationRequest.h"
#include "FrontendTheme.h"
#include "OnlineUIFixtures.h"
#include <ui/Screen.h>
#include <HostViewport.h>
#include <ScreenStack.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <SDL3_net/SDL_net.h>
#include <cstdio>
#include <functional>
#include <set>
#include <stdexcept>

using namespace GAGGUI::ui;

namespace
{
void require(bool value, const std::string &message)
{
	GLOB2_REQUIRE(value, message);
}

struct Viewport
{
	const char *name;
	int width, height;
};
const Viewport viewports[] = {{"small-portrait", 320, 568},  {"small-landscape", 568, 320},
							  {"tablet-portrait", 768, 1024}, {"tablet-landscape", 1024, 768},
							  {"laptop", 1280, 800},          {"fullhd", 1920, 1080}};
const GAGCore::SafeInsets insetSets[] = {{}, {0, 24, 0, 48}, {48, 12, 24, 20}};

struct Fixture
{
	const char *name;
	std::function<std::unique_ptr<GAGGUI::Screen>(GAGGUI::ScreenStack &)> make;
	// Screens that immediately open a child (offline network errors) cannot take input.
	bool navigable = true;
};

// A LAN room as its host sees it, offline (no listener): the host, one AI and the
// open seats, through the real LanRoom backend in the Room screen.
std::unique_ptr<GAGGUI::Screen> lanRoom(GAGGUI::ScreenStack &s)
{
	Lan::LanHost::Options options;
	options.hostName = "Harness host";
	options.map = Engine().loadMapHeader("maps/balanced.map");
	options.network = false;
	auto room = Lan::LanRoom::host(std::move(options));
	room->addAI(AI::NICOWAR);
	room->update();
	return std::make_unique<RoomScreen>(s, room);
}

std::vector<Fixture> fixtures()
{
	auto &strings = *GAGCore::Toolkit::getStringTable();
	return {
		{"main-menu", [](GAGGUI::ScreenStack &) { return std::make_unique<MainMenuScreen>(); }},
		{"main-menu-more", [](GAGGUI::ScreenStack &) { return std::make_unique<MainMenuScreen>(); }},
		{"campaign-menu", [](GAGGUI::ScreenStack &s) { return std::make_unique<CampaignMainMenu>(s); }},
		{"editor-menu", [](GAGGUI::ScreenStack &s) { return std::make_unique<EditorMainMenu>(s); }},
		{"lan-menu", [](GAGGUI::ScreenStack &s) { return std::make_unique<LANMenuScreen>(s); }},
		{"credits", [](GAGGUI::ScreenStack &) { return std::make_unique<CreditScreen>(); }},
		{"message", [&strings](GAGGUI::ScreenStack &)
		 {
			 return std::make_unique<MessageScreen>(
				 strings.getString("[ERROR_CANT_LOAD_MAP]"),
				 std::vector<std::string>{strings.getString("[ok]"), strings.getString("[Cancel]")});
		 }},
		{"settings", [](GAGGUI::ScreenStack &) { return std::make_unique<SettingsScreen>(); }},
		{"custom-game", [](GAGGUI::ScreenStack &s) { return std::make_unique<CustomGameScreen>(s); }},
		{"custom-game-players", [](GAGGUI::ScreenStack &s)
		 {
			 auto lobby = std::make_unique<CustomGameScreen>(s);
			 lobby->selectTab(1);
			 return lobby;
		 }},
		{"custom-game-rules", [](GAGGUI::ScreenStack &s)
		 {
			 auto lobby = std::make_unique<CustomGameScreen>(s);
			 lobby->selectTab(2);
			 return lobby;
		 }},
		{"custom-game-rules-all", [](GAGGUI::ScreenStack &s)
		 {
			 auto lobby = std::make_unique<CustomGameScreen>(s);
			 lobby->selectTab(2);
			 lobby->selectRuleset("blitz");
			 lobby->setRulesView(CustomGameScreen::RulesView::All);
			 return lobby;
		 }},
		{"ruleset-choice", [](GAGGUI::ScreenStack &) { return std::make_unique<RulesetChoiceScreen>("blitz"); }},
		{"new-map", [](GAGGUI::ScreenStack &s) { return std::make_unique<NewMapScreen>(GeneratorRegistry::builtins(), &s); }},
		{"landscape-navigation", [](GAGGUI::ScreenStack &)
		 {
			 // Navigation needs a production picker, without running every generator.
			 GenerationRequest request;
			 request.setMethodDefaults(GenerationRequest::eSWAMP);
			 request.wDec = request.hDec = 6;
			 request.nbTeams = 2;
			 return std::make_unique<LandscapePickerScreen>("Choose a landscape",
				 std::vector<LandscapePickerScreen::Entry>{{GenerationRequest::methodName(request.method), request, request.method}}, 0);
		 }},
		{"landscape", [](GAGGUI::ScreenStack &)
		 {
			 GenerationRequest request;
			 request.setMethodDefaults(GeneratorRegistry::builtins().methods(false).front());
			 request.wDec = request.hDec = 6;
			 request.nbTeams = 2;
			 std::vector<LandscapePickerScreen::Entry> entries;
			 for (int method : GeneratorRegistry::builtins().methods(false))
			 {
				 auto entry = request;
				 entry.setMethodDefaults(method);
				 entry.wDec = entry.hDec = 6;
				 entry.nbTeams = 2;
				 LandscapePickerScreen::Entry e{GenerationRequest::methodName(method), entry, method};
				 if (const auto *definition = GeneratorRegistry::builtins().find(method))
					 e.tags = definition->tags;
				 entries.push_back(std::move(e));
			 }
			 return std::make_unique<LandscapePickerScreen>("Choose a landscape", std::move(entries), 0);
		 }},
		{"start-quality", [](GAGGUI::ScreenStack &)
		 {
			 return std::make_unique<StartQualityScreen>(MapGeneration::StartQualityReport{}, std::vector<std::string>{},
														  std::vector<GAGCore::Color>{});
		 }},
		{"ai-profile", [](GAGGUI::ScreenStack &)
		 {
			 std::vector<std::string> labels;
			 for (int i : AINames::selectionOrder())
				 labels.push_back(AINames::getAISelectorText(i));
			 return std::make_unique<CustomGameChoiceScreen>("AI profile", labels, 0, true, std::vector<bool>{});
		 }},
		{"campaign-select", [](GAGGUI::ScreenStack &) { return std::make_unique<CampaignSelectorScreen>(); }},
		{"campaign-saves", [](GAGGUI::ScreenStack &) { return std::make_unique<CampaignSelectorScreen>(true); }},
		{"tutorial-missions", [](GAGGUI::ScreenStack &s) { return std::make_unique<CampaignMenuScreen>("campaigns/Tutorial_Campaign.txt", s); }},
		{"campaign-editor", [](GAGGUI::ScreenStack &s) { return std::make_unique<CampaignEditor>("campaigns/Tutorial_Campaign.txt", s); }},
		{"map-repeat", [](GAGGUI::ScreenStack &)
		 {
			 auto screen = std::make_unique<ChooseMapScreen>("maps", "map", false);
			 screen->editMapParameters("maps/balanced_for_2.map.gz");
			 return screen;
		 }},
		{"load-map", [](GAGGUI::ScreenStack &) { return std::make_unique<ChooseMapScreen>("maps", "map", true); }},
		{"load-game", [](GAGGUI::ScreenStack &) { return std::make_unique<ChooseMapScreen>("games", "game", true, "replays", "replay", true); }},
		{"lan-find", [](GAGGUI::ScreenStack &s) { return std::make_unique<LANFindScreen>(s); }},
		{"lan-room", lanRoom},
		{"online-hub", [](GAGGUI::ScreenStack &s) { return OnlineUIFixtures::hubFixture(s); }},
		{"online-hub-signin", [](GAGGUI::ScreenStack &s)
		 {
			 return OnlineUIFixtures::hubFixture(s, [](OnlineHubScreen::Model &m)
												 {
													 m.signIn = OnlineHubScreen::Model::SignIn::Waiting;
													 m.confirmationCode = "KXQ742";
												 });
		 }},
		{"online-hub-signin-blocked", [](GAGGUI::ScreenStack &s)
		 {
			 return OnlineUIFixtures::hubFixture(s, [](OnlineHubScreen::Model &m)
												 {
													 m.signIn = OnlineHubScreen::Model::SignIn::Waiting;
													 m.confirmationCode = "KXQ742";
													 m.browserOpened = false;
												 });
		 }},
		{"online-hub-offline", [](GAGGUI::ScreenStack &s)
		 {
			 return OnlineUIFixtures::hubFixture(s, [](OnlineHubScreen::Model &m)
												 {
													 m.link = OnlineHubScreen::Model::Link::Offline;
													 m.retryInSeconds = 8;
													 m.displayName = "Bradley";
													 m.accountKind = "registered";
													 m.rooms = Online::Json::array();
												 });
		 }},
		{"online-hub-update", [](GAGGUI::ScreenStack &s)
		 {
			 return OnlineUIFixtures::hubFixture(s, [](OnlineHubScreen::Model &m)
												 {
													 m.link = OnlineHubScreen::Model::Link::UpdateRequired;
													 m.outdated = OnlineHubScreen::Model::Outdated::Client;
												 });
		 }},
		{"online-hub-server-behind", [](GAGGUI::ScreenStack &s)
		 {
			 return OnlineUIFixtures::hubFixture(s, [](OnlineHubScreen::Model &m)
												 {
													 m.link = OnlineHubScreen::Model::Link::UpdateRequired;
													 m.outdated = OnlineHubScreen::Model::Outdated::Server;
												 });
		 }},
		{"online-hub-trust", [](GAGGUI::ScreenStack &s)
		 {
			 auto hub = OnlineUIFixtures::hubFixture(s);
			 static_cast<OnlineHubScreen &>(*hub).acceptInvite("https://play.lanparty.net", "7HD21QABCD");
			 return hub;
		 }},
		{"room-host", [](GAGGUI::ScreenStack &s)
		 {
			 return std::make_unique<RoomScreen>(s, Online::PlatformRoom::preview(OnlineUIFixtures::roomState(), OnlineUIFixtures::HOST_ID, OnlineUIFixtures::roomChat()));
		 }},
		{"room-guest-map", [](GAGGUI::ScreenStack &s)
		 {
			 auto room = std::make_unique<RoomScreen>(s, Online::PlatformRoom::preview(OnlineUIFixtures::roomState(), OnlineUIFixtures::GUEST_ID, OnlineUIFixtures::roomChat()));
			 room->selectTab(RoomScreen::MapTab);
			 return room;
		 }},
		{"room-rules", [](GAGGUI::ScreenStack &s)
		 {
			 auto room = std::make_unique<RoomScreen>(s, Online::PlatformRoom::preview(OnlineUIFixtures::roomState(), OnlineUIFixtures::HOST_ID, OnlineUIFixtures::roomChat()));
			 room->selectTab(RoomScreen::RulesTab);
			 return room;
		 }},
		{"room-lan", [](GAGGUI::ScreenStack &s) { return std::make_unique<RoomScreen>(s, std::make_shared<OnlineUIFixtures::LanRoomFixture>()); }},
		// A member who joined a full room: listed as not seated, Ready disabled with why.
		{"room-unseated", [](GAGGUI::ScreenStack &s)
		 {
			 return std::make_unique<RoomScreen>(s, Online::PlatformRoom::preview(OnlineUIFixtures::fullRoomState(), OnlineUIFixtures::LATE_ID, OnlineUIFixtures::roomChat()));
		 }},
		// A premade map uploaded for the room, named by the server's mapTitle.
		{"room-premade-map", [](GAGGUI::ScreenStack &s)
		 {
			 auto room = std::make_unique<RoomScreen>(s, Online::PlatformRoom::preview(OnlineUIFixtures::premadeRoomState(), OnlineUIFixtures::HOST_ID, OnlineUIFixtures::roomChat()));
			 room->selectTab(RoomScreen::MapTab);
			 return room;
		 }},
		{"match-starting", [](GAGGUI::ScreenStack &s) { return std::make_unique<MatchStartScreen>(s, OnlineUIFixtures::startingMatch()); }},
		{"settings-online", [](GAGGUI::ScreenStack &)
		 {
			 auto settings = std::make_unique<SettingsScreen>();
			 settings->selectCategory(SettingsScreen::Category::Online);
			 return settings;
		 }},
		{"settings-hive-mind", [](GAGGUI::ScreenStack &)
		 {
			 auto settings = std::make_unique<SettingsScreen>();
			 settings->selectCategory(SettingsScreen::Category::HiveMind);
			 return settings;
		 }},
		{"settings-recording", [](GAGGUI::ScreenStack &)
		 {
			 auto settings = std::make_unique<SettingsScreen>();
			 settings->selectCategory(SettingsScreen::Category::Recording);
			 return settings;
		 }},
		// Online screens (quick match, profile, maps) on canned data.
		{"quick-match", [](GAGGUI::ScreenStack &s) { return OnlineUIFixtures::quickMatch(s, false); }},
		{"quick-match-searching", [](GAGGUI::ScreenStack &s) { return OnlineUIFixtures::quickMatch(s, true); }},
		{"match-found", [](GAGGUI::ScreenStack &) { return OnlineUIFixtures::matchFound(true); }},
		{"match-found-ai", [](GAGGUI::ScreenStack &) { return OnlineUIFixtures::matchFound(false); }},
		{"online-profile", [](GAGGUI::ScreenStack &s) { return OnlineUIFixtures::profile(s); }},
		{"online-maps", [](GAGGUI::ScreenStack &s)
		 { return OnlineUIFixtures::maps(s, OnlineMapsScreen::Tab::Browse, glob2test::sourceRoot().string() + "/"); }},
		{"online-my-maps", [](GAGGUI::ScreenStack &s)
		 { return OnlineUIFixtures::maps(s, OnlineMapsScreen::Tab::Mine, glob2test::sourceRoot().string() + "/"); }},
		{"map-share", [](GAGGUI::ScreenStack &) { return OnlineUIFixtures::share(0); }},
		{"map-share-checking", [](GAGGUI::ScreenStack &) { return OnlineUIFixtures::share(1); }},
		{"map-share-rejected", [](GAGGUI::ScreenStack &) { return OnlineUIFixtures::share(2); }},
		{"setup-options", [](GAGGUI::ScreenStack &)
		 {
			 static MapHeader mapHeader = Engine().loadMapHeader("maps/balanced.map");
			 static GameHeader gameHeader;
			 return std::make_unique<CustomGameOtherOptions>(gameHeader, mapHeader, false);
		 }},
	};
}

void resize(int width, int height)
{
	auto *gfx = globalContainer->gfx;
	auto *window = SDL_GetWindowFromID(gfx->windowID());
	int actualWidth = 0, actualHeight = 0;
	REQUIRE(SDL_GetWindowSize(window, &actualWidth, &actualHeight));
	if (actualWidth != width || actualHeight != height)
		REQUIRE(SDL_SetWindowSize(window, width, height));
	// X11 synchronization also waits for window position and can time out even
	// when a repeated resize already has the requested dimensions.
	const auto deadline = SDL_GetTicks() + 3000;
	do
	{
		SDL_PumpEvents();
		REQUIRE(SDL_GetWindowSize(window, &actualWidth, &actualHeight));
		if (actualWidth == width && actualHeight == height)
			break;
		SDL_Delay(10);
	} while (SDL_GetTicks() < deadline);
	int minWidth = 0, minHeight = 0, maxWidth = 0, maxHeight = 0;
	SDL_GetWindowMinimumSize(window, &minWidth, &minHeight);
	SDL_GetWindowMaximumSize(window, &maxWidth, &maxHeight);
	INFO("Requested " << width << "x" << height << "; actual " << actualWidth << "x" << actualHeight
		 << "; minimum " << minWidth << "x" << minHeight << "; maximum " << maxWidth << "x" << maxHeight
		 << "; flags " << SDL_GetWindowFlags(window) << "; SDL error: " << SDL_GetError());
	REQUIRE(actualWidth == width);
	REQUIRE(actualHeight == height);
	SDL_Event event{};
	event.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
	GAGCore::GraphicContext::translateMouseEvent(&event);
	REQUIRE(gfx->getW() == int(std::lround(width / gfx->getUiScale())));
	REQUIRE(gfx->getH() == int(std::lround(height / gfx->getUiScale())));
}

// The part of a node that clipping ancestors leave visible.
Rect visibleRect(Node &root, Node &target)
{
	std::vector<Node *> path;
	std::function<bool(Node &)> search = [&](Node &node)
	{
		path.push_back(&node);
		if (&node == &target)
			return true;
		for (auto &child : node.children)
			if (search(*child))
				return true;
		path.pop_back();
		return false;
	};
	if (!search(root))
		return {};
	Rect visible = target.bounds;
	for (auto *ancestor : path)
		if (ancestor != &target && ancestor->clipsChildren())
			visible = visible.intersect(ancestor->bounds);
	return visible;
}

void dump(Node &node, int depth)
{
	std::fprintf(stderr, "%*s%s key=%s bounds=%d,%d %dx%d flex=%d\n", depth * 2, "", node.name(), node.key.c_str(),
				 node.bounds.x, node.bounds.y, node.bounds.w, node.bounds.h, node.flex);
	for (auto &child : node.children)
		dump(*child, depth + 1);
}

// The invariants every screen inherits from the framework.
void verify(UIScreen &screen, const std::string &label);
void verifyOrDump(UIScreen &screen, const std::string &label)
{
	try
	{
		verify(screen, label);
	}
	catch (...)
	{
		if (SDL_getenv_unsafe("GLOB2_UI_DUMP"))
			dump(*screen.host().root(), 0);
		throw;
	}
}
void verify(UIScreen &screen, const std::string &label)
{
	auto &host = screen.host();
	const auto &p = host.presentation();
	std::set<std::string> keys;
	std::vector<Node *> interactive;
	host.root()->visit(
		[&](Node &node)
		{
			if (!node.key.empty())
				require(keys.insert(node.key).second, label + ": duplicate key " + node.key);
			if (node.interactive())
				interactive.push_back(&node);
		});
	require(!interactive.empty(), label + ": screen exposes no controls");
	std::vector<Rect> visible;
	for (auto *node : interactive)
	{
		const Rect shown = visibleRect(*host.root(), *node);
		visible.push_back(shown);
		if (shown.empty())
			continue;
		require(p.safe.contains(shown) || p.dialog.contains(shown),
				label + ": control " + node->key + " leaves the safe area");
		if (p.touch && shown == node->bounds)
			require(node->bounds.h >= host.metrics().minTarget,
					label + ": control " + node->key + " is shorter than the touch target (" +
						std::to_string(node->bounds.h) + " < " + std::to_string(host.metrics().minTarget) + ")");
	}
	// No two visible controls overlap unless one contains the other or a stack layers them.
	auto stackOf = [&](Node *target) -> Node *
	{
		Node *result = nullptr;
		std::vector<Node *> path;
		std::function<bool(Node &)> search = [&](Node &node)
		{
			path.push_back(&node);
			if (&node == target)
				return true;
			for (auto &child : node.children)
				if (search(*child))
					return true;
			path.pop_back();
			return false;
		};
		if (search(*host.root()))
			for (auto *ancestor : path)
				if (std::string(ancestor->name()) == "stack")
					result = ancestor;
		return result;
	};
	for (std::size_t i = 0; i < interactive.size(); ++i)
		for (std::size_t j = i + 1; j < interactive.size(); ++j)
		{
			const Rect a = visible[i], b = visible[j];
			if (a.empty() || b.empty() || !a.intersects(b))
				continue;
			bool nested = false;
			interactive[i]->visit([&](Node &n) { nested |= &n == interactive[j]; });
			interactive[j]->visit([&](Node &n) { nested |= &n == interactive[i]; });
			if (!nested)
			{
				auto *layer = stackOf(interactive[i]);
				nested = layer && layer == stackOf(interactive[j]);
			}
			require(nested, label + ": controls " + interactive[i]->key + " and " +
								interactive[j]->key + " overlap");
		}
	require(host.focusOrder().size() >= 1, label + ": nothing is focusable");
	// Text never spills out of the control that measured it, at any text size.
	ToolkitTextMeasurer measurer(screen.theme(), p.touch, p.textUnit);
	glob2test::RecordingCanvas canvas(p.viewport.size(), measurer);
	host.paint(canvas, 0);
	const auto spill = glob2test::textSpill(canvas, interactive,
											[&](Node &node) { return visibleRect(*host.root(), node); });
	require(spill.empty(), label + ": " + spill);
}
void run(const Viewport &viewport)
{
	if (const char *only = SDL_getenv_unsafe("GLOB2_UI_VIEWPORT");
		only && *only && std::string(only) != viewport.name)
		return;
	glob2test::GlobalsOptions options{.display = true, .loadStrings = true, .width = 800, .height = 600,
	                                  .screenFlags = GAGCore::GraphicContext::PORTABLEGPU | GAGCore::GraphicContext::RESIZABLE};
	glob2test::HeadlessGlobals globals(options);
	// This sweep constructs LAN discovery screens, just as Glob2::run does
	// after network initialization. SDL3_net resolvers require initialized
	// synchronization even when no connection is made by the fixture.
	struct NetworkScope {
		NetworkScope() { REQUIRE(NET_Init()); }
		~NetworkScope() { NET_Quit(); }
	} network;
	auto theme = std::make_unique<FrontendTheme>();
	int checked = 0;
	const bool capture = true;
	const auto captures = glob2test::artifactDir();
	const std::string capturePath = glob2test::artifactDirFromWorkingDirectory();
	for (const char *presentation : {"0", "1"})
	{
		GAGCore::setProcessEnvironment("GLOB2_MOBILE_UI", presentation, 1);
		if (presentation[0] == '0' && viewport.width < 600)
			continue;
		resize(viewport.width, viewport.height);
		for (const auto &fixture : fixtures())
		{
			if (const char *only = SDL_getenv_unsafe("GLOB2_UI_ONLY"); only && *only && std::string(only) != fixture.name)
				continue;
			// Keep generated previews while the same viewport changes safe insets.
			// Separate viewport cases remain independently shardable in CI.
			std::fprintf(stderr, "UI presentation fixture: %s touch=%s\n", fixture.name, presentation);
			GAGGUI::ScreenStack stack(*globalContainer->gfx);
			auto owned = fixture.make(stack);
			auto *screen = dynamic_cast<UIScreen *>(owned.get());
			require(screen != nullptr, std::string(fixture.name) + " is not a UIScreen");
			stack.push(std::move(owned));
			Uint32 tick = SDL_GetTicks();
			auto frame = [&](const std::vector<SDL_Event> &events = {})
			{
				stack.frame(tick, events);
				tick += 40;
			};
			// Every screen at the authored text size and at the largest text size.
			for (const int percent : {100, 150})
			for (const auto &insets : insetSets)
			{
				GAGCore::userTextScale = percent / 100.0;
				GAGCore::mobileSafeInsetsForTesting = insets;
				frame();
				frame();
				if (std::string(fixture.name) == "main-menu-more")
					if (auto *more = screen->host().find("menu/more"))
					{
						const auto r = more->bounds;
						screen->host().tapAt({r.x + r.w / 2, r.y + r.h / 2});
						frame();
					}
				require(stack.running(), std::string(fixture.name) + " ended during warm-up");
				const std::string label = std::string(fixture.name) + " " + viewport.name +
										  " touch=" + presentation + " text=" + std::to_string(percent) +
										  " bottom=" + std::to_string(int(insets.bottom));
				if (const char *reveal = SDL_getenv_unsafe("GLOB2_UI_REVEAL"); reveal && *reveal)

				{
					screen->host().scrollIntoView(reveal);
					frame();
				}
				require(stack.top() == screen, label + ": fixture is hidden beneath a child screen");
				std::string screenshot;
				if (capture && insets.bottom == 0)
				{
					screenshot = "ui-" + std::string(fixture.name) + "-" + viewport.name + "-touch" +
						presentation + (percent == 100 ? "" : "-text" + std::to_string(percent)) + ".bmp";
					// Retain captures directly; profile fallback can write to the
					// source tree, and copying captures doubles disk requirements.
					std::filesystem::remove(captures / screenshot);
					globalContainer->gfx->printScreen(capturePath + "/" + screenshot);
				}
				verifyOrDump(*screen, label);
				frame();
				if (!screenshot.empty())
					require(std::filesystem::exists(captures / screenshot) &&
						std::filesystem::file_size(captures / screenshot) > 0,
						label + ": screenshot was not retained");
				// Tab reaches every control and never throws.
				for (std::size_t i = 0; fixture.navigable && i < screen->host().focusOrder().size(); ++i)
				{
					SDL_Event tab{};
					tab.type = SDL_EVENT_KEY_DOWN;
					tab.key.key = SDLK_TAB;
					frame({tab});
				}
				if (fixture.navigable && screen->host().focused().empty())
				{
					std::string order;
					for (const auto &key : screen->host().focusOrder())
						order += key + " ";
					require(false, label + ": tab never focused a control (order: " + order + ", editing: " + screen->host().editing() + ")");
				}
				// The focused control's tooltip is an overlay that may cover its
				// neighbours (Leave's covers the room's tab bar on a small phone); the
				// next pass checks the layout, so dismiss it as Escape or a tap would.
				screen->host().dismissTooltip(screen->host().focused());
				++checked;
			}
			screen->endExecute(0);
			frame();
		}
	}
	GAGCore::mobileSafeInsetsForTesting.reset();
	GAGCore::userTextScale = 1;
	theme.reset();
	GAGCore::setProcessEnvironment("GLOB2_MOBILE_UI", "0", 1);
	std::printf("PASS ui presentation: %d screen/viewport combinations verified\n", checked);
}
} // namespace

TEST_SUITE("UIPresentation")
{
	TEST_CASE("every screen lays out; navigates and captures at small portrait across presentations and insets [display:1600x1400][artifacts][slow]")
	{
		run(viewports[0]);
	}
	TEST_CASE("every screen lays out; navigates and captures at small landscape across presentations and insets [display:1600x1400][artifacts][slow]")
	{
		run(viewports[1]);
	}
	TEST_CASE("every screen lays out; navigates and captures at tablet portrait across presentations and insets [display:1600x1400][artifacts][slow]")
	{
		run(viewports[2]);
	}
	TEST_CASE("every screen lays out; navigates and captures at tablet landscape across presentations and insets [display:1600x1400][artifacts][slow]")
	{
		run(viewports[3]);
	}
	TEST_CASE("every screen lays out; navigates and captures at laptop across presentations and insets [display:1600x1400][artifacts][slow]")
	{
		run(viewports[4]);
	}
	TEST_CASE("every screen lays out; navigates and captures at fullhd across presentations and insets [display:2200x1400][artifacts][slow]")
	{
		run(viewports[5]);
	}
}
