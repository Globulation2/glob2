#include <nlohmann/json.hpp>
#include "StartingLayout.h"
#include "ClearingLandscape.h"
#include "Homes.h"
#include "Orbits.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "CustomGameSetup.h"
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <BinaryStream.h>
#include <GzipUtil.h>
#include <StreamBackend.h>
#include <stdexcept>
#include <cstdlib>
#include <cstdint>
#include "Contact.h"
#include "Channels.h"
#include "Walls.h"
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
#include "MazeGenerator.h"
#include "CanalsGenerator.h"
#include "HoneycombIsleGenerator.h"
#include "FingerprintGenerator.h"
#include "CaravanseraiGenerator.h"
#include "Race.h"
#include "Resources.h"
#include "Sketch.h"
#include "StartingPositions.h"
#include "TerrainResourceProperties.h"
#include "Unit.h"
#include "Utilities.h"
#include <SDL3_image/SDL_image.h>
#include <Toolkit.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <map>
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
		// The drawn terrain look follows the request seed, not the sync stream.
		REQUIRE(first.map.terrainSeed() != 0);
		REQUIRE(first.map.terrainSeed() == repeat.map.terrainSeed());
		REQUIRE(first.map.terrainSeed() != other.map.terrainSeed());
		REQUIRE((request.seed == 22001 && request.options == DWithDefaults(method).options));
		if (method == GeneratorRegistry::builtins().idOf("lava-shield"))
		{
			if (!a)
				std::cerr << a.diagnostic() << std::endl;
			REQUIRE(a); // A repeatable failure is not a valid default map.
			GenerationContext probe(request);
			const auto &definition = GeneratorRegistry::builtins().at(method);
			REQUIRE(definition.validateWorld(first, probe).empty());
			// The default map is built from catalogue lava, and cooling the lava must be
			// caught by the generator's actual final-world validator, not merely by a golden
			// hash. Corrupt the unused repeated copy.
			int lava = 0;
			for (int y = 0; y < repeat.map.getH(); ++y)
				for (int x = 0; x < repeat.map.getW(); ++x)
				{
					REQUIRE(repeat.map.vertexTerrainAt(x, y) != VOID_HOLE);
					if (repeat.map.vertexTerrainAt(x, y) == LAVA ||
						repeat.map.vertexTerrainAt(x, y) == EMBER_FIELD)
						++lava;
				}
			REQUIRE(lava > 0);
			for (int y = 0; y < repeat.map.getH(); ++y)
				for (int x = 0; x < repeat.map.getW(); ++x)
					if (repeat.map.vertexTerrainAt(x, y) == LAVA ||
						repeat.map.vertexTerrainAt(x, y) == EMBER_FIELD)
						repeat.map.setVertexTerrain(x, y, GRASS);
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
				const auto generated = service.generate(world, rectangular);
				INFO(generated.diagnostic());
				REQUIRE(generated);
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
			 {GeneratorControl{"test-elevation", "Smoothing", 2, 10, 2, 6}.withSearchRange(4, 8),
			  GeneratorControl{"test-cell",
							   "Island size",
							   4,
							   16,
							   1,
							   8,
							   ControlGroup::Layout,
							   false,
							   false,
							   {4, 8, 16}}
				  .withSearchValues({8, 16}),
			  GeneratorControl{"test-gap", "Channel width", 1, 5, 2, 3, ControlGroup::Layout}
				  .withSearchRange(3, 5),
			  GeneratorControl::choice("test-shape", "Cell shape", {"Squares", "Hexagons"}, 0)
				  .withSearchValues({0, 1}),
			  GeneratorControl::toggle("test-switch", "Lake connects to fjords", false)
				  .withSearchValues({1})},
			 [](Game &game, GenerationContext &context)
			 {
				 const int elevation = context.request.option("test-elevation");
				 REQUIRE(elevation == 8);
				 REQUIRE(context.request.option("test-switch") == 1);
				 game.map.fillTerrain(GRASS);
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
			const auto on = GeneratorControl::toggle("switch", "Lake connects to fjords", true)
								.withSearchValues({0, 1});
			REQUIRE((on.isToggle() && on.defaultValue == 1 &&
				   on.values() == std::vector<int>({0, 1})));
			REQUIRE(!rejected(on));
			const auto amount = GeneratorControl::percentage("amount", "Fruit");
			REQUIRE((!amount.isToggle() && amount.defaultValue == 100 && !rejected(amount)));
			// A choice stores the index of its named option and is shown by name.
			const auto shape =
				GeneratorControl::choice("shape", "Cell shape", {"Squares", "Hexagons"}, 1)
					.withSearchValues({0, 1});
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
							world.map.setResourceByIndex(x, y, STONE, 1);
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
		game.map.fillTerrain(WATER);
		{
			auto batch = game.map.editTerrain();
			for (int y = 12; y < 52; ++y)
				for (int x = 12; x < 52; ++x)
					game.map.setVertexTerrain(x, y, GRASS);
			for (int y = 24; y < 40; ++y)
				for (int x = 24; x < 40; ++x)
					game.map.setVertexTerrain(x, y, WATER);
		}
		game.map.layBeaches();
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
		// Fresh editor and lobby setups use Lava Shield regardless of catalog order.
		D expectedDefault;
		expectedDefault.setMethodDefaults(GeneratorRegistry::builtins().idOf("lava-shield"));
		sameControls(s.descriptor, expectedDefault);
		sameControls(lobby.generator, expectedDefault);
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
				REQUIRE(IMG_SavePNG(s.gfx->getSDLSurface(), path.c_str()));
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
    TEST_CASE("starting layouts follow rectangular and overlay catalog footprints")
    {
        glob2test::HeadlessGlobals globals;
        for (const auto dimensions : {std::pair{1,2},std::pair{7,3},std::pair{2,9},std::pair{4,4}})
        for (bool overlay : {false,true})
        {
            CAPTURE(dimensions.first); CAPTURE(dimensions.second); CAPTURE(overlay);
            glob2test::HeadlessGame world({.wDec=6,.hDec=6,.terrain=WATER,.teams=0,.loadDefaultRace=true});
            auto& game=world.game;
            auto snapshot=nlohmann::json::parse(game.buildingsTypes.snapshotJson());
            const int id=game.buildingsTypes.getStartingBuildingTypeNum();
            auto& type=snapshot["variants"][id];
            type["properties"]["width"]=dimensions.first;
            type["properties"]["height"]=dimensions.second;
            type["semantics"]["occupiesGround"]=!overlay;
            game.buildingsTypes.loadSnapshotJson(snapshot.dump()); game.configureBuildingCatalog();
            GenerationRequest request; request.nbTeams=1; request.nbWorkers=6;
            GenerationContext context(request); context.bootX[0]=61; context.bootY[0]=2;
            REQUIRE(MapGeneration::placeStarts(game,context));
            CHECK(game.teams[0]->myBuildings[0]->typeNum==id);
            const MapGeneration::StartingLayout layout(dimensions.first,dimensions.second,request.nbWorkers);
            for (int i=0; i<request.nbWorkers; ++i)
            {
                const auto* worker=game.teams[0]->myUnits[i]; REQUIRE(worker);
                CHECK(worker->posX==game.map.normalizeX(61+layout.workerX(i)));
                CHECK(worker->posY==game.map.normalizeY(2+layout.workerY(i)));
                CHECK(game.map.getGroundUnit(worker->posX,worker->posY)==worker->gid);
                CHECK(game.map.getBuilding(worker->posX,worker->posY)==NOGBID);
            }
        }
    }

	TEST_CASE("search domains preserve legal values and constrain deterministic rolls")
	{
		for (int method : GeneratorRegistry::builtins().methods())
			for (const auto &c : D::controls(method))
			{
				REQUIRE(c.validSearchDomain());
				const auto legal = c.values();
				for (int value : c.searchValues())
					REQUIRE(std::binary_search(legal.begin(), legal.end(), value));
			}
		const auto accepts = [](GeneratorControl c)
		{
			GeneratorDefinition d{"test-search",
								  101,
								  "uniform terrain",
								  1,
								  false,
								  {std::move(c)},
								  [](Game &, GenerationContext &) { return false; }};
			d.tags = {"terrain:novelty"};
			try
			{
				GeneratorRegistry registry({d});
				return true;
			}
			catch (const std::invalid_argument &)
			{
				return false;
			}
		};
		const GeneratorControl numeric{"amount", "Amount", 0, 100, 25, 50};
		REQUIRE(!accepts(numeric));
		REQUIRE(accepts(numeric.withSearchRange(25, 75)));
		REQUIRE(!accepts(numeric.withSearchRange(20, 75)));
		REQUIRE(!accepts(numeric.withSearchRange(75, 25)));
		REQUIRE(!accepts(numeric.withSearchRange(-25, 75)));
		REQUIRE(!accepts(numeric.withSearchValues({})));
		REQUIRE(!accepts(numeric.withSearchValues({25, 25})));
		REQUIRE(!accepts(numeric.withSearchValues({75, 25})));
		REQUIRE(!accepts(numeric.withSearchValues({20, 75})));
		REQUIRE(!accepts(numeric.withSearchRange(25, 75).withSearchValues({50})));
		const auto toggle = GeneratorControl::toggle("switch", "Switch", true);
		REQUIRE(accepts(toggle.withSearchValues({1})));
		REQUIRE(!accepts(toggle.withSearchRange(0, 1)));
		const auto choice = GeneratorControl::choice("variant", "Variant", {"A", "B", "Random"}, 2);
		REQUIRE(accepts(choice.withSearchValues({0, 1})));
		REQUIRE(!accepts(choice.withSearchRange(0, 1)));
		D base;
		base.setMethodDefaults(GeneratorRegistry::builtins().idOf("coral"));
		base.wDec = 8;
		base.hDec = 7;
		base.nbTeams = 3;
		base.nbWorkers = 5;
		base.options["sand-roads"] = 0; // Still legal and manually selectable.
		REQUIRE(
			validateGenerationRequest(base, GeneratorRegistry::builtins().at(base.method)).empty());
		for (unsigned seed = 1; seed <= 64; ++seed)
		{
			D a = base, b = base;
			REQUIRE(a.randomizeControls(seed));
			REQUIRE(b.randomizeControls(seed));
			REQUIRE(a.options == b.options);
			REQUIRE(a.option("sand-roads") == 1);
			REQUIRE((a.wDec == 8 && a.hDec == 7 && a.nbTeams == 3 && a.nbWorkers == 5));
			for (const auto &c : D::controls(a.method))
			{
				const auto domain = c.searchValues();
				REQUIRE(std::binary_search(domain.begin(), domain.end(), c.get(a)));
			}
		}
		D failed = base;
		REQUIRE(!failed.randomizeControls(1, 0));
		REQUIRE(failed.options == base.options);
		failed.setMethodDefaults(GeneratorRegistry::builtins().idOf("maze"));
		failed.wDec = failed.hDec = 5;
		const D impossible = failed;
		REQUIRE(!failed.randomizeControls(42));
		REQUIRE(failed.options == impossible.options);
	}
	TEST_CASE("Design defaults and explicit Random resolve reproducibly without changing enums")
	{
		for (const std::string id :
			 {"maze", "canals", "honeycomb-isle", "fingerprint", "caravanserai"})
		{
			D r;
			r.setMethodDefaults(GeneratorRegistry::builtins().idOf(id));
			std::set<int> variants;
			const auto resolved = [&](const D &request)
			{
				if (id == "maze")
					return MazeOptions(request).cellShape;
				if (id == "canals")
					return CanalsOptions(request).blockShape;
				if (id == "honeycomb-isle")
					return HoneycombIsleOptions(request).blockShape;
				if (id == "fingerprint")
					return FingerprintOptions(request).pattern;
				return CaravanseraiOptions(request).desert;
			};
			const std::string key = id == "maze"           ? "cell-shape"
									: id == "fingerprint"  ? "pattern"
									: id == "caravanserai" ? "desert"
														   : "block-shape";
			const int count = id == "fingerprint" || id == "caravanserai" ? 3 : 2;
			REQUIRE(r.option(key) == (id == "honeycomb-isle" ? 1 : count));
			if (id == "honeycomb-isle")
			{
				for (unsigned seed = 1; seed <= 32; ++seed)
				{
					r.seed = seed;
					REQUIRE(resolved(r) == 1);
				}
				r.options[key] = count; // Random remains an explicit choice.
			}
			for (unsigned seed = 1; seed <= 32; ++seed)
			{
				r.seed = seed;
				REQUIRE(resolved(r) == resolved(r));
				variants.insert(resolved(r));
			}
			const auto domain = GenerationRequest::control(r.method, key).searchValues();
			REQUIRE(variants == std::set<int>(domain.begin(), domain.end()));
			for (int variant = 0; variant < count; ++variant)
			{
				r.options[key] = variant;
				REQUIRE(resolved(r) == variant);
			}
		}
		D fingerprint;
		fingerprint.setMethodDefaults(GeneratorRegistry::builtins().idOf("fingerprint"));
		REQUIRE(fingerprint.option("barrier") == 2);
		std::set<int> barriers;
		for (unsigned seed = 1; seed <= 32; ++seed)
		{
			fingerprint.seed = seed;
			const int value = FingerprintOptions(fingerprint).barrier;
			REQUIRE(value == FingerprintOptions(fingerprint).barrier);
			barriers.insert(value);
		}
		REQUIRE(barriers == std::set<int>({0, 1}));
		for (int value : {0, 1})
		{
			fingerprint.options["barrier"] = value;
			REQUIRE(FingerprintOptions(fingerprint).barrier == value);
		}
		D small;
		small.setMethodDefaults(GeneratorRegistry::builtins().idOf("maze"));
		small.wDec = small.hDec = 6;
		small.nbTeams = 1;
		// Hexagons cannot fit here, but Random retains the supported square layout.
		for (unsigned seed = 1; seed <= 8; ++seed)
		{
			small.seed = seed;
			REQUIRE(MazeOptions(small).cellShape == 0);
		}
	}
	TEST_CASE("resource habitat distinguishes terrain permission from growth and occupancy")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world;
		auto& map = world.game.map;
		// Tiles two apart share no corners, so each keeps its own terrain.
		MapGeneration::paintTile(map, 8, 8, TRAIL);
		MapGeneration::paintTile(map, 10, 8, ICE);
		MapGeneration::paintTile(map, 12, 8, WATER);
		CHECK(map.terrainPropertiesAt(8, 8).buildable);
		CHECK_FALSE(map.terrainSupportsResourceAtByIndex(8, 8, WHEAT));
		CHECK_FALSE(map.terrainSupportsResourceAtByIndex(10, 8, STONE));
		CHECK(map.terrainSupportsResourceAtByIndex(12, 8, ALGA));
		CHECK_FALSE(map.terrainSupportsResourceAtByIndex(12, 8, WHEAT));
		// The transition between trail and grass grows nothing.
		CHECK_FALSE(map.terrainSupportsResourceAtByIndex(7, 8, WHEAT));
		map.setResourceByIndex(5, 8, WHEAT, 1);
		CHECK(map.terrainSupportsResourceAtByIndex(5, 8, WOOD));
		CHECK_FALSE(map.terrainSupportsResourceAtByIndex(5, 8, -1));
		CHECK_FALSE(map.terrainSupportsResourceAtByIndex(5, 8, 99));
		auto custom = terrainProperties(GRASS);
		custom.resourcesGrow = false;
		CHECK(terrainSupportsResource(custom, WHEAT, true));
		custom.nonGrowingResources = false;
		CHECK_FALSE(terrainSupportsResource(custom, STONE, false));
		CHECK(terrainSupportsResource(custom, WOOD, true));
		custom.allowedResources &= ~(1u << WOOD);
		CHECK_FALSE(terrainSupportsResource(custom, WOOD, true));
	}
    TEST_CASE("shared route and wall contracts use terrain capabilities")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world;
        auto& map = world.game.map;
        const MapGeneration::Torus torus(map);
        // Tiles two apart share no corners, so each keeps its own terrain.
        MapGeneration::paintTile(map,8,8,TRAIL);
        MapGeneration::paintTile(map,10,8,ICE);
        MapGeneration::paintTile(map,12,8,WATER);
        MapGeneration::paintTile(map,14,8,SAND);
        const auto open = MapGeneration::walkableTiles(map);
        const auto beach = MapGeneration::beachTiles(map,torus);
        CHECK(open[torus.at(8,8)]);
        CHECK(open[torus.at(10,8)]);
        CHECK_FALSE(open[torus.at(12,8)]);
        // A water tile's mixed neighbours are walkable transitions.
        CHECK(open[torus.at(11,8)]);
        CHECK_FALSE(beach[torus.at(8,8)]);
        CHECK_FALSE(beach[torus.at(10,8)]);
        CHECK(beach[torus.at(14,8)]);
        CHECK_EQ(MapGeneration::stepCost(map,8,8,MapGeneration::StepCosts::walking()),1);
        CHECK_EQ(MapGeneration::stepCost(map,12,8,MapGeneration::StepCosts::walking()),-1);
        CHECK_EQ(MapGeneration::stepCost(map,12,8,MapGeneration::StepCosts::swimming(3)),3);
        std::vector<unsigned char> wall(torus.size(),0);
        wall[torus.at(6,8)]=wall[torus.at(8,8)]=wall[torus.at(10,8)]=1;
        const auto stone = MapGeneration::designedStone(map,torus,wall);
        CHECK(stone.stone[torus.at(6,8)]);
        CHECK_FALSE(stone.stone[torus.at(8,8)]);
        CHECK_FALSE(stone.stone[torus.at(10,8)]);
        CHECK_EQ(stone.gaps,2);
		const MapGeneration::SandFord ford{20, 20, 0, 1, 1, 0, 3, 1};
		MapGeneration::paintTile(map, 20, 20, ICE);
		MapGeneration::paintTile(map, 21, 20, TRAIL);
		CHECK(MapGeneration::fordWalkabilityFault(map, torus, ford).empty());
		MapGeneration::paintTile(map, 20, 20, WATER);
		CHECK_FALSE(MapGeneration::fordWalkabilityFault(map, torus, ford).empty());
    }
	TEST_CASE("toolkit geometry; raster; resource and home contracts")
	{
		DefaultsFixture fixture;
		ToolkitChecks::toolkitChecks();
		puts("PASS toolkit-only geometry, raster, resource and home contracts");
	}
	TEST_CASE("Random designs match explicit generated worlds on rectangles [slow]")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		GenerationService service;
		for (const std::string id :
			 {"maze", "canals", "honeycomb-isle", "fingerprint", "caravanserai"})
			for (const auto size : {std::pair{7, 8}, std::pair{8, 7}, std::pair{8, 8}})
				for (unsigned seed : {1u, 11u})
				{
					D random;
					random.setMethodDefaults(GeneratorRegistry::builtins().idOf(id));
					random.wDec = size.first;
					random.hDec = size.second;
					random.seed = seed;
					if (id == "honeycomb-isle")
						random.options["block-shape"] = 2;
					D concrete = random;
					if (id == "maze")
						concrete.options["cell-shape"] = MazeOptions(random).cellShape;
					if (id == "canals")
						concrete.options["block-shape"] = CanalsOptions(random).blockShape;
					if (id == "honeycomb-isle")
						concrete.options["block-shape"] = HoneycombIsleOptions(random).blockShape;
					if (id == "fingerprint")
					{
						const FingerprintOptions o(random);
						concrete.options["pattern"] = o.pattern;
						concrete.options["barrier"] = o.barrier;
					}
					if (id == "caravanserai")
						concrete.options["desert"] = CaravanseraiOptions(random).desert;
					Game a(nullptr), b(nullptr);
					const auto first = service.generate(a, random, true);
					const auto second = service.generate(b, concrete, true);
					REQUIRE_MESSAGE(bool(first) == bool(second), first.diagnostic());
					REQUIRE_MESSAGE(bool(first), first.diagnostic());
					REQUIRE(mapFingerprint(a) == mapFingerprint(b));
				}
	}
	TEST_CASE("Explicit designs preserve topology across the runtime-resource RNG epoch")
	{
#if (defined(__APPLE__) && defined(__aarch64__)) || (defined(__linux__) && defined(__x86_64__))
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		// Historical 256-square, four-colony, seed-1 rows before the appended Random values.
		// Fingerprint's historical world hash has build-specific existing goldens.
		// Unoptimized Clang 18 coverage preserves the same pre-Random world as macOS.
#if defined(__APPLE__) || (defined(__linux__) && defined(__x86_64__) && defined(__clang__) && \
						  __clang_major__ == 18 && defined(GLOB2_TEST_COVERAGE) && !defined(__OPTIMIZE__))
		constexpr std::uint64_t fingerprint = 15850274609968439542ULL;
#else
		constexpr std::uint64_t fingerprint = 5648058033288605271ULL;
#endif
        // Sprite selection ceased consuming simulation RNG in resource epoch1.
        // Current full references also include SIM40's map-owned placement
        // stock stream; the historical topology references stay unchanged.
        // Deposit stocks therefore need new full hashes; topology is checked
        // independently against maps produced by the archived pre-epoch engine.
        const char* baselineDirectory=std::getenv("GLOB2_RECORD_RESOURCE_DESIGN_GOLDENS");
#if defined(__APPLE__)
        const std::string platform="macos-arm64";
#elif defined(__clang__) && __clang_major__ == 18 && defined(GLOB2_TEST_COVERAGE) && !defined(__OPTIMIZE__)
        const std::string platform="linux-x86_64-clang18-coverage";
#else
        const std::string platform="linux-x86_64";
#endif
        nlohmann::json golden;
        const auto fixturePath=glob2test::sourceRoot()/"test/map-generator-resource-epoch.json";
        {
            std::ifstream fixture(fixturePath);
            REQUIRE_MESSAGE(fixture.good(),fixturePath.string());
            fixture>>golden;
        }
        REQUIRE(golden.at("resource_epoch")==1);
        nlohmann::json recorded=nlohmann::json::object();
        auto topologyHash=[](const Game& game) {
            std::uint64_t hash=14695981039346656037ull;
            auto add=[&](unsigned value) { hash=(hash^value)*1099511628211ull; };
            add(game.map.getW());add(game.map.getH());
            for(int y=0;y<game.map.getH();++y) for(int x=0;x<game.map.getW();++x) {
                add(game.map.vertexTerrainAt(x,y));
                const auto type=game.map.getResource(x,y).type;
                add(type==NO_RES_TYPE?255u:type);
            }
            add(game.teamsCount());
            for(int team=0;team<game.teamsCount();++team) {
                add(game.teams[team]->startPosX);add(game.teams[team]->startPosY);
            }
            return hash;
        };
		struct Original
		{
			const char *id, *key;
			int value;
			std::uint64_t hash;
		};
		// Honeycomb revision 4 intentionally removes the historical stone outlines;
		// its new layout is covered by its contracts and revisioned generator goldens.
		for (const auto original :
			 {Original{"maze", "cell-shape", 0, 6132248727033081699ULL},
			  Original{"fingerprint", "pattern", 0, fingerprint},
			  Original{"canals", "block-shape", 0, 16622643961647829752ULL},
			  Original{"caravanserai", "desert", 1, 6244602504248136250ULL}})
		{
			D request;
			request.setMethodDefaults(GeneratorRegistry::builtins().idOf(original.id));
			request.seed = 1;
			request.options[original.key] = original.value;
			if (request.method == 26)
				request.options["barrier"] = 0;
			Game game(nullptr);
			const auto result = GenerationService().generate(game, request);
			REQUIRE_MESSAGE(bool(result), result.diagnostic());
            const auto full=mapFingerprint(game),shape=topologyHash(game);
            // Retain observations even on platforms whose epoch-1 hashes have
            // not been independently accepted. The checks below still apply.
            recorded[original.id]={{"historical_full_reference",original.hash},
                {"topology",shape},{"full",full},
                {"accepted_platform_reference",golden.at("platforms").contains(platform)},
                {"portable_topology_reference",golden.at("portable_topology").contains(original.id)}};
            if(baselineDirectory) {
                const auto path=std::filesystem::path(baselineDirectory)/(std::string(original.id)+".map.gz");
                GAGCore::BinaryInputStream input(GAGCore::openInflatingFileStreamBackend(path.string()));
                Game historical(nullptr);
                REQUIRE_MESSAGE(historical.load(&input),path.string());
                REQUIRE_MESSAGE(mapFingerprint(historical)==original.hash,original.id);
                CHECK_MESSAGE(topologyHash(historical)==shape,original.id);
                recorded[original.id]={{"historical_full",original.hash},{"topology",topologyHash(historical)},
                    {"candidate_topology",shape},{"topology_preserved",topologyHash(historical)==shape},{"full",full}};
                std::cout<<"RESOURCE_DESIGN_EPOCH "<<platform<<' '<<original.id<<' '
                    <<recorded[original.id].dump()<<'\n';
            } else {
                if(golden.at("platforms").contains(platform)) {
                    const auto& row=golden.at("platforms").at(platform).at(original.id);
                    REQUIRE(row.at("historical_full").get<std::uint64_t>()==original.hash);
                    CHECK_MESSAGE(shape==row.at("topology").get<std::uint64_t>(),original.id);
                    CHECK_MESSAGE(full==row.at("full").get<std::uint64_t>(),original.id);
                } else {
                    // Four historical worlds were already identical across the
                    // supported platforms. Preserve their independently recorded
                    // topology checks while full epoch hashes await platform runs.
                    if(golden.at("portable_topology").contains(original.id))
                        CHECK_MESSAGE(shape==golden.at("portable_topology").at(original.id).get<std::uint64_t>(),original.id);
                    else
                        std::cout << "RESOURCE_DESIGN_UNVERIFIED portable-topology " << platform << ' ' << original.id << '\n';
                    std::cout << "RESOURCE_DESIGN_UNVERIFIED full-epoch " << platform << ' ' << original.id << '\n';
                }
            }
        }
        glob2test::writeFile(glob2test::artifactDir()/"resource-design-observations.json",
            nlohmann::json{{"resource_epoch",1},{"platform",platform},{"designs",recorded},
                {"acceptance","observations only; historical comparison required before accepting new hashes"}}.dump(2)+'\n');
        if(baselineDirectory) {
            std::ofstream output(std::filesystem::path(baselineDirectory)/("epoch1-"+platform+".json"));
            output<<nlohmann::json{{"resource_epoch",1},{"platform",platform},{"designs",recorded}}.dump(2)<<'\n';
            REQUIRE(output.good());
        }
#endif
    }
	TEST_CASE("Shattered Coast stocks expansion clumps with resource controls")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		for (int amount : {0, 100, 300})
		{
			D request;
			request.setMethodDefaults(GeneratorRegistry::builtins().idOf("shattered-coast"));
			request.seed = 7;
			for (const char *key : {"wheat-amount", "wood-amount", "stone-amount", "algae-amount"})
				request.options[key] = amount;
			Game world(nullptr);
			const auto result = GenerationService().generate(world, request, true);
			REQUIRE_MESSAGE(bool(result), result.diagnostic());
			for (int type : {WHEAT, WOOD, STONE, ALGA})
			{
				const std::string prefix = "shattered-coast.expansion." + std::to_string(type);
				int patches = -1, tiles = -1;
				for (const auto &record : result.telemetry.records())
				{
					if (record.key == prefix + ".clumps")
						patches = int(std::get<std::int64_t>(record.value));
					if (record.key == prefix + ".tiles")
						tiles = int(std::get<std::int64_t>(record.value));
				}
				CHECK((patches > 0) == (amount > 0));
				CHECK(tiles >= 8 * patches);
				if (!amount)
					CHECK(tiles == 0);
			}
			if (amount == 100)
				if (const char *baseline = std::getenv("GLOB2_SHATTERED_BASELINE_MAP"))
				{
					GAGCore::BinaryInputStream input(GAGCore::openInflatingFileStreamBackend(baseline));
					Game previous(nullptr);
					REQUIRE(previous.load(&input));
					REQUIRE(previous.map.getW() == world.map.getW());
					REQUIRE(previous.map.getH() == world.map.getH());
					const MapGeneration::Torus t(world.map);
					bool sameTerrain = true, retainedDeposits = true, sameHomes = true;
					int additions = 0;
					for (int i = 0; i < t.size(); ++i)
					{
						const int x = t.remainderX(i), y = i / t.w;
						sameTerrain &= previous.map.vertexTerrainAt(x, y) == world.map.vertexTerrainAt(x, y);
						const auto before = previous.map.getResource(i), after = world.map.getResource(i);
						retainedDeposits &= before.type == NO_RES_TYPE || before == after;
						if (before == after)
							continue;
						++additions;
						for (int team = 0; team < request.nbTeams; ++team)
						{
							const auto *home = previous.teams[team]->myBuildings[0];
							for (int dy = 0; dy < 4; ++dy)
								for (int dx = 0; dx < 4; ++dx)
									sameHomes &= t.chebyshev(x, y, home->posX + dx, home->posY + dy) > 32;
						}
					}
					CHECK(sameTerrain);
					CHECK(retainedDeposits);
					CHECK(sameHomes);
					CHECK(additions > 1000);
				}
			if (amount == 100)
			{
				for (int tick = 0; tick < 2048; ++tick)
					world.map.growResources();
				CHECK(MapGeneration::startingFloorFailure(world.map, request.nbTeams).empty());
			}
		}
	}
	TEST_CASE("Braided Delta uses coherent fields quarries and mixed orchards")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		const auto &definition = GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("braided-delta"));
		for (int variant = 0; variant < 4; ++variant)
		{
			D request;
			request.setMethodDefaults(definition.legacyId);
			request.seed = 7;
			if (variant == 3)
			{
				request.wDec = request.hDec = 7;
				request.options["rejoining-frequency"] = 3;
			}
			const int amount = variant == 1 ? 0 : variant >= 2 ? 300 : 100;
			for (const auto &control : definition.controls)
				if (control.group == ControlGroup::Resources)
					request.options[control.id] = amount;
			Game world(nullptr);
			const auto result = GenerationService().generate(world, request, true);
			REQUIRE_MESSAGE(bool(result), result.diagnostic());
			int islands = 0;
			for (const auto &record : result.telemetry.records())
				if (record.key == "braided-delta.islands")
					islands = int(std::get<std::int64_t>(record.value));
			const MapGeneration::Torus t(world.map);
			const auto walk = MapGeneration::walkFromFirstColony(world.map, request.nbTeams, "delta resources", "over the fords");
			REQUIRE(walk.error.empty());
			for (const int type : {STONE, CHERRY, ORANGE, PRUNE})
			{
				std::vector<unsigned char> mask(t.size(), 0);
				int count = 0;
				for (int i = 0; i < t.size(); ++i)
					count += mask[i] = world.map.getResource(i).type == type;
				const auto components = MapGeneration::connectedRegions(mask, t.w, t.h, true, MapGeneration::GridNeighbors::Cardinal);
				const int groups = *std::max_element(components.begin(), components.end()) + 1;
				CHECK(groups == (amount ? islands : 0));
				if (type != STONE)
					CHECK(count == islands * (amount ? std::max(1, 3 * amount / 100) : 0));
				std::vector<unsigned char> reachable(groups, 0);
				for (int i = 0; i < t.size(); ++i)
					if (mask[i])
						for (const auto &[dx, dy] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}})
							reachable[components[i]] |= walk.steps[t.at(t.remainderX(i) + dx, i / t.w + dy)] >= 0;
				for (int reached : reachable)
					CHECK(reached); // Actual finished gathering faces, after every later layer and repair.
			}
			CHECK(MapGeneration::startingAccessFailure(world.map, request.nbTeams,
				{{MaterialId::Food, 24, "food"}, {MaterialId::Wood, 32, "wood"}}).empty());
			GenerationContext check(request);
			REQUIRE(definition.validateWorld(world, check).empty());
			if (variant == 0)
			{
				for (int tick = 0; tick < 2048; ++tick)
					world.map.growResources();
				CHECK(definition.validateWorld(world, check).empty());
				std::vector<std::pair<int, Resource>> oranges;
				for (int i = 0; i < t.size(); ++i)
					if (world.map.getResource(i).type == ORANGE)
					{
						oranges.emplace_back(i, world.map.getResource(i));
						world.map.replaceResource(t.remainderX(i), i / t.w, Resource{});
					}
				CHECK(definition.validateWorld(world, check).find("fruit groups") != std::string::npos);
				for (const auto &[tile, resource] : oranges)
					world.map.replaceResource(t.remainderX(tile), tile / t.w, resource);
				CHECK(definition.validateWorld(world, check).empty());
			}
		}
	}
	TEST_CASE("Braided Delta retained generation and overgrowth contracts")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		GeneratorContracts::braidedDeltaChecks();
	}
	TEST_CASE("Glacis waterfront crops preserve gates and bare ground")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		const auto &definition = GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("glacis"));
		for (int variant = 0; variant < 5; ++variant)
		{
			D request;
			request.setMethodDefaults(definition.legacyId);
			request.seed = 7;
			request.options["bastions"] = 1 + variant % 3;
			request.options["gates"] = variant == 1 ? 1 : 4;
			if (variant == 1)
				request.options["fort-size"] = 32;
			if (variant == 2)
				request.options["wheat-amount"] = request.options["wood-amount"] = 300;
			if (variant == 3)
				request.options["wheat-amount"] = 0;
			if (variant == 4)
				request.options["wood-amount"] = 0;
			Game world(nullptr);
			const auto result = GenerationService().generate(world, request, true);
			REQUIRE_MESSAGE(bool(result), result.diagnostic());
			std::array<std::map<int, int>, 2> moat;
			std::array<int, 2> rivers{};
			int bankWidth = 0;
			for (const auto &record : result.telemetry.records())
			{
				if (record.key == "glacis.moat.bank-width")
					bankWidth = int(std::get<std::int64_t>(record.value));
				for (int crop = 0; crop < 2; ++crop)
				{
					if (record.key == (crop ? "glacis.moat.wood-planted" : "glacis.moat.wheat-planted"))
						moat[crop][record.subject] = int(std::get<std::int64_t>(record.value));
					if (record.key == (crop ? "glacis.rivers.wood-planted" : "glacis.rivers.wheat-planted"))
						rivers[crop] = int(std::get<std::int64_t>(record.value));
				}
			}
			CHECK(bankWidth == (variant == 1 ? 3 : 5));
			for (int crop = 0; crop < 2; ++crop)
			{
				REQUIRE(moat[crop].size() == size_t(request.nbTeams));
				const bool enabled = request.options[crop ? "wood-amount" : "wheat-amount"] > 0;
				for (int team = 0; team < request.nbTeams; ++team)
				{
					CHECK((moat[crop][team] > 0) == enabled);
					CHECK(moat[crop][team] == moat[crop][0]);
				}
				CHECK((rivers[crop] > 0) == enabled);
			}
			GenerationContext check(request);
			REQUIRE(definition.validateWorld(world, check).empty());
			if (variant == 0)
			{
				for (int tick = 0; tick < 2048; ++tick)
					world.map.growResources();
				CHECK(definition.validateWorld(world, check).empty());
				std::vector<std::pair<int, Resource>> timber;
				for (int i = 0; i < world.map.getW() * world.map.getH(); ++i)
					if (world.map.getResource(i).type == WOOD)
					{
						timber.emplace_back(i, world.map.getResource(i));
						world.map.replaceResource(i % world.map.getW(), i / world.map.getW(), Resource{});
					}
				CHECK(definition.validateWorld(world, check).find("moat bank has lost") != std::string::npos);
				for (const auto &[tile, resource] : timber)
					world.map.replaceResource(tile % world.map.getW(), tile / world.map.getW(), resource);
				CHECK(definition.validateWorld(world, check).empty());
			}
		}
	}
	TEST_CASE("Locust finite food remains dry after home jitter")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		GeneratorContracts::locustFoodChecks();
	}
	TEST_CASE("Clearing landscape homes jitter independently while preserving their room")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		for (const auto shape : {std::pair{8, 8}, std::pair{8, 7}, std::pair{7, 8}})
			for (int teams : {3, 4, 8})
				for (int seed : {1, 7})
				{
					D request;
					request.setMethodDefaults(GeneratorRegistry::builtins().idOf("old-growth"));
					request.wDec = shape.first;
					request.hDec = shape.second;
					request.nbTeams = teams;
					request.seed = seed;
					GenerationContext context(request);
					const MapGeneration::ClearingLandscapeOptions options{24, 5, 1, 90};
					const auto layout = MapGeneration::clearingLandscape(request, context, options);
					REQUIRE_MESSAGE(layout.failure.empty(), layout.failure);
					const auto lattice = MapGeneration::latticeSites(layout.t.w, layout.t.h, teams, 0, 0).sites;
					const double originalSpacing = MapGeneration::nearestSiteDistance(layout.t, lattice);
					CHECK(layout.homeRadius == std::min(24.0, std::floor(originalSpacing / 3 - 5)));
					CHECK(layout.spacing >= std::max(0.75 * originalSpacing, 3 * (layout.homeRadius + 5)));
					for (const auto home : layout.homes)
					{
						CHECK(home.x >= 0);
						CHECK(home.x < layout.t.w);
						CHECK(home.y >= 0);
						CHECK(home.y < layout.t.h);
					}
					if (shape == std::pair{8, 8} && teams == 4 && seed == 7)
					{
						const auto pairDistances = [&](const auto &sites)
						{
							std::vector<std::int64_t> result;
							for (size_t a = 0; a < sites.size(); ++a)
								for (size_t b = a + 1; b < sites.size(); ++b)
									result.push_back(layout.t.dist2(int(sites[a].x), int(sites[a].y), int(sites[b].x), int(sites[b].y)));
							std::sort(result.begin(), result.end());
							return result;
						};
						// A global shift or team shuffle leaves these distances unchanged.
						CHECK(pairDistances(layout.homes) != pairDistances(lattice));
						GenerationContext repeatContext(request);
						const auto repeat = MapGeneration::clearingLandscape(request, repeatContext, options);
						for (int team = 0; team < teams; ++team)
						{
							CHECK(layout.homes[team].x == repeat.homes[team].x);
							CHECK(layout.homes[team].y == repeat.homes[team].y);
						}
					}
				}
	}
	TEST_CASE("Plantations populate every owned island with the starting workers")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		const auto &definition = GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("plantations"));
		REQUIRE(definition.workersPerStartingSwarm);
		for (int id : GeneratorRegistry::builtins().methods())
			CHECK(GeneratorRegistry::builtins().at(id).workersPerStartingSwarm == (id == definition.legacyId));
		for (int workers : {1, 4, 8})
			for (int islands : {1, 3, 4})
				for (int causeways : {0, 1})
				{
					D request;
					request.setMethodDefaults(definition.legacyId);
					request.wDec = request.hDec = 8;
					request.nbWorkers = workers;
					request.seed = 7;
					request.options["islands-per-colony"] = islands;
					request.options["causeways"] = causeways;
					request.options["outpost-inns"] = causeways;
					Game world(nullptr);
					const auto result = GenerationService().generate(world, request, true);
					REQUIRE_MESSAGE(bool(result), result.diagnostic());
					for (int team = 0; team < request.nbTeams; ++team)
					{
						int swarms = 0, totalWorkers = 0;
						for (const auto *unit : world.teams[team]->liveUnits.entries())
							if (unit && unit->typeNum == WORKER)
								++totalWorkers;
						for (const auto *building : world.teams[team]->liveBuildings.entries())
							if (building && building->typeNum == world.buildingsTypes.getStartingBuildingTypeNum())
							{
								++swarms;
								int nearby = 0;
								for (const auto *unit : world.teams[team]->liveUnits.entries())
									if (unit && unit->typeNum == WORKER &&
										MapGeneration::touchesStartingFootprint(unit->posX, unit->posY,
											building->posX, building->posY, building->type->width, building->type->height,
											world.map.getMaskW(), world.map.getMaskH()))
										++nearby;
								CHECK(nearby == workers);
							}
						CHECK(swarms == islands);
						CHECK(totalWorkers == workers * swarms);
					}
					GenerationContext check(request);
					REQUIRE(definition.validateWorld(world, check).empty());
					const auto tiles = MapGeneration::unitTilesByTeam(world.map, request.nbTeams);
					const int tile = tiles[0].back(), x = tile % world.map.getW(), y = tile / world.map.getW();
					const auto id = world.map.getGroundUnit(x, y);
					world.map.setGroundUnit(x, y, NOGUID);
					CHECK(definition.validateWorld(world, check).find("starting workers") != std::string::npos);
					world.map.setGroundUnit(x, y, id);
					CHECK(definition.validateWorld(world, check).empty());
					// The ordinary per-colony contract still rejects the multiplied population.
					auto ordinary = definition;
					ordinary.workersPerStartingSwarm = false;
					CHECK(validateGeneratedWorld(world, request, ordinary).empty() == (islands == 1));
				}
	}
	TEST_CASE("Coral expansion courts have sealed grass and usable building room")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		const auto &definition = GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("coral"));
		for (const auto shape : {std::pair{8, 8}, std::pair{9, 8}, std::pair{8, 9}})
			for (int roads : {0, 1})
			{
				D request;
				request.setMethodDefaults(definition.legacyId);
				request.wDec = shape.first;
				request.hDec = shape.second;
				request.seed = 7;
				request.options["sand-roads"] = roads;
				for (const auto &control : definition.controls)
					if (control.group == ControlGroup::Resources)
						request.options[control.id] = control.maximum;
				Game world(nullptr);
				const auto result = GenerationService().generate(world, request, true);
				REQUIRE_MESSAGE(bool(result), result.diagnostic());
				std::map<int, std::pair<int, int>> courts;
				for (const auto &record : result.telemetry.records())
				{
					if (record.key == "coral.expansion.actual-per-fan")
						REQUIRE(std::get<std::int64_t>(record.value) == 2);
					if (record.key == "coral.expansion.x")
						courts[record.subject].first = int(std::get<std::int64_t>(record.value));
					if (record.key == "coral.expansion.y")
						courts[record.subject].second = int(std::get<std::int64_t>(record.value));
				}
				REQUIRE(courts.size() == size_t(2 * request.nbTeams));
				const auto emptyCourts = [&]()
				{
					for (const auto &[id, centre] : courts)
						for (int dy = -4; dy < 4; ++dy)
							for (int dx = -4; dx < 4; ++dx)
								if (dx * dx + dy * dy < 16 && (dx + 1) * (dx + 1) + dy * dy < 16 &&
									dx * dx + (dy + 1) * (dy + 1) < 16 &&
									(dx + 1) * (dx + 1) + (dy + 1) * (dy + 1) < 16)
								{
									const int x = centre.first + dx, y = centre.second + dy;
									REQUIRE(world.map.isGrass(x, y));
									REQUIRE(world.map.getResource(x, y).type == NO_RES_TYPE);
									REQUIRE(world.map.canResourcesGrow(x, y));
								}
				};
				emptyCourts();
				GenerationContext check(request);
				const auto [x, y] = courts.begin()->second;
				world.map.setResourceByIndex(x, y, WHEAT, 1);
				REQUIRE(definition.validateWorld(world, check).find("clear grass interior") != std::string::npos);
				world.map.setNoResource(x, y, 1);
				REQUIRE(world.map.vertexTerrainAt(x + 4, y) == SAND);
				world.map.setVertexTerrain(x + 4, y, GRASS);
				REQUIRE(definition.validateWorld(world, check).find("broken sand ring") != std::string::npos);
				world.map.setVertexTerrain(x + 4, y, SAND);
				REQUIRE(definition.validateWorld(world, check).empty());
				if (shape == std::pair{8, 8} && roads == 1)
				{
					for (int tick = 0; tick < 4096; ++tick)
						world.map.growResources();
					emptyCourts();
				}
			}
		D off;
		off.setMethodDefaults(definition.legacyId);
		off.options["expansion-nodes"] = 0;
		Game unchanged(nullptr);
		const auto result = GenerationService().generate(unchanged, off, true);
		REQUIRE_MESSAGE(bool(result), result.diagnostic());
		for (const auto &record : result.telemetry.records())
		{
			if (record.key == "coral.expansion.actual-per-fan")
				REQUIRE(std::get<std::int64_t>(record.value) == 0);
			REQUIRE(record.key != "coral.expansion.x");
		}

	}
	TEST_CASE("Honeycomb isle contracts [slow]")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		GeneratorContracts::honeycombIsleContracts();
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
    TEST_CASE("Drowned Forest circulation uses the configured starting definition")
    {
        glob2test::HeadlessGlobals globals;
        const auto& definition=GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("drowned-forest"));
        GenerationRequest request; request.setMethodDefaults(definition.legacyId);
        request.wDec=request.hDec=7; request.nbTeams=2; request.seed=7;
        GenerationService service;
        auto stock=std::make_unique<Game>(nullptr);
        auto renamed=std::make_unique<Game>(nullptr);
        auto catalog=nlohmann::json::parse(renamed->buildingsTypes.snapshotJson());
        for(auto& variant : catalog["variants"])
            if(variant["properties"]["type"]=="swarm") variant["properties"]["type"]="colony-anchor";
        renamed->buildingsTypes.loadSnapshotJson(catalog.dump()); renamed->configureBuildingCatalog();
        const auto original=service.generate(*stock,request);
        REQUIRE_MESSAGE(bool(original),original.diagnostic());
        const auto replacement=service.generate(*renamed,request);
        REQUIRE_MESSAGE(bool(replacement),replacement.diagnostic());
        CHECK(mapFingerprint(*stock)==mapFingerprint(*renamed));
        for(int team=0;team<request.nbTeams;++team)
            CHECK(renamed->teams[team]->myBuildings[0]->typeNum==renamed->buildingsTypes.getStartingBuildingTypeNum());
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
	TEST_CASE("Portage Lakes wheat opens into neighboring meadows")
	{
		DefaultsFixture fixture;
		MapGeneratorDefaultsTest::globalsInit();
		const auto &definition = GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("portage-lakes"));
		for (int variant = 0; variant < 4; ++variant)
		{
			D request;
			request.setMethodDefaults(definition.legacyId);
			request.seed = 7;
			request.options["wheat-amount"] = variant == 1 ? 0 : variant == 2 ? 300 : 100;
			if (variant == 3)
			{
				request.wDec = request.hDec = 6;
				request.nbTeams = 2;
			}
			Game world(nullptr);
			const auto result = GenerationService().generate(world, request, true);
			REQUIRE_MESSAGE(bool(result), result.diagnostic());
			const MapGeneration::Torus t(world.map);
			std::vector<unsigned char> grass(t.size());
			int sand = 0;
			for (int i = 0; i < t.size(); ++i)
			{
				grass[i] = world.map.isGrass(t.remainderX(i), i / t.w);
				sand += world.map.isSand(t.remainderX(i), i / t.w);
			}
			const auto groups = MapGeneration::connectedRegions(grass, t.w, t.h, true, MapGeneration::GridNeighbors::Eight);
			std::map<int, int> area, wheat;
			for (int i = 0; i < t.size(); ++i)
				if (groups[i] >= 0)
				{
					++area[groups[i]];
					wheat[groups[i]] += world.map.getResource(i).type == WHEAT;
				}
			bool open = false;
			for (const auto &[group, count] : wheat)
				open |= count > 0 && area[group] >= (variant == 3 ? 300 : 1000);
			CHECK(open);
			CHECK(sand > 0);
			GenerationContext context(request);
			CHECK(definition.validateWorld(world, context).empty());
			if (variant == 0)
			{
				setSyncRandSeed(4211);
				for (int tick = 0; tick < 4096; ++tick)
					world.map.growResources();
				const auto error = definition.validateWorld(world, context);
				CHECK_MESSAGE(error.empty(), error);
			}
		}
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

TEST_CASE("contact costs follow custom deposit obstruction and removal properties" * doctest::test_suite("MapGeneratorDefaults"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.clearImmobile=true,.header=true});
    auto& map=fixture.game.map;
    using Json=nlohmann::json;
    const auto original=Json::parse(map.resourceRegistry().serialize())["resources"][0];
    const MapGeneration::StepCosts costs{2,5,17,3,23};
    for (int bits=0;bits<8;++bits)
    {
        CAPTURE(bits);
        auto definition=original;
        definition["key"]="contact-properties-"+std::to_string(bits);
        const bool blocking=bits&1,clearable=bits&2,infinite=bits&4;
        definition["properties"]["blocksGround"]=blocking;
        definition["properties"]["clearable"]=clearable;
        definition["properties"]["habitatMask"]=3;
        definition["properties"]["requiresGrowthTerrain"]=false;
        definition["yields"]["wood"]["consumption"]=infinite ? "infinite" : "one";
        definition["yields"]["wood"]["destroysDeposit"]=!infinite;
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({definition})}}.dump());
        const auto id=*map.resourceRegistry().find(definition["key"].get<std::string>());
        for (auto terrain : {GRASS,WATER})
        {
            map.replaceResource(12,12,Resource{});
            MapGeneration::paintTile(map,12,12,terrain);
            map.setResource(12,12,id,0);
            REQUIRE(map.getResource(12,12).type==resourceIndex(id));
            const int open=terrain==GRASS ? costs.open : costs.water;
            const int expected=!blocking ? open : !clearable && infinite ? costs.eternal : costs.clearable;
            CHECK(MapGeneration::stepCost(map,12,12,costs)==expected);
        }
    }
}

TEST_CASE("Anthill and Lava Shield default passages survive conservative terrain connections" *
		  doctest::test_suite("MapGeneratorDefaults"))
{
	glob2test::HeadlessGlobals globals;
	for (const char *key : {"anthill", "lava-shield"})
		for (unsigned seed : {1u, 3u})
		{
			CAPTURE(key);
			CAPTURE(seed);
			GenerationRequest request;
			request.setMethodDefaults(GeneratorRegistry::builtins().idOf(key));
			request.wDec = request.hDec = 8;
			request.nbTeams = 4;
			request.seed = seed;
			Game game(nullptr);
			const auto result = GenerationService().generate(game, request);
			INFO(result.diagnostic());
			REQUIRE(result);
			GenerationContext proof(request);
			CHECK(GeneratorRegistry::builtins()
					  .at(request.method)
					  .validateWorld(game, proof)
					  .empty());
		}
}
