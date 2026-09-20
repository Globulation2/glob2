// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeneratorRegistry.h"
#include "AIImplementation.h"
#include "AINames.h"
#include "CustomGameScreen.h"
#include "CustomGameSetup.h"
#include "CustomGamePreferences.h"
#include "Engine.h"
#include "FrontendTheme.h"
#include "GlobalContainer.h"
#include "LandscapePickerScreen.h"
#include "StartQualityScreen.h"
#include "LobbyControls.h"
#include "LobbyMapCatalog.h"
#include "LobbyMapPreview.h"
#include "MapGenerator.h"
#include "Order.h"
#include "Player.h"
#include "ReplayWriter.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <GUIList.h>
#include <Toolkit.h>
#include <atomic>
#include <cassert>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <set>
#include <unistd.h>

GlobalContainer *globalContainer = nullptr;
struct CountingAI : AIImplementation {
  int calls = 0;
  bool load(GAGCore::InputStream *, Player *, Sint32) override { return true; }
  void save(GAGCore::OutputStream *) override {}
  std::shared_ptr<Order> getOrder() override {
    ++calls;
    return std::make_shared<NullOrder>();
  }
};
struct CustomGameSetupHarness
{
	// Every field() is set to its declared maximum, but only the options
	// actually registered for the chosen method survive a GenerationRequest
	// round trip (the rest reset to that method's own defaults) — this
	// mirrors CustomGamePreferences::encode/decode's own conversion so a
	// caller can compute the achievable ground truth deterministically.
	static MapGenerationDescriptor maxedLegacy(MapGenerationDescriptor::Method method, int repeat)
	{
		MapGenerationDescriptor legacy;
		for (const auto &field : CustomGamePreferences::fields())
			legacy.*(field.member) = field.maximum;
		legacy.method = method;
		legacy.logRepeatAreaTimes = repeat;
		return legacy;
	}
	static void setExtraRules(CustomGameSetup &s)
	{
		s.noResourceGrowth = s.instantConstruction = s.noHunger = true;
		s.resourceScarcity = s.stockpileStart = 3;
		s.unitUpgradesDisabled = s.unitsFearless = s.permadeathDisabled = s.peacefulMode = true;
		s.glassCannonLevel = s.buildingHpLevel = 2;
		s.startingUnitLevel = 3;
		s.suddenDeathMinutes = 90;
	}
	static void checkExtraRules(const CustomGameSetup &s)
	{
		assert(s.noResourceGrowth && s.instantConstruction && s.noHunger);
		assert(s.resourceScarcity == 3 && s.stockpileStart == 3);
		assert(s.unitUpgradesDisabled && s.unitsFearless && s.permadeathDisabled && s.peacefulMode);
		assert(s.glassCannonLevel == 2 && s.buildingHpLevel == 2);
		assert(s.startingUnitLevel == 3 && s.suddenDeathMinutes == 90);
	}
	static void preferencesModel()
	{
		CustomGamePreferences original;
		original.setup.random = true;
		original.setup.capacity = original.setup.generator.nbTeams = 12;
		original.setup.premadeMap = "/maps/My \"favorite\" / 地図.map";
		original.userMaps = true;
		original.librarySelection[0] = "maps/FourSquares1.map";
		original.librarySelection[1] = original.setup.premadeMap;
		original.expanded[0] = original.expanded[2] = true;
		original.landscapeSortOrder = 1;
		// Crater Lakes is one of the four modern height-map generators that
		// still exposes a "repeat landscape" control; Old Islands (the prior
		// choice) has no repeat option in the modular registry, so it would
		// always round-trip back to 0 regardless of what is written here.
		original.setup.generator = fromLegacyDescriptor(maxedLegacy(MapGenerationDescriptor::eCRATERLAKES, 5), 0);
		original.setup.presetRules(1);
		original.setup.prestige = false;
		original.setup.revealed = true;
		original.setup.locked = false;
		original.setup.ruleset = "Custom";
		setExtraRules(original.setup);
		original.setup.colonies[11].controller = CustomGameSetup::Closed;
		assert(original.setup.setController(3, CustomGameSetup::Shared));
		original.setup.colonies[3].ai = AI::CORTEX;
		original.setup.colonies[11].alliance = 7;
		original.setup.colonies[11].ai = AI::NICOWAR;
		CustomGamePreferences restored;
		const auto encoded = original.encode();
		assert(restored.decode(encoded) && restored.encode() == encoded);
		checkExtraRules(restored.setup);
		assert(restored.setup.mapRevision == 0);
		assert(restored.landscapeSortOrder == 1);
		// Both older formats still load; omitted rules take their normal defaults.
		for (int version : {1, 2})
		{
			auto old = encoded;
			auto removeLine = [&](const std::string &prefix) {
				const auto at = old.find("\n" + prefix), eol = old.find('\n', at + 1);
				assert(at != std::string::npos && eol != std::string::npos);
				old.erase(at, eol - at);
			};
			removeLine("rules ");
			if (version == 1) removeLine("picker ");
			old.replace(0, std::string("glob2-custom-game 3").size(),
				"glob2-custom-game " + std::to_string(version));
			CustomGamePreferences fromOld;
			assert(fromOld.decode(old) && fromOld.landscapeSortOrder == version - 1);
			assert(fromOld.setup.premadeMap == original.setup.premadeMap);
			assert(!fromOld.setup.unitUpgradesDisabled && !fromOld.setup.noHunger);
			assert(fromOld.setup.startingUnitLevel == 0 && fromOld.setup.suddenDeathMinutes == 0);
		}
		for (size_t length : {size_t(0), size_t(10), encoded.size() / 2, encoded.size() - 5})
		{
			assert(!restored.decode(encoded.substr(0, length)));
			assert(restored.encode() == encoded);
		}
		for (const auto &replacement : std::vector<std::pair<std::string, std::string>>{
			{"glob2-custom-game 3", "glob2-custom-game 4"},
			{"rules 1 3", "rules 2 3"}, {"rules 1 3", "rules 1 4"},
			{"2 3 90\nlabels", "2 4 90\nlabels"},
			{"2 3 90\nlabels", "2 3 31\nlabels"},
			{"wDec 9", "wDec 31"}, {"nbWorkers 8", "nbWorkers -1"},
			{"generator 4 5", "generator 0 5"}, {"generator 4 5", "generator 4 100"},
			{"colonies\n1 1 0", "colonies\n99 1 0"}})
		{
			auto corrupt = encoded;
			auto at = corrupt.find(replacement.first);
			assert(at != std::string::npos);
			corrupt.replace(at, replacement.first.size(), replacement.second);
			assert(!restored.decode(corrupt) && restored.encode() == encoded);
		}
		assert(!restored.decode(encoded + "trailing junk"));
		assert(!restored.decode(std::string(65537, 'x')));
		// Unfinished drafts remain editable rather than losing the user's choices.
		for (auto &c : original.setup.colonies) c.controller = CustomGameSetup::Closed;
		assert(restored.decode(original.encode()));
		assert(!restored.setup.validation().empty());
		std::cout << "PASS preferences round trip, hidden slots, bounds and corrupt-file recovery\n";
	}
	// Controls with no legacy descriptor field (every newer generator's, switches included) are
	// saved in their own section, so a lobby restart keeps them. A file written before that
	// section existed still loads, with those controls at their defaults.
	static void preferencesOptions()
	{
		auto roundTrip = [](int method, const std::map<std::string, int> &values) {
			CustomGamePreferences draft;
			draft.setup.random = true;
			draft.setup.generatorHistory.select(draft.setup.generator, method);
			for (const auto &[id, value] : values)
				GenerationRequest::control(method, id).set(draft.setup.generator, value);
			draft.setup.capacity = draft.setup.generator.nbTeams;
			CustomGamePreferences reloaded;
			const bool loaded = reloaded.decode(draft.encode());
			return loaded && reloaded.setup.generator.method == method &&
				   reloaded.setup.generator.options == draft.setup.generator.options;
		};
		const int fjord = GeneratorRegistry::builtins().idOf("fjord-continent"), ring = GeneratorRegistry::builtins().idOf("ring-world");
		// Fjord's lake size shares the legacy field that held the old generators' "use the
		// default" sentinel 50, and used to reload as 30.
		assert(roundTrip(fjord, {{"lake-size", 50}}));
		// Fjord's lake size 0 and 90, and Ring world's lake density 0, lie outside the legacy
		// fields' old bounds and used to make the whole file fail to load.
		assert(roundTrip(fjord, {{"lake-size", 0}}) && roundTrip(fjord, {{"lake-size", 90}}));
		assert(roundTrip(ring, {{"lake-density", 0}}));
		// Every control's whole range survives, whether it lives in a legacy field or not.
		for (int method : GeneratorRegistry::builtins().methods(false))
			for (const auto &c : GenerationRequest::controls(method))
			{
				if (!roundTrip(method, {{c.id, c.minimum}}) || !roundTrip(method, {{c.id, c.maximum}}))
					std::cerr << "control " << c.id << " of " << GeneratorRegistry::builtins().at(method).id
							  << " does not survive its range " << c.minimum << ".." << c.maximum << "\n";
				assert(roundTrip(method, {{c.id, c.minimum}}) && roundTrip(method, {{c.id, c.maximum}}));
			}

		CustomGamePreferences original;
		original.setup.random = true;
		original.setup.generatorHistory.select(original.setup.generator, fjord);
		auto &g = original.setup.generator;
		g.options["lake-connected"] = 1;
		g.options["resource-islands"] = 7;
		g.options["lake-size"] = 50;
		original.setup.capacity = g.nbTeams;
		const auto encoded = original.encode();
		CustomGamePreferences restored;
		assert(restored.decode(encoded) && restored.encode() == encoded);
		assert(restored.setup.generator.options == g.options);

		// The same file as an older build wrote it, without the options section.
		const auto at = encoded.find("\noptions ") + 1, eol = encoded.find('\n', at);
		const auto end = encoded.find("colonies\n");
		assert(at > 0 && eol < end);
		assert(restored.decode(encoded.substr(0, at) + encoded.substr(end)));
		GenerationRequest defaults;
		defaults.setMethodDefaults(fjord);
		for (const auto &c : GenerationRequest::controls(fjord))
			assert(c.get(restored.setup.generator) == (c.id == "lake-size" ? 50 : c.get(defaults)));

		// An option this build doesn't have is ignored; a value outside its domain is corrupt.
		const int count = std::stoi(encoded.substr(at + 8, eol - at - 8));
		const auto withOption = [&](const std::string &line) {
			return encoded.substr(0, at) + "options " + std::to_string(count + 1) + "\n" + line +
				   "\n" + encoded.substr(eol + 1);
		};
		assert(restored.decode(withOption("retired-option 5")));
		assert(restored.setup.generator.options == g.options && restored.encode() == encoded);
		for (const char *corrupt : {"lake-connected 2", "resource-islands 21", "coast-roughness -1"})
		{
			assert(!restored.decode(withOption(corrupt)));
			assert(restored.encode() == encoded);
		}
		assert(!restored.decode(encoded.substr(0, at) + "options 3\nlake-connected 1\n" +
								encoded.substr(end)));
		std::cout << "PASS preferences keep every generator option, and older files still load\n";
	}
	// FEEDBACK 2026-09-17, in two parts. First: "'random' isn't actually randomizing the order...
	// I want it to be dynamically random" - Random must not be a fixed order that looks shuffled
	// once (the registry's own catalog order, which used to be exactly what Random fell through
	// to) and then repeats identically forever. Second, refining that fix once it was tried in
	// the UI: "I don't like how the random order changes sometimes while I'm using the UI...
	// randomized once when the app starts and then stays consistent for the duration of
	// execution" - so the permutation itself must be a real shuffle (not the untouched catalog
	// order), but two sheets built in the same process run must land on the SAME order as each
	// other, not a fresh one each time; only a relaunch (a new process) draws again.
	static void landscapeRandomOrder()
	{
		std::vector<LandscapePickerScreen::Entry> shown;
		for (int method : GeneratorRegistry::builtins().methods(false))
		{
			GenerationRequest request;
			request.setMethodDefaults(method);
			shown.push_back({GenerationRequest::methodName(method), request, method});
		}
		std::vector<int> identity(shown.size());
		std::iota(identity.begin(), identity.end(), 0);
		LandscapePickerScreen first("Landscape", shown, 0);
		LandscapePickerScreen second("Landscape", shown, 0);
		assert(first.sortOrder == LandscapePickerScreen::SortOrder::Random &&
			  second.sortOrder == LandscapePickerScreen::SortOrder::Random);
		// A real shuffle, not the untouched catalog order left alone...
		assert(first.visible != identity);
		// ...but the SAME shuffle every time within this one process run, however many sheets are
		// opened - the process-lifetime permutation this fix's refinement asked for.
		assert(first.visible == second.visible);
		// Touching a filter must not redraw it either: narrowing still respects the one order
		// this run drew, just with the excluded entries missing from it.
		LandscapePickerScreen third("Landscape", shown, 0);
		if (!third.filterCategories.empty())
		{
			third.filters[0] = "does-not-exist";
			third.rebuild();
			third.filters[0].clear();
			third.rebuild();
		}
		assert(third.visible == first.visible);
		// Alphabetical stays the deterministic alternative: same entries, same order, every time.
		LandscapePickerScreen sortedA("Landscape", shown, 0,
									  LandscapePickerScreen::SortOrder::Alphabetical);
		LandscapePickerScreen sortedB("Landscape", shown, 0,
									  LandscapePickerScreen::SortOrder::Alphabetical);
		assert(sortedA.visible == sortedB.visible);
		std::cout << "PASS landscape picker Random order is a real shuffle drawn once per process "
					 "run, Alphabetical stays stable\n";
	}
	static void preferencesScreen(bool write)
	{
		auto *files = Toolkit::getFileManager();
		if (write) files->remove(CustomGamePreferences::filename);
		if (write)
		{
			CustomGameScreen screen;
			// Nothing saved: a random map (FEEDBACK 2026-09-14). The premade library is what this
			// file is written with, so switch to it first.
			assert(screen.setup.random && screen.previewPending && screen.setup.capacity == 4);
			screen.setMapMode(false);
			assert(!screen.setup.random && screen.validMap);
			assert(screen.setup.setController(2, CustomGameSetup::Shared));
			screen.setup.colonies[2].ai = AI::CORTEX;
			screen.setup.colonies[0].alliance = 2;
			screen.setup.colonies[11].ai = AI::NICOWAR;
			screen.setup.colonies[11].alliance = 7;
			screen.setup.presetRules(1);
			setExtraRules(screen.setup);
			screen.setup.generator = fromLegacyDescriptor(maxedLegacy(MapGenerationDescriptor::eISLANDS, 3), 0);
			screen.expanded[1] = true;
			screen.userMaps = screen.separateMapLibraries;
			screen.librarySelection[1] = "maps/favorite-user-map.map";
			// Persist while the screen is still open, as normal edits do.
			screen.onTimer(SDL_GetTicks());
			CustomGamePreferences disk;
			assert(disk.load(*files) && disk.setup.colonies[2].controller == CustomGameSetup::Shared);
		}
		else
		{
			std::string premade;
			{
				CustomGameScreen screen;
				assert(screen.validMap && !screen.setup.random && screen.setup.capacity == 4);
				assert(screen.setup.colonies[2].controller == CustomGameSetup::Shared);
				assert(screen.setup.colonies[2].ai == AI::CORTEX);
				assert(screen.setup.colonies[0].alliance == 2);
				assert(screen.setup.colonies[11].ai == AI::NICOWAR && screen.setup.colonies[11].alliance == 7);
				assert(screen.setup.speed == 3 && screen.setup.ruleset == "Quick clash");
				checkExtraRules(screen.setup);
				assert(screen.expanded[1]);
				assert(screen.userMaps == screen.separateMapLibraries);
				assert(screen.librarySelection[1] == "maps/favorite-user-map.map");
				// Only the options Islands actually registers survive the
				// GenerationRequest round trip; compare against the same
				// achievable conversion rather than the raw field maximums.
				const auto expected = toLegacyDescriptor(fromLegacyDescriptor(maxedLegacy(MapGenerationDescriptor::eISLANDS, 3), 0));
				const auto restoredLegacy = toLegacyDescriptor(screen.setup.generator);
				for (const auto &field : CustomGamePreferences::fields())
					assert(restoredLegacy.*(field.member) == expected.*(field.member));
				assert(restoredLegacy.logRepeatAreaTimes == expected.logRepeatAreaTimes);
				premade = screen.setup.premadeMap;
				screen.setup.generator.nbTeams = 4;
				screen.setMapMode(true);
				assert(screen.previewPending);
			}
			{
				CustomGameScreen screen;
				assert(screen.setup.random && screen.previewPending && !screen.validMap);
				assert(screen.snapshot.empty() && screen.source.empty());
				assert(screen.setup.premadeMap == premade);
				// Switching back restores the premade choice without disturbing teams.
				screen.setMapMode(false);
				assert(screen.validMap && screen.setup.colonies[0].alliance == 2);
				screen.setup.premadeMap = "/missing/saved-map.map";
			}
			{
				CustomGameScreen screen;
				assert(!screen.validMap && !screen.setup.random && !screen.message.empty());
				assert(screen.setup.colonies[2].ai == AI::CORTEX && screen.setup.speed == 3);
				assert(screen.setup.premadeMap == "/missing/saved-map.map");
			}
			files->writeAtomically(CustomGamePreferences::filename, [](GAGCore::OutputStream &out) {
				const std::string truncated = "glob2-custom-game 1\nsetup";
				out.write(truncated.data(), truncated.size(), "broken preferences");
			});
			{
				// A file the lobby cannot read is the same as none: a random map at four colonies,
				// its preview pending (FEEDBACK 2026-09-14: random maps are the default tab).
				CustomGameScreen screen;
				assert(screen.setup.random && screen.previewPending && !screen.validMap &&
					   screen.setup.capacity == 4 && screen.setup.speed == 0);
			}
			files->remove(CustomGamePreferences::filename);
		}
		std::cout << "PASS native preferences " << (write ? "write" : "reload, random preview, missing map and recovery") << "\n";
	}
	static void ui(const std::string &output, int control)
	{
		Toolkit::getFileManager()->remove(CustomGamePreferences::filename);
		FrontendTheme theme;
		struct Driver
		{
			struct Step
			{
				std::string name;
				std::function<void()> action;
				Uint32 delay;
			};
			std::vector<Step> steps;
			std::atomic<size_t> next{0};
			static Uint32 tick(Uint32, void *data)
			{
				auto &driver = *static_cast<Driver *>(data);
				size_t index = driver.next.fetch_add(1);
				if (index >= driver.steps.size())
				{
					SDL_Event quit = {};
					quit.type = SDL_QUIT;
					SDL_PushEvent(&quit);
					return 0;
				}
				auto &step = driver.steps[index];
				std::cout << "UI step " << index + 1 << ": " << step.name << std::endl;
				step.action();
				return index + 1 == driver.steps.size() ? 0 : step.delay;
			}
		} driver;
		auto click = [&](std::string name, int x, int y, Uint32 delay = 220)
		{
			driver.steps.push_back(
				{name,
				 [=]
				 {
					 for (Uint32 type : {Uint32(SDL_MOUSEBUTTONDOWN), Uint32(SDL_MOUSEBUTTONUP)})
					 {
						 SDL_Event event = {};
						 event.type = type;
						 event.button.button = SDL_BUTTON_LEFT;
						 event.button.state =
							 type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
						 event.button.x = x;
						 event.button.y = y;
						 SDL_PushEvent(&event);
					 }
				 },
				 delay});
		};
		auto key = [&](SDL_Keycode code, SDL_Keymod modifiers = KMOD_NONE, int repeats = 1)
		{
			driver.steps.push_back({"key " + std::to_string(code),
									[=]
									{
										for (int i = 0; i < repeats; ++i)
										{
											SDL_Event event = {};
											event.type = SDL_KEYDOWN;
											event.key.keysym.sym = code;
											event.key.keysym.mod = modifiers;
											SDL_PushEvent(&event);
										}
									},
									220});
		};
		// The lobby opens on a random map (2026-09-14); the flow was written from the premade
		// library, so start there and let the later Random mode click switch as it always did.
		click("Premade maps", 90, 100);
		click("Players tab", 320, 30);
		click("Inline controller", 170, 143);
		key(SDLK_DOWN);
		key(SDLK_RETURN);
		click("Shared control", 170, 143);
		key(SDLK_DOWN);
		key(SDLK_RETURN);
		click("2 vs 2 preset", 235, 100);
		click("Second colony AI", 350, 207);
		// Select the following row; the profile interaction below moves up once.
		const int aiSteps = AINames::selectionIndex(AI::NICOWAR) + 1 - AINames::selectionIndex(AI::NUMBI);
		key(aiSteps >= 0 ? SDLK_DOWN : SDLK_UP, KMOD_NONE, aiSteps >= 0 ? aiSteps : -aiSteps);
		key(SDLK_RETURN);
		click("Second colony profile", 580, 238);
		key(SDLK_UP);
		key(SDLK_RETURN);
		key(SDLK_3, KMOD_CTRL);
		click("Quick clash tile", 420, 127);
		key(SDLK_1, KMOD_CTRL);
		click("Random mode (automatic preview)", 225, 100, 1800);
		key(SDLK_2, KMOD_CTRL);
		if (control != CustomGameSetup::Shared)
		{
			click("Final controller", 170, 143);
			key(SDLK_UP, KMOD_NONE, control == CustomGameSetup::Computer ? 1 : 2);
			key(SDLK_RETURN);
		}
		click("Launch", 540, 445);

    globalContainer->settings.gameSpeed = 7;
    {
      Engine engine;
      auto timer = SDL_AddTimer(500, Driver::tick, &driver);
      assert(timer);
      auto watchdog = SDL_AddTimer(
          20000,
          [](Uint32, void *) -> Uint32 {
            SDL_Event event = {};
            event.type = SDL_QUIT;
            SDL_PushEvent(&event);
            return 0;
          },
          nullptr);
      int result = engine.initCustom();
      SDL_RemoveTimer(timer);
      SDL_RemoveTimer(watchdog);
      assert(result == Engine::EE_NO_ERROR);
      assert(driver.next == driver.steps.size());
      assert(globalContainer->liveSpectating ==
             (control == CustomGameSetup::Computer));
      assert(globalContainer->settings.gameSpeed == 3);
      assert(engine.gui.game.gameHeader.getNumberOfPlayers() ==
             (control == CustomGameSetup::Shared ? 5 : 4));
      assert(
          engine.gui.game.players[control == CustomGameSetup::Shared ? 2 : 1]
              ->ai->implementationID == AI::NICOWAR);
      assert(engine.gui.game.gameHeader.getAllyTeamNumber(0) ==
             engine.gui.game.gameHeader.getAllyTeamNumber(1));
      assert(engine.gui.game.gameHeader.getAllyTeamNumber(0) !=
             engine.gui.game.gameHeader.getAllyTeamNumber(2));
      if (globalContainer->liveSpectating) {
        SDL_Event pause = {};
        pause.type = SDL_KEYDOWN;
        pause.key.keysym.sym = SDLK_p;
        engine.gui.processEvent(&pause);
        assert(engine.gui.hardPause);
        engine.gui.processEvent(&pause);
        assert(!engine.gui.hardPause);
      }
      globalContainer->settings.save(); // Simulate persisting in-game options.
      globalContainer->automaticEndingGame = true;
      globalContainer->automaticEndingSteps = 30;
      globalContainer->automaticGameGlobalEndConditions = true;
      engine.run();
      {
        FrontendScope gameplay(false);
        engine.gui.drawAll(engine.gui.localTeamNo);
        globalContainer->gfx->printScreen(output + "/live-control-" +
                                          std::to_string(control) + ".bmp");
      }
    }
    assert(globalContainer->settings.gameSpeed == 7);
    Settings persisted;
    persisted.load();
    assert(persisted.gameSpeed == 7);
    std::cout << "PASS full SDL UI flow mode " << control
              << ": clicks, nested choices, shared control, presets, profiles, "
                 "random preview, "
                 "match launch and speed restoration\n";
  }

	static void visual(const std::string &output, bool onlyAIProfile = false)
	{
		FrontendTheme theme;
		FrontendScope scope;
		std::vector<std::string> labels;
		for (int i : AINames::selectionOrder())
			labels.push_back(AINames::getAISelectorText(i));
		{
			CustomGameChoiceScreen profile("AI strategy & counterplay", labels, AINames::selectionIndex(AI::CORTEX), true,
										   {});
			profile.dispatchInit();
			profile.dispatchPaint(false);
			globalContainer->gfx->printScreen(output + "/ai-profile.bmp");
			profile.onAction(nullptr, GAGGUI::BUTTON_SHORTCUT, -3, 0);
			assert(profile.returnCode == AINames::selectionIndex(AI::CORTEX));
		}
		if (onlyAIProfile)
			return;

    CustomGameScreen screen;
    screen.gfx = globalContainer->gfx;
    screen.dispatchInit();
    // With nothing saved the lobby opens on a random map (FEEDBACK 2026-09-14); the premade
    // library is a click away and is what the catalog checks below exercise.
    assert(screen.setup.random && screen.previewPending && !screen.validMap &&
           screen.setup.capacity == 4);
    // The first visit to the library preselects FourSquares1, the old opening map.
    screen.setMapMode(false);
    assert(!screen.setup.random && screen.validMap && !screen.previewPending &&
           std::filesystem::path(screen.setup.premadeMap).filename() == "FourSquares1.map");
    screen.separateMapLibraries = false;
    screen.listMaps();
    assert(std::any_of(
        screen.mapPaths.begin(), screen.mapPaths.end(), [](const auto &path) {
          return std::filesystem::path(path).filename() == "FourSquares1.map";
        }));
    screen.separateMapLibraries = true;
    screen.listMaps();

    auto paint = [&] { screen.dispatchPaint(false); };
    // The preview rolls its candidates on worker threads; wait for them as the timer would.
    auto preview = [&] {
      screen.onTimer(screen.previewDue);
      screen.finishPreview();
    };
    auto keyEvent = [&](SDL_Keycode key, Uint16 modifiers = KMOD_NONE) {
      SDL_Event e = {};
      e.type = SDL_KEYDOWN;
      e.key.keysym.sym = key;
      e.key.keysym.mod = modifiers;
      screen.dispatchEvents(&e);
      paint();
    };
    auto clickControl = [&](const std::string &id) {
      paint();
      auto find = [&] {
        return std::find_if(screen.controls->hits.begin(),
                            screen.controls->hits.end(),
                            [&](const auto &hit) { return hit.id == id; });
      };
      auto hit = find();
      assert(hit != screen.controls->hits.end());
      if (hit->region >= 0) {
        auto &r = screen.controls->regions[hit->region];
        if (hit->box.y < r.box.y)
          screen.controls->scroll(hit->region, hit->box.y - r.box.y);
        else if (hit->box.y + hit->box.h > r.box.y + r.box.h)
          screen.controls->scroll(hit->region,
                                  hit->box.y + hit->box.h - r.box.y - r.box.h);
        paint();
        hit = find();
      }
      auto r = hit->box;
      for (auto type : {SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP}) {
        SDL_Event e = {};
        e.type = type;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.x = r.x + r.w / 2;
        e.button.y = r.y + r.h / 2;
        screen.dispatchEvents(&e);
      }
      paint();
    };
    // Canonical identity, including repeated roots and symlink aliases.
    auto fixture =
        std::filesystem::temp_directory_path() /
        ("glob2-lobby-catalog-test-" + std::to_string(SDL_GetTicks()));
    std::filesystem::remove_all(fixture);
    std::filesystem::create_directories(fixture / "one");
    std::filesystem::create_directories(fixture / "two");
    for (auto name : {"one/zebra.map", "one/Alpha.map", "two/Alpha.map"}) {
      std::ofstream file(fixture / name);
      file << "fixture";
    }
    std::filesystem::create_directory_symlink(fixture / "one",
                                              fixture / "alias");
    auto catalog = lobbyMapCatalog(
        {fixture / "one", fixture / "one", fixture / "alias", fixture / "two"});
    assert(catalog.size() == 3 && catalog[0].name.find("Alpha") == 0 &&
           catalog[1].name.find("Alpha") == 0 && catalog[2].name == "zebra");
    assert(catalog[0].name != catalog[1].name);
    std::filesystem::remove_all(fixture);
    assert(std::set<std::string>(screen.mapPaths.begin(), screen.mapPaths.end())
               .size() == screen.mapPaths.size());
    paint();
    auto &mapRegion = screen.controls->regions[10];
    mapRegion.offset = std::min(56, mapRegion.maximum);
    paint();
    int savedOffset = mapRegion.offset;
    int row = savedOffset / 28 + 1;
    clickControl("map/entry/" + std::to_string(row));
    assert(mapRegion.offset == savedOffset);
    keyEvent(SDLK_DOWN);
    assert(mapRegion.offset == savedOffset);
    clickControl("map/library/1");
    clickControl("map/library/0");
    assert(mapRegion.offset == savedOffset);
    assert(screen.librarySelection[0] == screen.source);
    screen.loadMap("maps/FourSquares1.map");
    screen.activateGroup(screen.groups[1]);
    paint();
    auto alliances = screen.setup.colonies;
    clickControl("colony/0/controller");
    assert(screen.controls->popup.open);
    keyEvent(SDLK_DOWN);
    keyEvent(SDLK_RETURN);
    assert(screen.setup.colonies[0].controller == CustomGameSetup::Computer);
    for (int i = 0; i < 4; ++i)
      assert(screen.setup.colonies[i].alliance == alliances[i].alliance);
    assert(screen.controls->focus == "colony/0/controller");
    clickControl("colony/0/controller");
    keyEvent(SDLK_DOWN);
    keyEvent(SDLK_RETURN);
    assert(screen.setup.colonies[0].controller == CustomGameSetup::Shared);
    clickControl("colony/1/ai");
    keyEvent(SDLK_DOWN);
    keyEvent(SDLK_ESCAPE);
    assert(screen.setup.colonies[1].ai == AI::NUMBI);
    clickControl("colony/1/ai");
    SDL_Event outside = {};
    outside.type = SDL_MOUSEBUTTONDOWN;
    outside.button.x = 0;
    outside.button.y = 0;
    screen.dispatchEvents(&outside);
    assert(!screen.controls->popup.open);
    clickControl("format/1");
    assert(screen.setup.colonies[0].alliance ==
           screen.setup.colonies[1].alliance);
    screen.activateGroup(screen.groups[2]);
    paint();
    clickControl("rule/1/1");
    assert(screen.setup.revealed && screen.setup.ruleset == "Custom");
    assert(!screen.setup.unitUpgradesDisabled);
    screen.setup.unitUpgradesDisabled = true;
    int rulesOffset = screen.controls->regions[2].offset;
    clickControl("rule/1/0");
    assert(!screen.setup.revealed &&
           screen.controls->regions[2].offset == rulesOffset);
    assert(screen.setup.unitUpgradesDisabled);
    clickControl("ruleset/1");
    assert(screen.setup.speed == 3 && screen.setup.generator.nbWorkers == 8);
    screen.setup = CustomGameSetup();
    screen.loadMap("maps/FourSquares1.map");
    screen.controls->regions[2].offset = 0;
    screen.activateGroup(screen.groups[0]);
    screen.controls->regions[10].offset = 0;
    std::cout << "PASS canonical map catalog, selection stability, inline "
                 "controls, popup cancel, focus preservation and rules\n";
    auto capture = [&](const std::string &name) {
      screen.dispatchPaint(false);
      if (screen.preview->transitioning) {
        screen.preview->transitionPending = false;
        screen.preview->transitionStarted = SDL_GetTicks() - MapPreview::TransitionDurationMs - 1;
        screen.dispatchPaint(false);
      }
      globalContainer->gfx->printScreen(output + "/" + name + ".bmp");
    };
    capture("map-640");
    // Randomize, Reset to defaults and Random parameters only apply to random maps.
    assert(std::none_of(screen.controls->hits.begin(), screen.controls->hits.end(),
                        [](const auto &h) {
                          return h.id == "map/randomize" || h.id == "generator/reset" ||
                                 h.id == "generator/random" || h.id == "quality/info";
                        }));
    screen.activateGroup(screen.groups[1]);
    capture("players-640");
    screen.setup.colonies[1].ai = AI::CORTEX;
    capture("cortex-640");
    clickControl("colony/1/ai");
    capture("ai-dropdown");
    keyEvent(SDLK_ESCAPE);

    screen.activateGroup(screen.groups[2]);
    capture("rules-640");
    screen.setup.random = true;
    // The slider/failure checks below exercise River terrain weights explicitly.
    screen.setup.generatorHistory.select(screen.setup.generator, MapGenerationDescriptor::eRIVER);
    screen.invalidate();
    screen.activateGroup(screen.groups[0]);
    capture("random-controls-640");
    screen.expanded[0] = screen.expanded[1] = screen.expanded[2] = true;
    paint();
    screen.controls->regions[3].offset = 180;
    capture("generator-expanded");
    screen.controls->regions[3].offset = 0;
    // Preview appears from the timer, without a Generate control or click.
    assert(std::none_of(screen.controls->hits.begin(),
                        screen.controls->hits.end(),
                        [](const auto &h) { return h.id == "map/generate"; }));
    screen.onTimer(screen.previewDue - 1);
    assert(!screen.validMap);
    screen.controls->pressed = "generator/water";
    preview();
    assert(!screen.validMap);
    screen.controls->pressed.clear();
    preview();
    assert(screen.validMap && !screen.previewPending);
    auto first = screen.snapshot;
    auto revision = screen.previewRevision;
    assert(std::filesystem::exists(first));
    capture("random-preview-640");
    screen.setup.colonies[1].ai = AI::CASTOR;
    screen.setup.presetTeams(1);
    screen.onTimer(SDL_GetTicks() + 1000);
    assert(screen.previewRevision == revision && screen.sourceFile() == first);
    clickControl("generator/water");
    assert(!screen.validMap && screen.setup.mapRevision != revision);
    auto water = screen.setup.generator.options["water"];
    SDL_Event motion = {};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.x = 0;
    motion.motion.y = 0;
    screen.dispatchEvents(&motion);
    assert(screen.setup.generator.options["water"] == water);
    keyEvent(SDLK_RIGHT);
    assert(screen.setup.generator.options["water"] == water + 1);
    assert(screen.setup.colonies[0].alliance ==
           screen.setup.colonies[1].alliance);

    assert(screen.generateMap());
    assert(screen.snapshot != first && !std::filesystem::exists(first));
    // Randomize rolls the same settings again with a new seed, through the
    // normal preview path, and releases the snapshot it replaces.
    {
      const auto settings = screen.setup.generator;
      const auto mapRevision = screen.setup.mapRevision;
      const auto replaced = screen.snapshot;
      clickControl("map/randomize");
      assert(!screen.validMap && screen.previewPending);
      preview();
      assert(screen.validMap && screen.snapshot != replaced &&
             !std::filesystem::exists(replaced));
      assert(screen.setup.generator.method == settings.method &&
             screen.setup.generator.options == settings.options &&
             screen.setup.mapRevision == mapRevision);
      capture("randomize-640");
    }
    // Reset to defaults restores this landscape's registered values, keeping
    // the landscape and the Game Rules tab's starting workers.
    {
      GenerationRequest expected;
      expected.setMethodDefaults(screen.setup.generator.method);
      const auto method = screen.setup.generator.method;
      const auto workers = screen.setup.generator.nbWorkers;
      assert(screen.setup.generator.options != expected.options);
      clickControl("generator/reset");
      assert(screen.setup.generator.method == method &&
             screen.setup.generator.nbWorkers == workers &&
             screen.setup.generator.options == expected.options &&
             screen.setup.generator.wDec == expected.wDec &&
             screen.setup.generator.hDec == expected.hDec &&
             screen.setup.capacity == expected.nbTeams && !screen.validMap &&
             screen.previewPending);
      paint();
      const auto reset = std::find_if(
          screen.controls->hits.begin(), screen.controls->hits.end(),
          [](const auto &h) { return h.id == "generator/reset"; });
      assert(reset != screen.controls->hits.end() && !reset->enabled);
      // Reset to defaults sits at the top of the column, right under the landscape chooser,
      // with Random parameters beside it (FEEDBACK 2026-09-14).
      const auto landscape = std::find_if(
          screen.controls->hits.begin(), screen.controls->hits.end(),
          [](const auto &h) { return h.id == "generator/landscape"; });
      const auto random = std::find_if(
          screen.controls->hits.begin(), screen.controls->hits.end(),
          [](const auto &h) { return h.id == "generator/random"; });
      assert(landscape != screen.controls->hits.end() && random != screen.controls->hits.end());
      assert(reset->box.y > landscape->box.y && reset->box.y < landscape->box.y + 80 &&
             random->box.y == reset->box.y && random->box.x > reset->box.x && random->enabled);
      for (const auto &h : screen.controls->hits)
        if (h.id.rfind("generator/", 0) == 0 && h.id != "generator/landscape" &&
            h.id != "generator/reset" && h.id != "generator/random")
          assert(h.box.y >= reset->box.y);
      preview();
      assert(screen.validMap);
      capture("reset-640");
    }
    // Random parameters draws every one of the landscape's controls at random, keeping the size,
    // colony count and workers, and yields a request the generator accepts; the preview then
    // shows a map for it. Reset returns to the defaults from there.
    {
      const auto before = screen.setup.generator;
      const int capacity = screen.setup.capacity;
      const GeneratorDefinition &definition =
          GeneratorRegistry::builtins().at(before.method);
      bool changed = false;
      for (int attempt = 0; attempt < 4 && !changed; ++attempt) {
        clickControl("generator/random");
        changed = screen.setup.generator.options != before.options;
      }
      assert(changed && !screen.validMap && screen.previewPending && !screen.chosenSeed);
      assert(screen.setup.generator.method == before.method &&
             screen.setup.generator.nbWorkers == before.nbWorkers &&
             screen.setup.generator.wDec == before.wDec &&
             screen.setup.generator.hDec == before.hDec && screen.setup.capacity == capacity);
      assert(validateGenerationRequest(screen.setup.generator, definition).empty());
      // A random set the world refuses on every seed is redrawn by the preview itself, one
      // more round of candidates per draw; give it those rounds, and a fresh click should even
      // the last draw fail, so the check does not hang on one unlucky stream.
      for (int click = 0; click < 6 && !screen.validMap; ++click) {
        if (click > 0)
          clickControl("generator/random");
        for (int round = 0; round < CustomGameScreen::kRandomAttempts + 1 && !screen.validMap;
             ++round)
          preview();
      }
      assert(screen.validMap && screen.quality.measured &&
             screen.quality.colonies.size() == size_t(capacity));
      paint();
      // The start quality line under the preview: fairness and score, and its (i).
      assert(std::any_of(screen.controls->hits.begin(), screen.controls->hits.end(),
                         [](const auto &h) { return h.id == "quality/info" && h.enabled; }));
      capture("random-parameters-640");
      {
        std::vector<std::string> labels;
        std::vector<Color> colors;
        for (size_t i = 0; i < screen.quality.colonies.size(); ++i) {
          labels.push_back(screen.colonyLabel(int(i)));
          colors.push_back(screen.preview->starts[i].color);
        }
        StartQualityScreen breakdown(screen.quality, labels, colors);
        breakdown.dispatchInit();
        breakdown.dispatchPaint(false);
        globalContainer->gfx->printScreen(output + "/start-quality.bmp");
        assert(std::any_of(breakdown.controls->hits.begin(), breakdown.controls->hits.end(),
                           [](const auto &h) { return h.id == "quality/back"; }));
        SDL_Event e = {};
        e.type = SDL_KEYDOWN;
        e.key.keysym.sym = SDLK_ESCAPE;
        breakdown.dispatchEvents(&e);
        assert(breakdown.returnCode == StartQualityScreen::BACK);
      }
      const auto retainedQuality = screen.quality;
      clickControl("generator/reset");
      GenerationRequest expected;
      expected.setMethodDefaults(before.method);
      assert(screen.setup.generator.options == expected.options && screen.quality.measured &&
             screen.quality.fairness == retainedQuality.fairness &&
             screen.quality.score == retainedQuality.score);
      preview();
      assert(screen.validMap);
    }
    screen.setup.presetRules(1);
    screen.invalidate();
    assert(!screen.validMap);
    auto assignments = screen.setup.colonies;
    screen.setup.generator.options["water"] =
        screen.setup.generator.options["sand"] =
            screen.setup.generator.options["grass"] =
                screen.setup.generator.options["desert"] = 0;
    preview();
    assert(!screen.validMap && !screen.previewPending);
    for (int i = 0; i < 4; ++i)
      assert(screen.setup.colonies[i].alliance == assignments[i].alliance);
    screen.setup.generator.options["water"] =
        screen.setup.generator.options["sand"] =
            screen.setup.generator.options["grass"] =
                screen.setup.generator.options["desert"] = 50;
    screen.invalidate();
    preview();
    assert(screen.validMap);

    // Apply a landscape the way the picker's result does, then drive the same steppers used
    // by players, including measured five-unit steps. The picker itself is exercised below.
    auto landscape = [&](int method) {
      screen.applyLandscape(method, std::nullopt);
      paint();
      assert(screen.setup.generator.method == method);
    };
    landscape(MapGenerationDescriptor::eCONCRETEISLANDS);
    assert(screen.setup.generator.options["channel-width"] == 5 &&
           screen.setup.generator.options["extra-islands"] == 3);
    clickControl("generator/channel-width/+");
    assert(screen.setup.generator.options["channel-width"] == 6);
    capture("concrete-controls");
    landscape(MapGenerationDescriptor::eISLES);
    assert(screen.setup.generator.options["island-size"] == 60);
    clickControl("generator/island-size/+");
    assert(screen.setup.generator.options["island-size"] == 65);
    clickControl("generator/bridge-width/+");
    assert(screen.setup.generator.options["bridge-width"] == 5);
    capture("isles-controls");
    landscape(MapGenerationDescriptor::eCRATERLAKES);
    assert(screen.setup.generator.options["lake-size"] == 25 &&
           screen.setup.generator.options["grass"] == 75);
    clickControl("generator/lake-size/+");
    assert(screen.setup.generator.options["lake-size"] == 30);
    capture("crater-controls");
    landscape(MapGenerationDescriptor::eCONCRETEISLANDS);
    assert(screen.setup.generator.options["channel-width"] == 6);
    landscape(MapGenerationDescriptor::eOLDISLANDS);
    assert(screen.setup.generator.options["island-size"] == 65);
    capture("rugged-archipelago-controls");
    landscape(MapGenerationDescriptor::eOLDRANDOM);
    capture("shattered-coast-controls");
    // Switches are checkboxes: a click, or Space or Return on the focused row, flips one, and
    // either edit invalidates the preview like any other generator control.
    landscape(GeneratorRegistry::builtins().idOf("fjord-continent"));
    screen.expanded[0] = screen.expanded[1] = screen.expanded[2] = true;
    preview();
    assert(screen.validMap);
    {
      auto &options = screen.setup.generator.options;
      const auto revision = screen.setup.mapRevision;
      assert(options["lake-connected"] == 0);
      clickControl("generator/lake-connected");
      assert(options["lake-connected"] == 1 && screen.setup.mapRevision != revision &&
             !screen.validMap && screen.previewPending);
      assert(screen.controls->focus == "generator/lake-connected");
      keyEvent(SDLK_SPACE);
      assert(options["lake-connected"] == 0);
      keyEvent(SDLK_RETURN);
      assert(options["lake-connected"] == 1);
      // Tab walks off the checkbox and Shift+Tab back onto it, like any other control.
      keyEvent(SDLK_TAB);
      assert(screen.controls->focus != "generator/lake-connected");
      keyEvent(SDLK_TAB, KMOD_SHIFT);
      assert(screen.controls->focus == "generator/lake-connected");
      preview();
      assert(screen.validMap);
      capture("map-checkboxes");
      clickControl("generator/lake-connected");
      assert(options["lake-connected"] == 0);
    }
    landscape(MapGenerationDescriptor::eRIVER);

    screen.activateGroup(screen.groups[0]);
    capture("map-1000");
    // Rectangular terrain and markers must use the same cropped preview area.
    for (auto dimensions : {std::pair{9, 7}, std::pair{7, 9}, std::pair{9, 6}, std::pair{6, 9}}) {
      screen.setup.generatorHistory.select(screen.setup.generator, GeneratorRegistry::builtins().idOf("contested-commons"));
      screen.setup.generator.wDec = dimensions.first;
      screen.setup.generator.hDec = dimensions.second;
      screen.setup.setCapacity(4);
      screen.invalidate();
      assert(screen.generateMap());
      const std::string name = "rectangular-" + std::to_string(1 << dimensions.first) + "x" + std::to_string(1 << dimensions.second);
      const auto expectedStarts = screen.preview->starts;
      // Old premade maps can contain equivalent coordinates across a torus seam.
      screen.preview->starts[0].x -= (1 << dimensions.first);
      screen.preview->starts[0].y += (1 << dimensions.second);
      capture(name);
      const auto rect = screen.preview->getScreenRect();
      const int mapW = screen.preview->getLastWidth(), mapH = screen.preview->getLastHeight();
      assert(std::abs(rect.w * mapH - rect.h * mapW) < std::max(mapW, mapH));
      SDL_Surface *bmp = SDL_LoadBMP((output + "/" + name + ".bmp").c_str());
      assert(bmp);
      SDL_Surface *rgba = SDL_ConvertSurfaceFormat(bmp, SDL_PIXELFORMAT_RGBA32, 0);
      SDL_FreeSurface(bmp);
      assert(rgba);
      auto pixel = [&](int x, int y) {
        assert(x >= 0 && y >= 0 && x < rgba->w && y < rgba->h);
        return static_cast<const Uint8 *>(rgba->pixels) + y * rgba->pitch + x * 4;
      };
      // Markers stay centered on terrain and repeat across the torus seams.
      std::vector<int> markerX, markerY;
      for (const auto &start : expectedStarts) {
        markerX.push_back(rect.x + start.x * rect.w / mapW);
        markerY.push_back(rect.y + start.y * rect.h / mapH);
      }
      auto underMarker = [&](int x, int y) {
        for (size_t j = 0; j < expectedStarts.size(); ++j)
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              const int cx = markerX[j] + dx * rect.w, cy = markerY[j] + dy * rect.h;
              if (x >= cx - 10 && x < cx + 10 && y >= cy - 10 && y < cy + 10)
                return true;
            }
        return false;
      };
      // No thumbnail letterbox inside the map. The terrain palette has no black, but a start
      // near the map's corner puts its black number label there, so rather than one corner
      // pixel, check every pixel of the first row and column that no marker covers: a
      // letterbox band would blacken one of them whole.
      for (int x = rect.x + 1; x < rect.x + rect.w - 1; ++x)
        if (!underMarker(x, rect.y + 1)) {
          const auto p = pixel(x, rect.y + 1);
          assert(p[0] || p[1] || p[2]);
        }
      for (int y = rect.y + 1; y < rect.y + rect.h - 1; ++y)
        if (!underMarker(rect.x + 1, y)) {
          const auto p = pixel(rect.x + 1, y);
          assert(p[0] || p[1] || p[2]);
        }
      for (size_t i = 0; i < expectedStarts.size(); ++i) {
        const int sampleX = rect.x + (markerX[i] - rect.x - 7 + rect.w) % rect.w;
        const int sampleY = rect.y + (markerY[i] - rect.y - 7 + rect.h) % rect.h;
        // The frame is painted over the outermost terrain pixels.
        if (sampleX == rect.x || sampleX == rect.x + rect.w - 1 ||
            sampleY == rect.y || sampleY == rect.y + rect.h - 1)
          continue;
        bool covered = false;
for (size_t j = i + 1; j < expectedStarts.size() && !covered; ++j)
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      const int cx = markerX[j] + dx * rect.w, cy = markerY[j] + dy * rect.h;
      covered = covered || (sampleX >= cx - 10 && sampleX < cx + 10 &&
                            sampleY >= cy - 10 && sampleY < cy + 10);
    }
        if (covered)
          continue;
        const auto &start = expectedStarts[i];
        const auto swatch = pixel(sampleX, sampleY);
        if (!(swatch[0] == start.color.r && swatch[1] == start.color.g && swatch[2] == start.color.b))
          std::printf("marker mismatch %s: start (%d,%d) map %dx%d rect %d,%d %dx%d expected"
                      " %d,%d,%d got %d,%d,%d\n",
                      name.c_str(), start.x, start.y, mapW, mapH, rect.x, rect.y, rect.w, rect.h,
                      start.color.r, start.color.g, start.color.b, swatch[0], swatch[1], swatch[2]);
        assert(swatch[0] == start.color.r && swatch[1] == start.color.g && swatch[2] == start.color.b);
      }
      SDL_FreeSurface(rgba);
    }
    puts("PASS rectangular aspect ratios, cropped terrain and colony marker pixels");
    // The landscape picker: every playable landscape previewed on background threads,
    // selection by click and keys, regeneration with fresh seeds, and the lobby then
    // playing exactly the map that was shown.
    {
      screen.setup.generatorHistory.select(screen.setup.generator,
                                           GeneratorRegistry::builtins().idOf("contested-commons"));
      screen.setup.generator.wDec = screen.setup.generator.hDec = 8;
      screen.setup.setCapacity(4);
      screen.invalidate();
      paint();
      assert(std::any_of(screen.controls->hits.begin(), screen.controls->hits.end(),
                         [](const auto &h) { return h.id == "generator/landscape"; }));
      const auto entries = screen.landscapeEntries();
      assert(entries.size() == GeneratorRegistry::builtins().methods(false).size());
      for (const auto &[method, request] : entries)
        assert(request.method == method && request.nbTeams == 4 && request.wDec == 8 &&
               request.hDec == 8);
      std::vector<LandscapePickerScreen::Entry> shown;
      for (const auto &[method, request] : entries)
        shown.push_back({GenerationRequest::methodName(method), request});
      const int current =
          GeneratorRegistry::builtins().selectionIndex(screen.setup.generator.method, false);
      LandscapePickerScreen picker("Landscape", shown, current);
      picker.dispatchInit();
      assert(picker.previewer.threadCount() >= 1 && picker.busy());
      picker.dispatchPaint(false); // placeholders while every tile is still pending
      globalContainer->gfx->printScreen(output + "/landscape-picker-pending.bmp");
      auto settle = [&] {
        const Uint32 deadline = SDL_GetTicks() + 120000;
        while (picker.busy()) {
          assert(Sint32(SDL_GetTicks() - deadline) < 0);
          SDL_Delay(10);
          picker.dispatchTimer(SDL_GetTicks());
        }
        picker.dispatchTimer(SDL_GetTicks());
        picker.dispatchPaint(false);
        for (auto &tile : picker.tiles) {
          if (tile.widget && tile.widget->transitioning) {
            tile.widget->transitionPending = false;
            tile.widget->transitionStarted = SDL_GetTicks() - MapPreview::TransitionDurationMs - 1;
          }
        }
        picker.dispatchPaint(false);
      };
      auto pick = [&](const std::string &id) {
        picker.dispatchPaint(false);
        auto find = [&] {
          return std::find_if(picker.controls->hits.begin(), picker.controls->hits.end(),
                              [&](const auto &h) { return h.id == id; });
        };
        auto hit = find();
        assert(hit != picker.controls->hits.end());
        if (hit->region >= 0) {
          // Tiles below the fold are clipped, so bring one into view before clicking it.
          auto &region = picker.controls->regions[hit->region];
          if (hit->box.y < region.box.y)
            picker.controls->scroll(hit->region, hit->box.y - region.box.y);
          else if (hit->box.y + hit->box.h > region.box.y + region.box.h)
            picker.controls->scroll(hit->region,
                                    hit->box.y + hit->box.h - region.box.y - region.box.h);
          picker.dispatchPaint(false);
          hit = find();
        }
        const auto r = hit->box;
        for (auto type : {SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP}) {
          SDL_Event e = {};
          e.type = type;
          e.button.button = SDL_BUTTON_LEFT;
          e.button.x = r.x + r.w / 2;
          e.button.y = hit->region == 30 ? r.y + r.h - 24 : r.y + r.h / 2;
          picker.dispatchEvents(&e);
        }
        picker.dispatchPaint(false);
      };
      auto pickerKey = [&](SDL_Keycode key) {
        SDL_Event e = {};
        e.type = SDL_KEYDOWN;
        e.key.keysym.sym = key;
        picker.dispatchEvents(&e);
        picker.dispatchPaint(false);
      };
      settle();
      globalContainer->gfx->printScreen(output + "/landscape-picker.bmp");
      auto seedsShown = [&] {
        std::vector<std::uint32_t> seeds;
        for (const auto &tile : picker.tiles) {
          assert(tile.preview.state == LandscapePreviewer::State::Ready && tile.widget && tile.widget->isThumbnailLoaded() &&
                 tile.preview.width == 256 && tile.preview.height == 256 &&
                 tile.preview.starts.size() == 4);
          seeds.push_back(tile.preview.seed);
        }
        assert(std::set<std::uint32_t>(seeds.begin(), seeds.end()).size() == seeds.size());
        return seeds;
      };
      const auto first = seedsShown();
      assert(picker.selection() == current && picker.chosenSeed() == first[current]);
      // A click selects; arrows move by one tile or one row; Escape cancels.
      const int other = (current + 1) % int(shown.size());
      pick("landscape/" + std::to_string(other));
      assert(picker.selection() == other && picker.returnCode == 0);
      // Native image gestures inspect without confirming the selected landscape.
      {
        auto *widget = picker.tiles[other].widget;
        const auto area = widget->mapArea();
        SDL_Event e = {};
        e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT;
        e.button.x = area.x + area.w / 3; e.button.y = area.y + area.h / 3;
        picker.dispatchEvents(&e);
        e = {}; e.type = SDL_MOUSEMOTION; e.motion.state = SDL_BUTTON_LMASK;
        e.motion.x = area.x + 2 * area.w / 3; e.motion.y = area.y + 2 * area.h / 3;
        picker.dispatchEvents(&e);
        assert(widget->dragging && widget->view.offsetX > 0 && picker.returnCode == 0);
        e = {}; e.type = SDL_MOUSEBUTTONUP; e.button.button = SDL_BUTTON_LEFT;
        e.button.x = -20; e.button.y = -20;
        picker.dispatchEvents(&e);
        assert(!widget->dragging && picker.activePreview == -1 && picker.returnCode == 0);
        const auto before = widget->worldArea();
        const double anchor = MapPreviewGeometry::wrap(double(widget->mouseX - before.x) / before.w - widget->view.offsetX);
        e = {}; e.type = SDL_MOUSEWHEEL; e.wheel.y = 1;
        picker.dispatchEvents(&e);
        const auto after = widget->worldArea();
        assert(widget->zoom > 1 && std::abs(anchor - MapPreviewGeometry::wrap(double(widget->mouseX - after.x) / after.w - widget->view.offsetX)) < 1e-12);
        e = {}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_RIGHT;
        e.button.x = area.x + area.w / 2; e.button.y = area.y + area.h / 2;
        picker.dispatchEvents(&e);
        assert(widget->zoom == 1 && widget->view.offsetX == 0 && picker.returnCode == 0);
      }
      pickerKey(SDLK_LEFT);
      assert(picker.selection() == std::max(0, other - 1));
      pickerKey(SDLK_DOWN);
      assert(picker.selection() ==
             std::min(int(shown.size()) - 1, std::max(0, other - 1) + picker.columns));
      pickerKey(SDLK_ESCAPE);
      assert(picker.returnCode == LandscapePickerScreen::CANCEL);
      // Regenerate all rolls every landscape again with fresh seeds.
      pick("landscape/regenerate");
      assert(picker.busy());
      assert(!picker.chosenSeed());
      const int pendingReturnCode = picker.returnCode;
      picker.confirm();
      assert(picker.returnCode == pendingReturnCode);
      settle();
      const auto second = seedsShown();
      for (size_t i = 0; i < first.size(); ++i)
        assert(first[i] != second[i]);
      // Randomize parameters rolls every landscape with its controls drawn at random: the sheet
      // still fills with maps (a refused set is redrawn), and each tile's request is one its
      // generator accepts, at the lobby's size and colony count.
      pick("landscape/randomize");
      assert(picker.busy());
      settle();
      seedsShown();
      {
        bool anyDiffer = false;
        for (size_t i = 0; i < shown.size(); ++i) {
          const GenerationRequest rolled = picker.previewer.request(i);
          assert(rolled.method == shown[i].request.method && rolled.nbTeams == 4 &&
                 rolled.wDec == 8 && rolled.hDec == 8);
          assert(validateGenerationRequest(rolled, GeneratorRegistry::builtins().at(rolled.method))
                     .empty());
          anyDiffer = anyDiffer || rolled.options != shown[i].request.options;
        }
        assert(anyDiffer);
      }
      globalContainer->gfx->printScreen(output + "/landscape-picker-randomized.bmp");
      // Reset to defaults puts every landscape back on its registered controls at the sheet's
      // size and colony count, and rolls the sheet again.
      pick("landscape/reset");
      assert(picker.busy());
      settle();
      seedsShown();
      for (size_t i = 0; i < shown.size(); ++i) {
        const GenerationRequest rolled = picker.previewer.request(i);
        GenerationRequest expected;
        expected.setMethodDefaults(shown[i].request.method);
        assert(rolled.options == expected.options && rolled.nbTeams == 4 && rolled.wDec == 8 &&
               rolled.hDec == 8);
      }
      pick("landscape/randomize");
      settle();
      seedsShown();
      // Using a randomized landscape hands the lobby the parameters it was shown with.
      {
        pick("landscape/" + std::to_string(other));
        const GenerationRequest rolled = picker.chosenRequest();
        assert(rolled.method == entries[other].first);
        screen.applyLandscape(entries[other].first, picker.chosenSeed(), &rolled);
        assert(screen.setup.generator.method == entries[other].first &&
               screen.setup.generator.options == rolled.options &&
               screen.setup.generator.nbTeams == 4);
        // Back to the landscapes' own parameters for the checks below. Reset, not Regenerate:
        // regenerating keeps the random draw, and a random ridge layout can fail validation
        // once the map is resized below.
        pick("landscape/reset");
        settle();
        seedsShown();
      }
      // Return confirms the selection, as does clicking the selected tile.
      pick("landscape/" + std::to_string(other));
      pickerKey(SDLK_RETURN);
      assert(picker.returnCode == other);
      pick("landscape/" + std::to_string(other));
      assert(picker.returnCode == other);
      pick("landscape/use");
      assert(picker.returnCode == other);
      globalContainer->gfx->printScreen(output + "/landscape-picker-selected.bmp");
      // The lobby then rolls the very seed the picker showed: same starts, same map header.
      const auto seed = *picker.chosenSeed();
      const auto starts = picker.tiles[other].preview.starts;
      // As the lobby's Use does: the parameters travel with the seed. Without them the lobby
      // would keep the random draw it was handed above and roll a different map.
      const GenerationRequest request = picker.chosenRequest();
      screen.applyLandscape(entries[other].first, seed, &request);
      assert(screen.previewPending && screen.chosenSeed == seed &&
             screen.setup.generator.options == request.options &&
             screen.setup.generator.method == entries[other].first);
      preview();
      assert(screen.validMap && !screen.chosenSeed);
      assert(screen.preview->starts.size() == starts.size());
      for (size_t i = 0; i < starts.size(); ++i)
        assert(screen.preview->starts[i].x == starts[i].x &&
               screen.preview->starts[i].y == starts[i].y &&
               screen.preview->starts[i].color.r == starts[i].color.r &&
               screen.preview->starts[i].color.g == starts[i].color.g &&
               screen.preview->starts[i].color.b == starts[i].color.b);
      {
        Game shownMap(nullptr);
        GAGCore::BinaryInputStream in(
            Toolkit::getFileManager()->openInputStreamBackend(screen.snapshot));
        assert(shownMap.load(&in) && shownMap.gameHeader.getRandomSeed() == seed);
      }
      capture("landscape-applied");
      // An edit after the pick drops the shown seed: the next preview samples candidates again.
      screen.applyLandscape(entries[other].first, seed);
      assert(screen.chosenSeed == seed);
      clickControl("generator/width");
      assert(screen.controls->popup.open);
      // Enlarge it: shrinking can violate the selected landscape's minimum home spacing.
      keyEvent(SDLK_DOWN);
      keyEvent(SDLK_RETURN);
      assert(!screen.chosenSeed && screen.previewPending);
      preview();
      assert(screen.validMap);
      std::cout << "PASS landscape picker: " << shown.size() << " previews on "
                << picker.previewer.threadCount()
                << " threads, selection, regeneration and the shown map played\n";
    }
    screen.setup.generatorHistory.select(screen.setup.generator, MapGenerationDescriptor::eRIVER);
    screen.setup.setCapacity(12);
    screen.setup.generator.wDec = screen.setup.generator.hDec = 8;
    screen.invalidate();
    assert(screen.generateMap());
    screen.setup.setController(0, CustomGameSetup::Computer);
    screen.activateGroup(screen.groups[1]);
    capture("players-12-1000");
    clickControl("colony/0/controller");
    assert(!screen.controls->popup.enabled[CustomGameSetup::Shared]);
    capture("controller-limit");
    keyEvent(SDLK_DOWN);
    keyEvent(SDLK_RETURN);
    assert(screen.controls->popup.open && screen.setup.controllerCount() == 12);
    keyEvent(SDLK_ESCAPE);
    screen.controls->regions[1].offset = screen.controls->regions[1].maximum;
    capture("players-12-scrolled");
    clickControl("colony/11/ai");
    int rosterOffset = screen.controls->regions[1].offset;
    keyEvent(SDLK_DOWN);
    keyEvent(SDLK_RETURN);
    assert(screen.controls->regions[1].offset == rosterOffset);
    // Sequential keyboard focus scrolls an off-screen control into view.
    screen.controls->focus = "colony/11/team";
    keyEvent(SDLK_TAB);

		screen.activateGroup(screen.groups[2]);
		capture("rules-1000");
		screen.controls->regions[2].offset = screen.controls->regions[2].maximum;
		capture("rules-scrolled");
		std::cout << "PASS native rendering, snapshot reroll ownership, "
					 "invalidation, failure "
					 "recovery, preserved alliances\n";
	}
	static void model()
	{
		CustomGameSetup s;
		assert(s.capacity == 4 && s.activeColonies() == 4 && s.controllerCount() == 4 &&
			   s.humanColony() == 0);
		assert(s.presetTeams(1));
		auto alliances = s.colonies;
		s.colonies[2].ai = AI::CASTOR;
		for (int i = 0; i < 4; ++i)
			assert(s.colonies[i].alliance == alliances[i].alliance);
		assert(s.setController(0, CustomGameSetup::Shared) && s.controllerCount() == 5);
		s.setCapacity(2);
		s.setCapacity(4);
		assert(s.colonies[3].alliance == alliances[3].alliance);
		assert(s.setController(3, CustomGameSetup::Human));
		assert(s.humanColony() == 3 && s.colonies[0].controller == CustomGameSetup::Computer);
		GameHeader h;
		s.writeHeader(h, "test");
		assert(h.getBasePlayer(0).teamNumber == 3 && h.getNumberOfPlayers() == 4);
		assert(s.setController(3, CustomGameSetup::Shared));
		s.writeHeader(h, "test");
		assert(h.getNumberOfPlayers() == 5);
		assert(h.getBasePlayer(4).teamNumber == 3);
		s.setCapacity(12);
		assert(!s.validation().empty());
		assert(!s.setController(2, CustomGameSetup::Shared));
		assert(s.setController(3, CustomGameSetup::Computer));
		assert(s.controllerCount() == 12 && !s.humanColony());
		assert(!s.presetTeams(1) && !s.presetTeams(2));
		assert(s.presetTeams(0));
		s.setCapacity(4);
		auto revision = s.mapRevision;
		s.presetRules(1);
		assert(s.speed == 3 && s.generator.nbWorkers == 8 && s.mapRevision > revision);
		s.presetRules(2);
		assert(s.revealed && s.prestige);
		s.presetRules(3);
		assert(!s.prestige);
		for (int i = 0; i < 4; ++i)
			s.setController(i, CustomGameSetup::Closed);
		assert(!s.validation().empty());
		std::cout << "PASS model: defaults, alliances, restoration, shared "
					 "control, human "
					 "movement, limits, presets\n";
	}
	static void engine(const std::string &map, int control, const std::string &save)
	{
		Engine e;
		auto header = Engine::loadMapHeader(map);
		CustomGameSetup s;
		s.setCapacity(header.getNumberOfTeams());
		s.setController(0, (CustomGameSetup::Controller)control);
		GameHeader game;
		s.writeHeader(game, "test");
		assert(e.initGame(header, game, true, false, false, map) == Engine::EE_NO_ERROR);
		assert(globalContainer->liveSpectating == (control == CustomGameSetup::Computer));
		assert(!globalContainer->replaying);
		std::vector<CountingAI *> counters;
		for (int i = 0; i < e.gui.game.gameHeader.getNumberOfPlayers(); ++i)
			if (e.gui.game.players[i]->ai)
			{
				auto ai = e.gui.game.players[i]->ai;
				delete ai->aiImplementation;
				auto count = new CountingAI;
				ai->aiImplementation = count;
				counters.push_back(count);
			}
		if (globalContainer->liveSpectating)
		{
			e.gui.localPlayer = 2; // Optional viewpoint must not change controller routing.
			e.gui.orderQueue.push_back(std::make_shared<PlayerQuitsGameOrder>(0));
			assert(e.gui.getOrder()->getOrderType() == ORDER_NULL);
			assert(e.gui.orderQueue.empty());
			assert(globalContainer->replayVisibleTeams == 0xffffffffu &&
				   !globalContainer->replayShowFog);
		}
		e.gatherAndAdvanceOrders(true);
		for (auto ai : counters)
			assert(ai->calls == 1);
		// Repeating a not-ready tick must not ask any AI twice.
		e.gatherAndAdvanceOrders(false);
		for (auto ai : counters)
			assert(ai->calls == 1);
		std::cout << "PASS order routing: mode " << control
				  << ", every AI once, no duplicate polls\n";
		// Restore real AIs before serializing; counting AIs have no wire state.
		for (int i = 0; i < e.gui.game.gameHeader.getNumberOfPlayers(); ++i)
			if (e.gui.game.players[i]->ai)
				e.gui.game.players[i]->makeItAI(AI::ECONO);
		{
			GAGCore::BinaryOutputStream out(
				Toolkit::getFileManager()->openOutputStreamBackend(save));
			e.gui.save(&out, "Custom test");
		}
	}
	static void sessionReplay(const std::string &map, int control, bool solo = false)
	{
		globalContainer->automaticEndingGame = true;
		globalContainer->automaticEndingSteps = 500;
		globalContainer->automaticGameGlobalEndConditions = false;
		{
			const auto transient = (std::filesystem::temp_directory_path() /
									("glob2-live-test-" + std::to_string(getpid()) + ".map"))
									   .string();
			std::filesystem::copy_file(map, transient,
									   std::filesystem::copy_options::overwrite_existing);
			Engine e;
			auto header = Engine::loadMapHeader(transient);
			CustomGameSetup setup;
			setup.setCapacity(header.getNumberOfTeams());
			setup.setController(0, (CustomGameSetup::Controller)control);
			if (solo)
				for (int i = 1; i < setup.capacity; ++i)
					setup.setController(i, CustomGameSetup::Closed);
			GameHeader players;
			setup.writeHeader(players, "test");
			assert(e.initGame(header, players, true, false, false, transient) ==
				   Engine::EE_NO_ERROR);
			std::filesystem::remove(transient);
			e.run();
		}
		{
			Engine e;
			assert(e.loadReplay("replays/last_game.replay") == Engine::EE_NO_ERROR);
			assert(globalContainer->replaying && !globalContainer->liveSpectating);
			globalContainer->automaticEndingSteps = 250;
			e.run();
			e.clearReplayState();
		}
		globalContainer->automaticEndingGame = false;
		std::cout << "PASS real match and replay playback: control " << control << " solo " << solo
				  << "\n";
	}
	static void reload(const std::string &save, bool watching, int controllers)
	{
		Engine e;
		assert(e.initCustom(save) == Engine::EE_NO_ERROR);
		assert(globalContainer->liveSpectating == watching);
		assert(e.gui.game.gameHeader.getNumberOfPlayers() == controllers);
		std::cout << "PASS save/load: " << (watching ? "AI-only" : "human/shared") << "\n";
	}
};
int main(int argc, char **argv)
{
	GlobalContainer globals("glob2-custom-setup-tests");
	globalContainer = &globals;
	globals.runNoX = argc < 2;
	globals.settings.rememberUnit = false;
	globals.settings.screenWidth = argc > 2 && std::string(argv[2]) == "large" ? 1000 : 640;
	globals.settings.screenHeight = argc > 2 && std::string(argv[2]) == "large" ? 700 : 480;
	globals.settings.screenFlags = GraphicContext::USEGPU;
	globals.settings.mute = true;
	globals.load();
	if (argc > 1 && (std::string(argv[1]) == "preferences-write" || std::string(argv[1]) == "preferences-read"))
	{
		CustomGameSetupHarness::preferencesScreen(std::string(argv[1]) == "preferences-write");
		return 0;
	}
	Toolkit::getFileManager()->remove(CustomGamePreferences::filename);
	CustomGameSetupHarness::preferencesModel();
	CustomGameSetupHarness::preferencesOptions();
	CustomGameSetupHarness::landscapeRandomOrder();
	assert(SDLNet_Init() == 0);
	if (argc > 2 && std::string(argv[2]) == "ui")
	{
		for (int control : {int(CustomGameSetup::Computer), int(CustomGameSetup::Human),
							int(CustomGameSetup::Shared)})
			CustomGameSetupHarness::ui(argv[1], control);
		return 0;
	}
	if (argc > 1)
	{
		CustomGameSetupHarness::visual(argv[1], argc > 2 && std::string(argv[2]) == "ai-profile");
		return 0;
	}
	assert(SDLNet_Init() == 0);
	const auto playableMethods = GeneratorRegistry::builtins().methods(false);
	for (int method : playableMethods)
	{
		bool generated = false;
		for (int retry = 0; retry < 5 && !generated; ++retry)
		{
			Game game(nullptr);
			MapGenerator generator;
			GenerationRequest request;
			request.setMethodDefaults(method);
			request.seed = 0x5eed0000u + unsigned(method * 16 + retry);
			generated = generator.generateMap(game, request) && game.teamsCount() == 4;
		}
		assert(generated);
	}
	std::cout << "PASS all " << playableMethods.size() << " playable generator landscapes\n";

	CustomGameSetupHarness::model();
	const auto &selection = AINames::selectionOrder();
	assert(selection.back() == AI::NONE);
	assert((std::set<int>(selection.begin(), selection.end()) ==
		std::set<int>{AI::ECONO, AI::NUMBI, AI::WARRUSH, AI::CASTOR, AI::CORTEX,
			AI::CABINO, AI::NICOWAR, AI::MAXIMA, AI::NONE}));
	assert(selection.size() == 9);
	for (size_t i = 1; i + 1 < selection.size(); ++i)
		assert(AINames::getAIStrength(selection[i-1]) <= AINames::getAIStrength(selection[i]));
	for (int id : selection)
		if (id != AI::NONE)
			assert(AINames::getAISelectorText(id).find("(" +
				std::to_string(AINames::getAIStrength(id)) + ")") != std::string::npos);
	for (int id : AINames::selectionOrder())
		assert(AINames::selectionOrder()[AINames::selectionIndex(id)] == id);
	static_assert(AI::ECONO == 4, "Econo must retain its save ID");
	assert(AINames::parseAIName("Econo") == AI::ECONO);
	assert(AINames::getAISelectorText(AI::ECONO) == "Econo - Easy (" + std::to_string(AINames::getAIStrength(AI::ECONO)) + ") - No warriors");
	assert(AINames::getAIProfile(AI::CORTEX).find("wheat") != std::string::npos);
	assert(AINames::getAIProfile(AI::CORTEX).find("\n\nStrengths:") != std::string::npos);
	const auto dir =
		std::filesystem::temp_directory_path() / ("glob2-setup-test-" + std::to_string(getpid()));
	std::filesystem::create_directory(dir);
	const auto map = (dir / "generated.map").string(), save = (dir / "match.game").string();
	{
		// A river roll that cannot seat four starting colonies is normal, not a
		// bug: Map::makeRandomMap gives up when no grass patch is left far
		// enough from the teams already placed, and Game::makeRandomMap gives up
		// when the swarm or its workers will not fit. The game re-rolls rather
		// than reporting failure (CustomGameScreen::generateMap), and so does
		// the eight-landscape loop above. This call did neither, and a single
		// roll fails often enough -- measured at 1 in 25 on an untouched tree --
		// to have made this harness flake in CI.
		//
		// Re-rolling rather than pinning a seed is deliberate: the terrain comes
		// from HeightMap, which draws on rand() rather than syncRand, so a seed
		// would not reproduce the same map across platforms anyway -- and a seed
		// that happens to seat four colonies under glibc could fail every single
		// time under mingw or macOS libc.
		std::unique_ptr<Game> generated;
		for (int attempt = 0; attempt < 5 && !generated; ++attempt)
		{
			auto candidate = std::make_unique<Game>(nullptr);
			MapGenerator generator;
			MapGenerationDescriptor d;
			d.setMethodDefaults(MapGenerationDescriptor::eRIVER);
			d.nbTeams = 4;
			if (generator.generateMap(*candidate, d) && candidate->teamsCount() == 4)
				generated = std::move(candidate);
		}
		assert(generated);
		Game &g = *generated;
		CustomGameSetup setup;
		GameHeader header;
		setup.writeHeader(header, "test");
		g.setGameHeader(header);
		{
			GAGCore::BinaryOutputStream out(
				Toolkit::getFileManager()->openOutputStreamBackend(map));
			g.save(&out, true, "Random test");
		}
		Game loaded(nullptr);
		GAGCore::BinaryInputStream in(Toolkit::getFileManager()->openInputStreamBackend(map));
		assert(loaded.load(&in));
		assert(g.map.getW() == loaded.map.getW());
		assert(g.map.getH() == loaded.map.getH());
		for (int y = 0; y < g.map.getH(); ++y)
			for (int x = 0; x < g.map.getW(); ++x)
			{
				assert(g.map.getTerrain(x, y) == loaded.map.getTerrain(x, y));
				assert(g.map.getResource(x, y).type == loaded.map.getResource(x, y).type);
			}
		for (int i = 0; i < 4; ++i)
		{
			assert(g.teams[i]->startPosX == loaded.teams[i]->startPosX);
			assert(g.teams[i]->startPosY == loaded.teams[i]->startPosY);
		}
		std::cout << "PASS generated snapshot round trip: all terrain, resources, "
					 "and starts identical\n";
	}
	for (int control : {int(CustomGameSetup::Human), int(CustomGameSetup::Shared),
						int(CustomGameSetup::Computer)})
	{
		CustomGameSetupHarness::engine(map, control, save);
		CustomGameSetupHarness::reload(save, control == CustomGameSetup::Computer,
									   control == CustomGameSetup::Shared ? 5 : 4);
		CustomGameSetupHarness::sessionReplay(map, control);
	}
	CustomGameSetupHarness::sessionReplay("maps/FourSquares1.map", CustomGameSetup::Human, true);
	// Exercise new terrain through the real match/replay path, not just map bytes.
	// Explicit seeds and no rerolls keep failures reviewable; the engine's normal
	// replay checksum assertions remain active throughout playback.
	for (const char *id : {"sierpinski-gardens", "hilbert-river", "drowned-forest"})
	{
		Game game(nullptr);
		GenerationRequest request;
		request.setMethodDefaults(GeneratorRegistry::builtins().idOf(id));
		request.seed = 20001;
		request.wDec = request.hDec = 8;
		request.nbTeams = 4;
		MapGenerator generator;
		assert(generator.generateMap(game, request));
		const auto fractalMap = (dir / (std::string(id) + ".map")).string();
		{
			GAGCore::BinaryOutputStream out(
				Toolkit::getFileManager()->openOutputStreamBackend(fractalMap));
			game.save(&out, true, id);
		}
		CustomGameSetupHarness::sessionReplay(fractalMap, CustomGameSetup::Computer);
		std::cout << "PASS generated landscape replay: " << id << "\n";
	}

	std::filesystem::remove_all(dir);
	std::cout << "ALL CUSTOM SETUP TESTS PASSED\n";
}
