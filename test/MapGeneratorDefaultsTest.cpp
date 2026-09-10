// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "CustomGameSetup.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "LegacyGenerationDescriptor.h"
#include "MapGeneratorFrameworkChecks.h"
#include "NewMapScreen.h"
#include "Race.h"
#include "StartingPositions.h"
#include "Unit.h"
#include "Utilities.h"
#include <GUIList.h>
#include <GUINumber.h>
#include <SDL_image.h>
#include <Toolkit.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>

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
			if (w.number->visible && std::strcmp(w.definition.label, label) == 0)
			{
				w.number->setNth(w.definition.indexOf(value));
				s.onAction(w.number, GAGGUI::NUMBER_ELEMENT_SELECTED, 0, 0);
				assert(w.definition.get(s.descriptor) == value);
				return;
			}
		assert(false);
	}
	static std::uint64_t fingerprint(const Game &game)
	{
		std::uint64_t hash = 14695981039346656037ull;
		auto add = [&](unsigned v)
		{
			hash ^= v;
			hash *= 1099511628211ull;
		};
		for (int y = 0; y < game.map.getH(); ++y)
			for (int x = 0; x < game.map.getW(); ++x)
			{
				add(game.map.getUMTerrain(x, y));
				add(game.map.getTerrain(x, y));
				add(game.map.getResource(x, y).type);
				add(game.map.getResource(x, y).amount);
			}
		for (int i = 0; i < game.teamsCount(); ++i)
		{
			add(game.teams[i]->startPosX);
			add(game.teams[i]->startPosY);
			for (int j = 0; j < Unit::MAX_COUNT; ++j)
				if (auto *unit = game.teams[i]->myUnits[j])
				{
					add(j);
					add(unit->posX);
					add(unit->posY);
				}
		}
		return hash;
	}
	static void generationContracts()
	{
		globalsInit();
		frameworkChecks();
		GenerationService service;
		for (int method : GeneratorRegistry::builtins().methods())
		{
			D request;
			request.setMethodDefaults(method);
			request.seed = 22001;
			setSyncRandSeed(177);
			auto surrounding = randomGenerator;
			Game first(nullptr);
			auto a = service.generate(first, request);
			assert(randomGenerator == surrounding);
			auto hash = fingerprint(first);
			auto checksum = first.checkSum(nullptr, nullptr, nullptr, true);
			D intervening;
			intervening.setMethodDefaults(D::eISLANDS);
			intervening.seed = 9017;
			Game other(nullptr);
			service.generate(other, intervening);
			Game repeat(nullptr);
			auto b = service.generate(repeat, request);
			assert(bool(a) == bool(b) && a.stage == b.stage && hash == fingerprint(repeat));
			assert(checksum == repeat.checkSum(nullptr, nullptr, nullptr, true));
			assert(request.seed == 22001 && request.options == DWithDefaults(method).options);
			if (a)
			{
				auto rejected = service.generate(first, request);
				assert(rejected.error == GenerationError::NonEmptyTarget);
				assert(fingerprint(first) == hash);
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
		// Scoped RNG restoration must also hold when placement fails.
		setSyncRandSeed(711);
		auto savedFailureRng = randomGenerator;
		D invalid;
		invalid.method = 99999;
		Game fresh(nullptr);
		assert(service.generate(fresh, invalid).error == GenerationError::InvalidRequest);
		invalid = DWithDefaults(D::eRIVER);
		invalid.wDec = 31;
		assert(service.generate(fresh, invalid).error == GenerationError::InvalidRequest);
		invalid = DWithDefaults(D::eSWAMP);
		invalid.options["water"] = 100;
		invalid.options["grass"] = 0;
		auto failed = service.generate(fresh, invalid);
		assert(!failed && failed.error == GenerationError::PlacementFailed &&
			   !failed.stage.empty());
		// Legacy sentinel conversion belongs exclusively to the adapter.
		for (auto pair : {std::pair{D::eCRATERLAKES, 30}, std::pair{D::eCONCRETEISLANDS, 6},
						  std::pair{D::eISLES, 4}})
		{
			MapGenerationDescriptor legacy;
			legacy.setMethodDefaults(static_cast<MapGenerationDescriptor::Method>(pair.first));
			legacy.riverDiameter = 50;
			auto converted = fromLegacyDescriptor(legacy, 42);
			const char *key = pair.first == D::eCRATERLAKES ? "lake-size"
							  : pair.first == D::eISLES		? "bridge-width"
															: "channel-width";
			assert(converted.option(key) == pair.second);
		}
		// New registrations need no ordinal dispatch, UI edits or new descriptor fields.
		std::vector<GeneratorDefinition> definitions;
		for (int id : GeneratorRegistry::builtins().methods())
			definitions.push_back(GeneratorRegistry::builtins().at(id));
		definitions.push_back({"test-plateau",
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
								{"test-gap", "Channel width", 1, 5, 2, 3, ControlGroup::Layout}},
							   [](Game &game, GenerationContext &context)
							   {
								   const int elevation = context.request.option("test-elevation");
								   assert(elevation == 8);
								   game.map.makeHomogenMap(GRASS);
								   for (int team = 0; team < context.request.nbTeams; ++team)
								   {
									   context.bootX[team] = elevation + (team % 2) * 40;
									   context.bootY[team] = elevation + (team / 2) * 40;
								   }
								   return MapGeneration::placeStarts(game, context);
							   }});
		GeneratorRegistry registry(std::move(definitions));
		NewMapScreen screen(registry);
		select(screen, 101);
		assert(screen.descriptor.option("test-elevation") == 6);
		edit(screen, "Island size", 16);
		edit(screen, "Channel width", 5);
		select(screen, D::eRIVER);
		select(screen, 101);
		assert(screen.descriptor.option("test-cell") == 16 &&
			   screen.descriptor.option("test-gap") == 5);
		edit(screen, "Smoothing", 8);
		assert(registry.selectionIndex(101) == 13);
		const auto playable = registry.methods(false);
		assert(std::find(playable.begin(), playable.end(), 101) != playable.end());
		Game generated(nullptr);
		assert(GenerationService(registry).generate(generated, screen.descriptor));
		puts("PASS registration extension, explicit seeds, interleaved repeatability, RNG "
			 "isolation, errors and legacy sentinels");
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
		NewMapScreen s;
		s.gfx = globalContainer->gfx;
		s.dispatchInit();
		CustomGameSetup lobby;
		D river;
		river.setMethodDefaults(D::eRIVER);
		sameControls(lobby.generator, river);
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
			if (output && m >= 4 && m <= 8)
			{
				s.gfx->drawFilledRect(0, 0, 640, 480, GAGCore::Color(34, 55, 42));
				for (auto *w : s.widgets)
					if (w->visible)
						w->paint();
				std::string path = std::string(output) + "/editor-" + std::to_string(m) + ".png";
				assert(IMG_SavePNG(s.gfx->getSDLSurface(), path.c_str()) == 0);
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
	MapGeneratorDefaultsTest::run(argc == 3 ? argv[2] : nullptr);
}
