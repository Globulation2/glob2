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
#include "GUIGlob2FileList.h"
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
#include "ReplayWriter.h"
#include "ReplayReader.h"
#include "Utilities.h"
#include "Order.h"
#include "Unit.h"
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
		if (SDL_getenv("GLOB2_GALLERY_EDITOR_ONLY"))
		{
			stack.push(std::make_unique<MainMenuScreen>());
			frame(stack);
			screenShot(stack, "editor-menu", std::make_unique<EditorMainMenu>(stack));
			captureMapCreation(stack);
			captureMapLibrary(stack);
			captureCampaigns(stack);
			return;
		}
		captureMenus(stack);
		captureSettings(stack);
		captureLobby(stack);
	}

  private:
	static void captureCampaigns(GAGGUI::ScreenStack &stack)
	{
		auto owned = std::make_unique<CampaignEditor>("campaigns/Tutorial_Campaign.txt", stack);
		auto *editor = owned.get();
		// Extra missions make list navigation and prerequisite overflow visible.
		for (int i = 0; i < 10; ++i)
		{
			const auto name = "Frontier mission " + std::to_string(i + 1);
			CampaignMapEntry extra(name, "maps/balanced.map");
			editor->campaign.appendMap(extra);
			editor->mapList->addText(name);
		}
		stack.push(std::move(owned));
		frame(stack);
		stackShot(stack, "campaign-editor");
		editor->onAction(editor->mapsTab, BUTTON_RELEASED, 101, 0);
		stackShot(stack, "campaign-editor-maps");
		Campaign campaign = editor->campaign;
		editor->endExecute(CampaignEditor::CANCEL);
		frame(stack);
		if (!campaign.getMapCount())
			return;
		auto &entry = campaign.getMap(0);
		entry.setDescription(
			"Guide your colony into the frontier.\n\nPlace an inn, assign workers, and gather "
			"enough food for your first expansion.\n\nKeep the village supplied while scouts find "
			"a safe route to the next valley.");
		entry.getUnlockedByMaps().push_back(campaign.getMap(1).getMapName());
		auto mission = std::make_unique<CampaignMapEntryEditor>(campaign, entry);
		auto *view = mission.get();
		stack.push(std::move(mission));
		frame(stack);
		stackShot(stack, "campaign-map-entry");
		view->onAction(view->unlockTab, BUTTON_RELEASED, 101, 0);
		stackShot(stack, "campaign-map-unlocking");
		view->onAction(view->detailsTab, BUTTON_RELEASED, 100, 0);
		frame(stack);
		if (!desktopPresentation)
		{
			const auto bounds = view->descriptionEditor->getScreenRect();
			const int x = bounds.x, y = bounds.y;
			SDL_Event down{};
			down.type = SDL_FINGERDOWN;
			down.tfinger.fingerId = 1;
			down.tfinger.x = float(x + 12) / globalContainer->gfx->getW();
			down.tfinger.y = float(y + 12) / globalContainer->gfx->getH();
			auto up = down;
			up.type = SDL_FINGERUP;
			stack.frame(tick += frameMilliseconds, {down, up});
		}
		stackShot(stack, "campaign-description-editing");
		SDL_StopTextInput();
		view->endExecute(CampaignMapEntryEditor::CANCEL);
		frame(stack);
	}

	static void captureMapLibrary(GAGGUI::ScreenStack &stack)
	{
		auto chooser = std::make_unique<ChooseMapScreen>("maps", "map", true);
		auto *screen = chooser.get();
		stack.push(std::move(chooser));
		frame(stack);
		if (screen->fileList->getCount() > 0)
		{
			int index = 0;
			for (unsigned i = 0; i < screen->fileList->getCount(); ++i)
				if (screen->fileList->getText(i) == "balanced")
					index = int(i);
			screen->fileList->setSelectionIndex(index);
			screen->onAction(screen->fileList, LIST_ELEMENT_SELECTED, index, 0);
		}
		stackShot(stack, "load-map");
		screen->endExecute(ChooseMapScreen::CANCEL);
		frame(stack);
	}
	static void captureMapCreation(GAGGUI::ScreenStack &stack)
	{
		auto blank = std::make_unique<NewMapScreen>(GeneratorRegistry::builtins(), &stack);
		blank->descriptor.setMethodDefaults(GenerationRequest::eUNIFORM);
		blank->updateControls();
		screenShot(stack, "new-map", std::move(blank));
		auto screen = std::make_unique<NewMapScreen>(GeneratorRegistry::builtins(), &stack);
		auto *creation = screen.get();
		creation->descriptor.setMethodDefaults(GenerationRequest::eRIVER);
		creation->updateControls();
		stack.push(std::move(screen));
		frame(stack);
		frame(stack);
		stackShot(stack, "new-map-generated");
		creation->parameters = true;
		stackShot(stack, "new-map-parameters");
		creation->parameters = false;
		auto *picker = creation->chooseLandscape();
		frame(stack);
		const auto started = SDL_GetTicks();
		do
		{
			frame(stack);
			SDL_Delay(16);
		} while (!picker->presentationSettled() && SDL_GetTicks() - started < 45000);
		if (!picker->presentationSettled())
			throw std::runtime_error("Editor landscape previews did not settle");
		stackShot(stack, "editor-landscapes");
		picker->endExecute(LandscapePickerScreen::CANCEL);
		frame(stack);
		creation->endExecute(NewMapScreen::CANCEL);
		frame(stack);
	}

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
		captureCampaigns(stack);
		screenShot(stack, "editor-menu", std::make_unique<EditorMainMenu>(stack));
		captureMapCreation(stack);
		screenShot(stack, "load-game", std::make_unique<ChooseMapScreen>("games", "game", true));
		captureMapLibrary(stack);
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
// Gameplay advances a deterministic match to populate history and exports a replay
// into the disposable profile. It never submits chat or writes personal saves.
class MobileGalleryGameplay
{
  public:
	static void run()
	{
		if (SDL_getenv("GLOB2_GALLERY_EDITOR_ONLY"))
		{
			captureEditor();
			return;
		}
		captureGame();
		if (!SDL_getenv("GLOB2_GALLERY_GAME_ONLY"))
			captureEditor();
	}

  private:
	static void captureGame()
	{
		auto *gfx = globalContainer->gfx;
		gfx->setResponsiveViewport(true, 800, 600);
		// Use bundled content, so the tool does not depend on personal saves.
		setSyncRandSeed(0x474c4f42);
		GameGUI gui;
		auto map = Engine::loadMapHeader("maps/balanced.map");
		GameHeader players;
		players.setNumberOfPlayers(3);
		players.setAllyTeamsFixed(false);
		players.setRandomSeed(0x474c4f42);
		players.getBasePlayer(0) = BasePlayer(0, "Amber colony", 0, BasePlayer::P_LOCAL);
		players.getBasePlayer(1) = BasePlayer(1, "Violet colony", 1, BasePlayer::P_LOCAL);
		players.getBasePlayer(2) = BasePlayer(2, "Jade colony", 2, BasePlayer::P_LOCAL);
		if (!gui.loadFromHeaders(map, players, true, true))
			throw std::runtime_error("Map fixture failed");
		gui.localTeamNo = 0;
		gui.localPlayer = 0;
		gui.adjustLocalTeam();
		// Real simulation history avoids misleading empty chart captures. Each
		// viewport starts from the same bundled map and executes the same ticks.
		globalContainer->replayWriter = std::make_unique<ReplayWriter>();
		globalContainer->replayWriter->init("", gui);
		for (int tick = 0; tick < 4096; ++tick)
		{
			gui.game.syncStep(0);
			globalContainer->replayWriter->advanceStep();
		}
		globalContainer->replayWriter->finish();
		if (!globalContainer->replayWriter->write("replays/gallery-match.replay"))
			throw std::runtime_error("Replay fixture write failed");
		gui.game.missionBriefing =
			"Establish a sustainable colony, then secure the northern crossing. Coordinate with "
			"Violet while protecting the food supply.";
		gui.game.objectives.addNewObjective("Establish an inn", false, true, false,
											GameObjectives::Primary, 1);
		gui.game.objectives.addNewObjective("Secure the northern crossing", false, false, false,
											GameObjectives::Primary, 2);
		gui.game.objectives.addNewObjective("Explore the eastern island", false, false, false,
											GameObjectives::Secondary, 3);
		gui.game.map.setMapDiscovered();
		gui.updateCamera();
		gui.viewportX =
			(gui.localTeam->startPosX - int(gui.camera.visibleW() / 64)) & gui.game.map.getMaskW();
		gui.viewportY =
			(gui.localTeam->startPosY - int(gui.camera.visibleH() / 64)) & gui.game.map.getMaskH();
		gui.updateCamera();
		auto capture = [&](const std::string &name)
		{
			queueShot(name);
			gui.drawAll(0);
			gfx->nextFrame();
		};
		gui.clearSelection();
		gui.touch->panelOpen = false;
		capture("game-map");
		std::cout << "FIXTURE_CHECKSUM " << gui.game.checkSum() << std::endl;
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
		gui.setSelection(GameGUI::BRUSH_SELECTION);
		gui.brush.defaultSelection();
		gui.toolManager.activateZoneTool(GameGUIToolManager::ZoneType(1));
		gui.touch->panelOpen = false;
		if (!desktopPresentation)
		{
			// Capture a real, unfinished paint gesture. Cancelling afterwards
			// keeps later comparison states identical and emits no zone orders.
			for (int step = 0; step <= 8; ++step)
			{
				SDL_Event event{};
				event.type = step ? SDL_FINGERMOTION : SDL_FINGERDOWN;
				event.tfinger.touchId = 8;
				event.tfinger.fingerId = 1;
				event.tfinger.x = .20f + step * .035f;
				event.tfinger.y = .50f + std::sin(step * .4f) * .08f;
				gui.processEvent(&event);
				capture("gesture-zone-0" + std::to_string(step));
			}
		}
		capture("game-zone-paint");
		gui.touch->cancel();
		gui.clearSelection();
		gui.displayMode = GameGUI::STAT_TEXT_VIEW;
		gui.touch->panelOpen = true;
		capture("game-tactical-tools");
		gui.displayMode = GameGUI::FLAG_VIEW;
		gui.touch->panelOpen = false;
		gui.setSelection(GameGUI::TOOL_SELECTION, const_cast<char *>("inn"));
		const auto mapBounds = gui.touch->worldBounds();
		const GAGCore::ViewPoint previewPoint{mapBounds.x + mapBounds.w / 3,
											  mapBounds.y + mapBounds.h / 2};
		gui.touch->preview =
			GAGCore::ViewPoint{double(gui.mapMouseX(previewPoint.x) + gui.viewportX * 32),
							   double(gui.mapMouseY(previewPoint.y) + gui.viewportY * 32)};
		gui.touch->previewType = "inn";
		// Desktop previews follow the pointer rather than the touch confirmation UI.
		if (desktopPresentation)
		{
			gui.mouseX = int(previewPoint.x);
			gui.mouseY = int(previewPoint.y);
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
			// Stable review IDs now compare the same unified inspector.
			for (int i : {0, 1, 3, 4})
			{
				capture("game-inspector-" + std::to_string(i));
			}
		}
		auto inspect = [&](const std::string &name, Building *selected)
		{
			if (!selected)
				throw std::runtime_error("Missing inspector fixture: " + name);
			gui.setSelection(GameGUI::BUILDING_SELECTION, selected);
			gui.touch->panelOpen = true;
			gui.touch->actionScroll = 0;
			capture(name);
		};
		Building *production = nullptr;
		Building *enemy = nullptr;
		for (int i = 0; i < Building::MAX_COUNT; ++i)
		{
			auto *own = gui.localTeam->myBuildings[i];
			if (own && own->type->unitProductionTime)
				production = own;
			if (gui.game.teams[1]->myBuildings[i])
				enemy = gui.game.teams[1]->myBuildings[i];
		}
		inspect("game-inspector-production", production);
		inspect("game-inspector-enemy", enemy);
		const int hp = building->hp;
		building->hp = std::max(1, hp / 2);
		inspect("game-inspector-damaged", building);
		building->hp = hp;
		gui.clearSelection();
		gui.touch->panelOpen = false;
		auto dialog = [&](const std::string &name, GameGUI::InGameMenu mode,
						  std::unique_ptr<GAGGUI::OverlayScreen> screen)
		{
			gui.inGameMenu = mode;
			if (mode == GameGUI::IGM_OBJECTIVES)
				gui.touch->objectivePage = name == "game-briefing" ? 0 : 1;
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
		gui.localTeam->hasWon = true;
		dialog("game-victory", GameGUI::IGM_END_OF_GAME,
			   std::make_unique<InGameEndOfGameScreen>("Victory", true));
		gui.localTeam->hasWon = false;
		globalContainer->replayReader = std::make_unique<ReplayReader>();
		if (!globalContainer->replayReader->loadReplay("replays/gallery-match.replay"))
			throw std::runtime_error("Replay fixture read failed");
		for (int tick = 0; tick < 4096; ++tick)
			globalContainer->replayReader->advanceStep();
		globalContainer->replaying = true;
		globalContainer->replayVisibleTeams = gui.localTeam->me;
		dialog("replay-menu", GameGUI::IGM_MAIN, std::make_unique<InGameMainScreen>(true));
		gui.displayMode = GameGUI::STAT_TEXT_VIEW;
		gui.replayDisplayMode = GameGUI::RDM_STAT_TEXT_VIEW;
		gui.touch->panelOpen = true;
		capture("replay-controls");
		gui.touch->panelOpen = false;
		globalContainer->replaying = false;
		gui.typingInputScreen = new InGameTextInput(gfx);
		gui.typingInputScreen->setText("Meet at the northern crossing.");
		// Desktop normally animates this panel into view over several frames.
		if (desktopPresentation)
			gui.typingInputScreenPos = TYPING_INPUT_MAX_POS;
		capture("game-chat");
		delete gui.typingInputScreen;
		gui.typingInputScreen = nullptr;
		if (!desktopPresentation)
		{
			// Record actual SDL pointer dispatch through production placement.
			// This is native-host evidence, not a claim of physical-device input.
			gui.clearSelection();
			gui.displayMode = GameGUI::CONSTRUCTION_VIEW;
			gui.touch->panelOpen = true;
			gui.touch->panelScroll = 0;
			gui.updateCamera();
			const auto bounds = gui.touch->worldBounds(), palette = gui.touch->layout().panel;
			const double unit = gfx->logicalUnitsPerPoint();
			std::optional<GAGCore::ViewPoint> drop;
			const auto *type = globalContainer->buildingsTypes.get(
				globalContainer->buildingsTypes.getPlaceableTypeNum("inn"));
			for (double y = bounds.y + 104 * unit; y < bounds.y + bounds.h - 8 * unit && !drop;
				 y += 16 * unit)
				for (double x = bounds.x + 32 * unit; x < bounds.x + bounds.w - 16 * unit && !drop;
					 x += 16 * unit)
				{
					const GAGCore::ViewPoint p{x, y};
					if (palette.contains(p) || gui.touch->minimapRect().contains(p))
						continue;
					int mx, my;
					gui.game.map.cursorToBuildingPos(gui.mapMouseX(x), gui.mapMouseY(y - 48 * unit),
													 type->width, type->height, &mx, &my,
													 gui.viewportX, gui.viewportY);
					if (gui.game.checkHardRoomForBuilding(mx, my, type, &mx, &my))
						drop = p;
				}
			if (!drop)
				throw std::runtime_error("No visible construction site for gesture fixture");
			const auto index =
				std::find(gui.buildingsChoiceName.begin(), gui.buildingsChoiceName.end(), "inn") -
				gui.buildingsChoiceName.begin();
			const auto icon = gui.touch->paletteItemRect(index);
			const GAGCore::ViewPoint start{icon.x + icon.w / 2, icon.y + icon.h / 2};
			auto pointer = [&](Uint32 eventType, GAGCore::ViewPoint p)
			{
				SDL_Event event{};
				event.type = eventType;
				event.tfinger.touchId = 19;
				event.tfinger.fingerId = 1;
				event.tfinger.x = p.x / gfx->getW();
				event.tfinger.y = p.y / gfx->getH();
				gui.processEvent(&event);
			};
			pointer(SDL_FINGERDOWN, start);
			for (int frame = 0; frame < 12; ++frame)
			{
				const double progress = frame / 11.0;
				const GAGCore::ViewPoint p{start.x + (drop->x - start.x) * progress,
										   start.y + (drop->y - start.y) * progress};
				if (frame)
					pointer(SDL_FINGERMOTION, p);
				const auto name =
					std::string("gesture-build-") + (frame < 10 ? "0" : "") + std::to_string(frame);
				queueShot(name);
				gui.drawAll(0);
				gfx->drawCircle(int(p.x), int(p.y), int(10 * unit), GAGCore::Color(255, 220, 100));
				gfx->nextFrame();
			}
			pointer(SDL_FINGERUP, *drop);
			auto order = std::dynamic_pointer_cast<OrderCreate>(gui.toolManager.getOrder());
			if (!order || gui.toolManager.getOrder())
				throw std::runtime_error(
					"Gesture fixture did not produce exactly one construction order");
			capture("gesture-build-12");
			gui.ghostManager.removeBuilding(order->posX, order->posY);
			gui.clearSelection();
			gui.touch->panelOpen = false;
		}
		{
			class ResultsFixture : public EndGameScreen
			{
			  public:
				using EndGameScreen::EndGameScreen;
				void showFilters() { teamFiltersOpen = true; }
				void inspectValue()
				{
					teamFiltersOpen = false;
					statWidget->inspectScreenPoint(globalContainer->gfx->getW() / 2,
												   globalContainer->gfx->getH() / 2);
				}
			};
			GAGGUI::ScreenStack stack(*gfx);
			auto results = std::make_unique<ResultsFixture>(&gui);
			auto *view = results.get();
			stack.push(std::move(results));
			stackShot(stack, "game-results");
			view->showFilters();
			stackShot(stack, "game-results-filters");
			view->inspectValue();
			stackShot(stack, "game-results-value");
			view->endExecute(0);
			frame(stack);
		}
	}

	static void captureEditor()
	{
		// Match MapEditorScreen's viewport policy, including legacy map scaling.
		globalContainer->gfx->setResponsiveViewport(true, 800, 600);
		MapEdit editor;
		if (!editor.load("maps/balanced.map"))
			throw std::runtime_error("Editor fixture failed");
		// Populated editable content distinguishes real text canvases from
		// empty placeholders. These fixtures are never saved to user maps.
		editor.game.missionBriefing = "Restore the valley\n\nBuild a sustainable colony near the "
									  "river.\nKeep a route open to the northern grove.";
		editor.game.objectives.addNewObjective("Establish a colony beside the river.", false, false,
											   false, GameObjectives::Primary, 1);
		editor.game.gameHints.addNewHint("Paint a narrow crossing through the shallows.\nLeave "
										 "room for workers to reach the trees.",
										 false, 1);
		editor.game.sgslScript.sourceCode =
			"# Valley introduction\nGuiDisable(AllianceScreen)\nGuiDisable(FlagTab)\n";
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
		{
			auto &phone = *editor.phone;
			auto *gfx = globalContainer->gfx;
			const double unit = gfx->logicalUnitsPerPoint();
			auto pointer = [&](Uint32 kind, GAGCore::ViewPoint p)
			{
				SDL_Event event{};
				event.type = kind;
				event.tfinger.touchId = 27;
				event.tfinger.fingerId = 1;
				event.tfinger.x = p.x / gfx->getW();
				event.tfinger.y = p.y / gfx->getH();
				editor.advanceEditing({event}, 0);
			};
			phone.chooseMode(2);
			phone.prepare();
			editor.updateCamera();
			const auto icon = phone.rows.at(1).rect; // inn, stable production palette order
			const GAGCore::ViewPoint from{icon.x + icon.w / 2, icon.y + icon.h / 2};
			std::optional<GAGCore::ViewPoint> drop;
			const auto *type = globalContainer->buildingsTypes.get(
				globalContainer->buildingsTypes.getTypeNum("inn", 0, false));
			// Bring an existing legal footprint into the map window. A fixed
			// camera can open over sea at narrow aspect ratios.
			bool room = false;
			for (int y = 0; y < editor.game.map.getH() && !room; ++y)
				for (int x = 0; x < editor.game.map.getW(); ++x)
				{
					int tx, ty;
					if (editor.game.checkRoomForBuilding(x, y, type, &tx, &ty, editor.team, false))
					{
						int dx, dy;
						editor.game.map.cursorToBuildingPos(
							editor.mapMouseX(phone.content.x + phone.content.w / 2),
							editor.mapMouseY(phone.content.y + phone.content.h / 2 - 40 * unit),
							type->width, type->height, &dx, &dy, 0, 0);
						editor.viewportX = (x - dx) & editor.game.map.wMask;
						editor.viewportY = (y - dy) & editor.game.map.hMask;
						room = true;
						break;
					}
				}
			if (!room)
				throw std::runtime_error("No legal editor building footprint");
			editor.updateCamera();
			const GAGCore::ViewPoint centered{phone.content.x + phone.content.w / 2,
											  phone.content.y + phone.content.h / 2};
			int cx, cy, tx, ty;
			editor.game.map.cursorToBuildingPos(
				editor.mapMouseX(centered.x), editor.mapMouseY(centered.y - 40 * unit), type->width,
				type->height, &cx, &cy, editor.viewportX, editor.viewportY);
			if (editor.game.checkRoomForBuilding(cx, cy, type, &tx, &ty, editor.team, false))
				drop = centered;
			for (double y = phone.content.y + 48 * unit;
				 y < phone.content.y + phone.content.h - 8 * unit && !drop; y += 12 * unit)
				for (double x = phone.content.x + 24 * unit;
					 x < phone.content.x + phone.content.w - 16 * unit && !drop; x += 12 * unit)
				{
					int mx, my, tx, ty;
					editor.game.map.cursorToBuildingPos(
						editor.mapMouseX(x), editor.mapMouseY(y - 40 * unit), type->width,
						type->height, &mx, &my, editor.viewportX, editor.viewportY);
					if (editor.game.checkRoomForBuilding(mx, my, type, &tx, &ty, editor.team,
														 false))
						drop = GAGCore::ViewPoint{x, y};
				}
			if (!drop)
				throw std::runtime_error("No editor drag fixture site");
			auto buildingCount = [&]
			{
				int count = 0;
				for (int team = 0; team < editor.game.teamsCount(); ++team)
					for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
						count += editor.game.teams[team]->myBuildings[slot] != nullptr;
				return count;
			};
			const int beforeDrop = buildingCount();
			pointer(SDL_FINGERDOWN, from);
			for (int frame = 0; frame < 12; ++frame)
			{
				const double t = frame / 11.;
				pointer(SDL_FINGERMOTION,
						{from.x + (drop->x - from.x) * t, from.y + (drop->y - from.y) * t});
				editCapture("gesture-editor-build-" + std::to_string(10 + frame));
			}
			pointer(SDL_FINGERUP, *drop);
			if (buildingCount() != beforeDrop + 1)
				throw std::runtime_error(
					"Editor gesture recording did not place exactly one building");
			editCapture("gesture-editor-build-22");
			phone.chooseMode(0);
			editor.performAction("select water");
			phone.prepare();
			const GAGCore::ViewPoint a{phone.content.x + phone.content.w * .3,
									   phone.content.y + phone.content.h * .55};
			const auto beforePaint = editor.game.checkSum(nullptr, nullptr, nullptr, true);
			pointer(SDL_FINGERDOWN, a);
			GAGCore::ViewPoint b = a;
			for (int frame = 0; frame < 12; ++frame)
			{
				b = {a.x + phone.content.w * .3 * frame / 11., a.y};
				pointer(SDL_FINGERMOTION, b);
				editCapture("gesture-editor-paint-" + std::to_string(10 + frame));
			}
			pointer(SDL_FINGERUP, b);
			if (editor.game.checkSum(nullptr, nullptr, nullptr, true) == beforePaint)
				throw std::runtime_error("Editor paint recording did not change terrain");
			editCapture("gesture-editor-paint-22");
			phone.chooseMode(1);
			editCapture("editor-palette-resources");
		}
		else
		{
			editor.performAction("switch to terrain view");
			editCapture("editor-palette-resources");
		}

		// Existing-object inspection has its own composition, separate from the
		// placement palette. Build controlled editable examples on legal cells.
		auto focusObject = [&](int x, int y, const char *action)
		{
			editor.viewportX = (x - 3) & editor.game.map.wMask;
			editor.viewportY = (y - 3) & editor.game.map.hMask;
			editor.updateCamera();
			editor.mouseX = int((3 * 32 + 16 - editor.camera.fractionX()) * editor.camera.zoom +
								editor.camera.offsetX);
			editor.mouseY = int((3 * 32 + 16 - editor.camera.fractionY()) * editor.camera.zoom +
								editor.camera.offsetY);
			editor.performAction(action);
		};
		Building *inspectedBuilding = nullptr;
		const int innType = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
		for (int y = 0; y < editor.game.map.getH() && !inspectedBuilding; ++y)
			for (int x = 0; x < editor.game.map.getW() && !inspectedBuilding; ++x)
				if (editor.game.checkRoomForBuilding(
						x, y, globalContainer->buildingsTypes.get(innType), 0, false))
					inspectedBuilding = editor.game.addBuilding(x, y, innType, 0, 1, 0);
		if (!inspectedBuilding)
			throw std::runtime_error("No editor inspector building fixture");
		inspectedBuilding->hp = std::max(1, inspectedBuilding->type->hpMax * 2 / 3);
		focusObject(inspectedBuilding->posX, inspectedBuilding->posY, "select map building");
		editCapture("editor-inspector-building");
		Unit *inspectedUnit = nullptr;
		for (int y = 0; y < editor.game.map.getH() && !inspectedUnit; ++y)
			for (int x = 0; x < editor.game.map.getW() && !inspectedUnit; ++x)
				if (editor.game.map.isFreeForGroundUnit(x, y, false, Team::teamNumberToMask(0)))
					inspectedUnit = editor.game.addUnit(x, y, 0, WORKER, 0, 0, 0, 0);
		if (!inspectedUnit)
			throw std::runtime_error("No editor inspector unit fixture");
		focusObject(inspectedUnit->posX, inspectedUnit->posY, "select map unit");
		editCapture("editor-inspector-unit");
		editor.performAction("switch to terrain view");
		if (editor.phone)
			editor.phone->chooseMode(0);
		editor.performAction("select water");
		if (editor.phone)
		{
			editor.phone->tools = true;
			editor.phone->prepare();
			editor.phone->brushOpen = true;
		}
		editCapture("editor-brush-choices");
		if (editor.phone)
			editor.phone->brushOpen = false;

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
		withOverlay(std::make_unique<AskForTextInput>("[Change Area Name]", "Northern passage"), editor.areaName,
					editor.isShowingAreaName, [&] {
                        editCapture("editor-area-name");
                        if(editor.phone) {
                            auto *gfx=globalContainer->gfx;
                            editor.drawMap(0,0,gfx->getW(),gfx->getH());
                            editor.areaName->drawTouchInViewport({0,0,double(gfx->getW()),120*gfx->logicalUnitsPerPoint()});
                            queueShot("editor-area-name-keyboard");gfx->nextFrame();
                        } else editCapture("editor-area-name-keyboard");
                    });
		withOverlay(std::make_unique<TeamsEditor>(&editor.game), editor.teamsEditor,
					editor.showingTeamsEditor, [&] { editCapture("editor-teams"); });
		withOverlay(std::make_unique<ScriptEditorScreen>(&editor.game), editor.scriptEditor,
					editor.showingScriptEditor,
					[&]
					{
						editCapture("editor-script");
						for (auto [action, name] :
							 {std::pair{ScriptEditorScreen::LOAD, "editor-script-load"},
							  std::pair{ScriptEditorScreen::SAVE, "editor-script-save"}})
						{
							editor.scriptEditor->onAction(nullptr, GAGGUI::BUTTON_RELEASED, action,
														  0);
							editCapture(name);
							auto *child =
								dynamic_cast<LoadSaveScreen *>(editor.scriptEditor->phoneDialog());
							if (!child)
								throw std::runtime_error("Missing script file dialog fixture");
							if (editor.phone)
								editor.phone->closeOverlay();
							child->cancelPresentedFile();
							SDL_Event idle{};
							editor.scriptEditor->translateAndProcessEvent(&idle);
						}

						if (editor.phone)
						{
							auto *gfx = globalContainer->gfx;
							const double unit = gfx->logicalUnitsPerPoint();
							// Host-only clearance fixture: intentionally no fake
							// OS keyboard. The gallery labels the simulated inset.
							editor.drawMap(0, 0, gfx->getW(), gfx->getH());
							editor.scriptEditor->drawTouchInViewport(
								{0, 0, double(gfx->getW()), 160 * unit}, true);
							queueShot("editor-script-keyboard");
							gfx->nextFrame();
						}
						else
							editCapture("editor-script-keyboard");

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
	if (argc == 4 && !desktopPresentation && std::string_view(argv[3]) != "compact" &&
		std::string_view(argv[3]) != "touch-spacious" && std::string_view(argv[3]) != "touch-auto")
	{
		std::cerr << "Presentation must be compact or desktop\n";
		return 2;
	}
	SDL_setenv("GLOB2_MOBILE_UI",
			   desktopPresentation                                            ? "0"
			   : argc == 4 && std::string_view(argv[3]).starts_with("touch-") ? argv[3]
																			  : "1",
			   1);
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
			if (!SDL_getenv("GLOB2_GALLERY_GAME_ONLY"))
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
