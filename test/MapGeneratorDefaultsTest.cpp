// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "CustomGameSetup.h"
#include "Contact.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include "GenerationValidation.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "LegacyGenerationDescriptor.h"
#include "MapGeneratorFrameworkChecks.h"
#include "MapGeneratorContracts.h"
#include "MapGeneratorLandscapeChecks.h"
#include "MapGeneratorToolkitChecks.h"
#include "NewMapScreen.h"
#include "Race.h"
#include "Resources.h"
#include "Sketch.h"
#include "StartingPositions.h"
#include "Unit.h"
#include "Utilities.h"
#include <GUIButton.h>
#include <GUIList.h>
#include <GUINumber.h>
#include <GUIText.h>
#include <SDL_image.h>
#include <Toolkit.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <tuple>
#include <utility>

GlobalContainer *globalContainer = nullptr;
using D = GenerationRequest;

class MapGeneratorDefaultsTest
{
  public:
	static void select(NewMapScreen &s, int method)
	{
		s.methods->setSelectionIndex(s.registry.selectionIndex(method));
		s.onAction(s.methods, GAGGUI::LIST_ELEMENT_SELECTED, method, 0);
	}
	static void sameControls(const D &a, const D &b)
	{
		assert(a.method == b.method);
		for (const auto &c : D::controls(a.method))
			assert(c.get(a) == c.get(b));
		for (const auto &c : D::sharedControls())
			assert(c.get(a) == c.get(b));
	}
	static void edit(NewMapScreen &s, const char *label, int value)
	{
		for (auto &w : s.controlWidgets)
		{
			if (!w.field()->visible || std::strcmp(w.definition.label, label) != 0)
				continue;
			if (w.toggle)
			{
				assert(value == 0 || value == 1);
				w.toggle->setState(value != 0);
				s.onAction(w.toggle, GAGGUI::BUTTON_STATE_CHANGED, NewMapScreen::TOGGLE, value);
			}
			else
			{
				w.number->setNth(w.definition.indexOf(value));
				s.onAction(w.number, GAGGUI::NUMBER_ELEMENT_SELECTED, 0, 0);
			}
			assert(w.definition.get(s.descriptor) == value);
			return;
		}
		assert(false);
	}
	// Presses and releases the mouse over a widget, the way a player clicks it.
	static void click(NewMapScreen &s, GAGGUI::Widget *widget)
	{
		const auto box = static_cast<GAGGUI::RectangularWidget *>(widget)->getScreenRect();
		for (Uint32 type : {Uint32(SDL_MOUSEBUTTONDOWN), Uint32(SDL_MOUSEBUTTONUP)})
		{
			SDL_Event event = {};
			event.type = type;
			event.button.button = SDL_BUTTON_LEFT;
			event.button.x = box.x + box.w / 2;
			event.button.y = box.y + box.h / 2;
			s.dispatchEvents(&event);
		}
	}
	static void generationContracts()
	{
		globalsInit();
		GeneratorContracts::generatorContracts();
		frameworkChecks();
		ToolkitChecks::toolkitChecks();
		LandscapeChecks::landscapeChecks();
		gauntletContracts();
		GenerationService service;
		for (int method : GeneratorRegistry::builtins().methods())
		{
			D request;
			request.setMethodDefaults(method);
			request.seed = 22001;
			setSyncRandSeed(177);
			auto surrounding = syncRandEngine();
			Game first(nullptr);
			auto a = service.generate(first, request);
			assert(syncRandEngine() == surrounding);
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
			assert(bool(a) == bool(b) && a.stage == b.stage && hash == mapFingerprint(repeat));
			assert(checksum == repeat.checkSum(nullptr, nullptr, nullptr, true));
			assert(request.seed == 22001 && request.options == DWithDefaults(method).options);
			if (method == GeneratorRegistry::builtins().idOf("lava-shield"))
			{
				if (!a)
					std::cerr << a.diagnostic() << std::endl;
				assert(a); // A repeatable failure is not a valid default map.
				GenerationContext probe(request);
				const auto &definition = GeneratorRegistry::builtins().at(method);
				assert(definition.validateWorld(first, probe).empty());
				// Removing all rock must be caught by the generator's actual final-world
				// validator, not merely by a golden hash. Corrupt the unused repeated copy.
				for (int y = 0; y < repeat.map.getH(); ++y)
					for (int x = 0; x < repeat.map.getW(); ++x)
						if (repeat.map.getResource(x, y).type == STONE)
							repeat.map.setNoResource(x, y, 1);
				assert(!definition.validateWorld(repeat, probe).empty());
			}
			if (a)
			{
				auto rejected = service.generate(first, request);
				assert(rejected.error == GenerationError::NonEmptyTarget);
				assert(mapFingerprint(first) == hash);
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
						assert(service.generate(world, rectangular).error ==
							   GenerationError::InvalidRequest);
						continue;
					}
					assert(service.generate(world, rectangular));
					assert(world.map.getW() == (1 << dimensions.first));
					assert(world.map.getH() == (1 << dimensions.second));
					for (int team = 0; team < rectangular.nbTeams; ++team)
					{
						assert(world.teams[team]->startPosX >= 0 &&
							   world.teams[team]->startPosX < world.map.getW());
						assert(world.teams[team]->startPosY >= 0 &&
							   world.teams[team]->startPosY < world.map.getH());
					}
				}
			for (const auto &c : D::controls(method))
			{
				D invalid = request;
				invalid.options[c.id] = c.maximum + c.step;
				Game fresh(nullptr);
				auto failure = service.generate(fresh, invalid);
				assert(failure.error == GenerationError::InvalidRequest && fresh.teamsCount() == 0);
			}
		}
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
				assert(outcome && world.teamsCount() == teams);
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
			assert(validateGenerationRequest(lava, definition).empty());
			for (const char *key : {"tongue-count", "rim-width"})
			{
				D crowded = lava;
				crowded.options[key] = std::string(key) == "tongue-count" ? 9 : 12;
				Game untouched(nullptr);
				const auto result = service.generate(untouched, crowded);
				assert(result.error == GenerationError::InvalidRequest &&
					   untouched.teamsCount() == 0);
				crowded.wDec = crowded.hDec = 8;
				crowded.nbTeams = 4;
				assert(validateGenerationRequest(crowded, definition).empty());
			}
		}
		// Scoped RNG restoration must also hold when placement fails.
		setSyncRandSeed(711);
		auto savedFailureRng = syncRandEngine();
		D invalid;
		invalid.method = 99999;
		Game fresh(nullptr);
		assert(service.generate(fresh, invalid).error == GenerationError::InvalidRequest);
		invalid = DWithDefaults(D::eRIVER);
		invalid.wDec = 31;
		assert(service.generate(fresh, invalid).error == GenerationError::InvalidRequest);
		invalid = DWithDefaults(D::eSWAMP);
		// An all-water swamp can still fit its colonies on a big map, so pin this failure to 128x128
		// whatever the default size is.
		invalid.wDec = invalid.hDec = 7;
		invalid.options["water"] = 100;
		invalid.options["grass"] = 0;
		auto failed = service.generate(fresh, invalid);
		assert(!failed && failed.error == GenerationError::PlacementFailed &&
			   !failed.stage.empty());
		assert(syncRandEngine() == savedFailureRng);
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
			assert(converted.option(key) == pair.second);
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
				 assert(elevation == 8);
				 assert(context.request.option("test-switch") == 1);
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
			assert(on.isToggle() && on.defaultValue == 1 &&
				   on.values() == std::vector<int>({0, 1}));
			assert(!rejected(on));
			const auto amount = GeneratorControl::percentage("amount", "Fruit");
			assert(!amount.isToggle() && amount.defaultValue == 100 && !rejected(amount));
			// A choice stores the index of its named option and is shown by name.
			const auto shape =
				GeneratorControl::choice("shape", "Cell shape", {"Squares", "Hexagons"}, 1);
			assert(shape.isChoice() && !shape.isToggle() && shape.defaultValue == 1 &&
				   shape.values() == std::vector<int>({0, 1}) &&
				   std::string(shape.valueLabel(1)) == "Hexagons" && !shape.valueLabel(2) &&
				   shape.normalize(5) == 1 && !rejected(shape));
			auto unnamed = shape;
			unnamed.valueLabels.pop_back();
			auto blank = shape;
			blank.valueLabels[0] = "";
			assert(rejected(unnamed) && rejected(blank) && !on.isChoice() && !on.valueLabel(0));
			std::vector<GeneratorControl> broken(6, on);
			broken[0].maximum = 2;
			broken[1].minimum = broken[1].defaultValue = 1;
			broken[2].powerOfTwo = true;
			broken[3].terrainWeight = true;
			broken[4].allowedValues = {0, 1};
			broken[5].defaultValue = 2;
			for (const auto &control : broken)
				assert(rejected(control));
		}
		GeneratorRegistry registry(std::move(definitions));
		NewMapScreen screen(registry);
		select(screen, 101);
		assert(screen.descriptor.option("test-elevation") == 6);
		assert(screen.descriptor.option("test-switch") == 0);
		edit(screen, "Island size", 16);
		edit(screen, "Channel width", 5);
		edit(screen, "Lake connects to fjords", 1);
		assert(screen.descriptor.option("test-shape") == 0);
		edit(screen, "Cell shape", 1);
		select(screen, D::eRIVER);
		select(screen, 101);
		assert(screen.descriptor.option("test-cell") == 16 &&
			   screen.descriptor.option("test-gap") == 5 &&
			   screen.descriptor.option("test-shape") == 1 &&
			   screen.descriptor.option("test-switch") == 1);
		edit(screen, "Smoothing", 8);
		assert(registry.selectionIndex(101) == int(GeneratorRegistry::builtins().methods().size()));
		const auto playable = registry.methods(false);
		assert(std::find(playable.begin(), playable.end(), 101) != playable.end());
		Game generated(nullptr);
		assert(GenerationService(registry).generate(generated, screen.descriptor));
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
			assert(a && b && a.quality.measured);
			assert(a.quality.score == b.quality.score);
			assert(service.bestSeed(sampled, root) == chosen);
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
				assert(!rolled || rolled.quality.score <= a.quality.score);
			}
			assert(sawChosen);
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
								  std::pair{9, 8}})
		{
			D sized = request;
			sized.wDec = sized.hDec = dims;
			sized.nbTeams = teams;
			Game world(nullptr);
			const auto result = service.generate(world, sized);
			if (!result)
				std::cerr << "Gauntlet " << (1 << dims) << "x" << (1 << dims) << ", "
						  << teams << " colonies: " << result.diagnostic() << std::endl;
			assert(result && world.teamsCount() == teams);
			GenerationContext probe(sized);
			assert(definition.validateWorld(world, probe).empty());
		}
		for (auto [width, height, teams] : {std::tuple{7, 7, 4}, std::tuple{9, 7, 4},
										   std::tuple{8, 8, 1}, std::tuple{9, 9, 13}})
		{
			D invalid = request;
			invalid.wDec = width;
			invalid.hDec = height;
			invalid.nbTeams = teams;
			Game untouched(nullptr);
			assert(!validateGenerationRequest(invalid, definition).empty());
			assert(service.generate(untouched, invalid).error == GenerationError::InvalidRequest);
			assert(untouched.teamsCount() == 0);
		}
		// Test the final-world validator, not just generator success or a golden hash.
		for (int corruption = 0; corruption < 3; ++corruption)
		{
			Game world(nullptr);
			const auto result = service.generate(world, request, true);
			assert(result);
			GenerationContext probe(request);
			assert(definition.validateWorld(world, probe).empty());
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
				assert(outer > 0);
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
			assert(changed > 0);
			const auto error = definition.validateWorld(world, probe);
			assert(!error.empty());
			if (corruption == 1)
				assert(error.find("orchard") != std::string::npos);
			if (corruption == 2)
				assert(error.find("entrance") != std::string::npos);
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
				assert(game.map.isWater(x, y));
				(x >= 24 && x < 40 && y >= 24 && y < 40 ? lake : sea) += 1;
			}
		assert(sea > 0 && lake > 0);
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
		generationContracts();
		scatterAlgaeOnWater();
		NewMapScreen s;
		s.gfx = globalContainer->gfx;
		s.dispatchInit();
		CustomGameSetup lobby;
		// The catalog's order was shuffled once (FEEDBACK 2026-09-14: no bias towards the landscapes
		// that happen to be listed first). The editor opens on the catalog's first entry and the
		// lobby on its first playable one, whatever they are.
		D editorFirst, lobbyFirst;
		editorFirst.setMethodDefaults(GeneratorRegistry::builtins().methods().front());
		lobbyFirst.setMethodDefaults(GeneratorRegistry::builtins().methods(false).front());
		assert(GeneratorRegistry::builtins().methods(false).front() ==
			   GeneratorRegistry::builtins().idOf("fingerprint"));
		assert(s.methods->getSelectionIndex() == 0);
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
			s.dispatchTimer(1);
			sameControls(s.descriptor, expected);
			std::set<std::string> labels;
			for (const auto &c : D::controls(method))
			{
				assert(labels.insert(c.label).second);
				assert(c.step > 0 && c.maximum >= c.minimum);
				assert(!c.allowedValues.empty() || (c.maximum - c.minimum) % c.step == 0);
				assert(c.get(expected) == c.defaultValue);
				assert(c.normalize(c.defaultValue) == c.defaultValue);
				assert(c.normalize(c.minimum - 100) == c.minimum);
				assert(c.normalize(c.maximum + 100) == c.maximum);
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
				assert(decoded.setData(encoded.getData(), encoded.getDataLength()));
				sameControls(fromLegacyDescriptor(decoded, 0), expected);
			}
			if (output &&
				((m >= 4 && m <= 8) || m == GeneratorRegistry::builtins().idOf("fjord-continent")))
			{
				s.gfx->drawFilledRect(0, 0, 640, 480, GAGCore::Color(34, 55, 42));
				for (auto *w : s.widgets)
					if (w->visible)
						w->paint();
				std::string path = std::string(output) + "/editor-" + std::to_string(m) + ".png";
				assert(IMG_SavePNG(s.gfx->getSDLSurface(), path.c_str()) == 0);
			}
		}
		// Every switch is a check button in the editor, and clicking one flips the request's value.
		for (int m : GeneratorRegistry::builtins().methods())
		{
			select(s, m);
			for (auto &w : s.controlWidgets)
				if (w.method == m)
				{
					assert(w.definition.isToggle() == bool(w.toggle) &&
						   bool(w.number) != bool(w.toggle));
					assert(w.field()->visible && w.label->visible);
					if (!w.toggle)
						continue;
					const int before = w.definition.get(s.descriptor);
					assert(w.toggle->getState() == (before != 0));
					click(s, w.toggle);
					assert(w.definition.get(s.descriptor) == 1 - before &&
						   w.toggle->getState() == !before);
					click(s, w.toggle);
					assert(w.definition.get(s.descriptor) == before);
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
			assert(s.descriptor.wDec == 8 && s.descriptor.nbTeams == 6);
		}
		assert(s.descriptor.options["water"] == 37);
		lobby.random = true;
		lobby.generator.options["water"] = lobby.generator.options["grass"] =
			lobby.generator.options["sand"] = lobby.generator.options["desert"] = 0;
		assert(!lobby.validation().empty());
		lobby.generator.options["grass"] = 75;
		assert(lobby.validation().empty());
		puts("PASS shared presets, ranges and steps; all editor values; lobby/editor mode memory; "
			 "serialization and validation");
	}
};
int main(int argc, char **argv)
{
	assert(argc == 2 || argc == 3);
	SDL_SetMainReady();
	SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	GlobalContainer globals(argv[1]);
	globalContainer = &globals;
	globals.runNoX = true;
	globals.load();
	globals.gfx = GAGCore::Toolkit::initGraphic(640, 480, 0, "Map defaults test", "");
	GAGCore::Toolkit::loadFont("data/fonts/sans.ttf", 20, "menu");
	GAGCore::Toolkit::loadFont("data/fonts/sans.ttf", 13, "standard");
	// Fast iteration over reusable primitives and their raster/economy fixtures.
	// The ordinary invocation still runs the complete registry/editor/defaults
	// contract; this option never weakens that CI path or changes generated worlds.
	if (argc == 3 && std::string(argv[2]) == "--toolkit-only")
	{
		ToolkitChecks::toolkitChecks();
		puts("PASS toolkit-only geometry, raster, resource and home contracts");
		return 0;
	}
	if (argc == 3 && std::string(argv[2]) == "--drowned-forest-only")
	{
		GeneratorContracts::drownedForestContracts();
		return 0;
	}
	if (argc == 3 && std::string(argv[2]) == "--gauntlet-only")
	{
		MapGeneratorDefaultsTest::globalsInit();
		MapGeneratorDefaultsTest::gauntletContracts();
		puts("PASS Gauntlet supported shapes, request rejection and final-world corruption checks");
		return 0;
	}
	if (argc == 3 && std::string(argv[2]) == "--faulted-city-only")
	{
		MapGeneratorDefaultsTest::globalsInit();
		GeneratorContracts::faultedCityContracts();
		return 0;
	}
	MapGeneratorDefaultsTest::run(argc == 3 ? argv[2] : nullptr);
}
