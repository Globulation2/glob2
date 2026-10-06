// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeneratorRegistry.h"
#include <vector>
#include <string>
#include <map>
#include <fstream>
#include <utility>
#include <tuple>
#include <mutex>
#include <cstdio>
#include <cstdint>
#include "AIImplementation.h"
#include "AINames.h"
#include "CustomGameScreen.h"
#include "OnlineServices.h"
#include "InstanceConfig.h"
#include "Sha256.h"
#include <ScreenStack.h>
#include "CustomGameSetup.h"
#include "CustomGamePreferences.h"
#include "CustomGameRules.h"
#include "RulesetCatalog.h"
#include "RoomSetup.h"
#include <Stream.h>
#include <set>
#include "Engine.h"
#include "FrontendTheme.h"
#include "EngineFixtures.h"
#include "TeamStat.h"
#include <SDL3_image/SDL_image.h>
#include <cmath>
#include <cstdlib>
#include "GlobalContainer.h"
#include "LandscapePickerScreen.h"
#include "StartQualityScreen.h"
#include <ui/Screen.h>
#include "LobbyMapCatalog.h"
#include "LobbyMapPreview.h"
#include "MapGenerator.h"
#include "Order.h"
#include "Player.h"
#include "ReplayWriter.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <set>
#include <optional>
#include <nlohmann/json.hpp>
#include <unistd.h>

namespace
{
struct CountingAI : AIImplementation {
  int calls = 0;
  bool load(GAGCore::InputStream *, Player *, Sint32) override { return true; }
  void save(GAGCore::OutputStream *) override {}
  std::shared_ptr<Order> getOrder() override {
    ++calls;
    return std::make_shared<NullOrder>();
  }
};
} // namespace

// Named in friend declarations, so it stays at global scope.
struct CustomGameSetupHarness
{
    static void catalogExperimentsInPreview()
    {
        glob2test::HeadlessGame world({.teams=2, .header=true});
        auto json = nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        json["experiments"].push_back({{"key", "preview-building"}, {"label", "Preview"}, {"help", "Fixture"}});
        world.game.buildingsTypes.loadSnapshotJson(json.dump());
        world.game.configureBuildingCatalog();
        const auto path = glob2test::artifactDir() / "experiment-preview.map";
        {
            GAGCore::BinaryOutputStream out(Toolkit::getFileManager()->openOutputStreamBackend(path.string()));
            world.game.save(&out, true, "Preview experiment");
        }
        globalContainer->settings.experiments.set("preview-building", true, {"preview-building"});
        GAGGUI::ScreenStack stack(*globalContainer->gfx);
        CustomGameScreen screen(stack);
        for (int load=0; load<2; ++load)
        {
            REQUIRE(screen.loadMap(path.string()));
            CHECK(screen.getGameHeader().getExperiments().has("preview-building"));
            CHECK(screen.getGameHeader().getBuildingCatalogSnapshot() == world.game.buildingsTypes.snapshotJson());
        }
        REQUIRE(screen.loadMap((glob2test::sourceRoot() / "maps/FourSquares1.map.gz").string()));
        CHECK_FALSE(screen.getGameHeader().getExperiments().has("preview-building"));
        globalContainer->settings.experiments.clear();
    }

    static void catalogLaunch()
    {
        Online::ServicesOwner online;
        auto &services = online.get();
        std::string bytes;
        REQUIRE(Online::readMapBytes("maps/FourSquares1.map.gz", bytes));
        const auto hash = Online::Sha256::hex(bytes);
        REQUIRE(services.maps.insert(hash, bytes));
        GAGGUI::ScreenStack stack(*globalContainer->gfx);
        CustomGameScreen screen(stack);
        screen.loadCatalogMap({{"5f6a7b8c-9d0e-4f1a-8b2c-3d4e5f6a7b8c", hash, "Linked map"}, Online::OFFICIAL_INSTANCE_ORIGIN, Online::MapPlayRequest::Mode::Local});
        REQUIRE(!screen.setup.random);
        REQUIRE(!screen.validMap);
        REQUIRE(!screen.previewPending);
        screen.onTimer(SDL_GetTicks());
        REQUIRE(screen.validMap);
        REQUIRE(screen.sourceFile() == std::filesystem::canonical(
            std::filesystem::path(Toolkit::getFileManager()->getDir(0)) / *services.maps.path(hash)).string());
        REQUIRE(screen.setup.premadeMap == screen.sourceFile());
        REQUIRE(screen.getMapHeader().getNumberOfTeams() == 4);
        REQUIRE(!screen.generatedSnapshot);
        const auto brokenHash = Online::Sha256::hex("not a map");
        REQUIRE(services.maps.insert(brokenHash, "not a map"));
        screen.loadCatalogMap({{"5f6a7b8c-9d0e-4f1a-8b2c-3d4e5f6a7b8c", brokenHash, "Broken map"}, Online::OFFICIAL_INSTANCE_ORIGIN, Online::MapPlayRequest::Mode::Local});
        screen.onTimer(SDL_GetTicks());
        REQUIRE(!screen.validMap);
        REQUIRE(screen.sourceFile().empty());
        REQUIRE(!screen.message.empty());
    }

    static void probabilityVisual()
    {
        glob2test::HeadlessGame world({.teams = Team::MAX_COUNT, .header = true});
        for (int t = 0; t < Team::MAX_COUNT; ++t)
        {
            world.game.players[t]->name = "Long colony name " + std::to_string(t + 1);
            world.game.teams[t]->stats.getLatestStat()->totalUnit = 10 + t;
        }
        Scene scene;
        world.gui.extractScene(scene);
        world.gui.setPublishedScene(&scene);
        globalContainer->liveSpectating = true;
        world.gui.measurementPage = world.gui.statisticsPages() - 1;
        globalContainer->gfx->drawFilledRect(0, 0, 640, 480, 0, 0, 32);
        world.gui.drawStatisticsPage(195);
        const auto path = glob2test::artifactDir() / "probability-sixteen-colonies.png";
        REQUIRE(IMG_SavePNG(globalContainer->gfx->getSDLSurface(), path.string().c_str()));
        globalContainer->liveSpectating = false;
        world.gui.setPublishedScene(nullptr);
    }

    static void controllerHelp()
    {
        GAGGUI::ScreenStack stack(*globalContainer->gfx);
        CustomGameScreen screen(stack);
        screen.beginExecution(globalContainer->gfx);
        auto fields=screen.colonyFields(0,screen.presentation());
        fields.controller->tap({},screen.host());
        REQUIRE(screen.host().popupOpen());
        std::string text;
        std::function<void(GAGGUI::ui::Node*)> collect=[&](auto* node) {
            text+=node->accessibleText();
            for(auto& child:node->children) collect(child.get());
        };
        collect(screen.host().popupRoot());
        CHECK(text.find(std::to_string(Team::MAX_COUNT))!=std::string::npos);
        CHECK(text.find("%0")==std::string::npos);
    }

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
		s.winProbabilityPermille = 970;
	}
	static void checkExtraRules(const CustomGameSetup &s)
	{
		REQUIRE((s.noResourceGrowth && s.instantConstruction && s.noHunger));
		REQUIRE((s.resourceScarcity == 3 && s.stockpileStart == 3));
		REQUIRE((s.unitUpgradesDisabled && s.unitsFearless && s.permadeathDisabled && s.peacefulMode));
		REQUIRE((s.glassCannonLevel == 2 && s.buildingHpLevel == 2));
		REQUIRE((s.startingUnitLevel == 3 && s.suddenDeathMinutes == 90));
		REQUIRE(s.winProbabilityPermille == 970);
	}
	static void preferencesModel()
	{
		CustomGamePreferences original;
		original.setup.random = true;
		original.setup.capacity = original.setup.generator.nbTeams = Team::MAX_COUNT;
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
		original.setup.applyRuleset("quick-clash");
		original.setup.prestige = false;
		original.setup.revealed = true;
		original.setup.locked = false;
		setExtraRules(original.setup);
		original.setup.colonies[11].controller = CustomGameSetup::Closed;
		REQUIRE(original.setup.setController(3, CustomGameSetup::Shared));
		original.setup.colonies[3].ai = AI::JAVASCRIPT;
		original.setup.colonies[3].aiLibraryId = "91";
		original.setup.colonies[11].alliance = 7;
		original.setup.colonies[11].ai = AI::NICOWAR;
		CustomGamePreferences restored;
		const auto encoded = original.encode();
		REQUIRE((restored.decode(encoded) && restored.encode() == encoded));
		checkExtraRules(restored.setup);
		REQUIRE(restored.setup.mapRevision == 0);
		REQUIRE(restored.landscapeSortOrder == 1);
		// Format 5 retains custom AI identities but predates probability victory.
		{
			auto old = encoded;
			old.replace(0, std::string("glob2-custom-game 6").size(), "glob2-custom-game 5");
			const auto at = old.find("probability ");
			REQUIRE(at != std::string::npos);
			old.erase(at, old.find('\n', at) - at + 1);
			CustomGamePreferences fromOld;
			REQUIRE(fromOld.decode(old));
			REQUIRE(fromOld.setup.winProbabilityPermille == 0);
			REQUIRE(fromOld.setup.colonies[3].aiLibraryId == "91");
		}
		// Older formats omit library identities; their rules retain their defaults.
		for (int version : {1, 2, 3, 4})
		{
			auto old = encoded;
			auto removeLine = [&](const std::string &prefix)
			{
				const auto at = old.find("\n" + prefix), eol = old.find('\n', at + 1);
				REQUIRE((at != std::string::npos && eol != std::string::npos));
				old.erase(at, eol - at);
			};
			removeLine("probability ");
			if (version < 3)
				removeLine("rules ");
			if (version == 1)
				removeLine("picker ");
			old.replace(0, std::string("glob2-custom-game 6").size(),
						"glob2-custom-game " + std::to_string(version));
			// Reproduce the old twelve-record wire layout, including its draft capacity.
			const auto coloniesAt = old.find("colonies 16\n");
			REQUIRE(coloniesAt != std::string::npos);
			// Formats 1–4 have exactly three numeric columns per colony.
			size_t rowStart = old.find('\n', coloniesAt) + 1;
			for (int i = 0; i < Team::MAX_COUNT; ++i)
			{
				auto rowEnd = old.find('\n', rowStart);
				auto suffix = old.find(" \"", rowStart);
				REQUIRE(suffix < rowEnd);
				old.erase(suffix, rowEnd - suffix);
				rowStart = suffix + 1;
			}
			if (version < 4)
			{
				old.replace(coloniesAt, std::string("colonies 16").size(), "colonies");
				size_t recordsEnd = old.find('\n', coloniesAt) + 1;
				for (int i = 0; i < 12; ++i)
					recordsEnd = old.find('\n', recordsEnd) + 1;
				old.erase(recordsEnd, old.find("end\n", recordsEnd) - recordsEnd);
				REQUIRE(old.find("setup 1 16") != std::string::npos);
				old.replace(old.find("setup 1 16"), 10, "setup 1 12");
				REQUIRE(old.find("nbTeams 16") != std::string::npos);
				old.replace(old.find("nbTeams 16"), 10, "nbTeams 12");
			}
			CustomGamePreferences fromOld;
			REQUIRE((fromOld.decode(old) && fromOld.landscapeSortOrder == (version == 1 ? 0 : 1)));
			REQUIRE(fromOld.setup.winProbabilityPermille == 0);
			REQUIRE(fromOld.setup.premadeMap == original.setup.premadeMap);
			REQUIRE(fromOld.setup.colonies[3].aiLibraryId.empty());
			REQUIRE((fromOld.setup.unitUpgradesDisabled == (version >= 3) &&
					 fromOld.setup.noHunger == (version >= 3)));
			REQUIRE((fromOld.setup.startingUnitLevel == (version >= 3 ? 3 : 0) &&
					 fromOld.setup.suddenDeathMinutes == (version >= 3 ? 90 : 0)));
		}
		// Formats before 7 stored the ruleset's English name, "Custom" once edited. Both load as
		// the matching ruleset id, keeping the saved rule values; an id this build lacks loads as
		// Standard instead of rejecting the draft.
		REQUIRE(restored.setup.rulesetId == "quick-clash");
		for (const auto &[version, label, id] : std::vector<std::tuple<std::string, std::string, std::string>>{
				 {"6", "Quick clash", "quick-clash"}, {"6", "Last colony standing", "last-colony-standing"},
				 {"6", "Open book", "open-book"}, {"6", "Standard", "standard"},
				 {"6", "Custom", "standard"}, {"7", "blitz", "blitz"}, {"7", "no-such-ruleset", "standard"}})
		{
			auto old = encoded;
			old.replace(0, std::string("glob2-custom-game 7").size(), "glob2-custom-game " + version);
			const auto at = old.find("\"quick-clash\"");
			REQUIRE(at != std::string::npos);
			old.replace(at, std::string("\"quick-clash\"").size(), "\"" + label + "\"");
			CustomGamePreferences fromOld;
			REQUIRE((fromOld.decode(old) && fromOld.setup.rulesetId == id));
			checkExtraRules(fromOld.setup);
		}
		for (size_t length : {size_t(0), size_t(10), encoded.size() / 2, encoded.size() - 5})
		{
			REQUIRE(!restored.decode(encoded.substr(0, length)));
			REQUIRE(restored.encode() == encoded);
		}
		for (const auto &replacement : std::vector<std::pair<std::string, std::string>>{
				 {"glob2-custom-game 7", "glob2-custom-game 8"},
				 {"rules 1 3", "rules 2 3"},
				 {"rules 1 3", "rules 1 4"},
				 {"2 3 90\nprobability", "2 4 90\nprobability"},
				 {"2 3 90\nprobability", "2 3 31\nprobability"},
				 {"probability 970", "probability 500"},
				 {"wDec 9", "wDec 31"},
				 {"nbWorkers 8", "nbWorkers -1"},
				 {"generator 4 5", "generator 0 5"},
				 {"generator 4 5", "generator 4 100"},
				 {"colonies 16\n1 1 0", "colonies 16\n99 1 0"},
				 {"colonies 16", "colonies 17"},
				 {"colonies 16", "colonies 0"}})
		{
			auto corrupt = encoded;
			auto at = corrupt.find(replacement.first);
			REQUIRE(at != std::string::npos);
			corrupt.replace(at, replacement.first.size(), replacement.second);
			REQUIRE((!restored.decode(corrupt) && restored.encode() == encoded));
		}
		REQUIRE(!restored.decode(encoded + "trailing junk"));
		// The teams' format name is written for older builds, which accept only these four,
		// and no longer read: any name loads, and the teams come from the saved alliances.
		{
			const std::string labels = std::string("labels \"") + original.setup.legacyFormat() + "\"";
			const auto at = encoded.find(labels);
			REQUIRE(at != std::string::npos);
			auto relabelled = encoded;
			relabelled.replace(at, labels.size(), "labels \"3 vs 5\"");
			REQUIRE((restored.decode(relabelled) && restored.encode() == encoded));
		}
		REQUIRE(!restored.decode(std::string(65537, 'x')));
		// Unfinished drafts remain editable rather than losing the user's choices.
		for (auto &c : original.setup.colonies) c.controller = CustomGameSetup::Closed;
		REQUIRE(restored.decode(original.encode()));
		REQUIRE(!restored.setup.validation().empty());
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
		REQUIRE(roundTrip(fjord, {{"lake-size", 50}}));
		// Fjord's lake size 0 and 90, and Ring world's lake density 0, lie outside the legacy
		// fields' old bounds and used to make the whole file fail to load.
		REQUIRE((roundTrip(fjord, {{"lake-size", 0}}) && roundTrip(fjord, {{"lake-size", 90}})));
		REQUIRE(roundTrip(ring, {{"lake-density", 0}}));
		// Every control's whole range survives, whether it lives in a legacy field or not.
		for (int method : GeneratorRegistry::builtins().methods(false))
			for (const auto &c : GenerationRequest::controls(method))
			{
				if (!roundTrip(method, {{c.id, c.minimum}}) || !roundTrip(method, {{c.id, c.maximum}}))
					std::cerr << "control " << c.id << " of " << GeneratorRegistry::builtins().at(method).id
							  << " does not survive its range " << c.minimum << ".." << c.maximum << "\n";
				REQUIRE((roundTrip(method, {{c.id, c.minimum}}) && roundTrip(method, {{c.id, c.maximum}})));
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
		REQUIRE((restored.decode(encoded) && restored.encode() == encoded));
		REQUIRE(restored.setup.generator.options == g.options);

		// The same file as an older build wrote it, without the options section.
		const auto at = encoded.find("\noptions ") + 1, eol = encoded.find('\n', at);
		const auto end = encoded.find("colonies ");
		REQUIRE((at > 0 && eol < end));
		REQUIRE(restored.decode(encoded.substr(0, at) + encoded.substr(end)));
		GenerationRequest defaults;
		defaults.setMethodDefaults(fjord);
		for (const auto &c : GenerationRequest::controls(fjord))
			REQUIRE(c.get(restored.setup.generator) == (c.id == "lake-size" ? 50 : c.get(defaults)));

		// An option this build doesn't have is ignored; a value outside its domain is corrupt.
		const int count = std::stoi(encoded.substr(at + 8, eol - at - 8));
		const auto withOption = [&](const std::string &line) {
			return encoded.substr(0, at) + "options " + std::to_string(count + 1) + "\n" + line +
				   "\n" + encoded.substr(eol + 1);
		};
		REQUIRE(restored.decode(withOption("retired-option 5")));
		REQUIRE((restored.setup.generator.options == g.options && restored.encode() == encoded));
		for (const char *corrupt : {"lake-connected 2", "resource-islands 21", "coast-roughness -1"})
		{
			REQUIRE(!restored.decode(withOption(corrupt)));
			REQUIRE(restored.encode() == encoded);
		}
		REQUIRE(!restored.decode(encoded.substr(0, at) + "options 3\nlake-connected 1\n" +
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
		REQUIRE((first.sortOrder == LandscapePickerScreen::SortOrder::Random &&
			  second.sortOrder == LandscapePickerScreen::SortOrder::Random));
		// A real shuffle, not the untouched catalog order left alone...
		REQUIRE(first.visible != identity);
		// ...but the SAME shuffle every time within this one process run, however many sheets are
		// opened - the process-lifetime permutation this fix's refinement asked for.
		REQUIRE(first.visible == second.visible);
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
		REQUIRE(third.visible == first.visible);
		// Alphabetical stays the deterministic alternative: same entries, same order, every time.
		LandscapePickerScreen sortedA("Landscape", shown, 0,
									  LandscapePickerScreen::SortOrder::Alphabetical);
		LandscapePickerScreen sortedB("Landscape", shown, 0,
									  LandscapePickerScreen::SortOrder::Alphabetical);
		REQUIRE(sortedA.visible == sortedB.visible);
		std::cout << "PASS landscape picker Random order is a real shuffle drawn once per process "
					 "run, Alphabetical stays stable\n";
	}
	static void previewPriority()
	{
		GenerationRequest request;
		request.setMethodDefaults(0);
		request.wDec = request.hDec = 6;
		request.nbTeams = 1;
		LandscapePreviewer queue({request, request, request}, -1, true);
		queue.poll();
		REQUIRE((queue.finished() == 0 && queue.attempts == std::vector<int>({0, 0, 0})));
		const auto seeds = queue.seeds;
		queue.prioritize({2, 0, 1});
		queue.poll(std::vector<std::size_t>{});
		REQUIRE(queue.finished() == 0);
		queue.poll(std::vector<std::size_t>{0});
		REQUIRE((queue.preview(0).state == LandscapePreviewer::State::Ready && queue.finished() == 1));
		// Reset while preserving roots so the unrestricted sequence checks exactly the same maps.
		queue.regenerate();
		queue.seeds = seeds;
		queue.prioritize({2, 0, 1});
		queue.poll();
		REQUIRE(queue.preview(2).state == LandscapePreviewer::State::Ready);
		REQUIRE((queue.finished() == 1 &&
			   queue.preview(0).state == LandscapePreviewer::State::Pending));
		queue.prioritize({1, 1, 1000}); // Duplicates/out-of-range indices cannot lose work.
		queue.poll();
		REQUIRE((queue.preview(1).state == LandscapePreviewer::State::Ready && queue.finished() == 2));
		queue.poll();
		REQUIRE((!queue.busy() && queue.finished() == 3 && queue.seeds == seeds));
		for (std::size_t i = 0; i < seeds.size(); ++i)
		{
			const auto expected = LandscapePreviewer::roll(request, seeds[i]);
			const auto actual = queue.preview(i);
			REQUIRE((actual.seed == expected.seed && actual.score == expected.score &&
				   actual.thumbnail.pixels()->rgb == expected.thumbnail.pixels()->rgb));
		}
		// The same priority survives regeneration; failed requests terminate in one attempt.
		GenerationRequest invalid = request;
		invalid.method = -1;
		queue.restart({invalid, invalid, invalid});
		queue.poll();
		REQUIRE((queue.preview(1).state == LandscapePreviewer::State::Failed &&
			   queue.finished() == 1));
		queue.regenerate();
		REQUIRE(queue.attempts == std::vector<int>({0, 0, 0}));
		queue.poll();
		REQUIRE((queue.preview(1).state == LandscapePreviewer::State::Failed &&
			   queue.finished() == 1));
		// Uniform places one team: asking for two forces all three placement retries.
		auto retry = request;
		retry.nbTeams = 2;
		queue.restart({retry, request});
		queue.prioritize({0, 1});
		queue.poll();
		REQUIRE((queue.preview(0).state == LandscapePreviewer::State::Pending &&
			   queue.finished() == 0));
		queue.prioritize({1, 0});
		queue.poll();
		REQUIRE(queue.preview(1).state == LandscapePreviewer::State::Ready);
		queue.poll();
		REQUIRE(queue.preview(0).state == LandscapePreviewer::State::Pending);
		queue.poll();
		REQUIRE((queue.preview(0).state == LandscapePreviewer::State::Failed &&
			   queue.finished() == 2));
		queue.reroll(0, request);
		queue.poll();
		REQUIRE((queue.preview(0).state == LandscapePreviewer::State::Ready &&
			   queue.attempts[0] == 1));
		queue.restart({});
		queue.prioritize({0});
		queue.poll();
		REQUIRE((!queue.busy() && queue.finished() == 0));
		std::cout << "PASS deferred viewport priority, reprioritization, stable seeds, yielding "
					 "retries, restart/reroll\n";
	}

	// Fixed pixels isolate UI delivery and scrolling costs from generator/seed variance.
	static void landscapePerformance(const std::string &output, bool verify)
	{
		std::filesystem::create_directories(output);
		using Clock = std::chrono::steady_clock;
		auto elapsed = [](auto start)
		{ return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };
		for (const auto *id : {"isles", "river", "contested-commons"})
		{
			GenerationRequest request;
			request.setMethodDefaults(GeneratorRegistry::builtins().idOf(id));
			request.wDec = request.hDec = 8;
			request.nbTeams = 4;
			const auto result = LandscapePreviewer::roll(request, 71);
			REQUIRE(result.state == LandscapePreviewer::State::Ready);
			std::uint64_t hash = 14695981039346656037ULL;
			for (auto byte : result.thumbnail.pixels()->rgb)
			{
				hash ^= byte;
				hash *= 1099511628211ULL;
			}
			std::cout << "MAP " << id << " root 71 seed " << result.seed << " pixels " << hash
					  << " score " << result.score;
			for (const auto &start : result.starts)
				std::cout << " start " << start.x << "," << start.y;
			std::cout << "\n";
		}
		std::vector<LandscapePickerScreen::Entry> entries;
		for (int i = 0; i < 67; ++i)
		{
			GenerationRequest request;
			request.method = -1; // Finish cheaply; results are replaced with the fixture below.
			entries.push_back({"Landscape " + std::to_string(100 + i), request});
		}
		LandscapePickerScreen picker("Landscape", entries, 0,
									 LandscapePickerScreen::SortOrder::Alphabetical);
		picker.beginExecution(globalContainer->gfx);
		picker.paintFrame(0);
		while (picker.busy())
		{
			picker.onTimer(SDL_GetTicks());
			SDL_Delay(1);
		}
		Map map;
		map.setSize(8, 8, GRASS);
		for (int y = 0; y < 256; ++y)
			for (int x = 128; x < 256; ++x)
				map.setUMatPos(x, y, WATER, 1);
		MapThumbnail image;
		image.loadFromMap(map);
		{
			std::lock_guard<std::mutex> lock(picker.previewer.mutex);
			for (auto &slot : picker.previewer.slots)
			{
				slot.state = LandscapePreviewer::State::Ready;
				slot.thumbnail = image;
				slot.width = slot.height = 256;
				slot.starts = {{64, 128, Color(240, 40, 40)}};
				++slot.revision;
			}
		}
		const auto delivery = Clock::now();
		picker.refresh();
		picker.paintFrame(SDL_GetTicks());
		std::cout << "BENCH delivery_ms " << elapsed(delivery) << "\n";
		int uploaded = 0;
		for (const auto &tile : picker.tiles)
			if (tile.widget && tile.widget->surface)
				++uploaded;
		std::cout << "BENCH initial_uploaded " << uploaded << " / " << entries.size() << "\n";
		if (verify)
			REQUIRE((uploaded > 0 && uploaded < int(entries.size())));
		auto capture = [&](const char *name)
		{
			globalContainer->gfx->printScreen(output + "/" + name);
			// The portable renderer reads back a requested capture before presenting.
			globalContainer->gfx->nextFrame();
		};
		capture("top.bmp");
		std::vector<double> frames;
		auto *grid = picker.host().find("landscape/grid");
		REQUIRE(grid);
		for (int step = 0; step < 80; ++step)
		{
			const int target = grid->scrollMaximum() * (step < 40 ? step : 79 - step) / 39;
			grid->scrollBy(target - grid->scrollOffset(), picker.host());
			const auto frame = Clock::now();
			picker.paintFrame(SDL_GetTicks());
			frames.push_back(elapsed(frame));
			if (verify)
				for (const auto &tile : picker.tiles)
					if (tile.widget)
						REQUIRE((!tile.widget->transitioning && !tile.widget->transitionPending));
			if (step == 20)
				capture("middle.bmp");
			if (step == 39)
				capture("bottom.bmp");
		}
		std::sort(frames.begin(), frames.end());
		std::cout << "BENCH scroll_ms median " << frames[frames.size() / 2] << " p95 "
				  << frames[frames.size() * 95 / 100] << " max " << frames.back() << "\n";
		if (verify)
		{
			for (const char *name : {"top.bmp", "middle.bmp", "bottom.bmp"})
				REQUIRE(std::filesystem::exists(output + "/" + name));
			std::cout << "PASS offscreen previews stay CPU-only; scrolling never fades\n";
		}
		picker.endExecute(0);
		picker.finishExecution();
	}

	static void preferencesScreen(bool write)
	{
		auto *files = Toolkit::getFileManager();
		if (write) files->remove(CustomGamePreferences::filename);
		if (write)
		{
			GAGGUI::ScreenStack screens(*globalContainer->gfx);
			CustomGameScreen screen(screens);
			// Nothing saved: a random map (FEEDBACK 2026-09-14). The premade library is what this
			// file is written with, so switch to it first.
			REQUIRE((screen.setup.random && screen.previewPending && screen.setup.capacity == 4));
			screen.setMapMode(false);
			REQUIRE((!screen.setup.random && screen.validMap));
			REQUIRE(screen.setup.setController(2, CustomGameSetup::Shared));
			screen.setup.colonies[2].ai = AI::CORTEX;
			screen.setup.colonies[0].alliance = 2;
			screen.setup.colonies[11].ai = AI::NICOWAR;
			screen.setup.colonies[11].alliance = 7;
			screen.setup.applyRuleset("quick-clash");
			setExtraRules(screen.setup);
			screen.setup.generator = fromLegacyDescriptor(maxedLegacy(MapGenerationDescriptor::eISLANDS, 3), 0);
			screen.expanded[1] = true;
			screen.userMaps = screen.separateMapLibraries;
			screen.librarySelection[1] = "maps/favorite-user-map.map";
			// Persist while the screen is still open, as normal edits do.
			screen.onTimer(SDL_GetTicks());
			CustomGamePreferences disk;
			REQUIRE((disk.load(*files) && disk.setup.colonies[2].controller == CustomGameSetup::Shared));
		}
		else
		{
			std::string premade;
			{
				GAGGUI::ScreenStack screens(*globalContainer->gfx);
				CustomGameScreen screen(screens);
				REQUIRE((screen.validMap && !screen.setup.random && screen.setup.capacity == 4));
				REQUIRE(screen.setup.colonies[2].controller == CustomGameSetup::Shared);
				REQUIRE(screen.setup.colonies[2].ai == AI::CORTEX);
				REQUIRE(screen.setup.colonies[0].alliance == 2);
				REQUIRE((screen.setup.colonies[11].ai == AI::NICOWAR && screen.setup.colonies[11].alliance == 7));
				REQUIRE((screen.setup.speed == 3 && screen.setup.rulesetId == "quick-clash"));
				checkExtraRules(screen.setup);
				REQUIRE(screen.expanded[1]);
				REQUIRE(screen.userMaps == screen.separateMapLibraries);
				REQUIRE(screen.librarySelection[1] == "maps/favorite-user-map.map");
				// Only the options Islands actually registers survive the
				// GenerationRequest round trip; compare against the same
				// achievable conversion rather than the raw field maximums.
				const auto expected = toLegacyDescriptor(fromLegacyDescriptor(maxedLegacy(MapGenerationDescriptor::eISLANDS, 3), 0));
				const auto restoredLegacy = toLegacyDescriptor(screen.setup.generator);
				for (const auto &field : CustomGamePreferences::fields())
					REQUIRE(restoredLegacy.*(field.member) == expected.*(field.member));
				REQUIRE(restoredLegacy.logRepeatAreaTimes == expected.logRepeatAreaTimes);
				premade = screen.setup.premadeMap;
				screen.setup.generator.nbTeams = 4;
				screen.setMapMode(true);
				REQUIRE(screen.previewPending);
			}
			{
				GAGGUI::ScreenStack screens(*globalContainer->gfx);
				CustomGameScreen screen(screens);
				REQUIRE((screen.setup.random && screen.previewPending && !screen.validMap));
				REQUIRE((!screen.generatedSnapshot && screen.source.empty()));
				REQUIRE(screen.setup.premadeMap == premade);
				// Switching back restores the premade choice without disturbing teams.
				screen.setMapMode(false);
				REQUIRE((screen.validMap && screen.setup.colonies[0].alliance == 2));
				screen.setup.premadeMap = "/missing/saved-map.map";
			}
			{
				GAGGUI::ScreenStack screens(*globalContainer->gfx);
				CustomGameScreen screen(screens);
				REQUIRE((!screen.validMap && !screen.setup.random && !screen.message.empty()));
				REQUIRE((screen.setup.colonies[2].ai == AI::CORTEX && screen.setup.speed == 3));
				REQUIRE(screen.setup.premadeMap == "/missing/saved-map.map");
			}
			files->writeAtomically(CustomGamePreferences::filename, [](GAGCore::OutputStream &out) {
				const std::string truncated = "glob2-custom-game 1\nsetup";
				out.write(truncated.data(), truncated.size(), "broken preferences");
			});
			{
				// A file the lobby cannot read is the same as none: a random map at four colonies,
				// its preview pending (FEEDBACK 2026-09-14: random maps are the default tab).
				GAGGUI::ScreenStack screens(*globalContainer->gfx);
				CustomGameScreen screen(screens);
				REQUIRE((screen.setup.random && screen.previewPending && !screen.validMap &&
					   screen.setup.capacity == 4 && screen.setup.speed == 0));
			}
			files->remove(CustomGamePreferences::filename);
		}
		std::cout << "PASS native preferences " << (write ? "write" : "reload, random preview, missing map and recovery") << "\n";
	}
	static void ui(const std::string &output, int control)
	{
		Toolkit::getFileManager()->remove(CustomGamePreferences::filename);
		FrontendTheme theme;
		globalContainer->settings.gameSpeed = 7;
		{
			Engine engine;
			GAGGUI::ScreenStack screens(*globalContainer->gfx);
			std::optional<GAGCore::CooperativeTask> load;
			std::shared_ptr<void> mapFile;
			auto owned = std::make_unique<CustomGameScreen>(screens);
			auto *lobby = owned.get();
			screens.push(std::move(owned),
				[&](GAGGUI::Screen &screen, int result)
				{
					if (result != CustomGameScreen::OK) return;
					auto &selected = static_cast<CustomGameScreen &>(screen);
					// Like SinglePlayerFlow, read the generated map only after the
					// stack has destroyed this screen.
					auto map = selected.getMapHeader();
					auto players = selected.getGameHeader();
					auto team = selected.getSelectedColor(0);
					auto speed = selected.selectedSpeed();
					if (auto bytes = selected.releaseSnapshot())
					{
						load.emplace(engine.initCustomFromBytesTask(map, players, team, speed, bytes));
						mapFile = bytes;
					}
					else
						load.emplace(engine.initCustomTask(map, players, team, speed, selected.sourceFile()));
				});
			Uint32 tick = SDL_GetTicks();
			auto frames = [&](int count, std::vector<SDL_Event> events = {})
			{
				for (int i = 0; i < count && screens.running(); ++i)
				{
					screens.frame(tick += 40, i == 0 ? events : std::vector<SDL_Event>{});
					SDL_Delay(1);
				}
			};
			auto top = [&]
			{
				auto *screen = dynamic_cast<GAGGUI::ui::UIScreen *>(screens.top());
				REQUIRE(screen);
				return screen;
			};
			auto click = [&](const std::string &key)
			{
				std::cout << "UI step: click " << key << std::endl;
				frames(1);
				top()->host().scrollIntoView(key);
				frames(1);
				const auto r = top()->host().bounds(key);
				SDL_Event down{};
				down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
				down.button.button = SDL_BUTTON_LEFT;
				down.button.down = true;
				down.button.x = r.x + r.w / 2;
				down.button.y = r.y + r.h / 2;
				SDL_Event up = down;
				up.type = SDL_EVENT_MOUSE_BUTTON_UP;
				up.button.down = false;
				frames(1, {down, up});
				frames(2);
			};
			auto key = [&](SDL_Keycode code, SDL_Keymod modifiers = SDL_KMOD_NONE, int repeats = 1)
			{
				std::cout << "UI step: key " << code << std::endl;
				for (int i = 0; i < repeats; ++i)
				{
					SDL_Event event{};
					event.type = SDL_EVENT_KEY_DOWN;
					event.key.key = code;
					event.key.mod = modifiers;
					frames(1, {event});
				}
				frames(1);
			};
			frames(3);
			// The lobby opens on a random map (2026-09-14); the flow was written from the premade
			// library, so start there and let the later Random mode click switch as it always did.
			click("map/mode/0");
			click("tab/1");
			click("colony/0/controller");
			key(SDLK_DOWN);
			key(SDLK_RETURN);
			click("colony/0/controller");
			key(SDLK_DOWN);
			key(SDLK_RETURN);
			// Teams: FFA, then 2 vs 2.
			click("teams");
			key(SDLK_DOWN);
			key(SDLK_RETURN);
			click("colony/1/ai");
			// Select the following row; the profile interaction below moves back once.
			const int aiSteps = AINames::selectionIndex(AI::NICOWAR) + 1 - AINames::selectionIndex(AI::NUMBI);
			key(aiSteps >= 0 ? SDLK_DOWN : SDLK_UP, SDL_KMOD_NONE, aiSteps >= 0 ? aiSteps : -aiSteps);
			key(SDLK_RETURN);
			click("colony/1/info");
			{
				auto *profile = dynamic_cast<CustomGameChoiceScreen *>(screens.top());
				REQUIRE(profile);
				profile->choose(AINames::selectionIndex(AI::NICOWAR));
				click("profile/use");
			}
			key(SDLK_3, SDL_KMOD_CTRL);
			// Wide layouts list the rulesets beside the rules; narrower ones open them as a screen.
			frames(1);
			if (!top()->host().find("ruleset/quick-clash"))
			{
				click("rules/ruleset");
				REQUIRE(dynamic_cast<RulesetChoiceScreen *>(screens.top()));
			}
			click("ruleset/quick-clash");
			REQUIRE((lobby->setup.rulesetId == "quick-clash" && lobby->setup.speed == 3));
			key(SDLK_1, SDL_KMOD_CTRL);
			click("map/mode/1");
			for (Uint32 started = SDL_GetTicks(); (!lobby->validMap || lobby->previewBusy()) && SDL_GetTicks() - started < 60000;)
				frames(1);
			REQUIRE((lobby->validMap && !lobby->previewBusy()));
			key(SDLK_2, SDL_KMOD_CTRL);
			if (control != CustomGameSetup::Shared)
			{
				click("colony/0/controller");
				key(SDLK_UP, SDL_KMOD_NONE, control == CustomGameSetup::Computer ? 1 : 2);
				key(SDLK_RETURN);
			}
			click("start");
			for (int i = 0; i < 200 && screens.running(); ++i)
				frames(1);
			REQUIRE(!screens.running());
			const bool loaded = load && load->run();
			mapFile.reset();
			REQUIRE(loaded);
			REQUIRE(globalContainer->liveSpectating == (control == CustomGameSetup::Computer));
			REQUIRE(globalContainer->settings.gameSpeed == 3);
			REQUIRE(engine.gui.game.gameHeader.getNumberOfPlayers() ==
				   (control == CustomGameSetup::Shared ? 5 : 4));
			REQUIRE(engine.gui.game.players[control == CustomGameSetup::Shared ? 2 : 1]
					   ->ai->implementationID == AI::NICOWAR);
			REQUIRE(engine.gui.game.gameHeader.getAllyTeamNumber(0) ==
				   engine.gui.game.gameHeader.getAllyTeamNumber(1));
			REQUIRE(engine.gui.game.gameHeader.getAllyTeamNumber(0) !=
				   engine.gui.game.gameHeader.getAllyTeamNumber(2));
			if (globalContainer->liveSpectating)
			{
				SDL_Event pause = {};
				pause.type = SDL_EVENT_KEY_DOWN;
				pause.key.key = SDLK_P;
				engine.gui.processEvent(&pause);
				REQUIRE(engine.gui.hardPause);
				engine.gui.processEvent(&pause);
				REQUIRE(!engine.gui.hardPause);
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
		REQUIRE(globalContainer->settings.gameSpeed == 7);
		Settings persisted;
		persisted.load();
		REQUIRE(persisted.gameSpeed == 7);
		std::cout << "PASS full SDL UI flow mode " << control
				  << ": clicks, nested choices, shared control, presets, profiles, "
					 "random preview, "
					 "match launch and speed restoration\n";
	}

	static void strategyVisual(const std::string &output)
	{
		FrontendTheme theme;
		FrontendScope scope;
		GAGGUI::ScreenStack screens(*globalContainer->gfx);
		CustomGameScreen screen(screens);
		screen.beginExecution(globalContainer->gfx);
		screen.selectTab(1);
		screen.setup.colonies[1].ai = AI::MAXIMA;
		screen.invalidate();
		screen.paintFrame(0);
		globalContainer->gfx->printScreen(output + "/players.bmp");
		screen.endExecute(0);
		screen.finishExecution();
		std::vector<std::string> labels;
		for (int id : AINames::selectionOrder()) labels.push_back(AINames::getAISelectorText(id));
		for (int id : AINames::selectionOrder())
		{
			auto text = AINames::getAIProfile(id);
			REQUIRE(text.find("-Profile]") == std::string::npos);
			REQUIRE(text.find("-Summary]") == std::string::npos);
			CustomGameChoiceScreen profile(Toolkit::getStringTable()->getString("[AI strategy & counterplay]"), labels,
				AINames::selectionIndex(id), true, {});
			profile.beginExecution(globalContainer->gfx);
			profile.paintFrame(0);
			globalContainer->gfx->printScreen(output + "/" + AINames::getCLIName(id) + ".bmp");
			if (auto *details = profile.host().find("profile/details"))
				details->scrollBy(100000, profile.host());
			profile.paintFrame(0);
			profile.paintFrame(0);
			globalContainer->gfx->printScreen(output + "/" + AINames::getCLIName(id) + "-bottom.bmp");
			profile.endExecute(0);
			profile.finishExecution();
		}
		std::cout << "PASS strategy profiles and scrolling for every AI\n";
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
			profile.beginExecution(globalContainer->gfx);
			profile.paintFrame(0);
			globalContainer->gfx->printScreen(output + "/ai-profile.bmp");
			profile.use();
			REQUIRE(profile.returnCode == AINames::selectionIndex(AI::CORTEX));
			profile.finishExecution();
		}
		if (onlyAIProfile)
			return;
		{
			// The ruleset list for narrow layouts: tapping a card ends with that ruleset.
			RulesetChoiceScreen picker("standard");
			picker.beginExecution(globalContainer->gfx);
			picker.paintFrame(0);
			globalContainer->gfx->printScreen(output + "/ruleset-picker.bmp");
			const auto &rulesets = RulesetCatalog::shipped().rulesets;
			REQUIRE(picker.host().find("ruleset/standard"));
			picker.host().scrollIntoView("ruleset/blitz");
			picker.paintFrame(0);
			const auto r = picker.host().bounds("ruleset/blitz");
			for (Uint32 type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP})
			{
				SDL_Event e = {};
				e.type = type;
				e.button.button = SDL_BUTTON_LEFT;
				e.button.x = r.x + r.w / 2;
				e.button.y = r.y + r.h / 2;
				picker.handleExecutionEvent(e);
			}
			const auto blitz = std::find_if(rulesets.begin(), rulesets.end(), [](const Ruleset &x) { return x.id == "blitz"; });
			REQUIRE(picker.returnCode == int(blitz - rulesets.begin()));
			picker.finishExecution();
		}

    GAGGUI::ScreenStack screens(*globalContainer->gfx);

    CustomGameScreen screen(screens);
    screen.beginExecution(globalContainer->gfx);
    struct Finish
    {
      CustomGameScreen &screen;
      ~Finish()
      {
        if (screen.isExecutionRunning())
        {
          screen.endExecute(0);
          screen.finishExecution();
        }
      }
    } finish{screen};
    auto &host = screen.host();
    auto paint = [&] { screen.paintFrame(SDL_GetTicks()); };
    // With nothing saved the lobby opens on a random map (FEEDBACK 2026-09-14); the premade
    // library is a click away and is what the catalog checks below exercise.
    REQUIRE((screen.setup.random && screen.previewPending && !screen.validMap &&
           screen.setup.capacity == 4));
    // The first visit to the library preselects FourSquares1, the old opening map.
    screen.setMapMode(false);
    REQUIRE((!screen.setup.random && screen.validMap && !screen.previewPending &&
           std::filesystem::path(screen.setup.premadeMap).filename() == "FourSquares1.map.gz"));
    screen.separateMapLibraries = false;
    screen.listMaps();
    REQUIRE(std::any_of(
        screen.mapPaths.begin(), screen.mapPaths.end(), [](const auto &path) {
          return std::filesystem::path(path).filename() == "FourSquares1.map.gz";
        }));
    screen.separateMapLibraries = true;
    screen.listMaps();

    // The preview rolls its candidates on worker threads; wait for them as the timer would.
    auto preview = [&] {
      screen.onTimer(screen.previewDue);
      screen.finishPreview();
    };
    auto keyEvent = [&](SDL_Keycode key, Uint16 modifiers = SDL_KMOD_NONE) {
      SDL_Event e = {};
      e.type = SDL_EVENT_KEY_DOWN;
      e.key.key = key;
      e.key.mod = modifiers;
      screen.handleExecutionEvent(e);
      paint();
    };
    auto has = [&](const std::string &id) {
      paint();
      return host.find(id) != nullptr;
    };
    auto node = [&](const std::string &id) {
      paint();
      auto *found = host.find(id);
      REQUIRE(found);
      return found;
    };
    auto pointerAt = [&](int x, int y, Uint32 type) {
      SDL_Event e = {};
      e.type = type;
      e.button.button = SDL_BUTTON_LEFT;
      e.button.x = x;
      e.button.y = y;
      screen.handleExecutionEvent(e);
    };
    auto clickControl = [&](const std::string &id) {
      paint();
      host.scrollIntoView(id);
      paint();
      const auto r = host.bounds(id);
      pointerAt(r.x + r.w / 2, r.y + r.h / 2, SDL_EVENT_MOUSE_BUTTON_DOWN);
      pointerAt(r.x + r.w / 2, r.y + r.h / 2, SDL_EVENT_MOUSE_BUTTON_UP);
      paint();
    };
    // Steppers step from their end caps; the middle shows the value.
    auto step = [&](const std::string &id, int direction) {
      paint();
      host.scrollIntoView(id);
      paint();
      const auto r = host.bounds(id);
      const int x = direction > 0 ? r.x + r.w - 8 : r.x + 8;
      pointerAt(x, r.y + r.h / 2, SDL_EVENT_MOUSE_BUTTON_DOWN);
      pointerAt(x, r.y + r.h / 2, SDL_EVENT_MOUSE_BUTTON_UP);
      paint();
    };
    auto scrollOf = [&](const char *key) {
      paint();
      auto *found = host.find(key);
      REQUIRE(found);
      return found->scrollOffset();
    };
    auto scrollTo = [&](const char *key, int offset) {
      paint();
      auto *found = host.find(key);
      REQUIRE(found);
      found->scrollBy(offset - found->scrollOffset(), host);
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
    REQUIRE((catalog.size() == 3 && catalog[0].name.find("Alpha") == 0 &&
           catalog[1].name.find("Alpha") == 0 && catalog[2].name == "zebra"));
    REQUIRE(catalog[0].name != catalog[1].name);
    std::filesystem::remove_all(fixture);
    REQUIRE(std::set<std::string>(screen.mapPaths.begin(), screen.mapPaths.end())
               .size() == screen.mapPaths.size());
    paint();
    // Selecting a row, moving the selection and switching libraries keep the list where it was.
    scrollTo("map/list/0", std::min(56, node("map/list/0")->scrollMaximum()));
    const int savedOffset = scrollOf("map/list/0");
    {
      const auto list = host.bounds("map/list/0");
      pointerAt(list.x + 20, list.y + 20, SDL_EVENT_MOUSE_BUTTON_DOWN);
      pointerAt(list.x + 20, list.y + 20, SDL_EVENT_MOUSE_BUTTON_UP);
      paint();
    }
    REQUIRE(scrollOf("map/list/0") == savedOffset);
    keyEvent(SDLK_DOWN);
    REQUIRE(scrollOf("map/list/0") == savedOffset);
    clickControl("map/library/1");
    clickControl("map/library/0");
    REQUIRE(scrollOf("map/list/0") == savedOffset);
    REQUIRE(screen.librarySelection[0] == screen.source);
    screen.loadMap("maps/FourSquares1.map");
    screen.selectTab(1);
    paint();
    auto alliances = screen.setup.colonies;
    clickControl("colony/0/controller");
    REQUIRE(host.popupOpen());
    keyEvent(SDLK_DOWN);
    keyEvent(SDLK_RETURN);
    REQUIRE(screen.setup.colonies[0].controller == CustomGameSetup::Computer);
    for (int i = 0; i < 4; ++i)
      REQUIRE(screen.setup.colonies[i].alliance == alliances[i].alliance);
    REQUIRE(host.focused() == "colony/0/controller");
    clickControl("colony/0/controller");
    keyEvent(SDLK_DOWN);
    keyEvent(SDLK_RETURN);
    REQUIRE(screen.setup.colonies[0].controller == CustomGameSetup::Shared);
    clickControl("colony/1/ai");
    keyEvent(SDLK_DOWN);
    keyEvent(SDLK_ESCAPE);
    REQUIRE(screen.setup.colonies[1].ai == AI::NUMBI);
    clickControl("colony/1/ai");
    REQUIRE(host.popupOpen());
    pointerAt(0, 0, SDL_EVENT_MOUSE_BUTTON_DOWN);
    pointerAt(0, 0, SDL_EVENT_MOUSE_BUTTON_UP);
    paint();
    REQUIRE(!host.popupOpen());
    // The Teams choice: FFA, then 2 vs 2.
    REQUIRE(screen.setup.teamLayout().kind == TeamLayout::Layout::FreeForAll);
    clickControl("teams");
    keyEvent(SDLK_DOWN);
    keyEvent(SDLK_RETURN);
    REQUIRE(screen.setup.colonies[0].alliance ==
           screen.setup.colonies[1].alliance);
    REQUIRE(screen.setup.teamLayout() == (TeamLayout::Layout{TeamLayout::Layout::Split, {2, 2}, -1}));
    screen.selectTab(2);
    paint();
    // Summary lists the Match rules. Wide layouts list the rulesets beside them; narrower
    // ones open the rulesets as their own screen and show All rules one group at a time.
    const bool rulesetList = has("ruleset/standard");
    REQUIRE(has("rule/victory/0"));
    REQUIRE(!has("rule/revealTerrain"));
    REQUIRE(rulesetList != has("rules/ruleset"));
    clickControl("rules/view/1");
    REQUIRE(screen.rulesView == CustomGameScreen::RulesView::All);
    if (has("rules/group"))
    {
      REQUIRE(!has("rule/revealTerrain"));
      screen.rulesGroup = CustomGameRules::Group::Start;
      screen.invalidate();
    }
    clickControl("rule/revealTerrain");
    REQUIRE((screen.setup.revealed && screen.setup.rulesetId == "standard" && screen.setup.rulesetDiff().size() == 1));
    REQUIRE((has("rule/revealTerrain/reset") && has("rules/reset")));
    REQUIRE(!screen.setup.unitUpgradesDisabled);
    screen.setup.unitUpgradesDisabled = true;
    const int rulesOffset = scrollOf("lobby/rules");
    clickControl("rule/revealTerrain");
    REQUIRE((!screen.setup.revealed && scrollOf("lobby/rules") == rulesOffset));
    REQUIRE(screen.setup.unitUpgradesDisabled);
    // Undoing an edit leaves no change behind; a reset restores the ruleset's value.
    screen.setup.unitUpgradesDisabled = false;
    REQUIRE((screen.setup.rulesetDiff().empty() && !has("rule/revealTerrain/reset")));
    clickControl("rule/revealTerrain");
    clickControl("rule/revealTerrain/reset");
    REQUIRE((!screen.setup.revealed && screen.setup.rulesetDiff().empty()));
    screen.selectRuleset("quick-clash");
    REQUIRE((screen.setup.speed == 3 && screen.setup.generator.nbWorkers == 8));
    screen.setRulesView(CustomGameScreen::RulesView::Summary);
    screen.setup = CustomGameSetup();
    screen.loadMap("maps/FourSquares1.map");
    scrollTo("lobby/rules", 0);
    screen.selectTab(0);
    scrollTo("map/list/0", 0);
    std::cout << "PASS canonical map catalog, selection stability, inline "
                 "controls, popup cancel, focus preservation and rules\n";
    auto capture = [&](const std::string &name) {
      paint();
      if (screen.preview->transitioning) {
        screen.preview->transitionPending = false;
        screen.preview->transitionStarted = SDL_GetTicks() - MapPreview::TransitionDurationMs - 1;
        paint();
      }
      globalContainer->gfx->printScreen(output + "/" + name + ".bmp");
    };
    capture("map-640");
    // Randomize, Reset to defaults and Random parameters only apply to random maps.
    REQUIRE((!has("map/randomize") && !has("generator/reset") && !has("generator/random") &&
           !has("quality/info")));
    screen.selectTab(1);
    capture("players-640");
    screen.setup.colonies[1].ai = AI::CORTEX;
    screen.invalidate();
    capture("cortex-640");
    clickControl("colony/1/ai");
    capture("ai-dropdown");
    keyEvent(SDLK_ESCAPE);

    screen.selectTab(2);
    capture("rules-640");
    // The footer names the experiments the match will carry.
    globalContainer->settings.experiments.set(ExperimentId::GuardAreaBalancing);
    screen.invalidate();
    capture("experiments-footer-640");
    globalContainer->settings.experiments.clear();
    screen.invalidate();
    screen.setup.random = true;
    // The slider/failure checks below exercise River terrain weights explicitly.
    screen.setup.generatorHistory.select(screen.setup.generator, MapGenerationDescriptor::eRIVER);
    screen.invalidatePreview();
    screen.selectTab(0);
    capture("random-controls-640");
    screen.expanded[0] = screen.expanded[1] = screen.expanded[2] = true;
    screen.invalidate();
    paint();
    scrollTo("lobby/map", 180);
    capture("generator-expanded");
    scrollTo("lobby/map", 0);
    // Preview appears from the timer, without a Generate control or click.
    REQUIRE(!has("map/generate"));
    screen.onTimer(screen.previewDue - 1);
    REQUIRE(!screen.validMap);
    {
      // A held slider defers the preview until it is released.
      host.scrollIntoView("generator/water");
      paint();
      const auto slider = host.bounds("generator/water");
      pointerAt(slider.x + slider.w / 2, slider.y + slider.h / 2, SDL_EVENT_MOUSE_BUTTON_DOWN);
      REQUIRE(host.interacting());
      preview();
      REQUIRE(!screen.validMap);
      pointerAt(slider.x + slider.w / 2, slider.y + slider.h / 2, SDL_EVENT_MOUSE_BUTTON_UP);
      paint();
      REQUIRE(!host.interacting());
    }
    preview();
    REQUIRE((screen.validMap && !screen.previewPending));
    auto first = screen.generatedSnapshot;
    auto revision = screen.previewRevision;
    REQUIRE((first && !first->empty()));
    capture("random-preview-640");
    screen.setup.colonies[1].ai = AI::CASTOR;
    REQUIRE(screen.setup.applyTeamLayout(TeamLayout::Layout{TeamLayout::Layout::Split, {2, 2}, -1}));
    screen.onTimer(SDL_GetTicks() + 1000);
    REQUIRE((screen.previewRevision == revision && screen.generatedSnapshot == first));
    {
      // Nudging the slider from the keyboard is one edit: one invalidation, one step.
      host.focus("generator/water", true);
      const auto water = screen.setup.generator.options["water"];
      SDL_Event motion = {};
      motion.type = SDL_EVENT_MOUSE_MOTION;
      motion.motion.x = 0;
      motion.motion.y = 0;
      screen.handleExecutionEvent(motion);
      REQUIRE(screen.setup.generator.options["water"] == water);
      keyEvent(SDLK_RIGHT);
      REQUIRE(screen.setup.generator.options["water"] == water + 1);
      REQUIRE((!screen.validMap && screen.setup.mapRevision != revision));
    }
    REQUIRE(screen.setup.colonies[0].alliance ==
           screen.setup.colonies[1].alliance);

    REQUIRE(screen.generateMap());
    REQUIRE(screen.generatedSnapshot != first);
    // Randomize rolls the same settings again with a new seed, through the
    // normal preview path, and releases the snapshot it replaces.
    {
      const auto settings = screen.setup.generator;
      const auto mapRevision = screen.setup.mapRevision;
      const auto replaced = screen.generatedSnapshot;
      clickControl("map/randomize");
      REQUIRE((!screen.validMap && screen.previewPending));
      preview();
      REQUIRE((screen.validMap && screen.generatedSnapshot != replaced));
      REQUIRE((screen.setup.generator.method == settings.method &&
             screen.setup.generator.options == settings.options &&
             screen.setup.mapRevision == mapRevision));
      capture("randomize-640");
    }
    // Reset to defaults restores this landscape's registered values, keeping
    // the landscape and the Game Rules tab's starting workers.
    {
      GenerationRequest expected;
      expected.setMethodDefaults(screen.setup.generator.method);
      const auto method = screen.setup.generator.method;
      const auto workers = screen.setup.generator.nbWorkers;
      REQUIRE(screen.setup.generator.options != expected.options);
      clickControl("generator/reset");
      REQUIRE((screen.setup.generator.method == method &&
             screen.setup.generator.nbWorkers == workers &&
             screen.setup.generator.options == expected.options &&
             screen.setup.generator.wDec == expected.wDec &&
             screen.setup.generator.hDec == expected.hDec &&
             screen.setup.capacity == expected.nbTeams && !screen.validMap &&
             screen.previewPending));
      paint();
      auto *reset = node("generator/reset");
      REQUIRE(!reset->enabled());
      // Reset to defaults sits at the top of the column, right under the landscape chooser,
      // with Random parameters beside it (FEEDBACK 2026-09-14).
      auto *landscape = node("generator/landscape");
      auto *random = node("generator/random");
      REQUIRE((reset->bounds.y > landscape->bounds.y && reset->bounds.y < landscape->bounds.y + 80 &&
             random->bounds.y == reset->bounds.y && random->bounds.x > reset->bounds.x && random->enabled()));
      host.root()->visit([&](GAGGUI::ui::Node &n) {
        if (n.key.rfind("generator/", 0) == 0 && n.key != "generator/landscape" &&
            n.key != "generator/reset" && n.key != "generator/random")
          REQUIRE(n.bounds.y >= reset->bounds.y);
      });
      preview();
      REQUIRE(screen.validMap);
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
      REQUIRE((changed && !screen.validMap && screen.previewPending && !screen.chosenSeed));
      REQUIRE((screen.setup.generator.method == before.method &&
             screen.setup.generator.nbWorkers == before.nbWorkers &&
             screen.setup.generator.wDec == before.wDec &&
             screen.setup.generator.hDec == before.hDec && screen.setup.capacity == capacity));
      REQUIRE(validateGenerationRequest(screen.setup.generator, definition).empty());
	  for (const auto &control : definition.controls)
	  {
		  const auto domain = control.searchValues();
		  REQUIRE(std::find(domain.begin(), domain.end(), control.get(screen.setup.generator)) !=
				  domain.end());
	  }
	  // A random set the world refuses on every seed is redrawn by the preview itself, one
	  // more round of candidates per draw; give it those rounds, and a fresh click should even
	  // the last draw fail, so the check does not hang on one unlucky stream.
	  for (int click = 0; click < 6 && !screen.validMap; ++click)
	  {
		  if (click > 0)
			  clickControl("generator/random");
		  for (int round = 0; round < CustomGameScreen::kRandomAttempts + 1 && !screen.validMap;
			   ++round)
			  preview();
	  }
	  REQUIRE((screen.validMap && screen.quality.measured &&
			   screen.quality.colonies.size() == size_t(capacity)));
	  paint();
      // The start quality line under the preview: fairness and score, and its (i).
      REQUIRE((has("quality/info") && node("quality/info")->enabled()));
      capture("random-parameters-640");
      {
        std::vector<std::string> labels;
        std::vector<Color> colors;
        for (size_t i = 0; i < screen.quality.colonies.size(); ++i) {
          labels.push_back(screen.colonyLabel(int(i)));
          colors.push_back(screen.preview->starts[i].color);
        }
        StartQualityScreen breakdown(screen.quality, labels, colors);
        breakdown.beginExecution(globalContainer->gfx);
        breakdown.paintFrame(0);
        globalContainer->gfx->printScreen(output + "/start-quality.bmp");
        REQUIRE(breakdown.host().find("back"));
        SDL_Event e = {};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDLK_ESCAPE;
        breakdown.handleExecutionEvent(e);
        REQUIRE(breakdown.returnCode == StartQualityScreen::BACK);
        breakdown.finishExecution();
        paint();
      }
      const auto retainedQuality = screen.quality;
      clickControl("generator/reset");
      GenerationRequest expected;
      expected.setMethodDefaults(before.method);
      REQUIRE((screen.setup.generator.options == expected.options && screen.quality.measured &&
             screen.quality.fairness == retainedQuality.fairness &&
             screen.quality.score == retainedQuality.score));
      preview();
      REQUIRE(screen.validMap);
    }
    screen.setup.applyRuleset("quick-clash");
    screen.invalidatePreview();
    REQUIRE(!screen.validMap);
    auto assignments = screen.setup.colonies;
    screen.setup.generator.options["water"] =
        screen.setup.generator.options["sand"] =
            screen.setup.generator.options["grass"] =
                screen.setup.generator.options["desert"] = 0;
    preview();
    REQUIRE((!screen.validMap && !screen.previewPending));
    for (int i = 0; i < 4; ++i)
      REQUIRE(screen.setup.colonies[i].alliance == assignments[i].alliance);
    screen.setup.generator.options["water"] =
        screen.setup.generator.options["sand"] =
            screen.setup.generator.options["grass"] =
                screen.setup.generator.options["desert"] = 50;
    screen.invalidatePreview();
    preview();
    REQUIRE(screen.validMap);

    // Apply a landscape the way the picker's result does, then drive the same steppers used
    // by players, including measured five-unit steps. The picker itself is exercised below.
    auto landscape = [&](int method) {
      screen.applyLandscape(method, std::nullopt);
      paint();
      REQUIRE(screen.setup.generator.method == method);
    };
    landscape(MapGenerationDescriptor::eCONCRETEISLANDS);
    REQUIRE((screen.setup.generator.options["channel-width"] == 5 &&
           screen.setup.generator.options["extra-islands"] == 3));
    step("generator/channel-width", 1);
    REQUIRE(screen.setup.generator.options["channel-width"] == 6);
    capture("concrete-controls");
    landscape(MapGenerationDescriptor::eISLES);
    REQUIRE(screen.setup.generator.options["island-size"] == 60);
    step("generator/island-size", 1);
    REQUIRE(screen.setup.generator.options["island-size"] == 65);
    step("generator/bridge-width", 1);
    REQUIRE(screen.setup.generator.options["bridge-width"] == 5);
    capture("isles-controls");
    landscape(MapGenerationDescriptor::eCRATERLAKES);
    REQUIRE((screen.setup.generator.options["lake-size"] == 25 &&
           screen.setup.generator.options["grass"] == 75));
    step("generator/lake-size", 1);
    REQUIRE(screen.setup.generator.options["lake-size"] == 30);
    capture("crater-controls");
    landscape(MapGenerationDescriptor::eCONCRETEISLANDS);
    REQUIRE(screen.setup.generator.options["channel-width"] == 6);
    landscape(MapGenerationDescriptor::eOLDISLANDS);
    REQUIRE(screen.setup.generator.options["island-size"] == 65);
    capture("rugged-archipelago-controls");
    landscape(MapGenerationDescriptor::eOLDRANDOM);
    capture("shattered-coast-controls");
    // Switches are checkboxes: a click, or Space or Return on the focused row, flips one, and
    // either edit invalidates the preview like any other generator control.
    landscape(GeneratorRegistry::builtins().idOf("fjord-continent"));
    screen.expanded[0] = screen.expanded[1] = screen.expanded[2] = true;
    preview();
    REQUIRE(screen.validMap);
    {
      auto &options = screen.setup.generator.options;
      const auto revision = screen.setup.mapRevision;
      REQUIRE(options["lake-connected"] == 0);
      clickControl("generator/lake-connected");
      REQUIRE((options["lake-connected"] == 1 && screen.setup.mapRevision != revision &&
             !screen.validMap && screen.previewPending));
      REQUIRE(host.focused() == "generator/lake-connected");
      host.focus("generator/lake-connected", true);
      keyEvent(SDLK_SPACE);
      REQUIRE(options["lake-connected"] == 0);
      keyEvent(SDLK_RETURN);
      REQUIRE(options["lake-connected"] == 1);
      // Tab walks off the checkbox and Shift+Tab back onto it, like any other control.
      keyEvent(SDLK_TAB);
      REQUIRE(host.focused() != "generator/lake-connected");
      keyEvent(SDLK_TAB, SDL_KMOD_SHIFT);
      REQUIRE(host.focused() == "generator/lake-connected");
      preview();
      REQUIRE(screen.validMap);
      capture("map-checkboxes");
      clickControl("generator/lake-connected");
      REQUIRE(options["lake-connected"] == 0);
    }
    landscape(MapGenerationDescriptor::eRIVER);

    screen.selectTab(0);
    capture("map-1000");
    // Rectangular terrain and markers must use the same cropped preview area.
    for (auto dimensions : {std::pair{9, 7}, std::pair{7, 9}, std::pair{9, 6}, std::pair{6, 9}}) {
      screen.setup.generatorHistory.select(screen.setup.generator, GeneratorRegistry::builtins().idOf("contested-commons"));
      screen.setup.generator.wDec = dimensions.first;
      screen.setup.generator.hDec = dimensions.second;
      screen.setup.setCapacity(4);
      screen.invalidatePreview();
      REQUIRE(screen.generateMap());
      // The preview only records its rectangle while painted, so bring it back
      // into view after the generator controls scrolled it off.
      paint();
      host.scrollIntoView("map/preview");
      paint();
      const std::string name = "rectangular-" + std::to_string(1 << dimensions.first) + "x" + std::to_string(1 << dimensions.second);
      const auto expectedStarts = screen.preview->starts;
      // Old premade maps can contain equivalent coordinates across a torus seam.
      screen.preview->starts[0].x -= (1 << dimensions.first);
      screen.preview->starts[0].y += (1 << dimensions.second);
      capture(name);
      const auto rect = screen.preview->mapArea();
      const int mapW = screen.preview->getLastWidth(), mapH = screen.preview->getLastHeight();
      REQUIRE(std::abs(rect.w * mapH - rect.h * mapW) < std::max(mapW, mapH));
      SDL_Surface *bmp = SDL_LoadBMP((output + "/" + name + ".bmp").c_str());
      REQUIRE(bmp);
      SDL_Surface *rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
      SDL_DestroySurface(bmp);
      REQUIRE(rgba);
      auto pixel = [&](int x, int y) {
        REQUIRE((x >= 0 && y >= 0 && x < rgba->w && y < rgba->h));
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
          REQUIRE((p[0] || p[1] || p[2]));
        }
      for (int y = rect.y + 1; y < rect.y + rect.h - 1; ++y)
        if (!underMarker(rect.x + 1, y)) {
          const auto p = pixel(rect.x + 1, y);
          REQUIRE((p[0] || p[1] || p[2]));
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
        REQUIRE((swatch[0] == start.color.r && swatch[1] == start.color.g && swatch[2] == start.color.b));
      }
      SDL_DestroySurface(rgba);
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
      screen.invalidatePreview();
      paint();
      REQUIRE(has("generator/landscape"));
      const auto entries = screen.landscapeEntries();
      REQUIRE(entries.size() == GeneratorRegistry::builtins().methods(false).size());
      for (const auto &[method, request] : entries)
        REQUIRE((request.method == method && request.nbTeams == 4 && request.wDec == 8 &&
               request.hDec == 8));
      std::vector<LandscapePickerScreen::Entry> shown;
      for (const auto &[method, request] : entries)
        shown.push_back({GenerationRequest::methodName(method), request});
      const int current =
          GeneratorRegistry::builtins().selectionIndex(screen.setup.generator.method, false);
      LandscapePickerScreen picker("Landscape", shown, current);
      picker.beginExecution(globalContainer->gfx);
      auto &pickerHost = picker.host();
      auto pickerPaint = [&] { picker.paintFrame(SDL_GetTicks()); };
      REQUIRE((picker.previewer.threadCount() == 2 && picker.busy()));
      pickerPaint(); // placeholders while every tile is still pending
      globalContainer->gfx->printScreen(output + "/landscape-picker-pending.bmp");
      // Cards in view roll first, and scrolling the grid moves that eligible set with it.
      picker.updatePreviewPriority();
      REQUIRE((!picker.viewportSlots.empty() && picker.priorityOrder.size() == shown.size()));
      REQUIRE(std::find(picker.viewportSlots.begin(), picker.viewportSlots.end(),
                       picker.priorityOrder.front()) != picker.viewportSlots.end());
      {
        auto *grid = pickerHost.find("landscape/grid");
        REQUIRE((grid && grid->scrollMaximum() > 0));
        const auto initial = picker.viewportSlots;
        grid->scrollBy(grid->scrollMaximum(), pickerHost);
        pickerPaint();
        picker.updatePreviewPriority();
        REQUIRE(picker.viewportSlots != initial);
        REQUIRE(std::find(picker.viewportSlots.begin(), picker.viewportSlots.end(),
                         picker.priorityOrder.front()) != picker.viewportSlots.end());
        grid->scrollBy(-grid->scrollOffset(), pickerHost);
        pickerPaint();
      }
      auto settle = [&] {
        const Uint32 deadline = SDL_GetTicks() + 120000;
        while (picker.busy()) {
          REQUIRE(Sint32(SDL_GetTicks() - deadline) < 0);
          SDL_Delay(10);
          picker.onTimer(SDL_GetTicks());
        }
        picker.onTimer(SDL_GetTicks());
        pickerPaint();
        for (auto &tile : picker.tiles) {
          if (tile.widget && tile.widget->transitioning) {
            tile.widget->transitionPending = false;
            tile.widget->transitionStarted = SDL_GetTicks() - MapPreview::TransitionDurationMs - 1;
          }
        }
        pickerPaint();
      };
      auto pickerPointer = [&](int x, int y, Uint32 type) {
        SDL_Event e = {};
        e.type = type;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.x = x;
        e.button.y = y;
        picker.handleExecutionEvent(e);
      };
      // Press the preview area too: the entire card shares selection behavior.
      auto pick = [&](const std::string &id) {
        pickerPaint();
        pickerHost.scrollIntoView(id);
        pickerPaint();
        const auto r = pickerHost.bounds(id);
        const bool tile = id.size() > 10 && std::isdigit(static_cast<unsigned char>(id[10]));
        const int y = tile ? r.y + 32 : r.y + r.h / 2;
        pickerPointer(r.x + r.w / 2, y, SDL_EVENT_MOUSE_BUTTON_DOWN);
        pickerPointer(r.x + r.w / 2, y, SDL_EVENT_MOUSE_BUTTON_UP);
        pickerPaint();
      };
      auto pickerKey = [&](SDL_Keycode key) {
        SDL_Event e = {};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = key;
        picker.handleExecutionEvent(e);
        pickerPaint();
      };
      // Sheet-wide actions are buttons on wide windows and items of the "More" menu on
      // narrow ones, in the order regenerate, randomize, reset.
      auto pickAction = [&](const std::string &name, int menuIndex) {
        pickerPaint();
        if (pickerHost.find("landscape/" + name)) {
          pick("landscape/" + name);
          return;
        }
        INFO("action=" << name << " run=" << picker.run << " return=" << picker.returnCode << " focus=" << pickerHost.focused());
        pick("landscape/more");
        INFO("after click: run=" << picker.run << " return=" << picker.returnCode << " focus=" << pickerHost.focused());
        REQUIRE(pickerHost.popupOpen());
        pick("popup/" + std::to_string(menuIndex));
        REQUIRE(!pickerHost.popupOpen());
      };
      settle();
      globalContainer->gfx->printScreen(output + "/landscape-picker.bmp");
      auto seedsShown = [&] {
        std::vector<std::uint32_t> seeds;
        for (const auto &tile : picker.tiles) {
			REQUIRE((tile.preview.state == LandscapePreviewer::State::Ready &&
				   tile.preview.width == 256 && tile.preview.height == 256 &&
				   tile.preview.starts.size() == 4));
			seeds.push_back(tile.preview.seed);
        }
        REQUIRE(std::set<std::uint32_t>(seeds.begin(), seeds.end()).size() == seeds.size());
        return seeds;
      };
      const auto first = seedsShown();
      REQUIRE((picker.selection() == current && picker.chosenSeed() == first[current]));
      // A click selects; arrows move by one tile or one row; Escape cancels.
      const int other = (current + 1) % int(shown.size());
      pick("landscape/" + std::to_string(other));
      REQUIRE((picker.selection() == other && picker.returnCode == 0));
      // Swipes starting on a preview scroll the list without panning or selecting.
      {
        auto *widget = picker.tiles[other].widget;
        REQUIRE(widget);
        pickerHost.scrollIntoView("landscape/" + std::to_string(other));
        pickerPaint();
        auto *grid = pickerHost.find("landscape/grid");
        REQUIRE((grid && grid->scrollMaximum() > 0));
        const auto area = widget->mapArea();
        const auto previewZoom = widget->zoom;
        const auto previewX = widget->view.offsetX, previewY = widget->view.offsetY;
        globalContainer->gfx->printScreen(output + "/landscape-picker-before-swipe.bmp");
        const int beforeSwipe = grid->scrollOffset();
        const int distance = beforeSwipe > grid->scrollMaximum() / 2 ? 50 : -50;
        const int x = area.x + area.w / 2, y = area.y + area.h / 2;
        auto finger = [&](Uint32 type, int atY) {
          SDL_Event event{};
          event.type = type;
          event.tfinger.touchID = 1;
          event.tfinger.fingerID = 1;
          event.tfinger.x = float(x) / pickerHost.presentation().viewport.w;
          event.tfinger.y = float(atY) / pickerHost.presentation().viewport.h;
          picker.handleExecutionEvent(event);
        };
        finger(SDL_EVENT_FINGER_DOWN, y);
        finger(SDL_EVENT_FINGER_MOTION, y + distance);
        finger(SDL_EVENT_FINGER_UP, y + distance);
        REQUIRE(grid->scrollOffset() != beforeSwipe);
        std::cout << "PASS preview swipe: grid offset " << beforeSwipe << " -> "
                  << grid->scrollOffset() << "; preview and selection unchanged\n";
        REQUIRE((!widget->dragging && widget->zoom == previewZoom &&
                 widget->view.offsetX == previewX && widget->view.offsetY == previewY &&
                 picker.selection() == other && picker.returnCode == 0));
        pickerPaint();
        globalContainer->gfx->printScreen(output + "/landscape-picker-preview-swipe.bmp");
        // The picker intentionally reserves the wheel for grid scrolling, even
        // above a preview. MapPreviewHarness separately checks anchored zoom.
        pickerHost.scrollIntoView("landscape/" + std::to_string(other));
        pickerPaint();
        const auto wheelArea = widget->mapArea();
        SDL_Event e = {};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.x = wheelArea.x + wheelArea.w / 2;
        e.motion.y = wheelArea.y + wheelArea.h / 2;
        picker.handleExecutionEvent(e);
        bool scrolled = false;
        for (int direction : {-1, 1}) {
          const int before = grid->scrollOffset();
          e = {};
          e.type = SDL_EVENT_MOUSE_WHEEL;
          e.wheel.y = direction;
          picker.handleExecutionEvent(e);
          pickerPaint();
          const int after = grid->scrollOffset();
          REQUIRE((after >= 0 && after <= grid->scrollMaximum()));
          REQUIRE((direction > 0 ? after <= before : after >= before));
          scrolled |= after != before;
          REQUIRE((widget->zoom == previewZoom && widget->view.offsetX == previewX &&
                 widget->view.offsetY == previewY && picker.returnCode == 0));
        }
        REQUIRE(scrolled);
      }
      // Keyboard navigation follows the displayed sort/filter order, not the
      // registry identity of a landscape (these differ in Random order).
      const auto position = std::find(picker.visible.begin(), picker.visible.end(), other);
      REQUIRE(position != picker.visible.end());
      const int left = std::max(0, int(position - picker.visible.begin()) - 1);
      pickerKey(SDLK_LEFT);
      REQUIRE(picker.selection() == picker.visible[left]);
      pickerKey(SDLK_DOWN);
      REQUIRE(picker.selection() == picker.visible[
          std::min(int(picker.visible.size()) - 1, left + picker.columns)]);
      pickerKey(SDLK_ESCAPE);
      REQUIRE(picker.returnCode == LandscapePickerScreen::CANCEL);
      // Escape ended the execution; keep driving the same picker for the remaining checks.
      picker.run = true;
      picker.returnCode = 0;
      // Regenerate all rolls every landscape again with fresh seeds.
      pickAction("regenerate", 0);
      REQUIRE(picker.busy());
      REQUIRE(!picker.chosenSeed());
      const int pendingReturnCode = picker.returnCode;
      picker.confirm();
      REQUIRE(picker.returnCode == pendingReturnCode);
      settle();
      const auto second = seedsShown();
      for (size_t i = 0; i < first.size(); ++i)
        REQUIRE(first[i] != second[i]);
      // Randomize parameters rolls every landscape with its controls drawn at random: the sheet
      // still fills with maps (a refused set is redrawn), and each tile's request is one its
      // generator accepts, at the lobby's size and colony count.
      pickAction("randomize", 1);
      REQUIRE(picker.busy());
      settle();
      seedsShown();
      {
        bool anyDiffer = false;
        for (size_t i = 0; i < shown.size(); ++i) {
          const GenerationRequest rolled = picker.previewer.request(i);
          REQUIRE((rolled.method == shown[i].request.method && rolled.nbTeams == 4 &&
                 rolled.wDec == 8 && rolled.hDec == 8));
          REQUIRE(validateGenerationRequest(rolled, GeneratorRegistry::builtins().at(rolled.method))
                     .empty());

		  for (const auto &control : GenerationRequest::controls(rolled.method))
		  {
			  const auto domain = control.searchValues();
			  REQUIRE(std::find(domain.begin(), domain.end(), control.get(rolled)) != domain.end());
		  }
		  anyDiffer = anyDiffer || rolled.options != shown[i].request.options;
		}
		REQUIRE(anyDiffer);
	  }
      globalContainer->gfx->printScreen(output + "/landscape-picker-randomized.bmp");
      // Reset to defaults puts every landscape back on its registered controls at the sheet's
      // size and colony count, and rolls the sheet again.
      pickAction("reset", 2);
      REQUIRE(picker.busy());
      settle();
      seedsShown();
      for (size_t i = 0; i < shown.size(); ++i) {
        const GenerationRequest rolled = picker.previewer.request(i);
        GenerationRequest expected;
        expected.setMethodDefaults(shown[i].request.method);
        REQUIRE((rolled.options == expected.options && rolled.nbTeams == 4 && rolled.wDec == 8 &&
               rolled.hDec == 8));
      }
      pickAction("randomize", 1);
      settle();
      seedsShown();
      // Exercise the already-selected tile: its click confirms and ends execution.
      picker.select(other);
      // Using a randomized landscape hands the lobby the parameters it was shown with.
      {
        pick("landscape/" + std::to_string(other));
        const GenerationRequest rolled = picker.chosenRequest();
        REQUIRE(rolled.method == entries[other].first);
        screen.applyLandscape(entries[other].first, picker.chosenSeed(), &rolled);
        REQUIRE((screen.setup.generator.method == entries[other].first &&
               screen.setup.generator.options == rolled.options &&
               screen.setup.generator.nbTeams == 4));
        REQUIRE(!picker.run);
        REQUIRE(picker.returnCode == other);
        picker.finishExecution();
        picker.beginExecution(globalContainer->gfx);
        // Back to the landscapes' own parameters for the checks below. Reset, not Regenerate:
        // regenerating keeps the random draw, and a random ridge layout can fail validation
        // once the map is resized below.
        pickAction("reset", 2);
        settle();
        seedsShown();
      }
      // Each confirmation needs a live execution, so a stale return code cannot pass.
      REQUIRE(picker.selection() == other);
      pickerKey(SDLK_RETURN);
      REQUIRE(!picker.run);
      REQUIRE(picker.returnCode == other);
      picker.finishExecution();
      picker.beginExecution(globalContainer->gfx);
      pick("landscape/" + std::to_string(other));
      REQUIRE(!picker.run);
      REQUIRE(picker.returnCode == other);
      picker.finishExecution();
      picker.beginExecution(globalContainer->gfx);
      pick("landscape/use");
      REQUIRE(!picker.run);
      REQUIRE(picker.returnCode == other);
      globalContainer->gfx->printScreen(output + "/landscape-picker-selected.bmp");
      // The lobby then rolls the very seed the picker showed: same starts, same map header.
      const auto seed = *picker.chosenSeed();
      const auto starts = picker.tiles[other].preview.starts;
      // As the lobby's Use does: the parameters travel with the seed. Without them the lobby
      // would keep the random draw it was handed above and roll a different map.
      const GenerationRequest request = picker.chosenRequest();
      picker.finishExecution();
      screen.applyLandscape(entries[other].first, seed, &request);
      REQUIRE((screen.previewPending && screen.chosenSeed == seed &&
             screen.setup.generator.options == request.options &&
             screen.setup.generator.method == entries[other].first));
      preview();
      REQUIRE((screen.validMap && !screen.chosenSeed));
      REQUIRE(screen.preview->starts.size() == starts.size());
      for (size_t i = 0; i < starts.size(); ++i)
        REQUIRE((screen.preview->starts[i].x == starts[i].x &&
               screen.preview->starts[i].y == starts[i].y &&
               screen.preview->starts[i].color.r == starts[i].color.r &&
               screen.preview->starts[i].color.g == starts[i].color.g &&
               screen.preview->starts[i].color.b == starts[i].color.b));
      {
        Game shownMap(nullptr);
        // The memory backend copies its input through write() and leaves its
        // cursor at the end; read from the start as the loader does.
        auto *bytes = new GAGCore::MemoryStreamBackend(screen.generatedSnapshot->data(), screen.generatedSnapshot->size());
        bytes->seekFromStart(0);
        GAGCore::BinaryInputStream in(bytes);
        REQUIRE((shownMap.load(&in) && shownMap.gameHeader.getRandomSeed() == seed));
      }
      capture("landscape-applied");
      // An edit after the pick drops the shown seed: the next preview samples candidates again.
      screen.applyLandscape(entries[other].first, seed);
      REQUIRE(screen.chosenSeed == seed);
      clickControl("generator/width");
      REQUIRE(host.popupOpen());
      // Enlarge it: shrinking can violate the selected landscape's minimum home spacing.
      keyEvent(SDLK_DOWN);
      keyEvent(SDLK_RETURN);
      REQUIRE((!screen.chosenSeed && screen.previewPending));
      preview();
      REQUIRE(screen.validMap);
      std::cout << "PASS landscape picker: " << shown.size() << " previews on "
                << picker.previewer.threadCount()
                << " threads, selection, regeneration and the shown map played\n";
    }
    {
      // Watching eight AIs: the Teams choice offers every shape the eight can take, and one
      // against all leaves the first colony alone, as when testing one AI against the rest.
      screen.setup.setCapacity(8);
      for (int i = 0; i < 8; ++i)
        REQUIRE(screen.setup.setController(i, CustomGameSetup::Computer));
      REQUIRE(screen.setup.applyTeamLayout(TeamLayout::Layout{TeamLayout::Layout::FreeForAll, {}, -1}));
      screen.invalidatePreview();
      REQUIRE(screen.generateMap());
      screen.selectTab(1);
      scrollTo("lobby/players", 0);
      paint();
      clickControl("teams");
      REQUIRE(host.popupOpen());
      capture("teams-8-watching-open");
      // FFA, 4 vs 4, 3 vs 5, 2 vs 6, then Red vs all.
      for (int i = 0; i < 4; ++i)
        keyEvent(SDLK_DOWN);
      keyEvent(SDLK_RETURN);
      REQUIRE(!host.popupOpen());
      const auto layout = screen.setup.teamLayout();
      REQUIRE((layout.oneVsAll() && screen.setup.loneColony(layout) == 0));
      capture("teams-8-watching-one-vs-all");
      // Two by two, set from the list, then broken by hand: the choice names it Custom teams.
      REQUIRE(screen.setup.applyTeamLayout(TeamLayout::Layout{TeamLayout::Layout::Split, {2, 2, 2, 2}, -1}));
      screen.setup.colonies[7].alliance = 0;
      REQUIRE(screen.setup.teamLayout().kind == TeamLayout::Layout::Custom);
      screen.invalidate();
      capture("teams-8-custom");
      REQUIRE(screen.setup.applyTeamLayout(TeamLayout::Layout{TeamLayout::Layout::FreeForAll, {}, -1}));
    }
    screen.setup.generatorHistory.select(screen.setup.generator, MapGenerationDescriptor::eRIVER);
    screen.setup.setCapacity(Team::MAX_COUNT);
    screen.setup.generator.wDec = screen.setup.generator.hDec = 8;
    screen.invalidatePreview();
    REQUIRE(screen.generateMap());
    screen.setup.setController(0, CustomGameSetup::Computer);
    screen.selectTab(1);
    capture("players-16-1000");
    clickControl("colony/0/controller");
    REQUIRE(host.popupOpen());
    REQUIRE(!screen.setup.setController(0, CustomGameSetup::Shared));
    capture("controller-limit");
    keyEvent(SDLK_HOME);
    keyEvent(SDLK_DOWN);
    keyEvent(SDLK_DOWN);
    keyEvent(SDLK_RETURN);
    REQUIRE((host.popupOpen() && screen.setup.controllerCount() == Team::MAX_COUNT));
    keyEvent(SDLK_ESCAPE);
    scrollTo("lobby/players", node("lobby/players")->scrollMaximum());
    capture("players-16-scrolled");
    clickControl("colony/15/ai");
    const int rosterOffset = scrollOf("lobby/players");
    keyEvent(SDLK_DOWN);
    keyEvent(SDLK_RETURN);
    REQUIRE(scrollOf("lobby/players") == rosterOffset);
    // Sequential keyboard focus scrolls an off-screen control into view.
    host.focus("colony/15/team", true);
    keyEvent(SDLK_TAB);

		screen.selectTab(2);
		capture("rules-1000");
		// The ruleset list sits beside the rules when it fits; otherwise it is its own screen.
		if (has("ruleset/blitz"))
			clickControl("ruleset/blitz");
		else
		{
			REQUIRE(has("rules/ruleset"));
			screen.selectRuleset("blitz");
		}
		REQUIRE(screen.setup.rulesetId == "blitz");
		capture("rules-blitz-1000");
		screen.setRulesView(CustomGameScreen::RulesView::All);
		screen.setup.peacefulMode = true;
		screen.setup.prestige = false;
		screen.setup.suddenDeathMinutes = 0;
		screen.invalidate();
		capture("rules-all-changed-1000");
		screen.selectRuleset("standard");
		screen.setRulesView(CustomGameScreen::RulesView::Summary);
		screen.setup.winProbabilityPermille = 970;
		screen.invalidate();
		paint();
		REQUIRE(host.find("rule/winProbability"));
		host.scrollIntoView("rule/winProbability");
		capture("probability-rule");
		scrollTo("lobby/rules", node("lobby/rules")->scrollMaximum());
		capture("rules-scrolled");
		std::cout << "PASS native rendering, snapshot reroll ownership, "
					 "invalidation, failure "
					 "recovery, preserved alliances\n";
	}
	// Rulesets come from data/rulesets.json and every rule from CustomGameRules; edits are
	// measured against the chosen ruleset, and rooms compare only the rules they carry.
	static void rulesets()
	{
		using CustomGameRules::Rule;
		auto rule = [](const char *id) -> const Rule & {
			const Rule *found = CustomGameRules::find(id);
			REQUIRE(found);
			return *found;
		};
		// The registry agrees with itself, with the setup's choice tables and with the string
		// tables, and every value survives its accessors.
		std::set<std::string> textKeys;
		{
			GAGCore::InputLineStream keys(Toolkit::getFileManager()->openInputStreamBackend("data/texts.keys.txt"));
			while (!keys.isEndOfStream())
				textKeys.insert(keys.readLine());
		}
		auto hasText = [&](const char *key) { return textKeys.count("[" + std::string(key) + "]") == 1; };
		for (CustomGameRules::Group group : CustomGameRules::groups)
			REQUIRE(hasText(CustomGameRules::groupLabel(group)));
		CustomGameSetup probe;
		probe.random = true;
		for (const auto &r : CustomGameRules::rules())
		{
			INFO(r.id);
			REQUIRE((hasText(r.label) && hasText(r.help)));
			for (const char *option : r.optionLabels)
				REQUIRE(hasText(option));
			if (r.kind == CustomGameRules::Kind::Toggle || r.kind == CustomGameRules::Kind::Stepper)
				REQUIRE((r.optionIds.empty() && r.optionLabels.empty()));
			else if (std::string_view(r.id) != "speed")
				REQUIRE(r.optionLabels.size() == r.optionIds.size());
			REQUIRE((!r.needsCombat || r.group == CustomGameRules::Group::Combat));
			for (int v = CustomGameRules::minimum(r, probe); v <= CustomGameRules::maximum(r, probe); ++v)
			{
				probe.setRule(r, v);
				REQUIRE(probe.ruleValue(r) == v);
				REQUIRE(!CustomGameRules::optionText(r, v).empty());
			}
		}
		REQUIRE(rule("timeLimit").optionIds.size() == CustomGameSetup::suddenDeathMinuteChoices.size());
		REQUIRE(rule("winProbability").optionIds.size() == CustomGameSetup::winProbabilityChoices.size());
		REQUIRE(rule("speed").optionIds.size() == std::size_t(Settings::GAME_SPEED_MAXIMUM + 1));
		for (int v = 0; v <= Settings::GAME_SPEED_MAXIMUM; ++v)
		{
			Settings speed = globalContainer->settings;
			speed.gameSpeed = v;
			REQUIRE(CustomGameRules::optionText(rule("speed"), v) == speed.getGameSpeedText());
		}

		// The shipped file is valid, starts with Standard and uses every rule somewhere.
		std::vector<std::string> errors;
		const auto parsed = RulesetCatalog::load(RulesetCatalog::filename, errors);
		REQUIRE(parsed);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? std::string() : errors.front()));
		const auto &catalog = RulesetCatalog::shipped();
		REQUIRE((catalog.rulesets.size() == parsed->rulesets.size() && catalog.rulesets.size() == 13));
		REQUIRE((catalog.standard().id == "standard" && catalog.standard().values.empty()));
		std::set<const Rule *> used;
		for (const auto &ruleset : catalog.rulesets)
			for (const auto &[r, value] : ruleset.values)
				used.insert(r);
		REQUIRE(used.size() == CustomGameRules::rules().size());

		// The four rulesets that predate the file keep their exact values.
		CustomGameSetup s;
		s.random = true;
		s.applyRuleset("standard");
		const CustomGameSetup standard = s;
		const int defaultWorkers = GenerationRequest::control(s.generator.method, "workers").defaultValue;
		REQUIRE((s.prestige && !s.revealed && s.locked && s.speed == 0 && s.generator.nbWorkers == defaultWorkers));
		REQUIRE((!s.noResourceGrowth && s.resourceScarcity == 0 && !s.instantConstruction && s.stockpileStart == 0));
		REQUIRE((!s.noHunger && !s.unitUpgradesDisabled && s.glassCannonLevel == 0 && !s.unitsFearless));
		REQUIRE((!s.permadeathDisabled && !s.peacefulMode && s.buildingHpLevel == 0 && s.startingUnitLevel == 0 &&
				 s.suddenDeathMinutes == 0));
		s.applyRuleset("quick-clash");
		REQUIRE((s.speed == 3 && s.generator.nbWorkers == 8));
		s.applyRuleset("open-book");
		REQUIRE((s.revealed && s.speed == 0 && s.generator.nbWorkers == defaultWorkers && s.prestige));
		s.applyRuleset("last-colony-standing");
		REQUIRE((!s.prestige && !s.revealed));
		s.applyRuleset("blitz");
		REQUIRE((s.speed == 3 && s.generator.nbWorkers == 8 && s.startingUnitLevel == 1 && s.stockpileStart == 2 &&
				 s.instantConstruction && s.suddenDeathMinutes == 45));
		// Unknown ids fall back to Standard.
		s.applyRuleset("no-such-ruleset");
		REQUIRE(s.rulesetId == "standard");

		// Edits are changes from the chosen ruleset; undoing one leaves the ruleset whole.
		s.applyRuleset("blitz");
		REQUIRE((s.rulesetDiff().empty() && s.rulesetTitle() == Toolkit::getStringTable()->getString("[Blitz]")));
		auto revision = s.mapRevision;
		REQUIRE((!s.setRule(rule("timeLimit"), 3) && s.mapRevision == revision));
		REQUIRE((s.setRule(rule("workers"), 6) && s.mapRevision > revision));
		REQUIRE((s.rulesetDiff().size() == 2 && s.ruleChanged(rule("workers")) && !s.ruleChanged(rule("speed"))));
		REQUIRE(s.rulesetTitle() != Toolkit::getStringTable()->getString("[Blitz]"));
		s.setRule(rule("workers"), 8);
		s.setRule(rule("timeLimit"), 2);
		REQUIRE(s.rulesetDiff().empty());
		// Values clamp to the rule's range.
		s.setRule(rule("workers"), 99);
		REQUIRE(s.generator.nbWorkers == CustomGameRules::maximum(rule("workers"), s));
		s.setRule(rule("workers"), 8);
		// Premade maps keep their own starting units: those rules are not changes.
		s.random = false;
		s.startingUnitLevel = 0;
		REQUIRE((s.rulesetDiff().empty() && !CustomGameRules::appliesTo(rule("unitLevel"), s)));
		s.random = true;
		s.startingUnitLevel = 1;

		// One Regrowth choice covers both stored resource fields.
		s.setRule(rule("regrowth"), 4);
		REQUIRE((s.noResourceGrowth && s.resourceScarcity == 0));
		s.setRule(rule("regrowth"), 2);
		REQUIRE((!s.noResourceGrowth && s.resourceScarcity == 2));
		s.noResourceGrowth = true;
		s.resourceScarcity = 3;
		REQUIRE(s.ruleValue(rule("regrowth")) == 4);
		// Positive toggles read the stored negative fields.
		s.applyRuleset("sandbox");
		REQUIRE((s.peacefulMode && s.noHunger && s.permadeathDisabled && s.ruleValue(rule("combat")) == 0 &&
				 s.ruleValue(rule("hunger")) == 0 && s.ruleValue(rule("unitsCanDie")) == 0));

		// Rooms carry no speed, workers or unit level: they never count as changes there, and
		// every ruleset survives the room's MatchRules round trip (Quick clash reads as Standard).
		s.applyRuleset("standard");
		s.speed = 5;
		REQUIRE((s.rulesetDiff(true).empty() && s.rulesetDiff().size() == 1));
		for (const auto &ruleset : catalog.rulesets)
		{
			CustomGameSetup draft;
			draft.random = true;
			draft.applyRuleset(ruleset.id);
			CustomGameSetup room;
			Online::applyRulesToSetup(Online::matchRules(draft), room);
			const std::string expected = ruleset.id == "quick-clash" ? "standard" : ruleset.id;
			REQUIRE_MESSAGE(room.rulesetId == expected, ruleset.id);
			REQUIRE(room.rulesetDiff(true).empty());
		}
		CustomGameSetup custom;
		custom.buildingHpLevel = 1;
		custom.glassCannonLevel = 1;
		REQUIRE(Online::matchingRuleset(custom).empty());
		// A room with no matching ruleset reads as Standard and its changes, as the lobby would.
		CustomGameSetup unmatched;
		Online::applyRulesToSetup(Online::matchRules(custom), unmatched);
		REQUIRE((unmatched.rulesetId == "standard" && unmatched.rulesetDiff(true).size() == 2));
		// A room can set a time limit the lobby's menu does not offer: it counts as a change
		// and reads as its minutes.
		CustomGameSetup longRoom;
		longRoom.suddenDeathMinutes = 120;
		REQUIRE((longRoom.ruleValue(rule("timeLimit")) == -1 && longRoom.ruleNonStandard(rule("timeLimit"), true)));
		REQUIRE(CustomGameRules::valueText(rule("timeLimit"), longRoom).find("120") != std::string::npos);
		REQUIRE(Online::matchingRuleset(longRoom).empty());

		// A malformed file keeps the game playable on Standard; a bad entry is skipped alone.
		errors.clear();
		REQUIRE((RulesetCatalog::parse("not json", errors).rulesets.size() == 1 && !errors.empty()));
		errors.clear();
		const auto mixed = RulesetCatalog::parse(R"({"version": 1, "rulesets": [
			{"id": "fast", "name": "[Blitz]", "description": "[Blitz]", "rules": {"speed": "2x"}},
			{"id": "standard", "name": "[Standard]", "description": "[Standard]"},
			{"id": "fast", "name": "[Blitz]", "description": "[Blitz]"},
			{"id": "typo", "name": "[Blitz]", "description": "[Blitz]", "rules": {"sped": "2x"}},
			{"id": "range", "name": "[Blitz]", "description": "[Blitz]", "rules": {"workers": 20}},
			{"id": "option", "name": "[Blitz]", "description": "[Blitz]", "rules": {"regrowth": "sometimes"}},
			{"id": "kind", "name": "[Blitz]", "description": "[Blitz]", "rules": {"combat": "off"}},
			{"id": "key", "name": "Blitz", "description": "[Blitz]"}]})", errors);
		REQUIRE((mixed.rulesets.size() == 2 && mixed.standard().id == "standard" && mixed.find("fast")));
		REQUIRE(mixed.find("fast")->values.size() == 1);
		REQUIRE(errors.size() == 6);
		errors.clear();
		const auto noStandard = RulesetCatalog::parse(R"({"version": 1, "rulesets": [
			{"id": "fast", "name": "[Blitz]", "description": "[Blitz]", "rules": {"speed": "2x"}}]})", errors);
		REQUIRE((noStandard.standard().id == "standard" && noStandard.find("fast") && errors.size() == 1));
		// Standard cannot change rules, and an unknown version is reported but still read.
		errors.clear();
		const auto strict = RulesetCatalog::parse(R"({"version": 2, "rulesets": [
			{"id": "standard", "name": "[Standard]", "description": "[Standard]", "rules": {"speed": "2x"}}]})", errors);
		REQUIRE((strict.rulesets.size() == 1 && strict.standard().values.empty() && errors.size() == 2));
		std::cout << "PASS rulesets: shipped catalog, legacy values, changes, regrowth, rooms, invalid files\n";
	}
	static void model()
	{
		CustomGameSetup s;
		REQUIRE((s.capacity == 4 && s.activeColonies() == 4 && s.controllerCount() == 4 &&
			   s.humanColony() == 0));
		const TeamLayout::Layout twoVsTwo{TeamLayout::Layout::Split, {2, 2}, -1};
		REQUIRE(s.teamLayout().kind == TeamLayout::Layout::FreeForAll);
		REQUIRE(s.applyTeamLayout(twoVsTwo));
		REQUIRE((s.teamLayout() == twoVsTwo && std::string(s.legacyFormat()) == "2 vs 2"));
		auto alliances = s.colonies;
		s.colonies[2].ai = AI::CASTOR;
		for (int i = 0; i < 4; ++i)
			REQUIRE(s.colonies[i].alliance == alliances[i].alliance);
		REQUIRE((s.setController(0, CustomGameSetup::Shared) && s.controllerCount() == 5));
		// Changing who controls a colony leaves its teams, and their name, alone.
		REQUIRE(s.teamLayout() == twoVsTwo);
		s.setCapacity(2);
		s.setCapacity(4);
		REQUIRE(s.colonies[3].alliance == alliances[3].alliance);
		REQUIRE(s.setController(3, CustomGameSetup::Human));
		REQUIRE((s.humanColony() == 3 && s.colonies[0].controller == CustomGameSetup::Computer));
		GameHeader h;
		s.writeHeader(h, "test");
		REQUIRE((h.getBasePlayer(0).teamNumber == 3 && h.getNumberOfPlayers() == 4));
		REQUIRE(s.setController(3, CustomGameSetup::Shared));
		s.writeHeader(h, "test");
		REQUIRE(h.getNumberOfPlayers() == 5);
		REQUIRE(h.getBasePlayer(4).teamNumber == 3);
		s.setCapacity(Team::MAX_COUNT);
		REQUIRE(!s.validation().empty());
		REQUIRE(!s.setController(2, CustomGameSetup::Shared));
		REQUIRE(s.setController(3, CustomGameSetup::Computer));
		REQUIRE((s.controllerCount() == Team::MAX_COUNT && !s.humanColony()));
		// Only watching: one against all leaves the first colony alone, and every equal split
		// of the sixteen colonies is offered.
		REQUIRE(TeamLayout::offered(s.activeColonies()).size() == 11);
		REQUIRE(s.applyTeamLayout(TeamLayout::Layout{TeamLayout::Layout::Split, {1, 15}, -1}));
		REQUIRE((s.teamLayout().oneVsAll() && s.loneColony(s.teamLayout()) == 0));
		REQUIRE(std::string(s.legacyFormat()) == "Custom teams");
		REQUIRE(s.applyTeamLayout(TeamLayout::Layout{TeamLayout::Layout::Split, {4, 4, 4, 4}, -1}));
		REQUIRE((s.colonies[3].alliance == 0 && s.colonies[4].alliance == 1 && s.colonies[15].alliance == 3));
		REQUIRE(!s.applyTeamLayout(TeamLayout::Layout{}));
		REQUIRE(s.applyTeamLayout(TeamLayout::Layout{TeamLayout::Layout::FreeForAll, {}, -1}));
		REQUIRE(s.teamLayout().kind == TeamLayout::Layout::FreeForAll);
		// A team set colony by colony that no offered shape matches reads as custom.
		s.colonies[1].alliance = s.colonies[2].alliance;
		REQUIRE(s.teamLayout().kind == TeamLayout::Layout::Custom);
		REQUIRE(s.applyTeamLayout(TeamLayout::Layout{TeamLayout::Layout::FreeForAll, {}, -1}));
		s.setCapacity(4);
		auto revision = s.mapRevision;
		REQUIRE(s.applyRuleset("quick-clash"));
		REQUIRE((s.speed == 3 && s.generator.nbWorkers == 8 && s.mapRevision > revision));
		s.applyRuleset("open-book");
		REQUIRE((s.revealed && s.prestige && s.speed == 0));
		s.applyRuleset("last-colony-standing");
		REQUIRE((!s.prestige && !s.revealed));
		for (int i = 0; i < 4; ++i)
			s.setController(i, CustomGameSetup::Closed);
		REQUIRE(!s.validation().empty());
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
		REQUIRE(e.initGame(header, game, true, false, false, map) == Engine::EE_NO_ERROR);
		REQUIRE(globalContainer->liveSpectating == (control == CustomGameSetup::Computer));
		REQUIRE(!globalContainer->replaying);
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
			REQUIRE(e.gui.getOrder()->getOrderType() == ORDER_NULL);
			REQUIRE(e.gui.orderQueue.empty());
			REQUIRE((globalContainer->replayVisibleTeams == 0xffffffffu &&
				   !globalContainer->replayShowFog));
		}
		e.gatherAndAdvanceOrders(true);
		for (auto ai : counters)
			REQUIRE(ai->calls == 1);
		// Repeating a not-ready tick must not ask any AI twice.
		e.gatherAndAdvanceOrders(false);
		for (auto ai : counters)
			REQUIRE(ai->calls == 1);
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
			// Mirror whatever container the source map actually uses (raw or ".gz")
			// so the copy holds a byte-identical, correctly-suffixed container.
			const std::string resolvedMap = glob2PreferGzipReadPath(*Toolkit::getFileManager(), map);
			const std::string transientBase = (std::filesystem::temp_directory_path() /
									("glob2-live-test-" + std::to_string(getpid()) + ".map"))
									   .string();
			const std::string transient = glob2IsGzipPath(resolvedMap) ? glob2GzipWritePath(transientBase) : transientBase;
			std::filesystem::copy_file(glob2test::sourceRoot() / resolvedMap, transient,
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
			REQUIRE(e.initGame(header, players, true, false, false, transient) ==
				   Engine::EE_NO_ERROR);
			std::filesystem::remove(transient);
			e.run();
		}
		{
			Engine e;
			REQUIRE(e.loadReplay("replays/last_game.replay") == Engine::EE_NO_ERROR);
			REQUIRE((globalContainer->replaying && !globalContainer->liveSpectating));
			globalContainer->automaticEndingSteps = 250;
			e.run();
			e.clearReplayState();
		}
		globalContainer->automaticEndingGame = false;
		std::cout << "PASS real match and replay playback: control " << control << " solo " << solo
				  << "\n";
	}
	// Engine::gui is private; this struct is its friend.
	static void experiments()
	{
		const auto dir = std::filesystem::temp_directory_path() / ("glob2-experiments-test-" + std::to_string(getpid()));
		std::filesystem::create_directory(dir);
		const auto save = (dir / "experiment.game").string();
		const std::string map = "maps/FourSquares1.map";
		// The lobby path: the screen builds the header and applies the player's
		// experiments to it, and the game runs with that header.
		auto launchFromLobby = [&](Engine &e)
		{
			auto header = Engine::loadMapHeader(map);
			CustomGameSetup s;
			s.setCapacity(header.getNumberOfTeams());
			GameHeader game;
			s.writeHeader(game, "test");
			Engine::applyLocalExperiments(game, header);
			REQUIRE(e.initGame(header, game, true, false, false, map) == Engine::EE_NO_ERROR);
		};
		globalContainer->settings.experiments.set(ExperimentId::GuardAreaBalancing);
		{
			Engine e;
			launchFromLobby(e);
			REQUIRE(e.gui.game.gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing));
			GAGCore::BinaryOutputStream out(Toolkit::getFileManager()->openOutputStreamBackend(save));
			e.gui.save(&out, "Experiment test");
		}
		{
			// Campaign missions play as authored, whatever the settings say.
			Engine e;
			REQUIRE(e.initCampaign(map) == Engine::EE_NO_ERROR);
			REQUIRE(e.gui.game.gameHeader.getExperiments().empty());
		}
		{
			// The helper leaves a saved game's header alone.
			const auto savedMap = Engine::loadMapHeader(save);
			REQUIRE(savedMap.getIsSavedGame());
			GameHeader header;
			Engine::applyLocalExperiments(header, savedMap);
			REQUIRE(header.getExperiments().empty());
		}
		// The setting is turned off again: the save keeps the set it was started
		// with, and a fresh game no longer gets it.
		globalContainer->settings.experiments.clear();
		{
			Engine e;
			REQUIRE(e.initCustom(save) == Engine::EE_NO_ERROR);
			REQUIRE(e.gui.game.gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing));
		}
		{
			Engine e;
			launchFromLobby(e);
			REQUIRE(e.gui.game.gameHeader.getExperiments().empty());
		}
		// The lobby model itself stays free of settings: writeHeader() leaves the
		// set alone for the screen to fill in.
		CustomGameSetup setup;
		GameHeader header;
		header.getExperiments().set(ExperimentId::GuardAreaBalancing);
		setup.writeHeader(header, "test");
		REQUIRE(header.hasExperiment(ExperimentId::GuardAreaBalancing));
		std::filesystem::remove_all(dir);
		std::cout << "PASS experiments baked into a new game, kept by its save, never in a campaign\n";
	}
	static void reload(const std::string &save, bool watching, int controllers)
	{
		Engine e;
		REQUIRE(e.initCustom(save) == Engine::EE_NO_ERROR);
		REQUIRE(globalContainer->liveSpectating == watching);
		REQUIRE(e.gui.game.gameHeader.getNumberOfPlayers() == controllers);
		std::cout << "PASS save/load: " << (watching ? "AI-only" : "human/shared") << "\n";
	}
};
static void checkPreviewRestart()
{
    GenerationRequest request;
    request.setMethodDefaults(0);
    request.wDec = request.hDec = 8;
    request.nbTeams = 4;
    LandscapePreviewer previewer({request}, 1);
    const auto started = SDL_GetTicks();
    while (previewer.preview(0).state == LandscapePreviewer::State::Pending &&
           SDL_GetTicks() - started < 10000)
        SDL_Delay(1);
    // The worker has picked the request up. A preview this small can already have
    // finished (usually Failed: four colonies do not fit), so accept any started state;
    // restart must leave nothing busy either way.
    REQUIRE(previewer.preview(0).state != LandscapePreviewer::State::Pending);
    previewer.restart({});
    REQUIRE((!previewer.busy() && previewer.finished() == 0));
    // Destruction joins the in-flight worker after restart has removed its slot.
}

// The modes the old command line selected, one case each. Display cases open the
// 640x480 window (1000x700 for the large layouts) the wrapper commands used to ask for.
static glob2test::GlobalsOptions setupOptions(bool display, bool large = false, const char* language = nullptr)
{
	glob2test::GlobalsOptions options{.display = display, .loadStrings = true, .width = large ? 1000 : 640, .height = large ? 700 : 480,
	                                  .screenFlags = GraphicContext::USEGPU, .profileName = "glob2-custom-setup-tests"};
	if (language)
		options.beforeLoad = [language](GlobalContainer& globals) { globals.settings.language = language; };
	return options;
}

// Preferences model, options, landscape order and preview priority precede every mode
// that drives the setup screens.
static void commonChecks()
{
	Toolkit::getFileManager()->remove(CustomGamePreferences::filename);
	CustomGameSetupHarness::preferencesModel();
	CustomGameSetupHarness::preferencesOptions();
	CustomGameSetupHarness::landscapeRandomOrder();
	CustomGameSetupHarness::previewPriority();
	REQUIRE(NET_Init());
}

TEST_SUITE("CustomGameSetup")
{
    TEST_CASE("map previews retain embedded experimental catalogs through cache hits")
    {
        glob2test::HeadlessGlobals globals(setupOptions(false));
        CustomGameSetupHarness::catalogExperimentsInPreview();
    }

    TEST_CASE("local building experiments are filtered by the destination catalog while saves retain theirs")
    {
        glob2test::HeadlessGlobals globals;
        const std::vector<std::string> localKeys{"fixture-local", "fixture-foreign"};
        auto& settings = globalContainer->settings.experiments;
        settings.set(ExperimentId::GuardAreaBalancing);
        for (const auto& key : localKeys) settings.set(key, true, localKeys);
        BuildingsTypes catalog;
        catalog.initLegacy();
        auto json = nlohmann::json::parse(catalog.snapshotJson());
        json["experiments"].push_back({{"key", localKeys[0]}, {"label", "Local"}, {"help", "Fixture"}});
        GameHeader header;
        header.setBuildingCatalogSnapshot(json.dump());
        MapHeader map;
        map.setIsSavedGame(false);
        Engine::applyLocalExperiments(header, map);
        CHECK(header.getExperiments().has(ExperimentId::GuardAreaBalancing));
        CHECK(header.getExperiments().has(localKeys[0]));
        CHECK_FALSE(header.getExperiments().has(localKeys[1]));
        header.setBuildingCatalogSnapshot(catalog.snapshotJson());
        Engine::applyLocalExperiments(header, map);
        CHECK(header.getExperiments().size() == 1);
        header.setBuildingCatalogSnapshot(json.dump());
        header.getExperiments().set(localKeys[0], true, localKeys);
        const auto saved = header.getExperiments();
        settings.clear();
        map.setIsSavedGame(true);
        Engine::applyLocalExperiments(header, map);
        CHECK(header.getExperiments() == saved);
    }

    TEST_CASE("catalog local launch loads the exact cached version and rejects invalid maps [writes-preferences]")
    {
        glob2test::HeadlessGlobals globals(setupOptions(false));
        CustomGameSetupHarness::catalogLaunch();
    }

	TEST_CASE("custom AI library identities and released preferences round trip")
	{
		glob2test::HeadlessGlobals globals(setupOptions(false));
		CustomGameSetupHarness::preferencesModel();
	}

	TEST_CASE("preferences; landscapes; AI catalogue; snapshot round trip; engine; reload and session replay [slow][writes-preferences]")
	{
		glob2test::HeadlessGlobals globals(setupOptions(false));
		commonChecks();
	REQUIRE(NET_Init());
	checkPreviewRestart();
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
		REQUIRE(generated);
	}
	std::cout << "PASS all " << playableMethods.size() << " playable generator landscapes\n";

	CustomGameSetupHarness::model();
	CustomGameSetupHarness::rulesets();
	const auto &selection = AINames::selectionOrder();
	REQUIRE(selection.back() == AI::NONE);
	REQUIRE((std::set<int>(selection.begin(), selection.end()) ==
		std::set<int>{AI::ECONO, AI::NUMBI, AI::WARRUSH, AI::CASTOR, AI::CORTEX,
			AI::CABINO, AI::NICOWAR, AI::MAXIMA, AI::NONE}));
	REQUIRE(selection.size() == 9);
	for (size_t i = 1; i + 1 < selection.size(); ++i)
		REQUIRE(AINames::getAIStrength(selection[i-1]) <= AINames::getAIStrength(selection[i]));
	for (int id : selection)
		if (id != AI::NONE)
			REQUIRE(AINames::getAISelectorText(id).find("(" +
				std::to_string(AINames::getAIStrength(id)) + ")") != std::string::npos);
	for (int id : AINames::selectionOrder())
		REQUIRE(AINames::selectionOrder()[AINames::selectionIndex(id)] == id);
	static_assert(AI::ECONO == 4, "Econo must retain its save ID");
	REQUIRE(AINames::parseAIName("Econo") == AI::ECONO);
	REQUIRE(AINames::getAISelectorText(AI::ECONO) == "Econo - Easy (" + std::to_string(AINames::getAIStrength(AI::ECONO)) + ") - No warriors");
	REQUIRE(AINames::getAIProfile(AI::CORTEX).find("wheat") != std::string::npos);
	REQUIRE(AINames::getAIProfile(AI::CORTEX).find("\n\nStrengths and weaknesses:") != std::string::npos);
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
		REQUIRE(generated);
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
		REQUIRE(loaded.load(&in));
		REQUIRE(g.map.getW() == loaded.map.getW());
		REQUIRE(g.map.getH() == loaded.map.getH());
		for (int y = 0; y < g.map.getH(); ++y)
			for (int x = 0; x < g.map.getW(); ++x)
			{
				REQUIRE(g.map.getTerrain(x, y) == loaded.map.getTerrain(x, y));
				REQUIRE(g.map.getResource(x, y).type == loaded.map.getResource(x, y).type);
			}
		for (int i = 0; i < 4; ++i)
		{
			REQUIRE(g.teams[i]->startPosX == loaded.teams[i]->startPosX);
			REQUIRE(g.teams[i]->startPosY == loaded.teams[i]->startPosY);
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
		REQUIRE(generator.generateMap(game, request));
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
	TEST_CASE("generated map snapshot launches from memory")
	{
		glob2test::HeadlessGlobals globals(setupOptions(false));
		Game map(nullptr);
		GAGCore::BinaryInputStream source(
			Toolkit::getFileManager()->openInflatingInputStreamBackend("maps/balanced.map.gz"));
		REQUIRE(map.load(&source));
		CustomGameSetup setup;
		setup.setCapacity(map.mapHeader.getNumberOfTeams());
		GameHeader players;
		setup.writeHeader(players, "snapshot test");
		map.setGameHeader(players);
		auto *backend = new GAGCore::MemoryStreamBackend();
		GAGCore::BinaryOutputStream serialized(backend);
		map.save(&serialized, true, "Snapshot test");
		auto bytes = std::make_shared<std::string>(backend->takeContents());
		Engine engine;
		REQUIRE(engine.initCustomFromBytesTask(map.mapHeader, players, 0, -1, bytes).run());
		std::cout << "PASS generated map snapshot launches from memory\n";
	}
	TEST_CASE("experiments from settings are baked into a new game; a save keeps them; campaigns never take them")
	{
		glob2test::HeadlessGlobals globals(setupOptions(false));
		CustomGameSetupHarness::experiments();
	}
	TEST_CASE("preview queue priority and restart")
	{
		glob2test::HeadlessGlobals globals(setupOptions(false));
		CustomGameSetupHarness::previewPriority();
		checkPreviewRestart();
		std::cout << "PASS preview restart joins superseded workers safely\n";
	}
	TEST_CASE("landscape preview performance [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(setupOptions(true));
		CustomGameSetupHarness::landscapePerformance(glob2test::artifactDirFromWorkingDirectory(), false);
	}
	TEST_CASE("landscape preview stays responsive [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(setupOptions(true));
		CustomGameSetupHarness::landscapePerformance(glob2test::artifactDirFromWorkingDirectory(), true);
	}
	TEST_CASE("controller help is localized with the current player capacity [display]")
	{
		glob2test::HeadlessGlobals globals(setupOptions(true));
		std::string output;
		{
			glob2test::CapturedStdout out; glob2test::CapturedStderr err;
			CustomGameSetupHarness::controllerHelp();
			output=out.text()+err.text();
		}
		CHECK(output.find("no such key")==std::string::npos);
	}
	TEST_CASE("custom game screens; captures and translated keys [slow][display:1024x768][artifacts][writes-preferences]")
	{
		glob2test::HeadlessGlobals globals(setupOptions(true));
		commonChecks();
		std::string output;
		{
			// The old CI step failed on any "no such key" line in the log; keep that check here.
			glob2test::CapturedStdout out;
			glob2test::CapturedStderr err;
			CustomGameSetupHarness::visual(glob2test::artifactDirFromWorkingDirectory(), false);
			output = out.text() + err.text();
		}
		std::cout << output;
		REQUIRE_MESSAGE(output.find("no such key") == std::string::npos, "a screen asked for a missing translation key");
	}
    TEST_CASE("probability statistics fit sixteen colonies [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals({.display = true, .loadStrings = true});
        CustomGameSetupHarness::probabilityVisual();
    }

	TEST_CASE("AI profile captures [display][artifacts][writes-preferences]")
	{
		glob2test::HeadlessGlobals globals(setupOptions(true));
		commonChecks();
		CustomGameSetupHarness::visual(glob2test::artifactDirFromWorkingDirectory(), true);
	}
	TEST_CASE("player control widgets [display][artifacts][writes-preferences]")
	{
		glob2test::HeadlessGlobals globals(setupOptions(true));
		commonChecks();
		for (int control : {int(CustomGameSetup::Computer), int(CustomGameSetup::Human), int(CustomGameSetup::Shared)})
			CustomGameSetupHarness::ui(glob2test::artifactDirFromWorkingDirectory(), control);
	}
	TEST_CASE("AI strategy profiles in English [display][artifacts][writes-preferences]")
	{
		glob2test::HeadlessGlobals globals(setupOptions(true, false, "en"));
		commonChecks();
		REQUIRE(Toolkit::getStringTable()->getString("[language-code]") == globalContainer->settings.language);
		CustomGameSetupHarness::strategyVisual(glob2test::artifactDirFromWorkingDirectory());
	}
	TEST_CASE("AI strategy profiles in English at the large layout [display:1024x768][artifacts][writes-preferences]")
	{
		glob2test::HeadlessGlobals globals(setupOptions(true, true, "en"));
		commonChecks();
		CustomGameSetupHarness::strategyVisual(glob2test::artifactDirFromWorkingDirectory());
	}
	TEST_CASE("preferences screen writes then reads back [display][writes-preferences]")
	{
		{
			glob2test::HeadlessGlobals globals(setupOptions(true));
			CustomGameSetupHarness::preferencesScreen(true);
		}
		{
			glob2test::HeadlessGlobals globals(setupOptions(true));
			CustomGameSetupHarness::preferencesScreen(false);
		}
	}
}
