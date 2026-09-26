// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlobalContainer.h"
#include "CustomGameScreen.h"
#include "SettingsScreen.h"
#include "LobbyControls.h"
#include "LobbyMapPreview.h"
#include "CustomGamePreferences.h"
#include "MessageScreen.h"
#include "gui/PhoneForm.h"
#include <ScreenStack.h>
#include <SDL_net.h>
#include <StringTable.h>
#include <GUIButton.h>
#include <cstdio>
#include <stdexcept>

GlobalContainer *globalContainer = nullptr;
static void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}
struct MobilePresentationHarness
{
	static void run()
	{
		FrontendLayout compactTablet;
		compactTablet.touch = true;
		require(compactTablet.singleColumn(), "A narrow tablet must use a stacked setup flow");
		compactTablet.twoPane = true;
		require(!compactTablet.singleColumn(), "A spacious tablet should retain two panes");
		compactTablet.touch = false;
		compactTablet.twoPane = false;
		require(!compactTablet.singleColumn(), "A narrow desktop retains desktop organization");
		auto *strings = GAGCore::Toolkit::getStringTable();
		const int originalLanguage = strings->getLang();
		for (int language = 0; language < strings->getNumberOfLanguage(); ++language)
		{
			strings->setLang(language);
			for (const char *key :
				 {"[More]", "[No items]", "[Opponents]", "[Review]", "[Review & Play]",
				  "[Edit opponents]", "[Map settings]", "[Next]", "[Done]", "[Contribution]",
				  "[Choose who controls this colony]", "[Settings]", "[settings Back]",
				  "[settings Save the game automatically about every minute.]", "[AI opponents]"})
			{
				const auto value = strings->getString(key);
				require(!value.empty() && value != key,
						"New frontend keys must resolve to translations or English fallback in "
						"every locale");
			}
		}
		strings->setLang(originalLanguage);
		auto *gfx = globalContainer->gfx;
		Uint32 tick = 0;
		for (auto [width, height] :
			 {std::pair{320, 568}, std::pair{568, 320}, std::pair{390, 844}, std::pair{844, 390}})
		{
			SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
			SDL_Event resize{};
			resize.type = SDL_WINDOWEVENT;
			resize.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
			GAGCore::GraphicContext::translateMouseEvent(&resize);
			const std::string orientation = width < height ? "portrait" : "landscape";
			GAGGUI::ScreenStack stack(*gfx);
			auto owned = std::make_unique<CustomGameScreen>(stack);
			auto *lobby = owned.get();
			stack.push(std::move(owned));
			stack.frame(tick += 40, {});
			auto tap = [&](int x, int y)
			{
				SDL_Event down{};
				down.type = SDL_FINGERDOWN;
				down.tfinger.touchId = 1;
				down.tfinger.fingerId = 1;
				down.tfinger.x = float(x) / gfx->getW();
				down.tfinger.y = float(y) / gfx->getH();
				auto up = down;
				up.type = SDL_FINGERUP;
				stack.frame(tick += 40, {down, up});
			};
			auto hit = [&](const std::string &id)
			{
				for (auto &item : lobby->controls->hits)
					if (item.id == id)
						return item.box;
				throw std::runtime_error("Missing touch target: " + id);
			};
			auto press = [&](const std::string &id)
			{
				// Reveal a target through its owning scroll region before sending
				// real touch events. This also exercises clipped hit registration.
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
				stack.frame(tick += 40, {});
				auto r = hit(id);
				tap(r.x + r.w / 2, r.y + r.h / 2);
				stack.frame(tick += 40, {});
			};
			auto snapshot = [&](const std::string &name)
			{
				gfx->printScreen(name + "-" + orientation + ".bmp");
				stack.frame(tick += 40, {});
			};
			require(gfx->getW() == width && gfx->getH() == height,
					"Setup must use phone dimensions");
			lobby->onAction(nullptr, GAGGUI::BUTTON_SHORTCUT, CustomGameScreen::OK, 0);
			require(lobby->isExecutionRunning(),
					"A phone keyboard shortcut cannot launch before Review");
			snapshot("setup-map");
			auto dialog = std::make_unique<MessageScreen>(
				"A long translated message that wraps across several lines without moving the "
				"actions outside their owning surface.",
				std::vector<std::string>{"Continue with the selected map", "Back"});
			auto *affirmative =
				new GAGGUI::TextButton(250, 340, 180, 40, ALIGN_SCREEN_CENTERED,
									   ALIGN_SCREEN_CENTERED, "menu", "Load", 10, SDLK_RETURN);
			dialog->addWidget(new GAGGUI::TextButton(10, 20, 180, 40, ALIGN_SCREEN_CENTERED,
													 ALIGN_SCREEN_CENTERED, "menu", "Delete", 11));
			dialog->addWidget(affirmative);
			auto *dialogPtr = dialog.get();
			stack.push(std::move(dialog));
			stack.frame(tick += 40, {});
			auto *adapter = dialogPtr->phoneForm.get();
			require(adapter != nullptr, "Frontend dialog uses the responsive adapter");
			require(adapter->primaryAction() == affirmative,
					"Default affirmative action must outrank an earlier destructive action");
			const auto panel = adapter->surfaceBounds;
			require(panel.x >= 0 && panel.y >= 0 && panel.x + panel.w <= width &&
						panel.y + panel.h <= height,
					"Measured dialog stays inside the viewport");
			for (const auto &row : adapter->rows)
			{
				require(row.rect.x >= panel.x && row.rect.x + row.rect.w <= panel.x + panel.w,
						"Every dialog row stays inside its owning surface");
				if (row.kind == 1)
					require(row.rect.h >= 48, "Dialog actions keep minimum touch height");
			}
			dialogPtr->endExecute(0);
			stack.frame(tick += 40, {});
			auto profile = std::make_unique<CustomGameChoiceScreen>(
				"AI profile", std::vector<std::string>{"One", "Two"}, 0, false,
				std::vector<bool>{});
			auto *profileScreen = profile.get();
			stack.push(std::move(profile));
			stack.frame(tick += 40, {});
			auto profileHit = [&](const std::string &id)
			{
				for (const auto &item : profileScreen->controls->hits)
					if (item.id == id)
						return item.box;
				throw std::runtime_error("Missing profile control: " + id);
			};
			const auto back = profileHit("profile/back"), use = profileHit("profile/use");
			require(back.x + back.w <= use.x, "Phone profile Back and Use must not overlap");
			require(back.h >= 48 && use.h >= 48, "Phone profile actions need touch-sized targets");
			snapshot("ai-profile");
			tap(back.x + back.w / 2, back.y + back.h / 2);
			stack.frame(tick += 40, {});
			lobby->setMapMode(true);
			stack.frame(tick += 40, {});
			press("landscape");
			stack.frame(tick += 40, {});
			require(gfx->getW() == width && gfx->getH() == height,
					"Landscape picker retains phone dimensions");
			snapshot("landscape-picker");
			tap(40, height - 32);
			stack.frame(tick += 40, {});

			press("tab/1");
			const auto firstTeam = hit("colony/0/team");
			const auto playerViewport = lobby->controls->regions[101].box;
			require(firstTeam.y >= playerViewport.y &&
						firstTeam.y + firstTeam.h <= playerViewport.y + playerViewport.h,
					"The first colony must be fully editable without scrolling, even in landscape");
			require(lobby->currentTab == lobby->groups[1], "Touch opens Players tab");
			snapshot("setup-players");
			press("colony/0/controller");
			auto *controls = lobby->controls;
			require(controls->popup.open, "Controller dropdown opens by touch");
			auto popup = controls->popupRect();
			require(popup.y >= 0 && popup.y + popup.h <= height,
					"Dropdown stays on screen in both orientations");
			require(controls->popupRowHeight() >= 48, "Dropdown has phone-sized touch rows");
			snapshot("setup-dropdown");
			tap(2, 2);
			const auto settleStarted = SDL_GetTicks();
			do
			{
				stack.frame(tick += 40, {});
				SDL_Delay(16);
			} while (lobby->previewBusy() && SDL_GetTicks() - settleStarted < 30000);
			require(!lobby->previewBusy() && lobby->validMap,
					"Generated preview must complete before testing navigation stability");
			press("tab/0");
			const auto mapPreview = hit("map/preview");
			const auto mapViewport = lobby->controls->regions[100].box;
			require(mapPreview.y >= mapViewport.y &&
						mapPreview.y + mapPreview.h <= mapViewport.y + mapViewport.h,
					"The entire map preview must fit the first viewport in both orientations");
			press("tab/1");
			const auto revision = lobby->setup.mapRevision;
			const auto seed = lobby->chosenSeed;
			const auto snapshotPath = lobby->snapshot;
			press("next");
			require(lobby->currentTab == lobby->groups[2], "Next opens Review");
			press("back");
			require(lobby->currentTab == lobby->groups[1], "Back returns to Opponents");
			require(lobby->setup.mapRevision == revision && lobby->chosenSeed == seed &&
						lobby->snapshot == snapshotPath,
					"Wizard navigation must not reroll or replace the preview");
			press("next");
			press("review/rules");
			require(lobby->phonePage == CustomGameScreen::PhonePage::Rules,
					"Review opens Rules child page");
			press("ruleset/1");
			require(lobby->setup.ruleset != "Standard", "Touch rules preset updates shared setup");
			snapshot("setup-rules");
			// A swipe must scroll without activating the preset beneath its release.
			const auto rules = lobby->setup.ruleset;
			SDL_Event down{};
			down.type = SDL_FINGERDOWN;
			down.tfinger.touchId = 1;
			down.tfinger.fingerId = 2;
			down.tfinger.x = .5f;
			down.tfinger.y = .60f;
			auto move = down;
			move.type = SDL_FINGERMOTION;
			move.tfinger.y = .30f;
			auto up = move;
			up.type = SDL_FINGERUP;
			stack.frame(tick += 40, {down, move, up});
			require(lobby->setup.ruleset == rules, "Scrolling must not select a rules preset");
			auto settings = std::make_unique<SettingsScreen>();
			auto *form = settings.get();
			stack.push(std::move(settings));
			stack.frame(tick += 40, {});
			const auto visible = form->visibleCategories();
			require(std::find(visible.begin(), visible.end(), SettingsScreen::Category::Controls) ==
						visible.end(),
					"Mobile navigation must exclude Controls");
			form->selectCategory(SettingsScreen::Category::Controls);
			require(form->category() != SettingsScreen::Category::Controls,
					"Direct selection must not expose Controls");
			for (int category = 0; category < 6; ++category)
			{
				form->selectCategory(SettingsScreen::Category(category));
				stack.frame(tick += 40, {});
				for (const auto &row : form->rows())
					if (row.kind != SettingsScreen::Kind::Section &&
						row.kind != SettingsScreen::Kind::Info)
					{
						require(row.control.x >= 0 && row.control.x + row.control.w <= width,
								"Settings control extends outside screen");
						require(row.control.h >= 48, "Settings touch control is too short");
						if (row.control.x == row.bounds.x)
							require(row.control.w == row.bounds.w,
									"Stacked fields must fill their row");
						require(row.control.y >= row.bounds.y &&
									row.control.y + row.control.h <= row.bounds.y + row.bounds.h,
								"A settings control must fit its measured row");
					}
				snapshot("settings-" + std::to_string(category));
			}
			const auto beforeKeyboard = FrontendLayout::resolve(gfx);
			GAGCore::presentationViewport.keyboardInset = 160;
			const auto afterKeyboard = FrontendLayout::resolve(gfx);
			require(beforeKeyboard.phone == afterKeyboard.phone &&
						afterKeyboard.safe.h <= beforeKeyboard.safe.h,
					"Keyboard occlusion must not reclassify the device");
			GAGCore::presentationViewport.keyboardInset = 0;
			SDL_setenv("GLOB2_MOBILE_UI", "0", 1);
			const auto desktopCategories = form->visibleCategories();
			require(std::find(desktopCategories.begin(), desktopCategories.end(),
							  SettingsScreen::Category::Controls) != desktopCategories.end(),
					"Narrow desktop windows must retain keyboard settings");
			SDL_setenv("GLOB2_MOBILE_UI", "1", 1);
			form->selectCategory(SettingsScreen::Category::Buildings);
			bool visibleBuilding = false;
			for (const auto &row : form->rows())
				if (row.id == "buildings.open.0")
					visibleBuilding =
						row.control.y >= 56 && row.control.y + row.control.h <= height;
			require(visibleBuilding,
					"Building list must show an actionable building in its first viewport");
			const auto defaults = globalContainer->settings.defaultUnitsAssigned[0][1];
			form->activateSetting("buildings.open.0");
			for (const auto &row : form->rows())
				if (row.id.rfind("units.", 0) == 0)
				{
					require(row.kind == SettingsScreen::Kind::Slider,
							"Phone building defaults must use integer sliders");
					require(row.minimum == 1 && row.maximum == 20,
							"Building slider bounds must retain existing semantics");
				}
			require(globalContainer->settings.defaultUnitsAssigned[0][1] == defaults,
					"Opening building details must not change defaults");
			std::string unitId;
			int oldUnits = 0;
			for (const auto &row : form->rows())
				if (row.id.rfind("units.", 0) == 0)
				{
					unitId = row.id;
					oldUnits = row.number;
					break;
				}
			require(!unitId.empty(), "Building detail exposes a supported assignment");
			for (const auto &row : form->rows())
				if (row.id == unitId)
					require(row.control.y >= 56 && row.control.y + row.control.h <= height,
							"Building detail must expose its first slider without scrolling");
			form->changeSetting(unitId, 0);
			for (const auto &row : form->rows())
				if (row.id == unitId)
					require(row.number == 1, "Integer slider clamps below its supported minimum");
			form->changeSetting(unitId, 99);
			for (const auto &row : form->rows())
				if (row.id == unitId)
					require(row.number == 20, "Integer slider clamps above its supported maximum");
			form->changeSetting(unitId, oldUnits);
			Settings loaded;
			loaded.load();
			require(loaded.defaultUnitsAssigned[0][1] ==
						globalContainer->settings.defaultUnitsAssigned[0][1],
					"Building changes persist using the existing slots");
			tap(48, 24);
			stack.frame(tick += 40, {});
			require(form->isExecutionRunning(), "Back from building detail returns to its list");
			bool returnedToList = false;
			for (const auto &row : form->rows())
				returnedToList |= row.id == "buildings.open.0";
			require(returnedToList, "A single fixed Back control follows the building hierarchy");
			form->selectCategory(SettingsScreen::Category::Audio);
			stack.frame(tick += 40, {});
			const bool originalMute = globalContainer->settings.mute;
			SDL_Event key{};
			key.type = SDL_KEYDOWN;
			key.key.keysym.sym = SDLK_TAB;
			stack.frame(tick += 40, {key});
			stack.frame(tick += 40, {key});
			key.key.keysym.sym = SDLK_SPACE;
			stack.frame(tick += 40, {key});
			require(globalContainer->settings.mute != originalMute,
					"Tab must progress past the scrollable category selector to the first field");
			stack.frame(tick += 40, {key});
			require(globalContainer->settings.mute == originalMute,
					"Keyboard activation restores the same setting without duplicate focus IDs");
			for (const auto &row : form->rows())
				if (row.id == "audio.mute")
				{
					const auto r = row.control;
					const bool muted = globalContainer->settings.mute;
					if (r.y + r.h > height)
						break;
					tap(r.x + r.w / 2, r.y + r.h / 2);
					require(globalContainer->settings.mute != muted,
							"Phone toggle updates shared settings exactly once");
					tap(r.x + r.w / 2, r.y + r.h / 2);
					require(globalContainer->settings.mute == muted,
							"Phone toggle can be restored");
					break;
				}
			form->selectCategory(SettingsScreen::Category::Gameplay);
			stack.frame(tick += 40, {});
			snapshot("settings-gameplay");
			form->endExecute(0);
			stack.frame(tick += 40, {});
			require(gfx->getW() == width && gfx->getH() == height,
					"Returning to setup preserves phone viewport");
			press("page/done");
			const auto launchWait = SDL_GetTicks();
			do
			{
				stack.frame(tick += 40, {});
				SDL_Delay(16);
			} while (lobby->previewBusy() && SDL_GetTicks() - launchWait < 30000);
			require(lobby->validMap && !lobby->previewBusy(),
					"Edited generated draft must finish before launch");
			const auto launchSource = lobby->sourceFile();
			require(!launchSource.empty() && launchSource == lobby->snapshot,
					"Generated launch owns the currently previewed snapshot");
			lobby->onAction(nullptr, GAGGUI::BUTTON_SHORTCUT, CustomGameScreen::OK, 0);
			require(!lobby->isExecutionRunning() && lobby->sourceFile() == launchSource,
					"Launching a generated draft must not replace the preview");
			stack.frame(tick += 40, {});
			auto premade = std::make_unique<CustomGameScreen>(stack);
			lobby = premade.get();
			std::string launchedPremade;
			int launchResult = 0;
			stack.push(std::move(premade),
					   [&](GAGGUI::Screen &screen, int result)
					   {
						   launchResult = result;
						   launchedPremade = static_cast<CustomGameScreen &>(screen).sourceFile();
					   });
			stack.frame(tick += 40, {});
			lobby->setMapMode(false);
			require(lobby->loadMap("maps/balanced.map"), "Premade journey loads a bundled map");
			const auto premadeSource = lobby->sourceFile();
			stack.frame(tick += 40, {});
			press("next");
			press("next");
			press("start");
			require(launchResult == CustomGameScreen::OK && launchedPremade == premadeSource,
					"Premade wizard launches exactly its selected preview");
		}
	}
};
int main()
{
	if (!SDL_getenv("GLOB2_USER_DATA_DIR"))
		return 2;
	SDL_setenv("GLOB2_MOBILE_UI", "1", 1);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	try
	{
		globalContainer = new GlobalContainer("glob2-phone-presentation-test");
		auto &settings = globalContainer->settings;
		settings.screenWidth = 800;
		settings.screenHeight = 600;
		settings.screenFlags =
			GAGCore::GraphicContext::PORTABLEGPU | GAGCore::GraphicContext::RESIZABLE;
		settings.mute = true;
		globalContainer->load();
		require(SDLNet_Init() == 0, "SDL networking failed");
		MobilePresentationHarness::run();
		delete globalContainer;
		globalContainer = nullptr;
		SDLNet_Quit();
		std::puts("PASS phone presentation: setup touch dispatch, scrolling, dropdown bounds, "
				  "category filtering, dialog containment, slider bounds, locale fallback, "
				  "draft/launch identity and viewport restoration at all four phone sizes");
	}
	catch (const std::exception &error)
	{
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		return 1;
	}
}
