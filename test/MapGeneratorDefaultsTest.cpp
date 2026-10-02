// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "CustomGameSetup.h"
#include <vector>
#include <string>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstdint>
#include "Contact.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include "GenerationValidation.h"
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "LegacyGenerationDescriptor.h"
#include "MapGeneratorFrameworkChecks.h"
#include "MapGeneratorContracts.h"
#include "HungryMarchesContracts.h"
#include "MapGeneratorLandscapeChecks.h"
#include "MapGeneratorToolkitChecks.h"
#include "NewMapScreen.h"
#include "Race.h"
#include "Resources.h"
#include "Sketch.h"
#include "StartingPositions.h"
#include "Unit.h"
#include "Utilities.h"
#include <SDL_image.h>
#include <Toolkit.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <tuple>
#include <utility>

using D = GenerationRequest;

class MapGeneratorDefaultsTest
{
  public:
	static void select(NewMapScreen &s, int method) { s.chooseMethod(method); }
	static void sameControls(const D &a, const D &b)
	{
		REQUIRE(a.method == b.method);
		for (const auto &c : D::controls(a.method))
			REQUIRE(c.get(a) == c.get(b));
		for (const auto &c : D::sharedControls())
			REQUIRE(c.get(a) == c.get(b));
	}
	// Edits the control the form shows under `label`, the way the screen's own choice or
	// toggle callback would.
	static void edit(NewMapScreen &s, const char *label, int value)
	{
		std::vector<const GeneratorControl *> shown;
		const bool blank = s.descriptor.method == GenerationRequest::eUNIFORM;
		for (const auto &c : D::sharedControls())
			if (!blank || c.id == "width" || c.id == "height")
				shown.push_back(&c);
		if (!blank)
			for (const auto &c : s.registry.at(s.descriptor.method).controls)
				shown.push_back(&c);
		for (const auto *c : shown)
		{
			if (std::strcmp(c->label, label) != 0)
				continue;
			if (c->isToggle())
				REQUIRE((value == 0 || value == 1));
			c->set(s.descriptor, value);
			s.invalidatePreview();
			REQUIRE(c->get(s.descriptor) == value);
			return;
		}
		REQUIRE(false);
	}
	static void defaultGenerationContract(int method)
	{
		globalsInit();
		GenerationService service;
		D request;
		request.setMethodDefaults(method);
		request.seed = 22001;
		setSyncRandSeed(177);
		auto surrounding = syncRandEngine();
		Game first(nullptr);
		auto a = service.generate(first, request);
		REQUIRE(syncRandEngine() == surrounding);
		auto hash = mapFingerprint(first);
		auto checksum = first.checkSum(nullptr, nullptr, nullptr, true);
		D intervening;
		intervening.setMethodDefaults(D::eISLANDS);
		intervening.seed = 9017;
		Game other(nullptr);
		service.generate(other, intervening);
		Game repeat(nullptr);
		auto b = service.generate(repeat, request);
		if (bool(a) != bool(b) || a.stage != b.stage || hash != mapFingerprint(repeat))
			std::fprintf(stderr,
						 "Not repeatable after an intervening map: generator %d (%s, %s)\n",
						 method, a.diagnostic().c_str(), b.diagnostic().c_str());
		REQUIRE((bool(a) == bool(b) && a.stage == b.stage && hash == mapFingerprint(repeat)));
		REQUIRE(checksum == repeat.checkSum(nullptr, nullptr, nullptr, true));
		REQUIRE((request.seed == 22001 && request.options == DWithDefaults(method).options));
		if (method == GeneratorRegistry::builtins().idOf("lava-shield"))
		{
			if (!a)
				std::cerr << a.diagnostic() << std::endl;
			REQUIRE(a); // A repeatable failure is not a valid default map.
			GenerationContext probe(request);
			const auto &definition = GeneratorRegistry::builtins().at(method);
			REQUIRE(definition.validateWorld(first, probe).empty());
			// Removing all rock must be caught by the generator's actual final-world
			// validator, not merely by a golden hash. Corrupt the unused repeated copy.
			for (int y = 0; y < repeat.map.getH(); ++y)
				for (int x = 0; x < repeat.map.getW(); ++x)
					if (repeat.map.getResource(x, y).type == STONE)
						repeat.map.setNoResource(x, y, 1);
			REQUIRE(!definition.validateWorld(repeat, probe).empty());
		}
		if (a)
		{
			auto rejected = service.generate(first, request);
			REQUIRE(rejected.error == GenerationError::NonEmptyTarget);
			REQUIRE(mapFingerprint(first) == hash);
		}
		if (method != D::eUNIFORM)
			for (auto dimensions : {std::pair{9, 7}, std::pair{7, 9}})
			{
				D rectangular = request;
				rectangular.seed = 31001;
				rectangular.wDec = dimensions.first;
				rectangular.hDec = dimensions.second;
				Game world(nullptr);
				// A landscape may refuse a shape its concept cannot hold (Emoji needs a
				// square); it must then say so up front, through its request check.
				if (const auto &d = GeneratorRegistry::builtins().at(method);
					d.validateRequest && !d.validateRequest(rectangular).empty())
				{
					REQUIRE(service.generate(world, rectangular).error ==
						   GenerationError::InvalidRequest);
					continue;
				}
				REQUIRE(service.generate(world, rectangular));
				REQUIRE(world.map.getW() == (1 << dimensions.first));
				REQUIRE(world.map.getH() == (1 << dimensions.second));
				for (int team = 0; team < rectangular.nbTeams; ++team)
				{
					REQUIRE((world.teams[team]->startPosX >= 0 &&
						   world.teams[team]->startPosX < world.map.getW()));
					REQUIRE((world.teams[team]->startPosY >= 0 &&
						   world.teams[team]->startPosY < world.map.getH()));
				}
			}
		for (const auto &c : D::controls(method))
		{
			D invalid = request;
			invalid.options[c.id] = c.maximum + c.step;
			Game fresh(nullptr);
			auto failure = service.generate(fresh, invalid);
			REQUIRE((failure.error == GenerationError::InvalidRequest && fresh.teamsCount() == 0));
		}
	}
	static void generationContracts()
	{
		globalsInit();
		GenerationService service;
		// Contested commons spreads its colonies with the whole-region search, which used to run
		// out of a fixed evaluation budget past four colonies at 256 and at any count at 512.
		for (auto [dims, teams] : {std::pair{8, 8}, std::pair{9, 4}, std::pair{9, 12}})
			for (std::uint32_t seed = 1; seed <= 3; ++seed)
			{
				D crowded;
				crowded.setMethodDefaults(GeneratorRegistry::builtins().idOf("contested-commons"));
				crowded.wDec = crowded.hDec = dims;
				crowded.nbTeams = teams;
				crowded.seed = seed;
				Game world(nullptr);
				const auto outcome = service.generate(world, crowded);
				if (!outcome)
					std::cerr << outcome.diagnostic() << std::endl;
				REQUIRE((outcome && world.teamsCount() == teams));
			}
		// These combined controls exhausted real coastal towns/approaches in held-out
		// seeds. Reject them before world mutation, while retaining the full control
		// range on an ordinary four-colony map. Test each budget independently.
		for (int dims : {7, 8})
		{
			D lava;
			lava.setMethodDefaults(GeneratorRegistry::builtins().idOf("lava-shield"));
			lava.wDec = lava.hDec = dims;
			lava.nbTeams = dims == 7 ? 2 : 8;
			const auto &definition = GeneratorRegistry::builtins().at(lava.method);
			REQUIRE(validateGenerationRequest(lava, definition).empty());
			for (const char *key : {"tongue-count", "rim-width"})
			{
				D crowded = lava;
				crowded.options[key] = std::string(key) == "tongue-count" ? 9 : 12;
				Game untouched(nullptr);
				const auto result = service.generate(untouched, crowded);
				REQUIRE((result.error == GenerationError::InvalidRequest &&
					   untouched.teamsCount() == 0));
				crowded.wDec = crowded.hDec = 8;
				crowded.nbTeams = 4;
				REQUIRE(validateGenerationRequest(crowded, definition).empty());
			}
		}
		// Scoped RNG restoration must also hold when placement fails.
		setSyncRandSeed(711);
		auto savedFailureRng = syncRandEngine();
		D invalid;
		invalid.method = 99999;
		Game fresh(nullptr);
		REQUIRE(service.generate(fresh, invalid).error == GenerationError::InvalidRequest);
		invalid = DWithDefaults(D::eRIVER);
		invalid.wDec = 31;
		REQUIRE(service.generate(fresh, invalid).error == GenerationError::InvalidRequest);
		invalid = DWithDefaults(D::eSWAMP);
		// An all-water swamp can still fit its colonies on a big map, so pin this failure to 128x128
		// whatever the default size is.
		invalid.wDec = invalid.hDec = 7;
		invalid.options["water"] = 100;
		invalid.options["grass"] = 0;
		auto failed = service.generate(fresh, invalid);
		REQUIRE((!failed && failed.error == GenerationError::PlacementFailed &&
			   !failed.stage.empty()));
		REQUIRE(syncRandEngine() == savedFailureRng);
		// Legacy sentinel conversion belongs exclusively to the adapter.
		for (auto pair : {std::pair{D::eCRATERLAKES, 30}, std::pair{D::eCONCRETEISLANDS, 6},
						  std::pair{D::eISLES, 4}})
		{
			MapGenerationDescriptor legacy;
			legacy.setMethodDefaults(static_cast<MapGenerationDescriptor::Method>(pair.first));
			legacy.riverDiameter = 50;
			auto converted = fromLegacyDescriptor(legacy, 42);
			const char *key = pair.first == D::eCRATERLAKES ? "lake-size"
							  : pair.first == D::eISLES     ? "bridge-width"
															: "channel-width";
			REQUIRE(converted.option(key) == pair.second);
		}
		// New registrations need no ordinal dispatch, UI edits or new descriptor fields.
		std::vector<GeneratorDefinition> definitions;
		for (int id : GeneratorRegistry::builtins().methods())
			definitions.push_back(GeneratorRegistry::builtins().at(id));
		definitions.push_back(
			{"test-plateau",
			 101,
			 "uniform terrain",
			 1,
			 false,
			 {{"test-elevation", "Smoothing", 2, 10, 2, 6},
			  {"test-cell",
			   "Island size",
			   4,
			   16,
			   1,
			   8,
			   ControlGroup::Layout,
			   false,
			   false,
			   {4, 8, 16}},
			  {"test-gap", "Channel width", 1, 5, 2, 3, ControlGroup::Layout},
			  GeneratorControl::choice("test-shape", "Cell shape", {"Squares", "Hexagons"}, 0),
			  GeneratorControl::toggle("test-switch", "Lake connects to fjords", false)},
			 [](Game &game, GenerationContext &context)
			 {
				 const int elevation = context.request.option("test-elevation");
				 REQUIRE(elevation == 8);
				 REQUIRE(context.request.option("test-switch") == 1);
				 game.map.makeHomogenMap(GRASS);
				 for (int team = 0; team < context.request.nbTeams; ++team)
				 {
					 context.bootX[team] = elevation + (team % 2) * 40;
					 context.bootY[team] = elevation + (team / 2) * 40;
				 }
				 return MapGeneration::placeStarts(game, context);
			 }});
		// Every playable registration needs catalog tags (#333).
		definitions.back().tags = {"terrain:novelty"};
		// A toggle is exactly 0 or 1, shown as a checkbox: any other domain is a registration error.
		{
			const auto rejected = [](GeneratorControl control)
			{
				GeneratorDefinition definition{"test-toggle",
											   102,
											   "uniform terrain",
											   1,
											   false,
											   {std::move(control)},
											   [](Game &, GenerationContext &) { return false; }};
				// Tagged, so a rejection can only come from the control under test.
				definition.tags = {"terrain:novelty"};
				try
				{
					GeneratorRegistry({definition});
				}
				catch (const std::invalid_argument &)
				{
					return true;
				}
				return false;
			};
			const auto on = GeneratorControl::toggle("switch", "Lake connects to fjords", true);
			REQUIRE((on.isToggle() && on.defaultValue == 1 &&
				   on.values() == std::vector<int>({0, 1})));
			REQUIRE(!rejected(on));
			const auto amount = GeneratorControl::percentage("amount", "Fruit");
			REQUIRE((!amount.isToggle() && amount.defaultValue == 100 && !rejected(amount)));
			// A choice stores the index of its named option and is shown by name.
			const auto shape =
				GeneratorControl::choice("shape", "Cell shape", {"Squares", "Hexagons"}, 1);
			REQUIRE((shape.isChoice() && !shape.isToggle() && shape.defaultValue == 1 &&
				   shape.values() == std::vector<int>({0, 1}) &&
				   std::string(shape.valueLabel(1)) == "Hexagons" && !shape.valueLabel(2) &&
				   shape.normalize(5) == 1 && !rejected(shape)));
			auto unnamed = shape;
			unnamed.valueLabels.pop_back();
			auto blank = shape;
			blank.valueLabels[0] = "";
			REQUIRE((rejected(unnamed) && rejected(blank) && !on.isChoice() && !on.valueLabel(0)));
			std::vector<GeneratorControl> broken(6, on);
			broken[0].maximum = 2;
			broken[1].minimum = broken[1].defaultValue = 1;
			broken[2].powerOfTwo = true;
			broken[3].terrainWeight = true;
			broken[4].allowedValues = {0, 1};
			broken[5].defaultValue = 2;
			for (const auto &control : broken)
				REQUIRE(rejected(control));
		}
		GeneratorRegistry registry(std::move(definitions));
		NewMapScreen screen(registry);
		select(screen, 101);
		REQUIRE(screen.descriptor.option("test-elevation") == 6);
		REQUIRE(screen.descriptor.option("test-switch") == 0);
		edit(screen, "Island size", 16);
		edit(screen, "Channel width", 5);
		edit(screen, "Lake connects to fjords", 1);
		REQUIRE(screen.descriptor.option("test-shape") == 0);
		edit(screen, "Cell shape", 1);
		select(screen, D::eRIVER);
		select(screen, 101);
		REQUIRE((screen.descriptor.option("test-cell") == 16 &&
			   screen.descriptor.option("test-gap") == 5 &&
			   screen.descriptor.option("test-shape") == 1 &&
			   screen.descriptor.option("test-switch") == 1));
		edit(screen, "Smoothing", 8);
		REQUIRE(registry.selectionIndex(101) == int(GeneratorRegistry::builtins().methods().size()));
		const auto playable = registry.methods(false);
		REQUIRE(std::find(playable.begin(), playable.end(), 101) != playable.end());
		Game generated(nullptr);
		REQUIRE(GenerationService(registry).generate(generated, screen.descriptor));
		// Candidate sampling picks the best-scoring roll and hands back its seed, which the
		// editor then regenerates. That only works because generation is deterministic and
		// because the score is a pure function of the finished map.
		{
			D sampled = DWithDefaults(D::eRIVER);
			sampled.nbTeams = 4;
			const std::uint32_t root = 20260911;
			const std::uint32_t chosen = service.bestSeed(sampled, root);
			// Regenerating the chosen seed reproduces the roll that was scored, and choosing
			// again from the same root returns the same seed.
			D winner = sampled;
			winner.seed = chosen;
			Game first(nullptr), second(nullptr);
			const auto a = service.generate(first, winner);
			const auto b = service.generate(second, winner);
			REQUIRE((a && b && a.quality.measured));
			REQUIRE(a.quality.score == b.quality.score);
			REQUIRE(service.bestSeed(sampled, root) == chosen);
			// It is one of the candidates, and none of the others scores higher.
			bool sawChosen = false;
			for (int attempt = 0; attempt < GenerationService::kSampledCandidates; ++attempt)
			{
				D roll = sampled;
				roll.seed =
					GenerationContext::deriveSeed(root, "attempt/" + std::to_string(attempt));
				sawChosen = sawChosen || roll.seed == chosen;
				Game world(nullptr);
				const auto rolled = service.generate(world, roll);
				REQUIRE((!rolled || rolled.quality.score <= a.quality.score));
			}
			REQUIRE(sawChosen);
		}
		puts("PASS registration extension, explicit seeds, interleaved repeatability, RNG "
			 "isolation, errors and legacy sentinels");
	}
	static void gauntletContracts()
	{
		GenerationService service;
		D request;
		request.setMethodDefaults(GeneratorRegistry::builtins().idOf("gauntlet"));
		request.seed = 22001;
		const auto &definition = GeneratorRegistry::builtins().at(request.method);
		// Exercise both opponents sharing two fronts, the usual four-colony arena,
		// and the denser circuit at both supported sizes.
		for (auto [dims, teams] : {std::pair{8, 2}, std::pair{8, 4}, std::pair{8, 8},
								  std::pair{9, 8}, std::pair{9, 13}, std::pair{9, 16}})
		{
			D sized = request;
			sized.wDec = sized.hDec = dims;
			sized.nbTeams = teams;
			Game world(nullptr);
			const auto result = service.generate(world, sized);
			if (!result)
				std::cerr << "Gauntlet " << (1 << dims) << "x" << (1 << dims) << ", "
						  << teams << " colonies: " << result.diagnostic() << std::endl;
			REQUIRE((result && world.teamsCount() == teams));
			GenerationContext probe(sized);
			REQUIRE(definition.validateWorld(world, probe).empty());
		}
		for (auto [width, height, teams] : {std::tuple{7, 7, 4}, std::tuple{9, 7, 4},
										   std::tuple{8, 8, 1}, std::tuple{9, 9, 17}})
		{
			D invalid = request;
			invalid.wDec = width;
			invalid.hDec = height;
			invalid.nbTeams = teams;
			Game untouched(nullptr);
			REQUIRE(!validateGenerationRequest(invalid, definition).empty());
			REQUIRE(service.generate(untouched, invalid).error == GenerationError::InvalidRequest);
			REQUIRE(untouched.teamsCount() == 0);
		}
		// Test the final-world validator, not just generator success or a golden hash.
		for (int corruption = 0; corruption < 3; ++corruption)
		{
			Game world(nullptr);
			const auto result = service.generate(world, request, true);
			REQUIRE(result);
			GenerationContext probe(request);
			REQUIRE(definition.validateWorld(world, probe).empty());
			int changed = 0;
			for (int y = 0; y < world.map.getH(); ++y)
				for (int x = 0; x < world.map.getW(); ++x)
				{
					const int type = world.map.getResource(x, y).type;
					// Retain cherries and prunes: total fruit alone cannot prove
					// that a court supplies all three upgrade ingredients.
					if ((corruption == 0 && type == STONE) ||
						(corruption == 1 && type == ORANGE))
					{
						world.map.setNoResource(x, y, 1);
						++changed;
					}
				}
			if (corruption == 2)
			{
				// Close the circular home/court boundary, regardless of the seed's
				// rotation. This plugs every door without removing any structural wall.
				double outer = -1;
				for (const auto &record : result.telemetry.records())
					if (record.key == "gauntlet.arena.outer-radius")
						outer = std::get<double>(record.value);
				REQUIRE(outer > 0);
				for (int y = 0; y < world.map.getH(); ++y)
					for (int x = 0; x < world.map.getW(); ++x)
						if (std::abs(std::hypot(x - world.map.getW() / 2.,
											   y - world.map.getH() / 2.) - outer) < 1.5 &&
							world.map.getResource(x, y).type != STONE)
						{
							world.map.setResource(x, y, STONE, 1);
							++changed;
						}
			}
			REQUIRE(changed > 0);
			const auto error = definition.validateWorld(world, probe);
			REQUIRE(!error.empty());
			if (corruption == 1)
				REQUIRE(error.find("orchard") != std::string::npos);
			if (corruption == 2)
				REQUIRE(error.find("entrance") != std::string::npos);
		}
	}
	// scatterResources used to group algae candidates by land component, and land components
	// never label water, so no algae density ever placed a single tile. Algae is now shared out
	// between water bodies: a sea with an island, and a lake inside the island, must both get some.
	static void scatterAlgaeOnWater()
	{
		Game game(nullptr);
		game.map.setSize(6, 6);
		game.map.setGame(&game);
		game.map.makeHomogenMap(WATER);
		for (int y = 12; y < 52; ++y)
			for (int x = 12; x < 52; ++x)
				game.map.setUMTerrain(x, y, GRASS);
		for (int y = 24; y < 40; ++y)
			for (int x = 24; x < 40; ++x)
				game.map.setUMTerrain(x, y, WATER);
		game.map.controlSand();
		game.map.rebuildTerrain();
		D request;
		request.seed = 7;
		GenerationContext context(request);
		MapGeneration::scatterResources(game, context, {0, 0, 0, 50, 0});
		int sea = 0, lake = 0;
		for (int y = 0; y < game.map.getH(); ++y)
			for (int x = 0; x < game.map.getW(); ++x)
			{
				if (game.map.getResource(x, y).type != ALGA)
					continue;
				REQUIRE(game.map.isWater(x, y));
				(x >= 24 && x < 40 && y >= 24 && y < 40 ? lake : sea) += 1;
			}
		REQUIRE((sea > 0 && lake > 0));
		puts("PASS scatterResources places algae on water, in every water body");
	}
	static D DWithDefaults(int method)
	{
		D r;
		r.setMethodDefaults(method);
		return r;
	}
	static void globalsInit()
	{
		globalContainer->buildingsTypes.init();
		IntBuildingType::init();
		Race::loadDefault();
	}
	static void run(const char *output)
	{
		NewMapScreen s;
		s.beginExecution(globalContainer->gfx);
		CustomGameSetup lobby;
		// The catalog's order was shuffled once (FEEDBACK 2026-09-14: no bias towards the landscapes
		// that happen to be listed first). The editor opens on the catalog's first entry and the
		// lobby on its first playable one, whatever they are.
		D editorFirst, lobbyFirst;
		editorFirst.setMethodDefaults(GeneratorRegistry::builtins().methods().front());
		lobbyFirst.setMethodDefaults(GeneratorRegistry::builtins().methods(false).front());
		REQUIRE(GeneratorRegistry::builtins().methods(false).front() ==
			   GeneratorRegistry::builtins().idOf("fingerprint"));
		REQUIRE(s.descriptor.method == GeneratorRegistry::builtins().methods().front());
		sameControls(s.descriptor, editorFirst);
		sameControls(lobby.generator, lobbyFirst);
		for (int m : GeneratorRegistry::builtins().methods())
		{
			auto method = static_cast<D::Method>(m);
			D expected;
			expected.setMethodDefaults(method);
			select(s, method);
			lobby.generatorHistory.select(lobby.generator, method);
			sameControls(s.descriptor, expected);
			sameControls(lobby.generator, expected);
			s.onTimer(1);
			sameControls(s.descriptor, expected);
			std::set<std::string> labels;
			for (const auto &c : D::controls(method))
			{
				REQUIRE(labels.insert(c.label).second);
				REQUIRE((c.step > 0 && c.maximum >= c.minimum));
				REQUIRE((!c.allowedValues.empty() || (c.maximum - c.minimum) % c.step == 0));
				REQUIRE(c.get(expected) == c.defaultValue);
				REQUIRE(c.normalize(c.defaultValue) == c.defaultValue);
				REQUIRE(c.normalize(c.minimum - 100) == c.minimum);
				REQUIRE(c.normalize(c.maximum + 100) == c.maximum);
				// The actual editor must expose every value, including 75 grass and 65 size.
				for (int v : c.values())
					edit(s, c.label, v);
				edit(s, c.label, c.defaultValue);
			}
			sameControls(s.descriptor, expected);
			if (m <= D::eOLDISLANDS)
			{
				auto encoded = toLegacyDescriptor(s.descriptor);
				MapGenerationDescriptor decoded;
				REQUIRE(decoded.setData(encoded.getData(), encoded.getDataLength()));
				sameControls(fromLegacyDescriptor(decoded, 0), expected);
			}
			if (output &&
				((m >= 4 && m <= 8) || m == GeneratorRegistry::builtins().idOf("fjord-continent")))
			{
				s.paintFrame(0);
				std::string path = std::string(output) + "/editor-" + std::to_string(m) + ".png";
				REQUIRE(IMG_SavePNG(s.gfx->getSDLSurface(), path.c_str()) == 0);
			}
		}
		// Every switch is a check button in the editor, and clicking one flips the request's value.
		for (int m : GeneratorRegistry::builtins().methods())
		{
			select(s, m);
// Every toggle the method shows flips through the screen's own edit path and back.
			for (const auto &c : s.registry.at(m).controls)
			{
				if (!c.isToggle())
					continue;
				const int before = c.get(s.descriptor);
				edit(s, c.label, 1 - before);
				REQUIRE(c.get(s.descriptor) == 1 - before);
				edit(s, c.label, before);
				REQUIRE(c.get(s.descriptor) == before);
			}
		}
		select(s, D::eRIVER);
		edit(s, "Water weight", 37);
		edit(s, "Width", 8);
		edit(s, "Colonies", 6);
		lobby.generatorHistory.select(lobby.generator, D::eRIVER);
		lobby.generator.options["water"] = 37;
		lobby.generator.wDec = 8;
		lobby.generator.nbTeams = 6;
		for (auto method : {D::eISLANDS, D::eISLES, D::eRIVER})
		{
			select(s, method);
			lobby.generatorHistory.select(lobby.generator, method);
			sameControls(s.descriptor, lobby.generator);
			REQUIRE((s.descriptor.wDec == 8 && s.descriptor.nbTeams == 6));
		}
		REQUIRE(s.descriptor.options["water"] == 37);
		lobby.random = true;
		lobby.generator.options["water"] = lobby.generator.options["grass"] =
			lobby.generator.options["sand"] = lobby.generator.options["desert"] = 0;
		REQUIRE(!lobby.validation().empty());
		lobby.generator.options["grass"] = 75;
		REQUIRE(lobby.validation().empty());
		puts("PASS shared presets, ranges and steps; all editor values; lobby/editor mode memory; "
			 "serialization and validation");
	}
};
namespace
{
// Globals, a dummy-driver graphic context and the menu fonts the editor screen draws with.
struct DefaultsFixture
{
	glob2test::HeadlessGlobals globals{{.loadStrings = true}};
	DefaultsFixture()
	{
		globalContainer->gfx = GAGCore::Toolkit::initGraphic(640, 480, 0, "Map defaults test", "");
		GAGCore::Toolkit::loadFont("data/fonts/sans.ttf", 20, "menu");
		GAGCore::Toolkit::loadFont("data/fonts/sans.ttf", 13, "standard");
	}
};
}

// Keep expensive registry checks separate: each case gets its own timeout,
// diagnostic log, and coverage profile. No generator contract is dropped.
TEST_SUITE("MapGeneratorDefaults")
{
	TEST_CASE("toolkit geometry; raster; resource and home contracts")
	{
		DefaultsFixture fixture;
		ToolkitChecks::toolkitChecks();
		puts("PASS toolkit-only geometry, raster, resource and home contracts");
	}
	TEST_CASE("Bastion Keys contracts")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		GeneratorContracts::bastionKeysContracts();
	}
	TEST_CASE("Who Ate the Map contracts")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		GeneratorContracts::eatenMapContracts();
	}
	TEST_CASE("Drowned Forest contracts [slow]")
	{
		DefaultsFixture fixture;
		GeneratorContracts::drownedForestContracts();
	}
	TEST_CASE("Gauntlet shapes; request rejection and final-world corruption checks")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		MapGeneratorDefaultsTest::gauntletContracts();
		puts("PASS Gauntlet supported shapes, request rejection and final-world corruption checks");
	}
	TEST_CASE("Hungry Marches contracts")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		GeneratorContracts::hungryMarchesContracts();
	}
	TEST_CASE("Faulted City contracts")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		GeneratorContracts::faultedCityContracts();
	}
	TEST_CASE("Portage Lakes contracts [slow]")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		GeneratorContracts::portageLakesContracts();
	}
	TEST_CASE("Last Treeline checks")
	{
		DefaultsFixture fixture;
		LastTreelineChecks::run();
	}
	TEST_CASE("Last Treeline profile [benchmark]")
	{
		DefaultsFixture fixture;
		LastTreelineChecks::profile();
	}
	TEST_CASE("editor and lobby defaults contract [slow][map-generators]")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::run(nullptr);
	}
}

template<int Index> struct RegistryEntry { static constexpr int index = Index; };
DOCTEST_TYPE_TO_STRING_AS("fingerprint", RegistryEntry<0>);
DOCTEST_TYPE_TO_STRING_AS("oldTown", RegistryEntry<1>);
DOCTEST_TYPE_TO_STRING_AS("symmetricArena", RegistryEntry<2>);
DOCTEST_TYPE_TO_STRING_AS("swamp", RegistryEntry<3>);
DOCTEST_TYPE_TO_STRING_AS("tidalFlats", RegistryEntry<4>);
DOCTEST_TYPE_TO_STRING_AS("river", RegistryEntry<5>);
DOCTEST_TYPE_TO_STRING_AS("isles", RegistryEntry<6>);
DOCTEST_TYPE_TO_STRING_AS("ringWorld", RegistryEntry<7>);
DOCTEST_TYPE_TO_STRING_AS("amphitheatre", RegistryEntry<8>);
DOCTEST_TYPE_TO_STRING_AS("shatteredCoast", RegistryEntry<9>);
DOCTEST_TYPE_TO_STRING_AS("craterLakes", RegistryEntry<10>);
DOCTEST_TYPE_TO_STRING_AS("fjordContinent", RegistryEntry<11>);
DOCTEST_TYPE_TO_STRING_AS("spiderWeb", RegistryEntry<12>);
DOCTEST_TYPE_TO_STRING_AS("concreteIslands", RegistryEntry<13>);
DOCTEST_TYPE_TO_STRING_AS("watershed", RegistryEntry<14>);
DOCTEST_TYPE_TO_STRING_AS("maze", RegistryEntry<15>);
DOCTEST_TYPE_TO_STRING_AS("islands", RegistryEntry<16>);
DOCTEST_TYPE_TO_STRING_AS("stoneHighlands", RegistryEntry<17>);
DOCTEST_TYPE_TO_STRING_AS("switchbacks", RegistryEntry<18>);
DOCTEST_TYPE_TO_STRING_AS("cityStates", RegistryEntry<19>);
DOCTEST_TYPE_TO_STRING_AS("canals", RegistryEntry<20>);
DOCTEST_TYPE_TO_STRING_AS("sierpinskiGardens", RegistryEntry<21>);
DOCTEST_TYPE_TO_STRING_AS("hilbertRiver", RegistryEntry<22>);
DOCTEST_TYPE_TO_STRING_AS("lavaShield", RegistryEntry<23>);
DOCTEST_TYPE_TO_STRING_AS("honeycombIsle", RegistryEntry<24>);
DOCTEST_TYPE_TO_STRING_AS("karstTowers", RegistryEntry<25>);
DOCTEST_TYPE_TO_STRING_AS("bajada", RegistryEntry<26>);
DOCTEST_TYPE_TO_STRING_AS("centralQuarry", RegistryEntry<27>);
DOCTEST_TYPE_TO_STRING_AS("hiddenOasis", RegistryEntry<28>);
DOCTEST_TYPE_TO_STRING_AS("drownedForest", RegistryEntry<29>);
DOCTEST_TYPE_TO_STRING_AS("portageLakes", RegistryEntry<30>);
DOCTEST_TYPE_TO_STRING_AS("orchardCommons", RegistryEntry<31>);
DOCTEST_TYPE_TO_STRING_AS("lastTreeline", RegistryEntry<32>);
DOCTEST_TYPE_TO_STRING_AS("gauntlet", RegistryEntry<33>);
DOCTEST_TYPE_TO_STRING_AS("faultedCity", RegistryEntry<34>);
DOCTEST_TYPE_TO_STRING_AS("comb", RegistryEntry<35>);
DOCTEST_TYPE_TO_STRING_AS("encircledKingdom", RegistryEntry<36>);
DOCTEST_TYPE_TO_STRING_AS("bastionKeys", RegistryEntry<37>);
DOCTEST_TYPE_TO_STRING_AS("evenGround", RegistryEntry<38>);
DOCTEST_TYPE_TO_STRING_AS("marchland", RegistryEntry<39>);
DOCTEST_TYPE_TO_STRING_AS("whoAteTheMap", RegistryEntry<40>);
DOCTEST_TYPE_TO_STRING_AS("ruggedArchipelago", RegistryEntry<41>);
DOCTEST_TYPE_TO_STRING_AS("hungryMarches", RegistryEntry<42>);
DOCTEST_TYPE_TO_STRING_AS("contestedCommons", RegistryEntry<43>);
DOCTEST_TYPE_TO_STRING_AS("rainShadow", RegistryEntry<44>);
DOCTEST_TYPE_TO_STRING_AS("everglades", RegistryEntry<45>);
DOCTEST_TYPE_TO_STRING_AS("polder", RegistryEntry<46>);
DOCTEST_TYPE_TO_STRING_AS("carousel", RegistryEntry<47>);
DOCTEST_TYPE_TO_STRING_AS("oldGrowth", RegistryEntry<48>);
DOCTEST_TYPE_TO_STRING_AS("anthill", RegistryEntry<49>);
DOCTEST_TYPE_TO_STRING_AS("coral", RegistryEntry<50>);
DOCTEST_TYPE_TO_STRING_AS("emoji", RegistryEntry<51>);
DOCTEST_TYPE_TO_STRING_AS("forts", RegistryEntry<52>);
DOCTEST_TYPE_TO_STRING_AS("braidedDelta", RegistryEntry<53>);
DOCTEST_TYPE_TO_STRING_AS("breachableHighlands", RegistryEntry<54>);
DOCTEST_TYPE_TO_STRING_AS("hedgerowCountry", RegistryEntry<55>);
DOCTEST_TYPE_TO_STRING_AS("glacis", RegistryEntry<56>);
DOCTEST_TYPE_TO_STRING_AS("allotments", RegistryEntry<57>);
DOCTEST_TYPE_TO_STRING_AS("caravanserai", RegistryEntry<58>);
DOCTEST_TYPE_TO_STRING_AS("braidedRiver", RegistryEntry<59>);
DOCTEST_TYPE_TO_STRING_AS("drumlinField", RegistryEntry<60>);
DOCTEST_TYPE_TO_STRING_AS("continents", RegistryEntry<61>);
DOCTEST_TYPE_TO_STRING_AS("savannah", RegistryEntry<62>);
DOCTEST_TYPE_TO_STRING_AS("hills", RegistryEntry<63>);
DOCTEST_TYPE_TO_STRING_AS("riceTerraces", RegistryEntry<64>);
DOCTEST_TYPE_TO_STRING_AS("locust", RegistryEntry<65>);
DOCTEST_TYPE_TO_STRING_AS("plantations", RegistryEntry<66>);
DOCTEST_TYPE_TO_STRING_AS("uniform", RegistryEntry<67>);

TEST_CASE_TEMPLATE("default generation repeatability rectangles and rejection [slow][map-generators]", Entry,
    RegistryEntry<0>,
    RegistryEntry<1>,
    RegistryEntry<2>,
    RegistryEntry<3>,
    RegistryEntry<4>,
    RegistryEntry<5>,
    RegistryEntry<6>,
    RegistryEntry<7>,
    RegistryEntry<8>,
    RegistryEntry<9>,
    RegistryEntry<10>,
    RegistryEntry<11>,
    RegistryEntry<12>,
    RegistryEntry<13>,
    RegistryEntry<14>,
    RegistryEntry<15>,
    RegistryEntry<16>,
    RegistryEntry<17>,
    RegistryEntry<18>,
    RegistryEntry<19>,
    RegistryEntry<20>,
    RegistryEntry<21>,
    RegistryEntry<22>,
    RegistryEntry<23>,
    RegistryEntry<24>,
    RegistryEntry<25>,
    RegistryEntry<26>,
    RegistryEntry<27>,
    RegistryEntry<28>,
    RegistryEntry<29>,
    RegistryEntry<30>,
    RegistryEntry<31>,
    RegistryEntry<32>,
    RegistryEntry<33>,
    RegistryEntry<34>,
    RegistryEntry<35>,
    RegistryEntry<36>,
    RegistryEntry<37>,
    RegistryEntry<38>,
    RegistryEntry<39>,
    RegistryEntry<40>,
    RegistryEntry<41>,
    RegistryEntry<42>,
    RegistryEntry<43>,
    RegistryEntry<44>,
    RegistryEntry<45>,
    RegistryEntry<46>,
    RegistryEntry<47>,
    RegistryEntry<48>,
    RegistryEntry<49>,
    RegistryEntry<50>,
    RegistryEntry<51>,
    RegistryEntry<52>,
    RegistryEntry<53>,
    RegistryEntry<54>,
    RegistryEntry<55>,
    RegistryEntry<56>,
    RegistryEntry<57>,
    RegistryEntry<58>,
    RegistryEntry<59>,
    RegistryEntry<60>,
    RegistryEntry<61>,
    RegistryEntry<62>,
    RegistryEntry<63>,
    RegistryEntry<64>,
    RegistryEntry<65>,
    RegistryEntry<66>,
    RegistryEntry<67>)
{
    DefaultsFixture fixture;
    const auto methods = GeneratorRegistry::builtins().methods();
    REQUIRE(methods.size() == 68);
    MapGeneratorDefaultsTest::defaultGenerationContract(methods[Entry::index]);
}

TEST_SUITE("MapGeneratorRegistry")
{
    TEST_CASE("catalog generation contracts [slow][map-generators]")
    {
        DefaultsFixture fixture;
        MapGeneratorDefaultsTest::globalsInit();
        GeneratorContracts::generatorContracts();
    }
    TEST_CASE("framework contracts [slow][map-generators]")
    {
        DefaultsFixture fixture;
        MapGeneratorDefaultsTest::globalsInit();
        frameworkChecks();
    }
    TEST_CASE("landscape contracts [slow][map-generators]")
    {
        DefaultsFixture fixture;
        MapGeneratorDefaultsTest::globalsInit();
        LandscapeChecks::landscapeChecks();
    }
    TEST_CASE("stress rejection and registration contracts [slow][map-generators]")
    {
        DefaultsFixture fixture;
        MapGeneratorDefaultsTest::globalsInit();
        MapGeneratorDefaultsTest::generationContracts();
    }
    TEST_CASE("water algae distribution [slow][map-generators]")
    {
        DefaultsFixture fixture;
        MapGeneratorDefaultsTest::globalsInit();
        MapGeneratorDefaultsTest::scatterAlgaeOnWater();
    }
}
