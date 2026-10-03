// SPDX-License-Identifier: GPL-3.0-or-later
// Generate the bundled colony and exercise the real simulation/presentation.
#include "GlobalContainer.h"
#include "FrontendTheme.h"
#include "MenuColony.h"
#include <StringTable.h>
#include "MainMenuScreen.h"
#include <GameplayRecording.h>
#include "SettingsScreen.h"
#include "LANMenuScreen.h"
#include "ChooseMapScreen.h"
#include "CustomGameScreen.h"
#include "CustomGamePreferences.h"
#include "CustomGameOtherOptions.h"
#include "gui/LoadSaveDialog.h"
#include "CampaignMainMenu.h"
#include "EditorMainMenu.h"
#include "CreditScreen.h"
#include "MapGenerationDescriptor.h"
#include "MapGenerator.h"
#include "FertilityCalculator.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include "GameGUI.h"
#include "Engine.h"
#include "MapEdit.h"
#include "MapEditorScreen.h"
#include "EndGameScreen.h"
#include "CampaignMenuScreen.h"
#include "CampaignSelectorScreen.h"
#include "NewMapScreen.h"
#include "LANFindScreen.h"
#include "ReplayWriter.h"
#include "DatasetWriter.h"
#include <filesystem>
#include <sstream>
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include <ScreenStack.h>
#include <SDL3_image/SDL_image.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <chrono>

GlobalContainer *globalContainer = nullptr;
using namespace GAGCore;
std::string replayFilenameToName(const std::string &);

void require(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		std::exit(2);
	}
}
// Menus keep the frontend theme active for their lifetime, even when created
// while gameplay is still winding down, and their background never changes when
// a pending gameplay screen acquires its scope.
void checkMenuPainting()
{
	struct Menu : Glob2UI::Screen
	{
		Glob2UI::Element build(const Glob2UI::Presentation &) override { return Glob2UI::label("menu"); }
	};
	{
		FrontendScope game(false);
		{
			Menu menu;
			require(Style::style == FrontendTheme::current,
					"menu created during game teardown activates theme");
		}
		require(Style::style != FrontendTheme::current,
				"menu destruction restores game presentation");
	}
	{
		Menu menu;
		menu.beginExecution(globalContainer->gfx);
		auto raster = []
		{
			DrawableSurface image(globalContainer->gfx->getW(), globalContainer->gfx->getH());
			image.drawSurface(0, 0, globalContainer->gfx);
			auto *pixels = image.getSDLSurface();
			return std::string(static_cast<const char *>(pixels->pixels),
							   pixels->pitch * pixels->h);
		};
		menu.paintFrame(0);
		const auto expected = raster();
		{
			FrontendScope pendingGame(false);
			menu.paintFrame(0);
			require(raster() == expected,
					"pending game cannot restore the legacy grass background");
		}
		menu.endExecute(0);
		menu.finishExecution();
	}
}

void generate(const char *path)
{
	setSyncRandSeed(481516);
	std::srand(481516);
	Game game(nullptr);
	MapGenerationDescriptor d;
	// Start from the generator's own current defaults (valid control ranges
	// move as generators are tuned) and override only what this decorative
	// colony actually needs to differ.
	d.setMethodDefaults(MapGenerationDescriptor::eOLDISLANDS);
	d.wDec = d.hDec = 7;
	d.nbTeams = 1;
	d.nbWorkers = 8;
	d.waterRatio = 25;
	d.grassRatio = 65;
	d.sandRatio = 10;
	// The registry-driven generator owns map sizing and the game association;
	// pass the fixed seed explicitly since it no longer follows the global
	// sync-rand state seeded above.
	require(MapGenerator().generateMap(game, d, 481516u), "generate terrain");
	// The legacy island generator supplies terrain only. Seed small groves
	// and grain fields with the existing resource API, leaving walking lanes.
	const int bx = d.bootX[0], by = d.bootY[0];
	for (int y = 0; y < game.map.getH(); ++y)
		for (int x = 0; x < game.map.getW(); ++x)
		{
			const int dx = ((x - bx + 64) & 127) - 64, dy = ((y - by + 64) & 127) - 64;
			if (game.map.getUMTerrain(x, y) != GRASS || (std::abs(dx) < 6 && std::abs(dy) < 6))
				continue;
			if ((x % 8 < 4) && (y % 8 < 4))
			{
				const int resource =
					((x / 8 + y / 8) % 5 == 0) ? STONE : ((x / 8 + y / 8) % 2 ? WHEAT : WOOD);
				game.map.setResource(x, y, resource, 3);
			}
		}
	game.sgslScript.compileScript(&game);
	FertilityCalculator::compute(game.map, {});
	GameHeader header;
	header.setNumberOfPlayers(1);
	header.setRandomSeed(481516);
	header.getBasePlayer(0) =
		BasePlayer(0, "Menu colony", 0, BasePlayer::playerTypeFromImplementationID(AI::ECONO));
	header.getWinningConditions().clear();
	game.setGameHeader(header);
	game.setAlliances();
	game.map.getResourceGradient(0, WHEAT, 0);
	game.teams[0]->color = Color(73, 191, 184);
	for (int i = 0; i < 12000; ++i)
	{
		auto order = game.players[0]->ai->getOrder(false);
		order->sender = 0;
		game.executeOrder(order, 0);
		game.syncStep(0);
		if (i % 3000 == 0)
		{
			int buildings = 0, units = 0;
			for (int j = 0; j < Building::MAX_COUNT; ++j)
			{
				auto *b = game.teams[0]->myBuildings[j];
				if (b && !b->type->isVirtual)
					++buildings;
			}
			for (int j = 0; j < Unit::MAX_COUNT; ++j)
				if (game.teams[0]->myUnits[j])
					++units;
			std::cout << "warmup_tick=" << i << " buildings=" << buildings << " units=" << units
					  << std::endl;
		}
	}
	BinaryOutputStream out(Toolkit::getFileManager()->openOutputStreamBackend(path));
	out.writeText("glob2-menu-colony-1", "format");
	game.save(&out, false, "Menu colony");
	std::ostringstream colonyRandom;
	colonyRandom << game.syncRandom;
	out.writeText(colonyRandom.str(), "rng");
	std::cout << "Generated colony at tick " << game.stepCounter << '\n';
}

// Drives a declarative screen outside a stack: layout, painting and pointer or
// keyboard input through the production host.
template <class T> class Preview : public T
{
  public:
	using T::T;
	~Preview()
	{
		if (this->isExecutionRunning())
		{
			this->endExecute(0);
			this->finishExecution();
		}
	}
	void prepare()
	{
		if (!prepared)
		{
			this->beginExecution(globalContainer->gfx);
			prepared = true;
		}
		this->paintFrame(SDL_GetTicks());
	}
	void render()
	{
		prepare();
		this->paintFrame(SDL_GetTicks());
	}
	void advance(unsigned frames)
	{
		prepare();
		for (unsigned i = 0; i < frames; ++i)
			this->updateExecution(i * 40);
	}
	void checkBounds()
	{
		prepare();
		for (auto *node : this->GAGGUI::ui::UIScreen::host().interactiveNodes())
		{
			const auto r = node->bounds;
			require(r.x >= 0 && r.y >= 0 && r.x + r.w <= globalContainer->gfx->getW() &&
						r.y + r.h <= globalContainer->gfx->getH(),
					"control within screen");
		}
	}
	void clickButton(const std::string &key)
	{
		prepare();
		const auto r = this->GAGGUI::ui::UIScreen::host().bounds(key);
		SDL_Event e{};
		e.button.button = SDL_BUTTON_LEFT;
		e.button.x = r.x + r.w / 2;
		e.button.y = r.y + r.h / 2;
		e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
		this->handleExecutionEvent(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_UP;
		this->handleExecutionEvent(e);
	}
	void key(SDL_Keycode code)
	{
		prepare();
		SDL_Event e{};
		e.type = SDL_EVENT_KEY_DOWN;
		e.key.key = code;
		this->handleExecutionEvent(e);
	}
	int result() const { return GAGGUI::Screen::returnCode; }

  private:
	bool prepared = false;
};
void capture(const std::string &name, const std::string &path)
{
	FrontendScope scope;
	GAGGUI::ScreenStack screens(*globalContainer->gfx);
	if (name == "colony")
	{
		FrontendTheme::current->colony->draw(globalContainer->gfx->getW(),
											 globalContainer->gfx->getH());
	}
	else if (name == "main" || name == "fallback")
	{
		Preview<MainMenuScreen> s;
		s.render();
		s.checkBounds();
	}
	else if (name == "options")
	{
		Preview<CustomGameScreen> parent(screens);
		parent.prepare();
		Preview<CustomGameOtherOptions> s(parent.getGameHeader(), parent.getMapHeader(), false);
		s.render();
		s.checkBounds();
	}
	else if (name == "save-replay")
	{
		Preview<CampaignMainMenu> parent(screens);
		parent.render();
		LoadSaveDialog s("replays", "replay", false, "Save replay", "", replayFilenameToName,
						 glob2NameToFilename);
		s.attach(*globalContainer->gfx);
		s.draw(0);
	}
	else if (name == "settings" || name == "settings-buildings" || name == "settings-keys")
	{
		Preview<SettingsScreen> s;
		if (name == "settings-buildings")
			s.selectCategory(SettingsScreen::Category::Buildings);
		if (name == "settings-keys")
			s.selectCategory(SettingsScreen::Category::Controls);
		s.render();
		s.checkBounds();
	}
	else if (name == "lan")
	{
		Preview<LANMenuScreen> s(screens);
		s.render();
		s.checkBounds();
	}
	else if (name == "campaign")
	{
		Preview<CampaignMainMenu> s(screens);
		s.render();
		s.checkBounds();
	}
	else if (name == "editor")
	{
		Preview<EditorMainMenu> s(screens);
		s.render();
		s.checkBounds();
	}
	else if (name == "credits")
	{
		Preview<CreditScreen> s;
		s.advance(450);
		s.render();
		s.checkBounds();
	}
	else if (name == "load")
	{
		Preview<ChooseMapScreen> s("games", "game", true);
		s.render();
		s.checkBounds();
	}
	else if (name == "missions")
	{
		Preview<CampaignMenuScreen> s("campaigns/Tutorial_Campaign.txt", screens);
		s.render();
		s.checkBounds();
	}
	else if (name == "campaign-select")
	{
		Preview<CampaignSelectorScreen> s;
		s.render();
		s.checkBounds();
	}
	else if (name == "new-map")
	{
		Preview<NewMapScreen> s;
		s.render();
		s.checkBounds();
	}
	else if (name == "lan-find")
	{
		Preview<LANFindScreen> s(screens);
		s.render();
		s.checkBounds();
	}
	else if (name == "results")
	{
		GameGUI gui;
		BinaryInputStream in(
			Toolkit::getFileManager()->openInputStreamBackend("data/menu/colony.bin"));
		in.readText("format");
		require(gui.game.load(&in), "load results fixture");
		gui.localTeamNo = 0;
		gui.localPlayer = 0;
		gui.adjustLocalTeam();
		Preview<EndGameScreen> s(&gui);
		s.render();
		s.checkBounds();
	}
	else if (name == "custom" || name == "custom-players" || name == "custom-rules")
	{
		Preview<CustomGameScreen> s(screens);
		s.prepare();
		if (name == "custom-players")
			s.selectTab(1);
		if (name == "custom-rules")
			s.selectTab(2);
		s.render();
		s.checkBounds();
	}
	else
		require(false, "unknown screen");
	DrawableSurface shot(globalContainer->gfx->getW(), globalContainer->gfx->getH());
	shot.drawSurface(0, 0, globalContainer->gfx);
	require(IMG_SavePNG(shot.getSDLSurface(), path.c_str()), "save PNG");
}
// Only the test driver uses a timer: inject normal UI events into session loops.
struct SessionExit
{
	int phase = 0, tabs = 0;
	bool replay = false;
};
Uint32 SDLCALL exitSession(void *data, SDL_TimerID, Uint32)
{
	auto &state = *static_cast<SessionExit *>(data);
	SDL_Event e{};
	if (state.replay)
	{
		e.type = SDL_EVENT_KEY_DOWN;
		e.key.key = SDLK_RETURN;
		SDL_PushEvent(&e);
	}
	else if (state.phase == 0)
	{
		e.type = SDL_EVENT_KEY_DOWN;
		e.key.key = SDLK_ESCAPE;
		SDL_PushEvent(&e);
	}
	else if (state.phase == 1)
	{
		// Quit is the last action of the in-game and editor menus: Tab to it, then Return.
		e.type = SDL_EVENT_KEY_DOWN;
		e.key.key = SDLK_TAB;
		for (int i = 0; i < state.tabs; ++i)
			SDL_PushEvent(&e);
		e.key.key = SDLK_RETURN;
		SDL_PushEvent(&e);
	}
	else
	{
		e.type = SDL_EVENT_KEY_DOWN;
		e.key.key = SDLK_RETURN;
		SDL_PushEvent(&e);
	}
	++state.phase;
	return 500;
}

int main(int argc, char **argv)
{
	require(argc >= 3, "usage: MenuColonyHarness generate PATH | check PATH | capture SCREEN "
					   "OUTPUT [W H] | soak SECONDS");
	const auto startupBegin = std::chrono::steady_clock::now();
	GlobalContainer globals("glob2-frontend-test");
	globalContainer = &globals;
	globals.settings.mute = 1;
	globals.settings.screenWidth = argc > 4 ? std::atoi(argv[4]) : 1152;
	globals.settings.screenHeight = argc > 5 ? std::atoi(argv[5]) : 720;
	globals.settings.screenFlags = std::getenv("GLOB2_PREVIEW_GL") ? GraphicContext::USEGPU : 0;
	globals.load();
	if (const char *lang = std::getenv("GLOB2_PREVIEW_LANGUAGE"))
		Toolkit::getStringTable()->setLang(Toolkit::getStringTable()->getLangCode(lang));
	const std::string mode = argv[1];
	if (mode == "generate")
	{
		generate(argv[2]);
		return 0;
	}
	std::cout << "global_assets_ms="
			  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
														   startupBegin)
					 .count()
			  << std::endl;
	const auto themeBegin = std::chrono::steady_clock::now();
	FrontendTheme theme;
	std::cout << "theme_setup_ms="
			  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
														   themeBegin)
					 .count()
			  << std::endl;
	if (mode == "capture")
	{
		require(argc >= 4, "capture needs screen and output");
		if (std::string(argv[2]) != "fallback")
			require(theme.colony->load(), "load bundled colony");
		capture(argv[2], argv[3]);
		return 0;
	}
	if (mode == "presentation")
	{
		checkMenuPainting();
		return 0;
	}
	if (mode == "check")
	{
		checkMenuPainting();
		// Team tinting must preserve transparent sprite pixels in software mode.
		DrawableSurface alpha(2, 2);
		auto *pixels = static_cast<Uint32 *>(alpha.getSDLSurface()->pixels);
		pixels[0] = SDL_MapSurfaceRGBA(alpha.getSDLSurface(), 51, 255, 153, 0);
		alpha.shiftHSV(35, 0, 0);
		Uint8 red, green, blue, opacity;
		SDL_GetRGBA(pixels[0], SDL_GetPixelFormatDetails(alpha.getSDLSurface()->format), SDL_GetSurfacePalette(alpha.getSDLSurface()), &red, &green, &blue, &opacity);
		require(opacity == 0, "team tint preserves transparency");
		const auto rng = getSyncRandState();
		const auto loadBegin = std::chrono::steady_clock::now();
		require(theme.colony->load(argv[2]), "load colony");
		require(getSyncRandState() == rng, "load restores RNG");
		std::cout << "colony_load_ms="
				  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
															   loadBegin)
						 .count()
				  << std::endl;
		const bool replaying = globals.replaying, flags = globals.replayShowFlags;
		const auto teams = globals.replayVisibleTeams;
		theme.colony->draw(globals.gfx->getW(), globals.gfx->getH());
		require(globals.replaying == replaying && globals.replayShowFlags == flags &&
					globals.replayVisibleTeams == teams && getSyncRandState() == rng,
				"drawing restores global state");
		const auto initialChecksum = theme.colony->checksum();
		const auto before = theme.colony->tick();
		theme.colony->update(1000);
		theme.colony->update(1040);
		require(theme.colony->tick() == before + 1, "one normal tick");
		require(getSyncRandState() == rng, "step restores RNG");
		theme.colony->pause();
		theme.colony->update(999999);
		require(theme.colony->tick() == before + 1, "resume has no catchup");
		theme.colony->update(1999999);
		require(theme.colony->tick() == before + 3, "catchup bounded to two ticks");
		const auto font = globals.standardFont->getStyle();
		auto *style = GAGGUI::Style::style;
		{
			FrontendScope menu;
			require(GAGGUI::Style::style == &theme, "menu theme");
			{
				FrontendScope gameplay(false);
				require(GAGGUI::Style::style == style, "gameplay style");
				FrontendScope nested;
				require(GAGGUI::Style::style == style, "gameplay dialog style");
			}
			require(GAGGUI::Style::style == &theme, "nested restoration");
		}
		require(GAGGUI::Style::style == style &&
					globals.standardFont->getStyle().color == font.color,
				"theme/font restoration");
		MenuColony a, b;
		require(a.load(argv[2]) && b.load(argv[2]), "two sessions load");
		for (int i = 0; i < 300; ++i)
		{
			a.update(1000 + i * 40);
			theme.colony->update(2000000 + i * 40);
			b.update(1000 + i * 40);
		}
		require(a.checksum() == b.checksum(),
				"interleaved colony cannot change another game's result");
		require(getSyncRandState() == rng, "interleaved games restore RNG");
		require(theme.colony->checksum() != initialChecksum, "live game state changes");
		// Compare actual GUI-backed game sessions, with and without menu work.
		auto simulate = [&](bool background)
		{
			GameGUI gui;
			BinaryInputStream in(Toolkit::getFileManager()->openInputStreamBackend(argv[2]));
			in.readText("format");
			require(gui.game.load(&in), "load real-game fixture");
			std::istringstream state(in.readText("rng") + " ");
			state >> gui.game.syncRandom;
			gui.game.map.getResourceGradient(0, WHEAT, 0);
			gui.localTeamNo = 0;
			gui.localPlayer = 0;
			gui.adjustLocalTeam();
			globals.replayWriter = std::make_unique<ReplayWriter>();
			globals.replayWriter->init("", gui);
			for (int i = 0; i < 300; ++i)
			{
				if (background)
				{
					auto *writer = globals.replayWriter.get();
					const auto position = writer->getBuffer()->getPosition();
					theme.colony->update(3000000 + i * 40);
					require(globals.replayWriter.get() == writer &&
								writer->getBuffer()->getPosition() == position,
							"menu cannot record into match replay");
				}
				auto order = gui.game.players[0]->ai->getOrder(false);
				order->sender = 0;
				gui.game.executeOrder(order, 0);
				gui.game.syncStep(0);
			}
			const auto result =
				std::make_pair(gui.game.checkSum(nullptr, nullptr, nullptr), getSyncRandState());
			globals.replayWriter.reset();
			return result;
		};
		require(simulate(false) == simulate(true),
				"real match checksum and RNG unaffected by menu");
		const auto datasetPath =
			(std::filesystem::temp_directory_path() / "glob2-menu-isolation.gds").string();
		globals.datasetWriter = std::make_unique<DatasetWriter>();
		require(globals.datasetWriter->open(datasetPath), "open recording isolation fixture");
		auto *dataset = globals.datasetWriter.get();
		theme.colony->pause();
		for (int i = 0; i < 30; ++i)
			theme.colony->update(4000000 + i * 40);
		require(globals.datasetWriter.get() == dataset, "restore dataset writer");
		globals.datasetWriter.reset();
		require(std::filesystem::file_size(datasetPath) == 8,
				"menu cannot append training records");
		std::filesystem::remove(datasetPath);
		MenuColony missing;
		require(!missing.load("data/menu/does-not-exist.bin"), "missing asset fallback");
		const auto invalidPath =
			(std::filesystem::temp_directory_path() / "glob2-invalid-colony.bin").string();
		{
			BinaryOutputStream invalid(
				Toolkit::getFileManager()->openOutputStreamBackend(invalidPath));
			invalid.writeText("unsupported-version", "format");
		}
		require(!missing.load(invalidPath), "incompatible asset fallback");
		std::filesystem::remove(invalidPath);
		{
			FrontendScope scope;
			const int actions[] = {
				MainMenuScreen::CUSTOM,           MainMenuScreen::CAMPAIGN,
				MainMenuScreen::LOAD_GAME,        MainMenuScreen::TUTORIAL,
				MainMenuScreen::PLAY_ONLINE, MainMenuScreen::MULTIPLAYERS_LAN,
				MainMenuScreen::GAME_SETUP,       MainMenuScreen::EDITOR,
				MainMenuScreen::CREDITS,          MainMenuScreen::QUIT};
			for (int action : actions)
			{
				Preview<MainMenuScreen> main;
				main.render();
				main.checkBounds();
				main.clickButton("menu/" + std::to_string(action));
				require(main.result() == action, "main mouse route");
			}
			// Tab walks every action in focus order; Return activates the focused one.
			Preview<MainMenuScreen> probe;
			probe.render();
			const auto order = probe.GAGGUI::ui::UIScreen::host().focusOrder();
			require(order.size() == std::size(actions) + (GAGCore::Recording::supported() ? 1 : 0),
					"main menu exposes navigation and recording controls to the keyboard");
			for (size_t i = 0; i < order.size(); ++i)
			{
				Preview<MainMenuScreen> main;
				main.render();
				for (size_t t = 0; t <= i; ++t)
					main.key(SDLK_TAB);
				// The recorder is exercised by GameplayRecording.Integration;
				// this loop verifies routes that leave the menu.
				if (order[i] == "recording/toggle")
					continue;
				main.key(SDLK_RETURN);
				require(main.result() == std::atoi(order[i].c_str() + 5), "main keyboard route");
			}
		}
		{
			FrontendScope scope;
			GAGGUI::ScreenStack screens(*globalContainer->gfx);
			LoadSaveDialog dialog("replays", "replay", false, "Save replay", "",
								  replayFilenameToName, glob2NameToFilename);
			dialog.attach(*globalContainer->gfx);
			dialog.draw(0);
			const auto name = dialog.host().bounds("name");
			dialog.host().tapAt({name.x + name.w / 2, name.y + name.h / 2});
			SDL_Event text{};
			text.type = SDL_EVENT_TEXT_INPUT;
			text.text.text = "colony-review";
			dialog.event(text);
			require(std::string(dialog.getName()) == "colony-review", "replay dialog text entry");
			SDL_Event key{};
			key.type = SDL_EVENT_KEY_DOWN;
			key.key.key = SDLK_BACKSPACE;
			dialog.event(key);
			require(std::string(dialog.getName()) == "colony-revie", "replay dialog editing");
			key.key.key = SDLK_ESCAPE;
			dialog.event(key);
			require(dialog.finished() && dialog.result() == LoadSaveDialog::CANCEL,
					"replay dialog cancellation");
			// A saved premade selection from an earlier run must not replace the random opening map.
			Toolkit::getFileManager()->remove(CustomGamePreferences::filename);
			int result = -1;
			auto owned = std::make_unique<CustomGameScreen>(screens);
			auto *custom = owned.get();
			screens.push(std::move(owned), [&](GAGGUI::Screen &, int code) { result = code; });
			// The lobby opens on a random map (2026-09-14) and previews it on worker threads; drive
			// its frames as the event loop would until the preview's map is loaded.
			for (Uint32 start = SDL_GetTicks();
				 custom->getMapHeader().getNumberOfTeams() == 0 && SDL_GetTicks() - start < 120000;)
			{
				SDL_Delay(10);
				screens.frame(SDL_GetTicks(), {});
			}
			require(custom->getMapHeader().getNumberOfTeams() > 0, "map selection loads teams");
			screens.frame(SDL_GetTicks(), {key});
			for (int i = 0; i < 10 && screens.running(); ++i)
				screens.frame(SDL_GetTicks(), {});
			require(result == CustomGameScreen::CANCEL, "custom game cancellation");
		}
		std::cout << "PASS: presentation, bounded text, timing, isolation, determinism, scoped "
					 "style, fallback\n";
		return 0;
	}
	if (mode == "sessions")
	{
		require(theme.colony->load(), "load session fixture");
		FrontendScope menu;
		for (bool replay : {false, true})
		{
			Engine engine;
			require((replay ? engine.loadReplay("replays/last_game.replay")
							: engine.initCampaign("maps/balanced.map")) == Engine::EE_NO_ERROR,
					"initialize real session");
			SessionExit sequence{0, replay ? 5 : 6, replay};
			globals.replayFastForward = replay;
			const auto timer = SDL_AddTimer(500, exitSession, &sequence);
			require(timer, "session input timer");
			const int result = engine.run();
			SDL_RemoveTimer(timer);
			require(result == Engine::EE_NO_ERROR, "return through results screen");
			require(GAGGUI::Style::style == &theme && FrontendTheme::allowed,
					"game and results restore menu theme");
		}
		globals.replaying = false;
		globals.replayFastForward = false;
		{
			auto editor = std::make_unique<MapEdit>();
			require(editor->load("maps/balanced.map"), "load editor fixture");
			GAGGUI::ScreenStack screens(*globals.gfx);
			screens.push(std::make_unique<MapEditorScreen>(screens, std::move(editor)));
			SessionExit sequence{0, 6};
			const auto timer = SDL_AddTimer(500, exitSession, &sequence);
			require(timer, "editor input timer");
			const int result = screens.execute(40);
			SDL_RemoveTimer(timer);
			require(result == 0, "return from editor");
			require(GAGGUI::Style::style == &theme && FrontendTheme::allowed,
					"editor restores menu theme");
		}
		const auto tick = theme.colony->tick();
		theme.colony->update(SDL_GetTicks());
		require(theme.colony->tick() == tick, "session return has no catch-up burst");
		std::cout << "PASS: game, replay, results and editor return to menu theme\n";
		return 0;
	}
	if (mode == "display")
	{
		require(theme.colony->load(), "load display fixture");
		const auto checksum = theme.colony->checksum();
		const auto rng = getSyncRandState();
		FrontendScope scope;
		for (const auto &size : {std::pair<int, int>{640, 480}, {1920, 1080}, {1152, 720}})
		{
			require(globals.gfx->setRes(size.first, size.second, globals.settings.screenFlags),
					"change logical resolution");
			{
				Preview<MainMenuScreen> screen;
				screen.render();
				screen.checkBounds();
			}
			globals.gfx->nextFrame();
		}
		require(theme.colony->checksum() == checksum && getSyncRandState() == rng,
				"display changes preserve simulation state");
		theme.colony = std::make_unique<MenuColony>();
		for (const auto &size : {std::pair<int, int>{640, 480}, {1920, 1080}})
		{
			require(globals.gfx->setRes(size.first, size.second, globals.settings.screenFlags),
					"resize fallback");
			{
				Preview<MainMenuScreen> screen;
				screen.render();
				screen.checkBounds();
			}
			globals.gfx->nextFrame();
		}
		std::cout << "PASS: live and fallback resolution changes preserve simulation state\n";
		return 0;
	}
	if (mode == "navigation")
	{
		FrontendScope scope;
		GAGGUI::ScreenStack screens(*globals.gfx);
		// Every menu leaves through Escape on the production stack and restores the theme.
		auto navigate = [&](std::unique_ptr<GAGGUI::Screen> screen, const char *what)
		{
			auto *before = GAGGUI::Style::style;
			int result = -1000;
			screens.push(std::move(screen), [&](GAGGUI::Screen &, int code) { result = code; });
			Uint32 tick = SDL_GetTicks();
			screens.frame(tick += 40, {});
			screens.frame(tick += 40, {});
			SDL_Event escape{};
			escape.type = SDL_EVENT_KEY_DOWN;
			escape.key.key = SDLK_ESCAPE;
			screens.frame(tick += 40, {escape});
			for (int i = 0; i < 50 && screens.running(); ++i)
				screens.frame(tick += 40, {});
			require(!screens.running() && result != -1000, what);
			require(GAGGUI::Style::style == before, "screen execution restores theme");
		};
		navigate(std::make_unique<MainMenuScreen>(), "main menu exits");
		navigate(std::make_unique<CampaignMainMenu>(screens), "campaign menu exits");
		navigate(std::make_unique<CampaignSelectorScreen>(), "campaign selector exits");
		navigate(std::make_unique<CampaignMenuScreen>("campaigns/Tutorial_Campaign.txt", screens), "mission list exits");
		navigate(std::make_unique<CustomGameScreen>(screens), "custom game exits");
		navigate(std::make_unique<ChooseMapScreen>("games", "game", true), "load game exits");
		navigate(std::make_unique<SettingsScreen>(), "settings exit");
		navigate(std::make_unique<EditorMainMenu>(screens), "editor menu exits");
		navigate(std::make_unique<NewMapScreen>(), "new map exits");
		navigate(std::make_unique<LANMenuScreen>(screens), "lan menu exits");
		navigate(std::make_unique<LANFindScreen>(screens), "lan find exits");
		navigate(std::make_unique<CreditScreen>(), "credits exit");
		std::cout << "PASS: actual screen loops, keyboard exits, theme restoration\n";
		return 0;
	}
	if (mode == "record")
	{
		require(argc >= 4, "record needs directory and frame count");
		require(theme.colony->load(), "load colony");
		std::filesystem::create_directories(argv[2]);
		FrontendScope scope;
		Preview<MainMenuScreen> menu;
		for (int i = 0; i < std::atoi(argv[3]); ++i)
		{
			theme.colony->update(1000 + i * 40);
			menu.render();
			DrawableSurface shot(globals.gfx->getW(), globals.gfx->getH());
			shot.drawSurface(0, 0, globals.gfx);
			char name[32];
			std::snprintf(name, sizeof(name), "/%04d.png", i);
			require(IMG_SavePNG(shot.getSDLSurface(), (std::string(argv[2]) + name).c_str()),
					"record PNG");
			globals.gfx->nextFrame();
		}
		return 0;
	}
	if (mode == "soak")
	{
		require(theme.colony->load(), "load colony");
		const Uint64 start = SDL_GetTicks(), end = start + std::atoi(argv[2]) * 1000ULL;
		Uint64 frames = 0, cost = 0, worst = 0;
		double updateCost = 0, worstUpdate = 0, worstInput = 0;
		FrontendScope scope;
		Preview<MainMenuScreen> menu;
		while (SDL_GetTicks() < end)
		{
			const auto begin = SDL_GetTicks();
			const auto updateStart = std::chrono::steady_clock::now();
			theme.colony->update(begin);
			const double updateMs = std::chrono::duration<double, std::milli>(
										std::chrono::steady_clock::now() - updateStart)
										.count();
			updateCost += updateMs;
			worstUpdate = std::max(worstUpdate, updateMs);
			menu.render();
			globals.gfx->nextFrame();
			SDL_Event event;
			while (SDL_PollEvent(&event))
			{
			}
			if (frames % 125 == 0)
			{
				const auto inputStart = std::chrono::steady_clock::now();
				menu.clickButton("menu/" + std::to_string(MainMenuScreen::CUSTOM));
				require(menu.result() == MainMenuScreen::CUSTOM, "soak input remains responsive");
				worstInput = std::max(worstInput, std::chrono::duration<double, std::milli>(
													  std::chrono::steady_clock::now() - inputStart)
													  .count());
			}
			const auto elapsed = SDL_GetTicks() - begin;
			cost += elapsed;
			worst = std::max(worst, elapsed);
			++frames;
			if (frames % 1500 == 0)
				std::cout << "elapsed_ms=" << begin - start << " tick=" << theme.colony->tick()
						  << " mean_frame_ms=" << double(cost) / frames << " max_frame_ms=" << worst
						  << std::endl;
			if (elapsed < 40)
				SDL_Delay(40 - elapsed);
		}
		std::cout << "update_mean_ms=" << updateCost / frames << " update_max_ms=" << worstUpdate
				  << " input_dispatch_max_ms=" << worstInput << std::endl;
		std::cout << "SOAK PASS frames=" << frames << " tick=" << theme.colony->tick()
				  << " mean_frame_ms=" << double(cost) / frames << " max_frame_ms=" << worst
				  << std::endl;
		return 0;
	}
	require(false, "unknown mode");
}
