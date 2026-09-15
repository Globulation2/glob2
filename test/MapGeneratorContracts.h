// SPDX-License-Identifier: GPL-3.0-or-later
// Contract checks for individual generators: the promises a map makes beyond the framework's
// common structure (its supported shapes, its variants, what its validator refuses), each run
// by MapGeneratorDefaultsTest. Shared-primitive checks live in MapGeneratorToolkitChecks.h and
// MapGeneratorLandscapeChecks.h; a check here is about one landscape's own contract.
#pragma once
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "MapGeneratorFrameworkChecks.h"
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

inline void generatorContracts()
{
	fortsContracts();
	emojiContracts();
}
} // namespace GeneratorContracts
