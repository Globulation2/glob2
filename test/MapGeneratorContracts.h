// SPDX-License-Identifier: GPL-3.0-or-later
// Contract checks for individual generators: the promises a map makes beyond the framework's
// common structure (its supported shapes, its variants, what its validator refuses), each run
// by MapGeneratorDefaultsTest. Shared-primitive checks live in MapGeneratorToolkitChecks.h and
// MapGeneratorLandscapeChecks.h; a check here is about one landscape's own contract.
#pragma once
#include "BraidedDeltaGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "Growth.h"
#include "MapGeneratorFrameworkChecks.h"
#include "Pipeline.h"
#include <cassert>
#include <cstdio>
#include <set>
#include <string>

namespace GeneratorContracts
{
using D = GenerationRequest;
using namespace MapGeneration;

inline void emojiContracts()
{
	const int method = GeneratorRegistry::builtins().idOf("emoji");
	D request;
	request.setMethodDefaults(method);
	for (const char *key : {"character", "outline", "inverse"})
		assert(request.option(key) == 0);
	GenerationService service;
	std::set<std::string> characters, styles, terrains;
	for (unsigned seed = 1; seed <= 16; ++seed)
	{
		request.seed = seed;
		Game world(nullptr);
		const auto result = service.generate(world, request, true);
		assert(result);
		for (const auto &record : result.telemetry.records())
		{
			if (record.key == "emoji.character")
				characters.insert(std::get<std::string>(record.value));
			if (record.key == "emoji.style")
				styles.insert(std::get<std::string>(record.value));
			if (record.key == "emoji.terrain")
				terrains.insert(std::get<std::string>(record.value));
		}
	}
	assert(characters.size() >= 4 && styles.size() == 2 && terrains.size() == 2);
	// All explicit variants must work and yield distinct maps at the same seed.
	std::set<std::uint64_t> variants;
	for (int character = 1; character <= 8; ++character)
		for (int outline : {1, 2})
			for (int inverse : {1, 2})
			{
				request.options["character"] = character;
				request.options["outline"] = outline;
				request.options["inverse"] = inverse;
				request.seed = 71;
				Game world(nullptr);
				const auto result = service.generate(world, request);
				if (!result)
					std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
				assert(result);
				variants.insert(mapFingerprint(world));
				// Colony count and workers must not stamp home terrain onto the art.
				auto solo = request;
				solo.nbTeams = 1;
				solo.nbWorkers = 1;
				Game alone(nullptr);
				assert(service.generate(alone, solo));
				for (int y = 0; y < (1 << request.hDec); ++y)
					for (int x = 0; x < (1 << request.wDec); ++x)
						assert(world.map.getUMTerrain(x, y) == alone.map.getUMTerrain(x, y));
			}
	assert(variants.size() == 32);
	// Playtest regression: dense deposits consumed the remaining construction room
	// around an eight-colony filled-water sunglasses start. Relief must preserve terrain.
	request.seed = 74021;
	request.nbTeams = request.nbWorkers = 8;
	request.options["character"] = 6;
	request.options["outline"] = 2;
	request.options["inverse"] = 1;
	for (const char *key : {"wheat-amount", "wood-amount", "stone-amount", "algae-amount",
							"fruit-amount"})
		request.options[key] = 200;
	{
		Game crowdedResources(nullptr);
		assert(service.generate(crowdedResources, request));
	}
	for (auto dimensions : {std::pair{7, 7}, std::pair{8, 9}, std::pair{9, 8}})
	{
		request.wDec = dimensions.first;
		request.hDec = dimensions.second;
		Game world(nullptr);
		assert(service.generate(world, request).error == GenerationError::InvalidRequest);
		assert(world.teamsCount() == 0);
	}
	request.wDec = request.hDec = 9;
	request.nbTeams = 9;
	Game crowded(nullptr);
	assert(service.generate(crowded, request).error == GenerationError::InvalidRequest);
	puts("PASS Emoji: random defaults, all 32 explicit variants, terrain independent of "
		 "colonies/workers, unsupported geometry rejected");
}

// The smallest fort must retain its enclosure and usable food/wood at both abundance extremes.
// Breaking a real rampart also exercises the generator's finished-world validator.
inline void fortsContracts()
{
	D request;
	request.setMethodDefaults(GeneratorRegistry::builtins().idOf("forts"));
	request.wDec = request.hDec = 6;
	request.nbTeams = 1;
	request.nbWorkers = 8;
	request.options["home-size"] = 20;
	request.options["gate-width"] = 8;
	request.options["river-width"] = 9;
	const auto &definition = GeneratorRegistry::builtins().at(request.method);
	for (int lakes : {0, 1, 3})
		for (int amount : {0, 300})
		{
			request.options["lakes"] = lakes;
			for (const char *key : {"wheat-amount", "wood-amount", "stone-amount",
									"algae-amount", "fruit-amount"})
				request.options[key] = amount;
			Game game(nullptr);
			const auto result = GenerationService().generate(game, request, true);
			if (!result)
				std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
			assert(result);
			int crops[2] = {0, 0}, wall = -1;
			for (int i = 0; i < 64 * 64; ++i)
			{
				const auto &resource = game.map.getResource(i);
				if (resource.type == WHEAT)
					++crops[0];
				if (resource.type == WOOD)
					++crops[1];
				if (resource.type == STONE && amount == 0)
					wall = i;
			}
			assert(crops[0] >= 32 && crops[1] >= 32);
			GenerationContext check(request);
			assert(definition.validateWorld(game, check).empty());
			if (amount > 0)
			{
				// Household fruit is an economic guarantee at nonzero default/max abundance.
				for (int i = 0; i < 64 * 64; ++i)
				{
					const int type = game.map.getResource(i).type;
					if (type >= CHERRY && type < CHERRY + 3)
						game.map.setNoResource(i % 64, i / 64, 1);
				}
				assert(definition.validateWorld(game, check).find("orchard") !=
					   std::string::npos);
			}
			if (amount == 0)
			{
				// Leave crops unharvested long enough to fill their plots: the courtyard and gates
				// must still be usable when growth, rather than an AI, shapes the opening.
				setSyncRandSeed(917);
				for (int tick = 0; tick < 20000; ++tick)
					game.map.growResources();
				assert(definition.validateWorld(game, check).empty());
				const Team *team = game.teams[0];
				for (int dy = -2; dy < 6; ++dy)
					for (int dx = -2; dx < 6; ++dx)
						assert(
							!game.map.isResource(team->startPosX + dx, team->startPosY + dy));
				assert(wall >= 0);
				game.map.setNoResource(wall % 64, wall / 64, 1);
				assert(!definition.validateWorld(game, check).empty());
			}
		}
	request.nbTeams = 4;
	Game crowded(nullptr);
	assert(GenerationService().generate(crowded, request).error ==
		   GenerationError::InvalidRequest);
}

// Exercise the production service and final-world validator, including deliberate damage.
// A successful seed alone would not show that the validator notices lost expansion ground.
inline void braidedDeltaChecks()
{
	const auto definition = braidedDeltaDefinition();
	GenerationRequest request;
	request.setMethodDefaults(definition.legacyId);
	request.wDec = request.hDec = 7;
	request.nbTeams = 4;
	request.options["braid-count"] = 2;
	request.options["rejoining-frequency"] = 3;
	for (unsigned seed : {7u, 101u, 22001u, 53006u, 711038u, 711131u, 711226u})
	{
		request = GenerationRequest();
		request.setMethodDefaults(definition.legacyId);
		request.wDec = request.hDec = 7;
		request.nbTeams = 4;
		request.options["braid-count"] = 2;
		request.options["rejoining-frequency"] = 3;
		request.seed = seed;
		// Bulk seed 711038 exhausts the wood kit's bank space when wheat and the
		// permanent resources are abundant. Emergency topups must not seed a town.
		if (seed == 711038u)
		{
			request.nbWorkers = 8;
			request.options["wheat-amount"] = 300;
			request.options["wood-amount"] = 0;
			request.options["stone-amount"] = 300;
			request.options["algae-amount"] = 0;
			request.options["fruit-amount"] = 300;
		}
		// The same bank-reservation failure also occurred on dense medium and large
		// islands. Keep one fixture of each size, including the opposite crop extreme.
		if (seed == 711131u || seed == 711226u)
		{
			const bool large = seed == 711226u;
			request.wDec = request.hDec = large ? 9 : 8;
			request.nbTeams = 12;
			request.nbWorkers = 8;
			request.options["braid-count"] = large ? 5 : 4;
			request.options["island-size"] = large ? 48 : 32;
			request.options["crossing-spacing"] = large ? 96 : 64;
			request.options["wheat-amount"] = large ? 0 : 300;
			request.options["wood-amount"] = large ? 300 : 0;
			request.options["stone-amount"] = large ? 300 : 0;
			request.options["algae-amount"] = large ? 300 : 0;
			request.options["fruit-amount"] = 300;
		}
		Game game(nullptr);
		const auto result = GenerationService().generate(game, request, true);
		assert(result);
		GenerationContext context(request);
		assert(definition.validateWorld(game, context).empty());
		// Saturate the eight-connected grass reached by existing wheat/wood. This is a
		// conservative growth envelope, not a growth-speed simulation: it deliberately ignores
		// fertility so an initially clear route cannot pass merely by waiting longer to clog.
		// Sand town rims keep their interiors out of the flood. Revision 1 fails here because
		// its fords have no permanent approaches through the bank crops.
		const MapGeneration::Torus torus(game.map);
		const auto overgrown = MapGeneration::cropSpreadEnvelope(game.map);
		for (int i : overgrown.visited)
			if (game.map.isResourceAllowed(i % torus.w, i / torus.w, WHEAT))
				game.map.setResource(i % torus.w, i / torus.w, WHEAT, 1);
		assert(MapGeneration::walkFromFirstColony(game.map, request.nbTeams, "the overgrown delta",
												  "over its ford approaches")
				   .error.empty());
		assert(definition.validateWorld(game, context).empty());
		// Fill all surviving grass with stone. Terrain and colony count still match, but the
		// usable-island contract must fail after furnishing, not just pass a terrain flood.
		for (int y = 0; y < game.map.getH(); ++y)
			for (int x = 0; x < game.map.getW(); ++x)
				if (game.map.isResourceAllowed(x, y, STONE))
					game.map.setResource(x, y, STONE, 1);
		assert(!definition.validateWorld(game, context).empty());
	}
	// Restore the smallest layout for request-boundary checks after the large fixture.
	request.wDec = request.hDec = 7;
	request.nbTeams = 4;
	request.options["island-size"] = 32;
	// Too many braids, too many colonies and undersized maps fail before mutating a Game.
	request.options["braid-count"] = 5;
	assert(!validateGenerationRequest(request, definition).empty());
	request.options["braid-count"] = 2;
	request.nbTeams = 5;
	assert(!validateGenerationRequest(request, definition).empty());
	request.nbTeams = 1;
	request.wDec = 6;
	assert(!validateGenerationRequest(request, definition).empty());
}

// These checks exercise the finished world's contract, including destructive edits a
// generation smoke test would miss: saddle preservation and permanent-ridge validation.
inline void breachableHighlandsContracts()
{
	GenerationService service;
	D request;
	request.setMethodDefaults(GeneratorRegistry::builtins().idOf("breachable-highlands"));
	request.seed = 1;
	Game game(nullptr);
	assert(service.generate(game, request));
	GenerationContext context(request);
	const auto &definition = GeneratorRegistry::builtins().at(request.method);
	const auto fertility = Fertility::forMap(game.map, false);
	std::vector<int> saddle;
	int ridge = -1;
	for (int i = 0; i < game.map.getW() * game.map.getH(); ++i)
	{
		const int x = i % game.map.getW(), y = i / game.map.getW();
		const int type = game.map.getResource(x, y).type;
		if (type == WOOD && !fertility.at(x, y))
			saddle.push_back(i);
		if (type == STONE)
			ridge = i;
	}
	assert(saddle.size() >= 12 && ridge >= 0);
	const auto before = MapGeneration::contactMatrix(game.map, request.nbTeams,
													 MapGeneration::StepCosts::walking());
	for (int i : saddle)
		game.map.setNoResource(i % game.map.getW(), i / game.map.getW(), 1);
	assert(definition.validateWorld(game, context).find("saddle") != std::string::npos);
	const auto after = MapGeneration::contactMatrix(game.map, request.nbTeams,
													MapGeneration::StepCosts::walking());
	int bestSaving = 0;
	for (int a = 0; a < request.nbTeams; ++a)
		for (int b = 0; b < request.nbTeams; ++b)
		{
			assert(after.cost[a][b] >= 0 && after.cost[a][b] <= before.cost[a][b]);
			bestSaving = std::max(bestSaving, before.cost[a][b] - after.cost[a][b]);
		}
	assert(bestSaving > 0);
	std::printf("Breachable highlands seed 1: clearing saddles saves up to %d walking steps\n",
				bestSaving);
	for (int i : saddle)
		game.map.setResource(i % game.map.getW(), i / game.map.getW(), WOOD, 1);
	assert(definition.validateWorld(game, context).empty());
	game.map.setNoResource(ridge % game.map.getW(), ridge / game.map.getW(), 1);
	assert(definition.validateWorld(game, context).find("ridge") != std::string::npos);
	// Initially separate deposits are insufficient: removing the sand containment
	// lets future wood growth reach food plots. Keep valid beaches and the original
	// deposits, then require the validator to reject that future growth connection.
	Game uncontained(nullptr);
	assert(service.generate(uncontained, request));
	const MapGeneration::Torus farmTorus{uncontained.map.getW(), uncontained.map.getH()};
	MapGeneration::TerrainSketch terrain(farmTorus.size());
	for (int i = 0; i < farmTorus.size(); ++i)
	{
		const int value = uncontained.map.getUMTerrain(i % farmTorus.w, i / farmTorus.w);
		terrain[i] = value == SAND ? GRASS : value;
	}
	MapGeneration::layBeaches(terrain, farmTorus);
	MapGeneration::writeUndermap(uncontained.map, terrain);
	assert(definition.validateWorld(uncontained, context).find("farm access lane") !=
		   std::string::npos);
	// Abundance changes the farms, never the stone or saddle geometry. Both extremes
	// still have viable starts and pass the generator's topology and dryness checks.
	for (int amount : {0, 300})
	{
		D changed = request;
		for (const char *key : {"wheat-amount", "wood-amount", "algae-amount", "fruit-amount"})
			changed.options[key] = amount;
		Game extreme(nullptr);
		assert(service.generate(extreme, changed));
		for (int i : saddle)
			assert(
				extreme.map.getResource(i % extreme.map.getW(), i / extreme.map.getW()).type ==
				WOOD);
		assert(extreme.map.getResource(ridge % extreme.map.getW(), ridge / extreme.map.getW())
				   .type == STONE);
	}
	D crowded = request;
	crowded.wDec = crowded.hDec = 7;
	Game rejected(nullptr);
	assert(service.generate(rejected, crowded).error == GenerationError::InvalidRequest);
	puts("PASS breachable highlands: dry saddles, real shortcuts, protected ridges, crop "
		 "containment, abundance "
		 "extremes and crowding");
}

inline void generatorContracts()
{
	breachableHighlandsContracts();
	braidedDeltaChecks();
	fortsContracts();
	emojiContracts();
}
} // namespace GeneratorContracts
