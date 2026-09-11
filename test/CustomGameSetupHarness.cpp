// SPDX-License-Identifier: GPL-3.0-or-later
#include "AIImplementation.h"
#include "AINames.h"
#include "CustomGameScreen.h"
#include "CustomGameSetup.h"
#include "CustomGamePreferences.h"
#include "Engine.h"
#include "FrontendTheme.h"
#include "GlobalContainer.h"
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
		original.setup.colonies[11].controller = CustomGameSetup::Closed;
		assert(original.setup.setController(3, CustomGameSetup::Shared));
		original.setup.colonies[3].ai = AI::CORTEX;
		original.setup.colonies[11].alliance = 7;
		original.setup.colonies[11].ai = AI::NICOWAR;
		CustomGamePreferences restored;
		const auto encoded = original.encode();
		assert(restored.decode(encoded) && restored.encode() == encoded);
		assert(restored.setup.mapRevision == 0);
		for (size_t length : {size_t(0), size_t(10), encoded.size() / 2, encoded.size() - 5})
		{
			assert(!restored.decode(encoded.substr(0, length)));
			assert(restored.encode() == encoded);
		}
		for (const auto &replacement : std::vector<std::pair<std::string, std::string>>{
			{"glob2-custom-game 1", "glob2-custom-game 2"},
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
	static void preferencesScreen(bool write)
	{
		auto *files = Toolkit::getFileManager();
		if (write) files->remove(CustomGamePreferences::filename);
		if (write)
		{
			CustomGameScreen screen;
			assert(screen.validMap && screen.setup.capacity == 4);
			assert(screen.setup.setController(2, CustomGameSetup::Shared));
			screen.setup.colonies[2].ai = AI::CORTEX;
			screen.setup.colonies[0].alliance = 2;
			screen.setup.colonies[11].ai = AI::NICOWAR;
			screen.setup.colonies[11].alliance = 7;
			screen.setup.presetRules(1);
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
				CustomGameScreen screen;
				assert(screen.validMap && screen.setup.capacity == 4 && screen.setup.speed == 0);
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
		click("Players tab", 320, 30);
		click("Inline controller", 170, 143);
		key(SDLK_DOWN);
		key(SDLK_RETURN);
		click("Shared control", 170, 143);
		key(SDLK_DOWN);
		key(SDLK_RETURN);
		click("2 vs 2 preset", 235, 100);
		click("Second colony AI", 350, 207);
		key(SDLK_DOWN, KMOD_NONE, 5);
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

	static void visual(const std::string &output)
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

    CustomGameScreen screen;
    screen.gfx = globalContainer->gfx;
    screen.dispatchInit();
    assert(screen.validMap && screen.setup.capacity == 4);
    screen.separateMapLibraries = false;
    screen.listMaps();
    assert(std::any_of(
        screen.mapPaths.begin(), screen.mapPaths.end(), [](const auto &path) {
          return std::filesystem::path(path).filename() == "FourSquares1.map";
        }));
    screen.separateMapLibraries = true;
    screen.listMaps();

    auto paint = [&] { screen.dispatchPaint(false); };
    auto keyEvent = [&](SDL_Keycode key) {
      SDL_Event e = {};
      e.type = SDL_KEYDOWN;
      e.key.keysym.sym = key;
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
    int rulesOffset = screen.controls->regions[2].offset;
    clickControl("rule/1/0");
    assert(!screen.setup.revealed &&
           screen.controls->regions[2].offset == rulesOffset);
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
      globalContainer->gfx->printScreen(output + "/" + name + ".bmp");
    };
    capture("map-640");
    // Randomize and Reset to defaults only apply to random maps.
    assert(std::none_of(screen.controls->hits.begin(), screen.controls->hits.end(),
                        [](const auto &h) {
                          return h.id == "map/randomize" || h.id == "generator/reset";
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
    screen.onTimer(screen.previewDue);
    assert(!screen.validMap);
    screen.controls->pressed.clear();
    screen.onTimer(screen.previewDue);
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
      screen.onTimer(screen.previewDue);
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
      screen.onTimer(screen.previewDue);
      assert(screen.validMap);
      capture("reset-640");
    }
    screen.setup.presetRules(1);
    screen.invalidate();
    assert(!screen.validMap);
    auto assignments = screen.setup.colonies;
    screen.setup.generator.options["water"] =
        screen.setup.generator.options["sand"] =
            screen.setup.generator.options["grass"] =
                screen.setup.generator.options["desert"] = 0;
    screen.onTimer(screen.previewDue);
    assert(!screen.validMap && !screen.previewPending);
    for (int i = 0; i < 4; ++i)
      assert(screen.setup.colonies[i].alliance == assignments[i].alliance);
    screen.setup.generator.options["water"] =
        screen.setup.generator.options["sand"] =
            screen.setup.generator.options["grass"] =
                screen.setup.generator.options["desert"] = 50;
    screen.invalidate();
    screen.onTimer(screen.previewDue);
    assert(screen.validMap);

    // Drive the same dropdown/steppers used by players, including measured
    // five-unit steps.
    auto landscape = [&](int method) {
      clickControl("generator/landscape");
      for (size_t i = 0; i < GeneratorRegistry::builtins().methods(false).size(); ++i)
        keyEvent(SDLK_UP);
      for (int i = 0; i < GeneratorRegistry::builtins().selectionIndex(method, false); ++i)
        keyEvent(SDLK_DOWN);
      keyEvent(SDLK_RETURN);
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
    landscape(MapGenerationDescriptor::eRIVER);

    screen.activateGroup(screen.groups[0]);
    capture("map-1000");
    // Rectangular terrain and markers must use the same cropped preview area.
    for (auto dimensions : {std::pair{9, 7}, std::pair{7, 9}, std::pair{9, 6}, std::pair{6, 9}}) {
      screen.setup.generatorHistory.select(screen.setup.generator, GenerationRequest::eCONTESTEDCOMMONS);
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
      const auto corner = pixel(rect.x + 1, rect.y + 1);
      assert(corner[0] || corner[1] || corner[2]); // no thumbnail letterbox inside the map
      for (const auto &start : expectedStarts) {
        const int x = rect.x + std::clamp(start.x * rect.w / mapW, 2, std::max(2, rect.w - 18));
        const int y = rect.y + std::clamp(start.y * rect.h / mapH, 2, std::max(2, rect.h - 18));
        const auto swatch = pixel(x + 1, y + 1);
        assert(swatch[0] == start.color.r && swatch[1] == start.color.g && swatch[2] == start.color.b);
      }
      SDL_FreeSurface(rgba);
    }
    puts("PASS rectangular aspect ratios, cropped terrain and colony marker pixels");
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
		CustomGameSetupHarness::visual(argv[1]);
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
	assert((AINames::selectionOrder() == std::vector<int>{AI::ECONO, AI::NUMBI, AI::WARRUSH, AI::CASTOR, AI::CORTEX, AI::NICOWAR, AI::NONE}));
	for (int id : AINames::selectionOrder())
		assert(AINames::selectionOrder()[AINames::selectionIndex(id)] == id);
	static_assert(AI::ECONO == 4, "Econo must retain its save ID");
	assert(AINames::parseAIName("Econo") == AI::ECONO);
	assert(AINames::getAISelectorText(AI::ECONO) == "Econo - Easy - No warriors");
	assert(AINames::getAISelectorText(AI::CORTEX).find("Medium") != std::string::npos);
	assert(AINames::getAIProfile(AI::CORTEX).find("wheat") != std::string::npos);
	assert(AINames::getAIProfile(AI::CORTEX).find("\n\nStrengths:") != std::string::npos);
	const auto dir =
		std::filesystem::temp_directory_path() / ("glob2-setup-test-" + std::to_string(getpid()));
	std::filesystem::create_directory(dir);
	const auto map = (dir / "generated.map").string(), save = (dir / "match.game").string();
	{
		Game g(nullptr);
		MapGenerator generator;
		MapGenerationDescriptor d;
		d.setMethodDefaults(MapGenerationDescriptor::eRIVER);
		d.nbTeams = 4;
		assert(generator.generateMap(g, d));
		assert(g.teamsCount() == 4);
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
	std::filesystem::remove_all(dir);
	std::cout << "ALL CUSTOM SETUP TESTS PASSED\n";
}
