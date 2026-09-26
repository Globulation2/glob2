// SPDX-License-Identifier: GPL-3.0-or-later
// Render production screens with disposable, offline review fixtures.
// Build: scons release=1 mobile-gallery
// Run via tools/mobile_gallery/capture.py; see docs/mobile/development.md.
// Stable capture names must also be documented in mobile_gallery/catalog.json.
#include "GlobalContainer.h"
#include "MainMenuScreen.h"
#include "CampaignMainMenu.h"
#include "CampaignSelectorScreen.h"
#include "CampaignMenuScreen.h"
#include "CampaignEditor.h"
#include "Campaign.h"
#include "AINames.h"
#include "EditorMainMenu.h"
#include "NewMapScreen.h"
#include "ChooseMapScreen.h"
#include "CreditScreen.h"
#include "LANMenuScreen.h"
#include "LANFindScreen.h"
#include "MessageScreen.h"
#include "YOGLoginScreen.h"
#include "YOGRegisterScreen.h"
#include "YOGClient.h"
#include "YOGClientMapUploadScreen.h"
#include "SettingsScreen.h"
#include "CustomGameScreen.h"
#include "CustomGameOtherOptions.h"
#include "StartQualityScreen.h"
#include "LobbyControls.h"
#include "GUIMapPreview.h"
#include "LobbyMapPreview.h"
#include "LandscapePickerScreen.h"
#include "FrontendTheme.h"
#include "Engine.h"
#include "GameGUITouch.h"
#include "GameGUIDialog.h"
#include "GameGUILoadSave.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include "EndGameScreen.h"
// Both legacy headers define these macros; neither definition is used by this tool.
#undef RIGHT_MENU_WIDTH
#undef RIGHT_MENU_OFFSET
#include "MapEdit.h"
#include "PhoneEditor.h"
#include "MapEditDialog.h"
#include "ScriptEditorScreen.h"
#include <ScreenStack.h>
#include <Toolkit.h>
#include <SDL_net.h>
#include <charconv>
#include <stdexcept>
#include <iostream>

GlobalContainer *globalContainer = nullptr;
namespace
{
constexpr Uint32 frameMilliseconds = 40;
Uint32 tick = 0;
bool desktopPresentation = false;
static void frame(GAGGUI::ScreenStack &stack)
{
	stack.frame(tick += frameMilliseconds, {});
}
// Queue before the owning renderer presents: SDL clears its backbuffer after present().
static void queueShot(const std::string &name)
{
	auto *gfx = globalContainer->gfx;
	gfx->printScreen(name + ".bmp");
	std::cout << "CAPTURE " << name << " " << gfx->getW() << " " << gfx->getH() << std::endl;
}
static void stackShot(GAGGUI::ScreenStack &stack, const std::string &name)
{
	// Animation clocks use SDL wall time. Synthetic simulation ticks alone do
	// not settle fades; present throughout the transition before saving pixels.
	const auto started = SDL_GetTicks();
	do
	{
		frame(stack);
		SDL_Delay(16);
	} while (SDL_GetTicks() - started < MapPreview::TransitionDurationMs + 32);
	queueShot(name);
	frame(stack);
}
static void screenShot(GAGGUI::ScreenStack &stack, const std::string &name,
					   std::unique_ptr<GAGGUI::Screen> screen)
{
	auto *ptr = screen.get();
	stack.push(std::move(screen));
	frame(stack);
	frame(stack);
	// Credits begin below the viewport; expose the first page without sleeping.
	if (name == "credits")
		for (int i = 0; i < 450; ++i)
			frame(stack);
	stackShot(stack, name);
	ptr->endExecute(0);
	frame(stack);
}
} // namespace

// Setup friendship is limited to selecting existing model states and reading
// production hit rectangles. Rendering and event dispatch remain in the real UI.
struct MobileGallerySetup
{
	static void run()
	{
		GAGGUI::ScreenStack stack(*globalContainer->gfx);
		captureMenus(stack);
		captureSettings(stack);
		captureLobby(stack);
	}

  private:
	static void captureMenus(GAGGUI::ScreenStack &stack)
	{
		// Keep a real menu beneath modal screens, matching the application's theme lifecycle.
		auto main = std::make_unique<MainMenuScreen>();
		auto *menu = main.get();
		stack.push(std::move(main));
		frame(stack);
		stackShot(stack, "main-menu");
		if (!desktopPresentation)
		{
			menu->more = true;
			stackShot(stack, "main-more");
			menu->more = false;
		}
		screenShot(stack, "campaign-menu", std::make_unique<CampaignMainMenu>(stack));
		screenShot(stack, "campaign-select", std::make_unique<CampaignSelectorScreen>());
		screenShot(stack, "campaign-saves", std::make_unique<CampaignSelectorScreen>(true));
		screenShot(stack, "tutorial-missions",
				   std::make_unique<CampaignMenuScreen>("campaigns/Tutorial_Campaign.txt", stack));
		screenShot(stack, "campaign-editor",
				   std::make_unique<CampaignEditor>("campaigns/Tutorial_Campaign.txt", stack));
		Campaign campaign;
		campaign.load("campaigns/Tutorial_Campaign.txt");
		if (campaign.getMapCount())
			screenShot(stack, "campaign-map-entry",
					   std::make_unique<CampaignMapEntryEditor>(campaign, campaign.getMap(0)));
		screenShot(stack, "editor-menu", std::make_unique<EditorMainMenu>(stack));
		screenShot(stack, "new-map", std::make_unique<NewMapScreen>());
		screenShot(stack, "load-game", std::make_unique<ChooseMapScreen>("games", "game", true));
		screenShot(stack, "load-map", std::make_unique<ChooseMapScreen>("maps", "map", true));
		screenShot(stack, "load-replay",
				   std::make_unique<ChooseMapScreen>("replays", "replay", true));
		screenShot(stack, "credits", std::make_unique<CreditScreen>());
		screenShot(stack, "lan-menu", std::make_unique<LANMenuScreen>(stack));
		screenShot(stack, "lan-find", std::make_unique<LANFindScreen>(stack));
		auto client = std::make_shared<YOGClient>();
		screenShot(stack, "online-login", std::make_unique<YOGLoginScreen>(stack, client));
		screenShot(stack, "online-register", std::make_unique<YOGRegisterScreen>(client));
		screenShot(stack, "map-upload",
				   std::make_unique<YOGClientMapUploadScreen>(stack, client, "maps/balanced.map"));
		screenShot(
			stack, "confirmation",
			std::make_unique<MessageScreen>("Save changes before leaving?",
											std::vector<std::string>{"Save", "Discard", "Cancel"}));
		screenShot(stack, "error-message",
				   std::make_unique<MessageScreen>(
					   "The map could not be loaded. Your current game has been preserved. Please "
					   "choose another file and try again.",
					   std::vector<std::string>{"OK"}));
	}

	// Page Down travels through the production scrolling path. A category with
	// no overflow intentionally has identical top/bottom images.
	static void captureSettings(GAGGUI::ScreenStack &stack)
	{
		const char *categories[] = {"display",   "audio",    "gameplay",
									"buildings", "controls", "player"};
		for (int i = 0; i < 6; ++i)
		{
			if (i == 4 && !desktopPresentation)
				continue;
			auto settings = std::make_unique<SettingsScreen>();
			auto *ptr = settings.get();
			stack.push(std::move(settings));
			frame(stack);
			ptr->selectCategory(SettingsScreen::Category(i));
			frame(stack);
			stackShot(stack, std::string("settings-") + categories[i]);
			SDL_Event pageDown{};
			pageDown.type = SDL_KEYDOWN;
			pageDown.key.keysym.sym = SDLK_PAGEDOWN;
			for (int page = 0; page < 10; ++page)
				stack.frame(tick += frameMilliseconds, {pageDown});
			stackShot(stack, std::string("settings-") + categories[i] + "-bottom");
			if (i == 3 && FrontendLayout::resolve(globalContainer->gfx).phone)
			{
				ptr->activateSetting("buildings.open.0");
				stackShot(stack, "settings-building-detail");
				ptr->selectCategory(SettingsScreen::Category::Buildings);
				ptr->activateSetting("buildings.open." +
									 std::to_string(IntBuildingType::EXPLORATION_FLAG));
				stackShot(stack, "settings-flag-detail");
			}
			ptr->endExecute(0);
			frame(stack);
		}
	}

	static void captureLobby(GAGGUI::ScreenStack &stack)
	{
		auto owned = std::make_unique<CustomGameScreen>(stack);
		auto *lobby = owned.get();
		stack.push(std::move(owned));
		frame(stack);
		auto press = [&](const std::string &id)
		{
			for (const auto &item : lobby->controls->hits)
				if (item.id == id && item.region >= 0)
				{
					auto &region = lobby->controls->regions[item.region];
					if (item.box.y < region.box.y)
						region.offset -= region.box.y - item.box.y;
					else if (item.box.y + item.box.h > region.box.y + region.box.h)
						region.offset += item.box.y + item.box.h - region.box.y - region.box.h;
					break;
				}
			frame(stack);
			for (const auto &item : lobby->controls->hits)
				if (item.id == id)
				{
					auto r = item.box;
					SDL_Event down{};
					down.type = SDL_FINGERDOWN;
					down.tfinger.touchId = 1;
					down.tfinger.fingerId = 1;
					down.tfinger.x = float(r.x + r.w / 2) / globalContainer->gfx->getW();
					down.tfinger.y = float(r.y + r.h / 2) / globalContainer->gfx->getH();
					auto up = down;
					up.type = SDL_FINGERUP;
					if (desktopPresentation)
					{
						down = {};
						down.type = SDL_MOUSEBUTTONDOWN;
						down.button.button = SDL_BUTTON_LEFT;
						down.button.x = r.x + r.w / 2;
						down.button.y = r.y + r.h / 2;
						up = down;
						up.type = SDL_MOUSEBUTTONUP;
					}
					stack.frame(tick += frameMilliseconds, {down, up});
					frame(stack);
					return;
				}
			throw std::runtime_error("Missing control: " + id);
		};
		// A settled premade fixture must not inherit last-session preferences or
		// race the automatic generated-map preview in a freshly opened lobby.
		lobby->setMapMode(false);
		if (!lobby->loadMap("maps/balanced.map"))
			throw std::runtime_error("Premade capture map could not load");
		const auto premadeStarted = SDL_GetTicks();
		do
		{
			frame(stack);
			SDL_Delay(16);
		} while (!lobby->preview->isPresentationSettled() &&
				 SDL_GetTicks() - premadeStarted < 3000);
		if (lobby->setup.random || !lobby->preview->isThumbnailLoaded() ||
			!lobby->preview->isPresentationSettled())
			throw std::runtime_error("Premade capture preview did not settle");
		stackShot(stack, "setup-map");
		// A fixed generator seed makes cross-size map previews comparable.
		lobby->setMapMode(true);
		lobby->chosenSeed = 42;
		lobby->generateMap();
		const auto started = SDL_GetTicks();
		do
		{
			frame(stack);
			SDL_Delay(16);
		} while (lobby->previewBusy() && SDL_GetTicks() - started < 30000);
		if (lobby->previewBusy())
			throw std::runtime_error("Generated preview timed out");
		frame(stack);
		const auto fadeStarted = SDL_GetTicks();
		do
		{
			frame(stack);
			SDL_Delay(16);
		} while (!lobby->preview->isPresentationSettled() && SDL_GetTicks() - fadeStarted < 2000);
		if (!lobby->preview->isPresentationSettled())
			throw std::runtime_error("Generated preview did not finish its transition");
		stackShot(stack, "setup-generated");
		lobby->preview->setState(MapPreview::State::Loading);
		stackShot(stack, "setup-preview-loading");
		lobby->preview->setState(MapPreview::State::Failed);
		stackShot(stack, "setup-preview-error");
		// Loading/Failed intentionally discard pixels. Restore the actual snapshot,
		// not just the Ready enum, before capturing any subsequent child pages.
		lobby->preview->setMapThumbnail(lobby->sourceFile());
		const auto restored = SDL_GetTicks();
		do
		{
			frame(stack);
			SDL_Delay(16);
		} while (!lobby->preview->isPresentationSettled() && SDL_GetTicks() - restored < 3000);
		if (!lobby->preview->isThumbnailLoaded() || !lobby->preview->isPresentationSettled())
			throw std::runtime_error("Fixture failed to restore the preview after error states");
		if (FrontendLayout::resolve(globalContainer->gfx).phone)
		{
			lobby->phonePage = CustomGameScreen::PhonePage::MapSettings;
			stackShot(stack, "setup-map-settings");
			lobby->phonePage = CustomGameScreen::PhonePage::Main;
		}
		// Open through the shared action: its button can be below the fold in
		// short landscape layouts. This tool captures states, not navigation tests.
		auto *picker = lobby->chooseLandscape();
		frame(stack);
		const auto pickerStarted = SDL_GetTicks();
		do
		{
			frame(stack);
			SDL_Delay(16);
		} while (!picker->presentationSettled() && SDL_GetTicks() - pickerStarted < 45000);
		if (!picker->presentationSettled())
			throw std::runtime_error("Landscape previews did not settle within 45 seconds");
		stackShot(stack, "landscape-picker");
		if (FrontendLayout::resolve(globalContainer->gfx).phone)
		{
			picker->settingsOpen = true;
			stackShot(stack, "landscape-settings");
			picker->settingsOpen = false;
		}
		SDL_Event escape{};
		escape.type = SDL_KEYDOWN;
		escape.key.keysym.sym = SDLK_ESCAPE;
		stack.frame(tick += frameMilliseconds, {escape});
		frame(stack);
		press("tab/1");
		stackShot(stack, "setup-players");
		press("colony/0/controller");
		stackShot(stack, "setup-controller");
		lobby->controls->popup.open = false;
		frame(stack);
		press("tab/2");
		if (FrontendLayout::resolve(globalContainer->gfx).phone)
		{
			stackShot(stack, "setup-review");
			lobby->phonePage = CustomGameScreen::PhonePage::Rules;
		}
		stackShot(stack, "setup-rules");
		std::vector<std::string> aiChoices;
		for (int id : AINames::selectionOrder())
			aiChoices.push_back(AINames::getAISelectorText(id));
		screenShot(stack, "ai-profile",
				   std::make_unique<CustomGameChoiceScreen>("AI profile", aiChoices, 0, true,
															std::vector<bool>{}));
		screenShot(stack, "setup-options",
				   std::make_unique<CustomGameOtherOptions>(lobby->getGameHeader(),
															lobby->getMapHeader(), false));
		std::vector<std::string> qualityNames;
		std::vector<GAGCore::Color> qualityColors;
		for (size_t i = 0; i < lobby->quality.colonies.size(); ++i)
		{
			qualityNames.push_back(lobby->colonyLabel(i));
			if (i < lobby->preview->starts.size())
				qualityColors.push_back(lobby->preview->starts[i].color);
		}
		auto report =
			std::make_unique<StartQualityScreen>(lobby->quality, qualityNames, qualityColors);
		auto *reportPtr = report.get();
		stack.push(std::move(report));
		frame(stack);
		stackShot(stack, "start-quality");
		if (FrontendLayout::resolve(globalContainer->gfx).phone)
		{
			reportPtr->expanded.insert(0);
			stackShot(stack, "start-quality-colony");
		}
		reportPtr->endExecute(0);
		frame(stack);
	}
};
// Gameplay/editor fixtures select otherwise hard-to-reach panels directly.
// They never advance simulation, submit saves, or execute queued game orders.
class MobileGalleryGameplay
{
  public:
	static void run()
	{
		captureGame();
		captureEditor();
	}

  private:
	static void captureGame()
	{
		auto *gfx = globalContainer->gfx;
		gfx->setResponsiveViewport(true, 800, 600);
		// Use bundled content, so the tool does not depend on personal saves.
		GameGUI gui;
		auto map = Engine::loadMapHeader("maps/balanced.map");
		GameHeader players;
		players.setNumberOfPlayers(1);
		players.getBasePlayer(0) = BasePlayer(0, "Review colony", 0, BasePlayer::P_LOCAL);
		if (!gui.loadFromHeaders(map, players, true, true))
			throw std::runtime_error("Map fixture failed");
		gui.localTeamNo = 0;
		gui.localPlayer = 0;
		gui.adjustLocalTeam();
		gui.game.map.setMapDiscovered();
		gui.viewportX = gui.viewportY = 0;
		auto capture = [&](const std::string &name)
		{
			queueShot(name);
			gui.drawAll(0);
			gfx->nextFrame();
		};
		gui.clearSelection();
		gui.touch->panelOpen = false;
		capture("game-map");
		gui.scriptText = "Build an inn to feed your workers. Select Build, choose an inn, then "
						 "place it beside your colony. Confirm the preview to begin construction.";
		capture("game-tutorial");
		gui.scriptText.clear();
		gui.displayMode = GameGUI::CONSTRUCTION_VIEW;
		gui.touch->panelOpen = true;
		gui.touch->panelScroll = 0;
		capture("game-build");
		gui.touch->panelScroll = 10000;
		capture("game-build-bottom");
		gui.displayMode = GameGUI::FLAG_VIEW;
		gui.touch->panelScroll = 0;
		capture("game-flags");
		gui.touch->panelOpen = false;
		gui.setSelection(GameGUI::TOOL_SELECTION, const_cast<char *>("inn"));
		gui.touch->preview = GAGCore::ViewPoint{192, 192};
		gui.touch->previewType = "inn";
		// Desktop previews follow the pointer rather than the touch confirmation UI.
		if (desktopPresentation)
		{
			gui.mouseX = 192;
			gui.mouseY = 192;
		}
		capture("game-placement");
		gui.touch->cancel();
		gui.clearSelection();
		Building *building = nullptr;
		for (int i = 0; i < Building::MAX_COUNT; ++i)
			if (gui.localTeam->myBuildings[i])
			{
				building = gui.localTeam->myBuildings[i];
				break;
			}
		if (building)
		{
			gui.setSelection(GameGUI::BUILDING_SELECTION, building);
			gui.touch->panelOpen = true;
			for (int i : gui.touch->allocationTabs())
			{
				gui.touch->allocationTab = i;
				capture("game-inspector-" + std::to_string(i));
			}
		}
		gui.clearSelection();
		gui.touch->panelOpen = false;
		auto dialog = [&](const std::string &name, GameGUI::InGameMenu mode,
						  std::unique_ptr<GAGGUI::OverlayScreen> screen)
		{
			gui.inGameMenu = mode;
			gui.gameMenuScreen = std::move(screen);
			gui.touch->dialogScroll = 0;
			capture(name);
			gui.inGameMenu = GameGUI::IGM_NONE;
			gui.gameMenuScreen.reset();
		};
		dialog("game-pause", GameGUI::IGM_MAIN, std::make_unique<InGameMainScreen>());
		dialog("game-options", GameGUI::IGM_OPTION, std::make_unique<InGameOptionScreen>(&gui));
		dialog("game-alliances", GameGUI::IGM_ALLIANCE,
			   std::make_unique<InGameAllianceScreen>(&gui));
		dialog("game-objectives", GameGUI::IGM_OBJECTIVES,
			   std::make_unique<InGameObjectivesScreen>(&gui, false));
		dialog("game-briefing", GameGUI::IGM_OBJECTIVES,
			   std::make_unique<InGameObjectivesScreen>(&gui, true));
		dialog("game-save", GameGUI::IGM_SAVE,
			   std::make_unique<LoadSaveScreen>("games", "game", false, "Save game", "Review game",
												glob2FilenameToName, glob2NameToFilename));
		dialog("game-load", GameGUI::IGM_LOAD,
			   std::make_unique<LoadSaveScreen>("games", "game", true, "Load game", "",
												glob2FilenameToName, glob2NameToFilename));
		dialog("game-victory", GameGUI::IGM_END_OF_GAME,
			   std::make_unique<InGameEndOfGameScreen>("Victory", true));
		dialog("replay-menu", GameGUI::IGM_MAIN, std::make_unique<InGameMainScreen>(true));
		gui.typingInputScreen = new InGameTextInput(gfx);
		gui.typingInputScreen->setText("Meet at the northern crossing.");
		// Desktop normally animates this panel into view over several frames.
		if (desktopPresentation)
			gui.typingInputScreenPos = TYPING_INPUT_MAX_POS;
		capture("game-chat");
		delete gui.typingInputScreen;
		gui.typingInputScreen = nullptr;
		{
			GAGGUI::ScreenStack stack(*gfx);
			screenShot(stack, "game-results", std::make_unique<EndGameScreen>(&gui));
		}
	}

	static void captureEditor()
	{
		// Match MapEditorScreen's viewport policy, including legacy map scaling.
		globalContainer->gfx->setResponsiveViewport(true, 800, 600);
		MapEdit editor;
		if (!editor.load("maps/balanced.map"))
			throw std::runtime_error("Editor fixture failed");
		editor.beginEditing();
		if (bool(editor.phone) == desktopPresentation)
			throw std::runtime_error("Unexpected editor presentation");
		auto editCapture = [&](const std::string &name)
		{
			queueShot(name);
			editor.drawEditing();
		};
		editCapture("editor-map");
		// Desktop keeps tools in its sidebar; top/bottom views intentionally
		// coincide when their mobile equivalents are separate scrolling states.
		if (editor.phone)
			editor.phone->tools = true;
		editCapture("editor-tools");
		if (editor.phone)
			editor.phone->offset = 10000;
		editCapture("editor-tools-bottom");
		for (const auto &category : {"flag", "terrain", "teams"})
		{
			editor.performAction(std::string("switch to ") + category + " view");
			if (editor.phone)
				editor.phone->offset = 0;
			editCapture(std::string("editor-palette-") + category);
		}
		if (editor.phone)
			editor.phone->tools = false;
		// MapEdit's legacy overlay fields borrow raw pointers. Keep ownership here
		// and detach the phone form before destroying each overlay, even on error.
		auto withOverlay =
			[&]<typename T>(std::unique_ptr<T> screen, T *&slot, bool &visible, auto capture)
		{
			slot = screen.get();
			visible = true;
			struct Detach
			{
				MapEdit &editor;
				T *&slot;
				bool &visible;
				~Detach()
				{
					if (editor.phone)
						editor.phone->closeOverlay();
					slot = nullptr;
					visible = false;
				}
			} detach{editor, slot, visible};
			capture();
		};
		withOverlay(std::make_unique<MapEditMenuScreen>(), editor.menuScreen,
					editor.showingMenuScreen, [&] { editCapture("editor-pause"); });
		withOverlay(std::make_unique<TeamsEditor>(&editor.game), editor.teamsEditor,
					editor.showingTeamsEditor, [&] { editCapture("editor-teams"); });
		withOverlay(std::make_unique<ScriptEditorScreen>(&editor.game), editor.scriptEditor,
					editor.showingScriptEditor,
					[&]
					{
						editCapture("editor-script");
						for (auto [tab, name] :
							 {std::pair{ScriptEditorScreen::TAB_OBJECTIVES, "objectives"},
							  {ScriptEditorScreen::TAB_BRIEFING, "briefing"},
							  {ScriptEditorScreen::TAB_HINTS, "hints"}})
						{
							editor.scriptEditor->onAction(nullptr, GAGGUI::BUTTON_RELEASED, tab, 0);
							editCapture(std::string("editor-script-") + name);
						}
					});
		withOverlay(std::make_unique<LoadSaveScreen>("maps", "map", false, "Save map", "Review map",
													 glob2FilenameToName, glob2NameToFilename),
					editor.loadSaveScreen, editor.showingSave, [&] { editCapture("editor-save"); });
		withOverlay(std::make_unique<LoadSaveScreen>("maps", "map", true, "Load map", "",
													 glob2FilenameToName, glob2NameToFilename),
					editor.loadSaveScreen, editor.showingLoad, [&] { editCapture("editor-load"); });
	}
};
namespace
{
int dimension(const char *value)
{
	int result = 0;
	const std::string_view text(value);
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
	if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result < 240 ||
		result > 4096)
		throw std::invalid_argument("Viewport dimensions must be integers between 240 and 4096");
	return result;
}
} // namespace

int main(int argc, char **argv)
{
	if ((argc != 3 && argc != 4) || !SDL_getenv("GLOB2_USER_DATA_DIR"))
	{
		std::cerr
			<< "Set GLOB2_USER_DATA_DIR; usage: mobile-gallery WIDTH HEIGHT [compact|desktop]\n";
		return 2;
	}
	desktopPresentation = argc == 4 && std::string_view(argv[3]) == "desktop";
	if (argc == 4 && !desktopPresentation && std::string_view(argv[3]) != "compact")
	{
		std::cerr << "Presentation must be compact or desktop\n";
		return 2;
	}
	SDL_setenv("GLOB2_MOBILE_UI", desktopPresentation ? "0" : "1", 1);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	try
	{
		const int width = dimension(argv[1]), height = dimension(argv[2]);
		auto globals = std::make_unique<GlobalContainer>("glob2-mobile-gallery");
		globalContainer = globals.get();
		auto &settings = globalContainer->settings;
		// Desktop's fixed logical canvas must match its requested window; setting
		// only the window size would magnify an 800x600 layout instead.
		settings.screenWidth = desktopPresentation ? width : 800;
		settings.screenHeight = desktopPresentation ? height : 600;
		settings.screenFlags =
			GAGCore::GraphicContext::PORTABLEGPU | GAGCore::GraphicContext::RESIZABLE;
		settings.mute = true;
		globalContainer->load();
		settings.setUsername("Review player");
		if (SDLNet_Init() != 0)
			throw std::runtime_error(SDLNet_GetError());
		SDL_SetWindowSize(SDL_GetWindowFromID(globalContainer->gfx->windowID()), width, height);
		SDL_Event resize{};
		resize.type = SDL_WINDOWEVENT;
		resize.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
		GAGCore::GraphicContext::translateMouseEvent(&resize);
		{
			FrontendTheme theme;
			MobileGallerySetup::run();
			MobileGalleryGameplay::run();
		}
		globals.reset();
		globalContainer = nullptr;
		SDLNet_Quit();
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		return 1;
	}
}
