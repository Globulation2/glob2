// SPDX-License-Identifier: GPL-3.0-or-later
// Contract checks for individual generators: the promises a map makes beyond the framework's
// common structure (its supported shapes, its variants, what its validator refuses), each run
// by MapGeneratorDefaultsTest. Shared-primitive checks live in MapGeneratorToolkitChecks.h and
// MapGeneratorLandscapeChecks.h; a check here is about one landscape's own contract.
#pragma once
#include "BraidedDeltaGenerator.h"
#include "Contact.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "Growth.h"
#include "MapGeneratorFrameworkChecks.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Sketch.h"
#include <cassert>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <set>
#include <string>
#include <tuple>
#include <vector>

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
	for (int character = 1; character <= 23; ++character)
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
	assert(variants.size() == 92);
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
	puts("PASS Emoji: random defaults, all 92 explicit variants, terrain independent of "
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

// Hedgerow's zero-gateway setting must still connect every field, and thicker hedges
// must remain dry at both resource extremes. The production validator checks those
// finished-world invariants; this matrix retains them as regression coverage.
inline void hedgerowContracts()
{
	GenerationService service;
	D request;
	request.setMethodDefaults(GeneratorRegistry::builtins().idOf("hedgerow-country"));
	request.wDec = 7;
	request.hDec = 8;
	request.nbWorkers = 8;
	request.options["existing-gateways"] = 0;
	for (int thickness : {2, 4})
		for (int amount : {0, 300})
		{
			request.seed = 20001;
			request.options["hedge-thickness"] = thickness;
			for (const char *key :
				 {"wheat-amount", "wood-amount", "stone-amount", "fruit-amount"})
				request.options[key] = amount;
			Game plain(nullptr), traced(nullptr);
			const auto before = std::chrono::steady_clock::now();
			assert(service.generate(plain, request));
			const auto middle = std::chrono::steady_clock::now();
			assert(service.generate(traced, request, true));
			const auto after = std::chrono::steady_clock::now();
			// Observations, not timing assertions: telemetry must preserve output, while
			// wall time varies with the machine and other work running on it.
			std::printf("HEDGEROW_TIMING thickness=%d amount=%d off_ms=%.3f on_ms=%.3f\n",
						thickness, amount,
						std::chrono::duration<double, std::milli>(middle - before).count(),
						std::chrono::duration<double, std::milli>(after - middle).count());
			assert(mapFingerprint(plain) == mapFingerprint(traced));
			// Farm shape and starting stock are guarantees, independent of random
			// deposit sprites, team indices, and the ambient abundance slider.
			const auto fertility = Fertility::forMap(plain.map, false);
			std::vector<int> expected;
			for (int team = 0; team < request.nbTeams; ++team)
			{
				std::vector<int> kit(4, 0);
				int renewableWheat = 0, renewableWood = 0;
				const int cx = plain.teams[team]->startPosX + 5;
				const int cy = plain.teams[team]->startPosY + 20;
				for (int dy = -14; dy <= 14; ++dy)
					for (int dx = -14; dx <= 14; ++dx)
					{
						const auto &crop = plain.map.getResource(cx + dx, cy + dy);
						if (crop.type == WHEAT)
						{
							++kit[0];
							kit[1] += crop.amount;
							renewableWheat += fertility.at(cx + dx, cy + dy) > 0;
						}
						if (crop.type == WOOD)
						{
							++kit[2];
							kit[3] += crop.amount;
							renewableWood += fertility.at(cx + dx, cy + dy) > 0;
						}
					}
				assert(kit[0] >= 48 && kit[2] >= 24 && renewableWheat && renewableWood);
				if (team == 0)
					expected = kit;
				assert(kit == expected);
			}
		}
	// Retain two maps where Euclidean-only placement hid a large land advantage.
	// Measure from actual workers on the finished world, independently of the
	// generator's sampled design score. These are seed regressions, not a global
	// promise that every setting gives exactly equal territories.
	for (auto [seed, minimumRatio] : {std::pair{7, 45}, std::pair{19, 60}})
	{
		D fair;
		fair.setMethodDefaults(GeneratorRegistry::builtins().idOf("hedgerow-country"));
		fair.seed = seed;
		Game world(nullptr), observed(nullptr);
		assert(service.generate(world, fair));
		// Exercise telemetry isolation through the nontrivial start-selection search too.
		assert(service.generate(observed, fair, true));
		assert(mapFingerprint(world) == mapFingerprint(observed));
		const MapGeneration::Torus t(world.map);
		const auto workers = MapGeneration::unitTilesByTeam(world.map, fair.nbTeams);
		std::vector<std::vector<int>> walks;
		for (const auto &units : workers)
			walks.push_back(MapGeneration::costsFrom(world.map, t, units,
													 MapGeneration::StepCosts::walking()));
		std::vector<int> territory(fair.nbTeams, 0);
		for (int i = 0; i < t.size(); ++i)
		{
			int best = INT_MAX, owner = -1;
			for (int team = 0; team < fair.nbTeams; ++team)
				if (walks[team][i] >= 0 && walks[team][i] < best)
				{
					best = walks[team][i];
					owner = team;
				}
				else if (walks[team][i] == best)
					owner = -1;
			if (owner >= 0)
				++territory[owner];
		}
		assert(100 * *std::min_element(territory.begin(), territory.end()) >=
			   minimumRatio * *std::max_element(territory.begin(), territory.end()));
	}
	// A wide, strongly warped edge used to enter the enlarged pond's diagonal
	// growth reach. Keep permanent breaches dry even at the largest hedge depth.
	D wide;
	wide.setMethodDefaults(GeneratorRegistry::builtins().idOf("hedgerow-country"));
	wide.seed = 30002;
	wide.wDec = 8;
	wide.hDec = 9;
	wide.nbTeams = 7;
	wide.options["field-size"] = 80;
	wide.options["hedge-thickness"] = 4;
	wide.options["existing-gateways"] = 0;
	wide.options["wooded-boundary-share"] = 100;
	Game wideWorld(nullptr);
	assert(service.generate(wideWorld, wide));
	// Retain the mixed-parameter bulk failure: a compact diagonal hedge was
	// fertile at (179,140). The raster growth-envelope safeguard must also be
	// deterministic when telemetry is enabled through its contraction branch.
	D compact = wide;
	compact.seed = 1012498260;
	compact.hDec = 8;
	compact.nbTeams = 5;
	compact.nbWorkers = 7;
	compact.options["field-size"] = 64;
	compact.options["existing-gateways"] = 50;
	compact.options["wooded-boundary-share"] = 80;
	compact.options["wheat-amount"] = 75;
	compact.options["wood-amount"] = 75;
	compact.options["stone-amount"] = 150;
	compact.options["fruit-amount"] = 150;
	Game compactWorld(nullptr), compactObserved(nullptr);
	assert(service.generate(compactWorld, compact));
	assert(service.generate(compactObserved, compact, true));
	assert(mapFingerprint(compactWorld) == mapFingerprint(compactObserved));
	// Reject the geometric relationship before creating teams, not after a partial map.
	request.wDec = 6;
	Game rejected(nullptr);
	assert(service.generate(rejected, request).error == GenerationError::InvalidRequest);
	assert(rejected.teamsCount() == 0);
	puts("PASS Hedgerow Country: connected dry hedges, resource extremes, telemetry isolation, "
		 "size rejection");
}

// Savannah's terrain is invariant under resource sliders, and its final validator must
// still accept natural growth. Test the real engine instead of simulating a new growth rule.
inline void savannahContracts()
{
	const auto &definition =
		GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("savannah"));
	D request;
	request.setMethodDefaults(definition.legacyId);
	assert(definition.legacyId == 45 && definition.revision == 2);
	assert(request.option("watering-holes") == 1 && request.option("dry-patches") == 8);
	GenerationService service;
	for (auto dimensions : {std::pair{7, 7}, std::pair{7, 8}, std::pair{8, 7}})
		for (int teams : {1, 3, 4})
		{
			D r = request;
			r.wDec = dimensions.first;
			r.hDec = dimensions.second;
			r.nbTeams = teams;
			r.seed = 17;
			Game g(nullptr);
			const auto result = service.generate(g, r);
			if (!result)
				std::fprintf(stderr, "Savannah contract: %s\n", result.diagnostic().c_str());
			assert(result);
		}
	Game zero(nullptr), abundant(nullptr);
	D r = request;
	r.wDec = r.hDec = 7;
	r.seed = 37;
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = 0;
	assert(service.generate(zero, r));
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = control.maximum;
	assert(service.generate(abundant, r));
	for (int y = 0; y < zero.map.getH(); ++y)
		for (int x = 0; x < zero.map.getW(); ++x)
		{
			assert(zero.map.isGrass(x, y) == abundant.map.isGrass(x, y));
			assert(zero.map.isWater(x, y) == abundant.map.isWater(x, y));
			assert(zero.map.isSand(x, y) == abundant.map.isSand(x, y));
		}
	GenerationContext context(r);
	setSyncRandSeed(3821);
	// 4096 full-map growth calls exercise many visits per deposit without an AI clearing
	// anything; the structural validator proves the bound beyond this finite stress test.
	for (int tick = 0; tick < 4096; ++tick)
		abundant.map.growResources();
	const auto growthError = definition.validateWorld(abundant, context);
	if (!growthError.empty())
		std::fprintf(stderr, "Savannah growth: %s\n", growthError.c_str());
	assert(growthError.empty());
	for (auto dimensions : {std::pair{6, 7}, std::pair{7, 9}, std::pair{9, 7}})
	{
		r.wDec = dimensions.first;
		r.hDec = dimensions.second;
		Game g(nullptr);
		assert(service.generate(g, r).error == GenerationError::InvalidRequest);
		assert(g.teamsCount() == 0);
	}
	r.wDec = r.hDec = 7;
	r.nbTeams = 5;
	Game crowded(nullptr);
	assert(service.generate(crowded, r).error == GenerationError::InvalidRequest);
	// Retained crowded seed: finite random pond darts used to miss its narrow legal gap.
	r = request;
	r.wDec = r.hDec = 7;
	r.seed = 1001;
	Game narrowGap(nullptr);
	assert(service.generate(narrowGap, r));
	puts("PASS Savannah: envelope, resource-independent terrain, contained unattended growth");
}

inline void locustFoodChecks()
{
	D request;
	request.setMethodDefaults(GeneratorRegistry::builtins().idOf("locust"));
	request.wDec = request.hDec = 8;
	request.nbTeams = 2;
	request.seed = 7;
	Game game(nullptr);
	assert(GenerationService().generate(game, request));
	Map &map = game.map;
	// Every wheat tile starts with three to five harvests.
	for (int i = 0; i < map.getW() * map.getH(); ++i)
		if (map.getResource(i).type == WHEAT)
			assert(map.getResource(i).amount >= 3 && map.getResource(i).amount <= 5);
	// The stock cap never refills a deposit or consumes RNG, even with a larger second cap. Cap to
	// one harvest here so the regrowth check below can harvest every other tile out in one step.
	const auto beforeCap = syncRandEngine();
	const auto capped = MapGeneration::capResourceStock(map, WHEAT, 1);
	const auto unchanged = MapGeneration::capResourceStock(map, WHEAT, 3);
	assert(capped.tiles > 96 && capped.amount == capped.tiles);
	assert(unchanged.amount == capped.amount && syncRandEngine() == beforeCap);
	bool rejected = false;
	try
	{
		MapGeneration::capResourceStock(map, WHEAT, 0);
	}
	catch (const GenerationFailure &)
	{
		rejected = true;
	}
	assert(rejected);
	assert(
		MapGeneration::startingAccessFailure(map, 2, {{WHEAT, 24, "wheat"}, {WOOD, 32, "wood"}})
			.empty());
	// No fruit exists here: a required absent supply must fail, as must an impossible room budget.
	assert(!MapGeneration::startingAccessFailure(map, 2, {{CHERRY, 24, "cherries"}}).empty());
	assert(
		!MapGeneration::startingAccessFailure(map, 2, {}, map.getW() * map.getH(), 1).empty());
	std::vector<unsigned char> food(map.getW() * map.getH(), 0);
	int harvested = 0;
	for (int i = 0; i < int(food.size()); ++i)
		if (map.getResource(i).type == WHEAT)
		{
			assert(map.getResource(i).amount == 1);
			if (++harvested % 2)
				map.decResource(i % map.getW(), i / map.getW());
			else
				food[i] = 1;
		}
	assert(harvested > 96);
	const auto savedRandom = syncRandEngine();
	syncRandEngine().seed(19);
	for (int tick = 0; tick < 2048; ++tick)
		map.growResources();
	syncRandEngine() = savedRandom;
	for (int i = 0; i < int(food.size()); ++i)
	{
		assert((map.getResource(i).type == WHEAT) == bool(food[i]));
		if (food[i])
			assert(map.getResource(i).amount == 1);
	}
	// Retained compact-rectangle failures: dry rations and quarry frontage can both
	// need several pockets. Keep the full kit rather than rejecting those otherwise viable starts.
	D compact = request;
	compact.hDec = 7;
	compact.nbTeams = 5;
	compact.options["home-size"] = 30;
	compact.options["lakes"] = 0;
	for (unsigned seed : {201u, 202u, 203u, 204u})
	{
		compact.seed = seed;
		Game repaired(nullptr);
		assert(GenerationService().generate(repaired, compact));
	}
	puts("PASS Locust: three to five harvests per wheat tile; harvested food never regrows");
}

// The three landscapes rebuilt on 2026-09-16 (revision 2): a shared envelope of shapes and colony
// counts, resource extremes, unattended growth that must stay inside every sealed garden, plot and
// glacis, and deliberate damage each validator has to notice.
inline void rebuiltLandscapeContracts()
{
	GenerationService service;
	const auto generate = [&](const char *id, int wDec, int hDec, int teams, std::uint32_t seed,
							  const std::vector<std::pair<const char *, int>> &options, Game &game)
	{
		D r;
		r.setMethodDefaults(GeneratorRegistry::builtins().idOf(id));
		r.wDec = wDec;
		r.hDec = hDec;
		r.nbTeams = teams;
		r.seed = seed;
		for (const auto &[key, value] : options)
			r.options[key] = value;
		const auto result = service.generate(game, r);
		if (!result)
			std::fprintf(stderr, "%s contract (%d x %d, %d colonies): %s\n", id, 1 << wDec,
						 1 << hDec, teams, result.diagnostic().c_str());
		return std::pair{r, bool(result)};
	};
	const auto amounts = [](int amount)
	{
		std::vector<std::pair<const char *, int>> all;
		for (const char *key :
			 {"wheat-amount", "wood-amount", "stone-amount", "algae-amount", "fruit-amount"})
			all.push_back({key, amount});
		return all;
	};
	for (const char *id : {"glacis", "allotments", "caravanserai"})
	{
		const auto &definition = GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf(id));
		// The Glacis went to revision 3 when its five-pointed forts were fixed (#331).
		assert(definition.revision == (std::string(id) == "glacis" ? 3 : 2));
		// The supported envelope: square and rectangular maps, one colony to a crowd.
		for (const auto &[w, h, teams] :
			 {std::tuple{8, 8, 1}, std::tuple{8, 8, 4}, std::tuple{8, 8, 6}, std::tuple{9, 8, 4},
			  std::tuple{8, 9, 8}, std::tuple{7, 7, 1}})
		{
			Game game(nullptr);
			assert(generate(id, w, h, teams, 11, {}, game).second);
		}
		// Resource extremes, then growth left alone on the abundant map.
		Game scarce(nullptr), abundant(nullptr);
		assert(generate(id, 7, 7, 2, 23, amounts(0), scarce).second);
		const auto [request, ok] = generate(id, 7, 7, 2, 23, amounts(300), abundant);
		assert(ok);
		setSyncRandSeed(4409);
		for (int tick = 0; tick < 2048; ++tick)
			abundant.map.growResources();
		GenerationContext check(request);
		const std::string grown = definition.validateWorld(abundant, check);
		if (!grown.empty())
			std::fprintf(stderr, "%s growth: %s\n", id, grown.c_str());
		assert(grown.empty());
		// A map far too small for its homes is refused before anything is built.
		Game crowded(nullptr);
		D tiny = request;
		tiny.wDec = tiny.hDec = 6;
		tiny.nbTeams = 4;
		assert(service.generate(crowded, tiny).error == GenerationError::InvalidRequest);
	}
	{
		// With no stone to scale and no mesas, the only stone is a designed wall: remove one and
		// the validator must notice, for the forts and the caravanserais alike.
		for (const auto &[id, options] :
			 {std::pair{"glacis", std::vector<std::pair<const char *, int>>{{"stone-amount", 0}}},
			  std::pair{"caravanserai",
						std::vector<std::pair<const char *, int>>{{"stone-amount", 0}, {"desert", 0}}}})
		{
			Game game(nullptr);
			const auto [request, ok] = generate(id, 8, 8, 4, 31, options, game);
			assert(ok);
			const auto &definition =
				GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf(id));
			GenerationContext check(request);
			assert(definition.validateWorld(game, check).empty());
			bool removed = false;
			for (int i = 0; i < 256 * 256 && !removed; ++i)
				if (game.map.getResource(i).type == STONE)
				{
					game.map.setNoResource(i % 256, i / 256, 1);
					removed = true;
				}
			assert(removed);
			assert(definition.validateWorld(game, check).find("wall") != std::string::npos);
		}
		// A glacis must stay bare: a crop planted on one is refused.
		Game game(nullptr);
		const auto [request, ok] = generate("glacis", 8, 8, 2, 31, {}, game);
		assert(ok);
		const auto &definition =
			GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("glacis"));
		GenerationContext check(request);
		// The glacis ring lies some 32 to 46 tiles out from the fort's middle: try tiles out there
		// until one is refused as glacis, putting back each one that was not.
		const Team *team = game.teams[0];
		bool planted = false;
		int tries = 0;
		for (int direction = 0; direction < 16 && !planted && tries < 48; ++direction)
			for (int d = 34; d <= 46 && !planted && tries < 48; d += 3)
			{
				const int x = (team->startPosX + int(std::lround(d * std::cos(direction * kPi / 8))) + 256) % 256;
				const int y = (team->startPosY + int(std::lround(d * std::sin(direction * kPi / 8))) + 256) % 256;
				if (!game.map.isGrass(x, y) || game.map.isResource(x, y) || game.map.getBuilding(x, y) != NOGBID)
					continue;
				++tries;
				game.map.setResource(x, y, WHEAT, 1);
				if (definition.validateWorld(game, check).find("glacis") != std::string::npos)
					planted = true;
				else
					game.map.setNoResource(x, y, 1);
			}
		assert(planted);
	}
	{
		// A village with its plots cleared has no wheat to walk to.
		Game game(nullptr);
		const auto [request, ok] = generate("allotments", 8, 8, 4, 31, {}, game);
		assert(ok);
		const auto &definition =
			GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("allotments"));
		GenerationContext check(request);
		assert(definition.validateWorld(game, check).empty());
		const Team *team = game.teams[0];
		for (int dy = -40; dy <= 40; ++dy)
			for (int dx = -40; dx <= 40; ++dx)
			{
				const int x = (team->startPosX + dx + 256) % 256, y = (team->startPosY + dy + 256) % 256;
				if (game.map.getResource(x, y).type == WHEAT)
					game.map.setNoResource(x, y, 1);
			}
		assert(!definition.validateWorld(game, check).empty());
	}
	puts("PASS The Glacis, Allotments, Caravanserai: envelope, resource extremes, contained growth, "
		 "walls, bare glacis and village crops");
}

// Honeycomb isle: its envelope (every shape with the colonies it promises, the refusal only a
// crowded smallest map gets), design variety, resource extremes, unattended growth that stays inside
// every street-sealed block, a design cache that never changes a map or its telemetry, and a colony
// whose starter wheat is taken away failing the validator.
inline void honeycombIsleContracts()
{
	const auto &definition =
		GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("honeycomb-isle"));
	assert(definition.legacyId == 53 && definition.revision == 1);
	D request;
	request.setMethodDefaults(definition.legacyId);
	assert(request.option("block-shape") == 1 && request.option("river-width") == 12 &&
		   request.option("blocks-per-colony") == 11 && request.option("crater-gardens") == 1);
	GenerationService service;
	const auto make = [&](int wDec, int hDec, int teams, std::uint32_t seed)
	{
		D r = request;
		r.wDec = wDec;
		r.hDec = hDec;
		r.nbTeams = teams;
		r.seed = seed;
		return r;
	};
	for (auto dimensions : {std::pair{7, 7}, std::pair{7, 8}, std::pair{8, 8}, std::pair{9, 8}})
		for (int teams : {2, 4, 8})
		{
			if (dimensions == std::pair{7, 7} && teams == 8)
				continue;
			Game g(nullptr);
			const auto result = service.generate(g, make(dimensions.first, dimensions.second, teams, 5));
			if (!result)
				std::fprintf(stderr, "Honeycomb isle contract (%d x %d, %d colonies): %s\n",
							 1 << dimensions.first, 1 << dimensions.second, teams,
							 result.diagnostic().c_str());
			assert(result);
		}
	{
		// Eight colonies never fit on the smallest map, even with the city squeezed.
		Game g(nullptr);
		assert(service.generate(g, make(7, 7, 8, 5)).error == GenerationError::InvalidRequest);
		assert(g.teamsCount() == 0);
	}
	std::set<std::string> homeDesigns, landmarks;
	for (unsigned seed = 1; seed <= 12; ++seed)
	{
		Game g(nullptr);
		const auto result = service.generate(g, make(8, 8, 4, seed), true);
		assert(result);
		for (const auto &record : result.telemetry.records())
		{
			if (record.key == "honeycomb-isle.home-design")
				homeDesigns.insert(std::get<std::string>(record.value));
			if (record.key == "honeycomb-isle.landmark-design")
				landmarks.insert(std::get<std::string>(record.value));
		}
	}
	assert(homeDesigns.size() == 3 && landmarks.size() == 3);
	{
		// The design is cached between the request check, generation and validation. Generating a map,
		// another map, then the first again must give the same map and the same telemetry.
		Game first(nullptr), other(nullptr), again(nullptr);
		const auto a = service.generate(first, make(8, 8, 4, 21), true);
		assert(service.generate(other, make(8, 8, 6, 22), true));
		const auto b = service.generate(again, make(8, 8, 4, 21), true);
		assert(a && b);
		assert(mapFingerprint(first) == mapFingerprint(again));
		assert(a.telemetry.records() == b.telemetry.records());
	}
	D r = make(7, 8, 4, 37);
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = 0;
	{
		Game zero(nullptr);
		assert(service.generate(zero, r));
	}
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = control.maximum;
	Game abundant(nullptr);
	assert(service.generate(abundant, r));
	GenerationContext context(r);
	setSyncRandSeed(4211);
	// Full-map growth with nobody harvesting: every field, garden and ruin near water grows, and
	// the paved streets must keep each inside its own block.
	for (int tick = 0; tick < 4096; ++tick)
		abundant.map.growResources();
	const std::string growthError = definition.validateWorld(abundant, context);
	if (!growthError.empty())
		std::fprintf(stderr, "Honeycomb isle growth: %s\n", growthError.c_str());
	assert(growthError.empty());
	{
		// A colony whose starter wheat is cleared has none within reach.
		Game game(nullptr);
		const D damaged = make(8, 8, 4, 41);
		assert(service.generate(game, damaged));
		GenerationContext check(damaged);
		assert(definition.validateWorld(game, check).empty());
		const Team *team = game.teams[0];
		for (int dy = -30; dy <= 30; ++dy)
			for (int dx = -30; dx <= 30; ++dx)
			{
				const int x = (team->startPosX + dx + 256) % 256, y = (team->startPosY + dy + 256) % 256;
				if (game.map.getResource(x, y).type == WHEAT)
					game.map.setNoResource(x, y, 1);
			}
		assert(!definition.validateWorld(game, check).empty());
	}
	puts("PASS Honeycomb isle: envelope and refusal, design variety, resource extremes, sealed "
		 "growth, cached designs, starter wheat");
}

inline void karstTowersContracts()
{
	const auto &definition =
		GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("karst-towers"));
	assert(definition.legacyId == 54 && definition.revision == 2);
	D request;
	request.setMethodDefaults(definition.legacyId);
	assert(request.option("tower-spacing") == 16 && request.option("river-width") == 7 &&
		   request.option("paddy-depth") == 24 && request.option("flooded-terraces") == 88 &&
		   request.option("lakes") == 1);
	GenerationService service;
	const auto make = [&](int wDec, int hDec, int teams, std::uint32_t seed)
	{
		D r = request;
		r.wDec = wDec;
		r.hDec = hDec;
		r.nbTeams = teams;
		r.seed = seed;
		return r;
	};
	for (auto dimensions : {std::pair{7, 7}, std::pair{7, 8}, std::pair{8, 8}, std::pair{9, 8}})
		for (int teams : {2, 4, 8})
		{
			if (dimensions.first == 7 && teams == 8)
				continue;
			Game g(nullptr);
			const auto result = service.generate(g, make(dimensions.first, dimensions.second, teams, 5));
			if (!result)
				std::fprintf(stderr, "Karst towers contract (%d x %d, %d colonies): %s\n",
							 1 << dimensions.first, 1 << dimensions.second, teams,
							 result.diagnostic().c_str());
			assert(result);
		}
	{
		// Eight colonies never fit on the smallest map, even with the homes shrunk and the rivers narrowed.
		Game g(nullptr);
		assert(service.generate(g, make(7, 7, 8, 5)).error == GenerationError::InvalidRequest);
		assert(g.teamsCount() == 0);
	}
	{
		// Big homes and the widest river on a crowded map shrink rather than refuse.
		D crowded = make(8, 8, 8, 1168);
		crowded.options["home-size"] = 22;
		crowded.options["river-width"] = 16;
		Game g(nullptr);
		assert(service.generate(g, crowded));
	}
	std::set<std::string> homeDesigns;
	for (unsigned seed = 1; seed <= 16; ++seed)
	{
		Game g(nullptr);
		const auto result = service.generate(g, make(8, 8, 4, seed), true);
		assert(result);
		for (const auto &record : result.telemetry.records())
			if (record.key == "karst.home.design")
				homeDesigns.insert(std::get<std::string>(record.value));
	}
	assert(homeDesigns.size() == 4);
	D r = make(8, 8, 4, 37);
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = 0;
	{
		Game zero(nullptr);
		assert(service.generate(zero, r));
	}
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = control.maximum;
	Game abundant(nullptr);
	assert(service.generate(abundant, r));
	GenerationContext context(r);
	setSyncRandSeed(4211);
	// Full-map growth with nobody harvesting: the terraces, home paddies and lake fields beside all that
	// water grow, and their bunds must keep every crop inside its own field.
	for (int tick = 0; tick < 4096; ++tick)
		abundant.map.growResources();
	const std::string growthError = definition.validateWorld(abundant, context);
	if (!growthError.empty())
		std::fprintf(stderr, "Karst towers growth: %s\n", growthError.c_str());
	assert(growthError.empty());
	{
		// Stone laid round a home where its ring stands closes the gates, which the validator refuses. A
		// round home's middle lies 6.5 tiles right of and 2 below its swarm's corner, and the ring's gates
		// open 16 to 21 tiles from it at the default home size.
		Game game(nullptr);
		const D damaged = make(8, 8, 4, 41);
		assert(service.generate(game, damaged));
		GenerationContext check(damaged);
		assert(definition.validateWorld(game, check).empty());
		const Team *team = game.teams[1];
		for (int dy = -22; dy <= 22; ++dy)
			for (int dx = -22; dx <= 22; ++dx)
			{
				const double d = std::hypot(dx, dy);
				if (d < 16 || d > 21)
					continue;
				const int x = (team->startPosX + 6 + dx + 256) % 256, y = (team->startPosY + 2 + dy + 256) % 256;
				if (game.map.isGrass(x, y) && !game.map.isResource(x, y))
					game.map.setResource(x, y, STONE, 1);
			}
		assert(!definition.validateWorld(game, check).empty());
	}
	puts("PASS Karst towers: envelope and refusal, crowded retry, home designs, resource extremes, "
		 "sealed growth, closed gates");
}

inline void bajadaContracts()
{
	const auto &definition = GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("bajada"));
	assert(definition.legacyId == 55 && definition.revision == 1);
	D request;
	request.setMethodDefaults(definition.legacyId);
	assert(request.option("range-spacing") == 128 && request.option("passes") == 2 &&
		   request.option("playa") == 80 && request.option("home-design") == 0);
	GenerationService service;
	const auto make = [&](int wDec, int hDec, int teams, std::uint32_t seed)
	{
		D r = request;
		r.wDec = wDec;
		r.hDec = hDec;
		r.nbTeams = teams;
		r.seed = seed;
		return r;
	};
	for (auto dimensions : {std::pair{7, 7}, std::pair{7, 8}, std::pair{8, 7}, std::pair{8, 8}, std::pair{9, 9}})
		for (int teams : {1, 2, 4, 6, 8})
		{
			if (dimensions.first == 7 && teams > 4)
				continue;
			Game g(nullptr);
			const auto result = service.generate(g, make(dimensions.first, dimensions.second, teams, 7));
			if (!result)
				std::fprintf(stderr, "Bajada contract (%d x %d, %d colonies): %s\n", 1 << dimensions.first,
							 1 << dimensions.second, teams, result.diagnostic().c_str());
			assert(result);
		}
	{
		// Six colonies crowd the homes of a 128-tile map into one another.
		Game g(nullptr);
		assert(service.generate(g, make(7, 7, 6, 7)).error == GenerationError::InvalidRequest);
		assert(g.teamsCount() == 0);
	}
	// Every home design comes up at random, and each can be pinned.
	std::set<std::string> designs;
	for (unsigned seed = 1; seed <= 16; ++seed)
	{
		Game g(nullptr);
		const auto result = service.generate(g, make(8, 8, 4, seed), true);
		assert(result);
		for (const auto &record : result.telemetry.records())
			if (record.key == "bajada.home.design")
				designs.insert(std::get<std::string>(record.value));
	}
	assert(designs.size() == 3);
	for (int design = 1; design <= 3; ++design)
	{
		D pinned = make(8, 8, 4, 11);
		pinned.options["home-design"] = design;
		Game g(nullptr);
		assert(service.generate(g, pinned));
	}
	D r = make(8, 8, 4, 37);
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = 0;
	{
		// No ambient stone: every stone on the map is a designed range or spur, and taking one away
		// is refused.
		Game zero(nullptr);
		assert(service.generate(zero, r));
		GenerationContext check(r);
		assert(definition.validateWorld(zero, check).empty());
		bool damaged = false;
		for (int i = 0; i < 256 * 256 && !damaged; ++i)
			if (zero.map.isResource(i % 256, i / 256) && zero.map.getResource(i % 256, i / 256).type == STONE)
			{
				zero.map.getResource(i % 256, i / 256).clear();
				damaged = true;
			}
		assert(damaged && !definition.validateWorld(zero, check).empty());
	}
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = control.maximum;
	Game abundant(nullptr);
	assert(service.generate(abundant, r));
	GenerationContext context(r);
	setSyncRandSeed(4211);
	// Full-map growth with nobody harvesting: the fans and meadows grow, and every town's ring must
	// keep its crops out.
	for (int tick = 0; tick < 4096; ++tick)
		abundant.map.growResources();
	const std::string growthError = definition.validateWorld(abundant, context);
	if (!growthError.empty())
		std::fprintf(stderr, "Bajada growth: %s\n", growthError.c_str());
	assert(growthError.empty());
	{
		// A crop planted in a town is refused.
		Game game(nullptr);
		const D planted = make(8, 8, 4, 41);
		assert(service.generate(game, planted));
		GenerationContext check(planted);
		assert(definition.validateWorld(game, check).empty());
		const Team *team = game.teams[2];
		bool sown = false;
		// A ring of wheat four to five tiles from the swarm's middle, which lies inside the town's ring.
		for (int dy = -5; dy <= 5; ++dy)
			for (int dx = -5; dx <= 5; ++dx)
			{
				const int x = (team->startPosX + 2 + dx + 256) % 256, y = (team->startPosY + 2 + dy + 256) % 256;
				if (std::max(std::abs(dx), std::abs(dy)) >= 4 && game.map.isResourceAllowed(x, y, WHEAT) &&
					game.map.isFreeForGroundUnit(x, y, false, 0))
				{
					game.map.setResource(x, y, WHEAT, 1);
					sown = true;
				}
			}
		assert(sown && !definition.validateWorld(game, check).empty());
	}
	puts("PASS Bajada: envelope and refusal, home designs, resource extremes, designed stone, sealed "
		 "towns under growth, crops refused in towns");
}

inline void evenGroundContracts()
{
	const auto &definition =
		GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("even-ground"));
	assert(definition.legacyId == 60 && definition.revision == 2);
	D request;
	request.setMethodDefaults(definition.legacyId);
	assert(request.option("water-share") == 10 && request.option("balance") == 70 &&
		   request.option("passes") == 40 && request.option("effort") == 1);
	GenerationService service;
	const auto make = [&](int wDec, int hDec, int teams, std::uint32_t seed)
	{
		D r = request;
		r.wDec = wDec;
		r.hDec = hDec;
		r.nbTeams = teams;
		r.seed = seed;
		return r;
	};
	// The shapes the search supports, including the long thin ones: the lattice sizes its cells off
	// the short side as well as the long one so that a 64 by 512 map is still searched in two
	// dimensions rather than refused.
	for (auto dimensions : {std::pair{7, 7}, std::pair{8, 8}, std::pair{6, 9}, std::pair{9, 6}})
		for (int teams : {1, 2, 5, 8})
		{
			Game g(nullptr);
			const auto result = service.generate(g, make(dimensions.first, dimensions.second, teams, 7));
			if (!result)
				std::fprintf(stderr, "Even Ground contract (%d x %d, %d colonies): %s\n",
							 1 << dimensions.first, 1 << dimensions.second, teams,
							 result.diagnostic().c_str());
			assert(result);
		}
	{
		// A 64-tile map has too few cells to balance eight colonies over, and says so up front.
		Game g(nullptr);
		assert(service.generate(g, make(6, 6, 8, 7)).error == GenerationError::InvalidRequest);
		assert(g.teamsCount() == 0);
	}
	// The water is a budget the search arranges rather than invents: more of it asked for is more of
	// it on the finished map, every time. The painted share is below the requested one because the
	// beaches take a tile from each bank, so this is an ordering, not an identity. The top of the
	// range is 40: past that the map drowns - a fifth of seeds at 60 had a colony with no wood in
	// reach or too little ground to build on - so the control stops where the map still is one.
	int previous = -1;
	for (int share : {0, 10, 25, 40})
	{
		D r = make(8, 8, 4, 23);
		r.options["water-share"] = share;
		Game g(nullptr);
		assert(service.generate(g, r));
		int water = 0;
		for (int y = 0; y < 256; ++y)
			for (int x = 0; x < 256; ++x)
				water += g.map.isWater(x, y);
		assert(water > previous);
		previous = water;
	}
	// Balance is how much search the stock pass gets, so it has to show in what that pass achieved:
	// at zero the crops sit where they were dealt, and at full the spread between the colonies'
	// catchments is a fraction of that.
	const auto spreadAt = [&](int balance)
	{
		D r = make(8, 8, 4, 23);
		r.options["balance"] = balance;
		Game g(nullptr);
		const auto result = service.generate(g, r, true);
		assert(result);
		double before = -1, after = -1, searchedCost = NAN, finalCost = NAN, fieldsCost = NAN;
		for (const auto &record : result.telemetry.records())
		{
			if (record.key == "even-ground.stock.fields.weighted")
				fieldsCost = std::get<double>(record.value);
			if (record.key == "even-ground.stock.cost-after")
				searchedCost = std::get<double>(record.value);
			if (record.key == "even-ground.stock.total")
				finalCost = std::get<double>(record.value);
			if (record.key == "even-ground.stock.spread-before")
				before = std::get<double>(record.value);
			if (record.key == "even-ground.stock.spread-after")
				after = std::get<double>(record.value);
		}
		assert(before >= 0 && after >= 0);
		// Incrementally scored swaps and the full rescan of the best arrangement must agree.
		assert(fieldsCost > 0); // this seed exercises the optional clustering term
		assert(std::abs(searchedCost - finalCost) < 1e-9);
		return std::pair{before, after};
	};
	const auto unsolved = spreadAt(0), solved = spreadAt(100);
	assert(unsolved.second == unsolved.first); // no moves: nothing moved
	assert(solved.second < solved.first / 4);  // searched: the spread is a fraction of what it was
	{
		// The map promises every colony a crop within reach, and refuses a world where that has been
		// taken away.
		D r = make(8, 8, 4, 23);
		Game g(nullptr);
		assert(service.generate(g, r));
		GenerationContext check(r);
		assert(definition.validateWorld(g, check).empty());
		for (int y = 0; y < 256; ++y)
			for (int x = 0; x < 256; ++x)
				if (g.map.isResource(x, y) && g.map.getResource(x, y).type == WHEAT)
					g.map.getResource(x, y).clear();
		assert(!definition.validateWorld(g, check).empty());
	}
	puts("PASS Even Ground: envelope including thin maps and refusal, water budget ordering, "
		 "balance buys a measurably smaller catchment spread, crop reach enforced");
}

inline void marchlandContracts()
{
	const auto &definition =
		GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("marchland"));
	assert(definition.legacyId == 61 && definition.revision == 2);
	D request;
	request.setMethodDefaults(definition.legacyId);
	assert(request.option("prizes") == 6 && request.option("march") == 16 &&
		   request.option("levelling") == 100 && request.option("lakes") == 10);
	GenerationService service;
	const auto make = [&](int wDec, int hDec, int teams, std::uint32_t seed)
	{
		D r = request;
		r.wDec = wDec;
		r.hDec = hDec;
		r.nbTeams = teams;
		r.seed = seed;
		return r;
	};
	for (auto dimensions : {std::pair{8, 8}, std::pair{9, 9}, std::pair{8, 9}})
		for (int teams : {2, 5, 8})
		{
			Game g(nullptr);
			const auto result = service.generate(g, make(dimensions.first, dimensions.second, teams, 31001));
			if (!result)
				std::fprintf(stderr, "Marchland contract (%d x %d, %d colonies): %s\n",
							 1 << dimensions.first, 1 << dimensions.second, teams,
							 result.diagnostic().c_str());
			assert(result);
		}
	// The 64-tile side used to invert the prize-gap clamp bounds (20 > 16).
	for (auto dimensions : {std::pair{6, 9}, std::pair{9, 6}})
	{
		Game g(nullptr);
		assert(service.generate(g, make(dimensions.first, dimensions.second, 6, 31001)));
	}
	// A long map needs a ring of colonies rather than a line of them, and gets one.
	for (auto dimensions : {std::pair{7, 9}, std::pair{9, 7}})
		for (int teams : {8, 12})
		{
			Game g(nullptr);
			assert(service.generate(g, make(dimensions.first, dimensions.second, teams, 31001)));
		}
	{
		// A rope needs two ends, a homeland needs room, and a long map needs enough colonies to
		// ring it: all three are refused up front rather than failing seed after seed.
		Game solo(nullptr);
		assert(service.generate(solo, make(8, 8, 1, 7)).error == GenerationError::InvalidRequest);
		assert(solo.teamsCount() == 0);
		Game crowded(nullptr);
		assert(service.generate(crowded, make(7, 7, 12, 7)).error ==
			   GenerationError::InvalidRequest);
		Game strung(nullptr);
		assert(service.generate(strung, make(7, 9, 4, 7)).error ==
			   GenerationError::InvalidRequest);
	}
	// A river is character, not structure: some seeds cut one and some do not, and a seed that does
	// is still a country every colony can cross. The bed is drawn and only its placement scored, so
	// what is checked here is the placement's two promises - that it keeps out of every town, and
	// that it is forded more often than bare connectivity would need. Crossed only where it must
	// be, the bed walls the march off and the rope stops being contestable, which is the failure
	// this map's own rope check caught at 512x512.
	{
		int withRiver = 0, without = 0;
		for (std::uint32_t seed : {9002u, 9009u, 9013u, 9016u, 9024u, 9001u, 9006u, 9012u})
		{
			Game g(nullptr);
			const auto result = service.generate(g, make(8, 8, 4, seed), true);
			assert(result);
			// Counts are recorded as integers and residuals as doubles, so read either.
			const auto number = [](const auto &record)
			{
				return std::holds_alternative<double>(record.value)
						   ? std::get<double>(record.value)
						   : double(std::get<std::int64_t>(record.value));
			};
			double tiles = -1, fords = -1, town = -1;
			for (const auto &record : result.telemetry.records())
			{
				if (record.key == "marchland.river.tiles")
					tiles = number(record);
				if (record.key == "marchland.river.fords")
					fords = number(record);
				if (record.key == "marchland.river.bed.town.residual")
					town = number(record);
			}
			if (tiles < 0)
			{
				++without;
				assert(fords < 0); // nothing is forded where nothing was cut
				continue;
			}
			++withRiver;
			assert(tiles > 0 && town == 0 && fords >= 4);
		}
		// Both outcomes happen, so the emphasis is genuinely a draw and not a constant.
		assert(withRiver > 0 && without > 0);
	}
	// Fruit grows on the rope and nowhere else, which is what makes the rope worth pulling, and
	// every colony's own quarry and lake are guaranteed whatever the sliders say.
	{
		D bare = make(8, 8, 4, 401);
		for (const auto &control : definition.controls)
			if (control.group == ControlGroup::Resources)
				bare.options[control.id] = 0;
		Game g(nullptr);
		assert(service.generate(g, bare));
		GenerationContext check(bare);
		assert(definition.validateWorld(g, check).empty());
		int fruit = 0, water = 0;
		for (int i = 0; i < 256 * 256; ++i)
		{
			const int type = g.map.getResource(i % 256, i / 256).type;
			fruit += type >= CHERRY && type < CHERRY + 3;
			water += g.map.isWater(i % 256, i / 256);
		}
		assert(fruit > 0 && water > 0);
		// Take the fruit away and the map is no longer a marchland; the validator must say so.
		for (int i = 0; i < 256 * 256; ++i)
		{
			const int type = g.map.getResource(i % 256, i / 256).type;
			if (type >= CHERRY && type < CHERRY + 3)
				g.map.setNoResource(i % 256, i / 256, 1);
		}
		assert(!definition.validateWorld(g, check).empty());
	}
	// Levelling is the map's own argument, so it has to be worth something measurable: the search
	// must cut the spread of the colonies' walks to the rope to a fraction of what dealing the
	// prizes at random leaves. Both figures are recorded for every map this generator makes.
	const auto rope = [&](int levelling)
	{
		D r = make(8, 8, 4, 401);
		r.options["levelling"] = levelling;
		Game g(nullptr);
		const auto result = service.generate(g, r, true);
		assert(result);
		int dealt = -1, solved = -1;
		for (const auto &record : result.telemetry.records())
		{
			if (record.key == "marchland.rope.share-dealt")
				dealt = int(std::get<std::int64_t>(record.value));
			if (record.key == "marchland.rope.share-solved")
				solved = int(std::get<std::int64_t>(record.value));
		}
		assert(dealt >= 0 && solved >= 0);
		return std::pair{dealt, solved};
	};
	const auto unsolved = rope(0), solved = rope(100);
	assert(unsolved.second == unsolved.first); // no moves: the rope is where chance left it
	assert(solved.second * 4 < solved.first);  // searched: a fraction of the spread it started with
	puts("PASS Marchland: envelope and refusals, fruit only on the rope, guaranteed home lake and quarry, "
		 "levelling measurably shares the rope out, rivers drawn on some seeds and forded on all "
		 "of them");
}

inline void combContracts()
{
	const int method = GeneratorRegistry::builtins().idOf("comb");
	const auto &definition = GeneratorRegistry::builtins().at(method);
	D request;
	request.setMethodDefaults(method);
	request.wDec = request.hDec = 8;
	request.nbTeams = 4;
	request.seed = 23;
	std::set<std::uint64_t> shapes;
	for (int count : {2, 3, 4})
	{
		request.options["peninsulas"] = count;
		Game world(nullptr);
		auto result = GenerationService().generate(world, request, true);
		assert(result);
		shapes.insert(mapFingerprint(world));
		bool counted = false;
		for (const auto &record : result.telemetry.records())
			if (record.key == "comb.peninsulas.actual")
			{
				counted = true;
				assert(std::get<std::int64_t>(record.value) == 2 * count);
			}
		assert(counted);
		GenerationContext context(request);
		assert(definition.validateWorld(world, context).empty());
	}
	assert(shapes.size() == 3);
	puts("PASS Comb: distinct peninsula counts, finished-world contracts and telemetry");
}

inline void encircledKingdomContracts()
{
	D request;
	request.setMethodDefaults(GeneratorRegistry::builtins().idOf("encircled-kingdom"));
	const auto &definition = GeneratorRegistry::builtins().at(request.method);
	for (int teams : {1, 2, 13})
	{
		request.nbTeams = teams;
		assert(!definition.validateRequest(request).empty());
	}
	request.nbTeams = 7;
	assert(!definition.validateRequest(request).empty());
	request.nbTeams = 4;
	request.wDec = 7;
	assert(!definition.validateRequest(request).empty());
	request.wDec = 8;
	for (int teams = 3; teams <= 12; ++teams)
		for (int plan = 1; plan <= 3; ++plan)
		{
			request.nbTeams = teams;
			request.wDec = teams >= 7 ? 9 : 8;
			request.hDec = 8;
			request.seed = 17 + teams;
			request.options["fortress-plan"] = plan;
			request.nbWorkers = teams % 2 ? 1 : 8;
			for (const char *key :
				 {"wheat-amount", "wood-amount", "stone-amount", "algae-amount", "fruit-amount"})
				request.options[key] = plan == 1 ? 0 : plan == 2 ? 300 : 100;
			request.options["gate-width"] = plan == 1 ? 6 : 14;
			request.options["heartland-farmland"] = plan == 1 ? 75 : 150;
			Game game(nullptr);
			const auto result = GenerationService().generate(game, request, true);
			if (!result)
				std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
			assert(result);
			const Torus t(game.map);
			assert(std::abs(game.teams[0]->startPosX - t.w / 2) < 20);
			assert(std::abs(game.teams[0]->startPosY - t.h / 2) < 20);
			for (int k = 1; k < teams; ++k)
				assert(std::max(std::abs(game.teams[k]->startPosX - t.w / 2),
								std::abs(game.teams[k]->startPosY - t.h / 2)) > 60);
			GenerationContext check(request);
			assert(definition.validateWorld(game, check).empty());
			// Plant a resource directly beside the capital, outside all gardens: containment
			// must reject it even though all original starting supplies remain intact.
			const int x = t.w / 2 + 2, y = t.h / 2 - 20;
			assert(game.map.isResourceAllowed(x, y, WHEAT));
			game.map.setResource(x, y, WHEAT, 1);
			assert(!definition.validateWorld(game, check).empty());
		}
	// Retained random-study failures: a concave rectangular approach and a remote outer town.
	for (bool rectangular : {true, false})
	{
		D edge;
		edge.setMethodDefaults(GeneratorRegistry::builtins().idOf("encircled-kingdom"));
		edge.seed = rectangular ? 100897 : 100972;
		edge.nbTeams = rectangular ? 9 : 5;
		edge.wDec = rectangular ? 8 : 9;
		edge.hDec = 9;
		edge.nbWorkers = rectangular ? 1 : 7;
		edge.options = {{"fortress-plan", 3},
						{"gate-width", rectangular ? 10 : 6},
						{"heartland-farmland", 75},
						{"wheat-amount", rectangular ? 200 : 275},
						{"wood-amount", rectangular ? 200 : 150},
						{"stone-amount", rectangular ? 75 : 150},
						{"algae-amount", rectangular ? 200 : 0},
						{"fruit-amount", rectangular ? 0 : 300}};
		Game game(nullptr);
		const auto result = GenerationService().generate(game, edge, true);
		if (!result)
			std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
		assert(result);
	}
	// Telemetry and intervening requests must not alter seeded terrain or colonies.
	request.setMethodDefaults(GeneratorRegistry::builtins().idOf("encircled-kingdom"));
	request.seed = 7;
	request.nbTeams = 4;
	request.wDec = request.hDec = 8;
	request.nbWorkers = 4;
	Game plain(nullptr), traced(nullptr), other(nullptr), repeated(nullptr);
	assert(GenerationService().generate(plain, request));
	const auto observed = GenerationService().generate(traced, request, true);
	assert(observed && mapFingerprint(plain) == mapFingerprint(traced));
	D alternate = request;
	alternate.seed = 91;
	alternate.options["fortress-plan"] = 2;
	assert(GenerationService().generate(other, alternate));
	const auto again = GenerationService().generate(repeated, request, true);
	assert(again && mapFingerprint(traced) == mapFingerprint(repeated));
	assert(observed.telemetry.records() == again.telemetry.records());
	for (int plan = 1; plan <= 3; ++plan)
	{
		D grown = request;
		grown.options["fortress-plan"] = plan;
		for (const auto &control : definition.controls)
			if (control.group == ControlGroup::Resources)
				grown.options[control.id] = control.maximum;
		Game abundant(nullptr);
		assert(GenerationService().generate(abundant, grown));
		setSyncRandSeed(4211);
		for (int tick = 0; tick < 4096; ++tick)
			abundant.map.growResources();
		GenerationContext check(grown);
		const auto error = definition.validateWorld(abundant, check);
		if (!error.empty())
			std::fprintf(stderr, "Kingdom growth: %s\n", error.c_str());
		assert(error.empty());
		// Stone is the fortress structure even with all renewable fields fully grown.
		bool removed = false;
		for (int y = 0; y < abundant.map.getH(); ++y)
			for (int x = 0; x < abundant.map.getW(); ++x)
				if (abundant.map.getResource(x, y).type == STONE)
				{
					abundant.map.getResource(x, y).clear();
					removed = true;
				}
		assert(removed && !definition.validateWorld(abundant, check).empty());
	}
	puts("PASS Encircled Kingdom: 3-12 colonies, all fortress plans, extremes, fixed capital, "
		 "containment");
}

inline void faultedCityContracts()
{
	const auto &definition = GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("faulted-city"));
	D request;
	request.setMethodDefaults(definition.legacyId);
	request.wDec = request.hDec = 8;
	request.nbTeams = 4;
	request.seed = 101;
	GenerationService service;
	{
		Game plain(nullptr), observed(nullptr);
		assert(service.generate(plain, request));
		const auto measured = service.generate(observed, request, true);
		assert(measured && !measured.telemetry.records().empty());
		assert(!measured.telemetry.droppedRecords() && !measured.telemetry.invalidValues());
		assert(mapFingerprint(plain) == mapFingerprint(observed));
	}
	for (auto shape : {std::pair{8, 8}, std::pair{8, 9}, std::pair{9, 8}, std::pair{9, 9}})
		for (int teams : {1, 4, 12})
		{
			D r = request;
			r.wDec = shape.first; r.hDec = shape.second; r.nbTeams = teams;
			Game world(nullptr);
			const auto result = service.generate(world, r);
			if (!result) std::fprintf(stderr, "Faulted City %dx%d/%d: %s\n", 1 << r.wDec, 1 << r.hDec, teams, result.diagnostic().c_str());
			assert(result);
		}
	{
		D r = request; r.wDec = 7;
		Game world(nullptr);
		assert(service.generate(world, r).error == GenerationError::InvalidRequest);
		assert(world.teamsCount() == 0);
	}
	for (int amount : {0, 300})
	{
		D r = request;
		for (const auto &control : definition.controls)
			if (control.group == ControlGroup::Resources) r.options[control.id] = amount;
		Game world(nullptr);
		assert(service.generate(world, r));
		GenerationContext context(r);
		if (amount == 300)
		{
			setSyncRandSeed(2026);
			for (int tick = 0; tick < 4096; ++tick) world.map.growResources();
			const auto error = definition.validateWorld(world, context);
			if (!error.empty()) std::fprintf(stderr, "Faulted City growth: %s\n", error.c_str());
			assert(error.empty());
		}
		// Structural stone is still required at zero ordinary stone abundance.
		bool damaged = false;
		for (int y = 0; y < world.map.getH() && !damaged; ++y)
			for (int x = 0; x < world.map.getW() && !damaged; ++x)
				if (amount == 0 && world.map.getResource(x, y).type == STONE)
				{ world.map.setNoResource(x, y, 1); damaged = true; }
		if (amount == 0) assert(damaged && !definition.validateWorld(world, context).empty());
	}
	{
		D scarce = request, rich = request;
		for (const auto &control : definition.controls) if (control.group == ControlGroup::Resources)
		{ scarce.options[control.id] = 0; rich.options[control.id] = 300; }
		Game low(nullptr), high(nullptr);
		assert(service.generate(low, scarce) && service.generate(high, rich));
		for (int y = 0; y < low.map.getH(); ++y) for (int x = 0; x < low.map.getW(); ++x)
			assert(low.map.getUMTerrain(x, y) == high.map.getUMTerrain(x, y));
	}
	{
		Game world(nullptr);
		const auto result = service.generate(world, request, true);
		assert(result);
		int x = -1, y = -1;
		for (const auto &record : result.telemetry.records()) if (record.subject == 0)
		{
			if (record.key == "faulted-city.junction.x") x = int(std::get<std::int64_t>(record.value));
			if (record.key == "faulted-city.junction.y") y = int(std::get<std::int64_t>(record.value));
		}
		assert(x >= 0 && y >= 0);
		// Leave the centre open, but obstruct its reserved gathering/circulation width.
		world.map.setResource((x + 1) % world.map.getW(), y, STONE, 1);
		GenerationContext context(request);
		assert(!definition.validateWorld(world, context).empty());
	}

	{
		Game world(nullptr);
		assert(service.generate(world, request));
		bool retained = false;
		for (int y = 0; y < world.map.getH(); ++y)
			for (int x = 0; x < world.map.getW(); ++x)
				if (world.map.getResource(x, y).type == WHEAT)
				{ if (retained) world.map.setNoResource(x, y, 1); retained = true; }
		GenerationContext context(request);
		assert(!definition.validateWorld(world, context).empty());
	}
	puts("PASS Faulted City: envelope, refusal, resource extremes, growth containment, stable resource terrain, narrow junction, masonry and token food mutations");
}

inline void eatenMapContracts()
{
	GenerationService service;
	D request;
	request.setMethodDefaults(GeneratorRegistry::builtins().idOf("who-ate-the-map"));
	request.wDec = request.hDec = 7;
	request.nbTeams = 4;
	request.seed = 7;
	const auto &definition = *GeneratorRegistry::builtins().find(request.method);
	for (int appetite : {0, 1, 2})
	{
		request.options["appetite"] = appetite;
		Game world(nullptr), repeated(nullptr), sparse(nullptr);
		assert(service.generate(world, request));
		const auto traced = service.generate(repeated, request, true);
		assert(traced && !traced.telemetry.records().empty());
		assert(traced.telemetry.invalidValues() == 0);
		assert(mapFingerprint(world) == mapFingerprint(repeated));
		auto zero = request;
		for (const char *key :
			 {"wheat-amount", "wood-amount", "stone-amount", "algae-amount", "fruit-amount"})
			zero.options[key] = 0;
		assert(service.generate(sparse, zero));
		for (int y = 0; y < 128; ++y)
			for (int x = 0; x < 128; ++x)
				assert(world.map.getUMTerrain(x, y) == sparse.map.getUMTerrain(x, y));
		GenerationContext check(request);
		assert(definition.validateWorld(world, check).empty());
		// Depleting the opening crops must be rejected even when the coastline survives.
		for (int y = 0; y < 128; ++y)
			for (int x = 0; x < 128; ++x)
				if (repeated.map.getResource(x, y).type == WHEAT)
					repeated.map.setNoResource(x, y, 0);
		assert(!definition.validateWorld(repeated, check).empty());
		world.map.setUMTerrain(0, 0, GRASS);
		assert(!definition.validateWorld(world, check).empty());
	}
	// A single greedy spread used to reject this usable crescent. Retry sites, not terrain.
	request.seed = 71;
	request.nbWorkers = 8;
	{
		Game crescent(nullptr);
		assert(service.generate(crescent, request));
	}
	{
		auto crowded = request;
		crowded.seed = 63577209;
		crowded.nbWorkers = 6;
		crowded.options["wheat-amount"] = 300;
		crowded.options["wood-amount"] = 175;
		crowded.options["algae-amount"] = crowded.options["fruit-amount"] = 0;
		Game crescent(nullptr);
		assert(service.generate(crescent, crowded));
	}
	request.seed = 7;
	request.nbWorkers = 4;
	for (auto dims :
		 {std::pair{7, 8}, std::pair{8, 7}, std::pair{9, 8}, std::pair{8, 9}, std::pair{9, 9}})
	{
		request.wDec = dims.first;
		request.hDec = dims.second;
		request.nbTeams = std::min(dims.first, dims.second) == 7 ? 4 : 8;
		Game world(nullptr);
		assert(service.generate(world, request));
	}
	for (auto dims : {std::pair{6, 6}, std::pair{7, 9}, std::pair{9, 7}})
	{
		request.wDec = dims.first;
		request.hDec = dims.second;
		assert(!definition.validateRequest(request).empty());
	}
	// Small detached crescents are scenery; a larger lobe still supports an island colony.
	for (const auto [dims, seed, expected] : {std::tuple{8, 7, 1}, std::tuple{9, 11, 2}})
	{
		auto split = request;
		split.wDec = split.hDec = dims;
		split.nbTeams = 4;
		split.seed = seed;
		split.options["appetite"] = 2;
		Game world(nullptr);
		assert(service.generate(world, split));
		const MapGeneration::Torus t(world.map);
		auto land = MapGeneration::pureTiles(world.map, WATER);
		for (auto &tile : land) tile = !tile;
		const auto labels = MapGeneration::connectedRegions(land, t.w, t.h, true,
			MapGeneration::GridNeighbors::Eight);
		std::set<int> occupied;
		for (const auto &units : MapGeneration::unitTilesByTeam(world.map, 4))
			occupied.insert(labels[units[0]]);
		assert(occupied.size() == size_t(expected));
	}
	puts("PASS Who Ate the Map: reproducibility, unchanged coastline at zero resources, corruption "
		 "rejection, envelope");
}

inline void portageLakesContracts()
{
	const auto &registry = GeneratorRegistry::builtins();
	const int method = registry.idOf("portage-lakes");
	const auto &definition = registry.at(method);
	GenerationService service;
	D r;
	r.setMethodDefaults(method);
	r.wDec = r.hDec = 8;
	r.nbTeams = 4;
	r.seed = 1;
	Game first(nullptr);
	auto report = service.generate(first, r, true);
	if (!report)
		std::fprintf(stderr, "%s\n", report.diagnostic().c_str());
	assert(report);
	const auto fingerprint = mapFingerprint(first);
	GenerationContext context(r);
	assert(definition.validateWorld(first, context).empty());
	// Telemetry must not change either placement or RNG, even between other requests.
	D compact = r;
	compact.wDec = compact.hDec = 6;
	compact.nbTeams = 2;
	Game small(nullptr);
	auto smallReport = service.generate(small, compact);
	if (!smallReport)
		std::fprintf(stderr, "%s\n", smallReport.diagnostic().c_str());
	assert(smallReport);
	Game repeated(nullptr);
	assert(service.generate(repeated, r, false));
	assert(mapFingerprint(repeated) == fingerprint);
	// Regrowth cannot invade the terrain-contained farms or refill a dry portage.
	setSyncRandSeed(4211);
	for (int tick = 0; tick < 4096; ++tick)
		first.map.growResources();
	auto error = definition.validateWorld(first, context);
	if (!error.empty())
		std::fprintf(stderr, "Portage Lakes growth: %s\n", error.c_str());
	assert(error.empty());
	// Missing dry structural wood must be caught, without relying on fixed coordinates.
	auto fertility = Fertility::forMap(repeated.map, false);
	bool removed = false;
	for (int y = 0; y < 256; ++y)
		for (int x = 0; x < 256; ++x)
			if (repeated.map.getResource(x, y).type == WOOD && !fertility.at(x, y))
			{
				repeated.map.getResource(x, y).clear();
				removed = true;
			}
	assert(removed && !definition.validateWorld(repeated, context).empty());
	// The compact policy applies to long rectangles too; each distant colony needs algae.
	for (const auto shape : {std::array<int, 3>{6, 8, 4}, std::array<int, 3>{7, 7, 4}})
	{
		D rectangular = r;
		rectangular.wDec = shape[0];
		rectangular.hDec = shape[1];
		rectangular.nbTeams = shape[2];
		rectangular.nbWorkers = 8;
		rectangular.seed = 31;
		Game world(nullptr);
		auto result = service.generate(world, rectangular);
		if (!result)
			std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
		assert(result);
	}
	// A change to the lake/road terrain cannot silently validate as the original design.
	const auto originalTerrain = small.map.getUMTerrain(0, 0);
	small.map.setUMTerrain(0, 0, originalTerrain == WATER ? GRASS : WATER);
	small.map.rebuildTerrain();
	GenerationContext compactContext(compact);
	assert(!definition.validateWorld(small, compactContext).empty());
	// Failed neutral bays restore a working layout; that must not invalidate its candidate scan.
	// This crowded request exposed both nondeterministic reconstruction and an iterator lifetime bug.
	{
		D crowded = r;
		crowded.nbTeams = 12;
		crowded.nbWorkers = 6;
		crowded.seed = 100078;
		crowded.options["lake-elongation"] = 175;
		crowded.options["portage-depth"] = 3;
		crowded.options["extra-trails"] = 0;
		crowded.options["wheat-amount"] = 200;
		crowded.options["wood-amount"] = 175;
		crowded.options["stone-amount"] = 175;
		crowded.options["algae-amount"] = 0;
		crowded.options["fruit-amount"] = 150;
		Game one(nullptr), two(nullptr);
		auto result = service.generate(one, crowded, true);
		if (!result)
			std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
		assert(result && service.generate(two, crowded));
		assert(mapFingerprint(one) == mapFingerprint(two));
	}
	// At zero abundance the guaranteed seed budget must still service the inn.
	// Fertility-only sowing previously exhausted all landscape attempts here.
	{
		D scarce = r;
		scarce.wDec = 9;
		scarce.hDec = 8;
		scarce.nbTeams = 9;
		scarce.nbWorkers = 1;
		scarce.seed = 100908;
		scarce.options["lake-elongation"] = 225;
		scarce.options["portage-depth"] = 2;
		scarce.options["extra-trails"] = 0;
		scarce.options["wheat-amount"] = 0;
		scarce.options["wood-amount"] = 50;
		scarce.options["stone-amount"] = 125;
		scarce.options["algae-amount"] = 75;
		scarce.options["fruit-amount"] = 250;
		Game world(nullptr);
		auto result = service.generate(world, scarce);
		if (!result)
			std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
		assert(result);
	}
	// Deliberately unsupported requests are rejected before touching the world.
	D invalid = compact;
	invalid.nbTeams = 3;
	assert(!definition.validateRequest(invalid).empty());
	for (int amount : {0, 300})
	{
		D extreme = r;
		for (const auto &control : definition.controls)
			if (control.group == ControlGroup::Resources)
				extreme.options[control.id] = amount;
		Game world(nullptr);
		auto result = service.generate(world, extreme);
		if (!result)
			std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
		assert(result);
	}
	puts("PASS Portage Lakes: compact/full, repeatability, growth containment, missing portage, "
		 "abundance extremes");
}
inline void drownedForestContracts()
{
	const auto &definition =
		GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("drowned-forest"));
	D request;
	request.setMethodDefaults(definition.legacyId);
	request.wDec = request.hDec = 8;
	request.nbTeams = 4;
	request.seed = 7;
	for (auto dimensions :
		 {std::pair{7, 7}, std::pair{7, 8}, std::pair{8, 7}, std::pair{8, 8}, std::pair{9, 9}})
	{
		auto r = request;
		r.wDec = dimensions.first;
		r.hDec = dimensions.second;
		r.nbTeams = std::min(8, (1 << r.wDec) * (1 << r.hDec) / 8192);
		assert(definition.validateRequest(r).empty());
		if (r.nbTeams < 8)
		{
			++r.nbTeams;
			assert(!definition.validateRequest(r).empty());
		}
	}
	for (auto dimensions : {std::pair{6, 7}, std::pair{7, 9}, std::pair{10, 9}})
	{
		auto r = request;
		r.wDec = dimensions.first;
		r.hDec = dimensions.second;
		assert(!definition.validateRequest(r).empty());
	}
	GenerationService service;
	Game first(nullptr), warm(nullptr);
	assert(service.generate(first, request));
	assert(service.generate(warm, request, true));
	assert(mapFingerprint(first) == mapFingerprint(warm));
	const auto fingerprint = mapFingerprint(first);
	GenerationContext check(request);
	assert(definition.validateWorld(first, check).empty());
	// Forced late growth must not seal circulation or invade the protected towns.
	setSyncRandSeed(58007);
	for (int tick = 0; tick < 4096; ++tick)
		first.map.growResources();
	assert(definition.validateWorld(first, check).empty());
	// Damage to a designated neck must be detected.
	Game observedWorld(nullptr);
	const auto observed = service.generate(observedWorld, request, true);
	int neck = -1;
	for (const auto &record : observed.telemetry.records())
		if (record.key == "drowned-forest.shortcut.centre")
		{
			neck = int(std::get<std::int64_t>(record.value));
			break;
		}
	assert(neck >= 0);
	observedWorld.map.setNoResource(neck % 256, neck / 256, 1);
	assert(!definition.validateWorld(observedWorld, check).empty());
	auto compact = request;
	compact.wDec = compact.hDec = 7;
	compact.nbTeams = 2;
	compact.nbWorkers = 8;
	Game crowded(nullptr);
	assert(service.generate(crowded, compact));
	for (int amount : {0, 300})
	{
		auto extreme = request;
		extreme.nbWorkers = 8;
		for (const auto &control : definition.controls)
			if (control.group == ControlGroup::Resources)
				extreme.options[control.id] = amount;
		Game world(nullptr);
		assert(service.generate(world, extreme));
		GenerationContext verify(extreme);
		assert(definition.validateWorld(world, verify).empty());
		if (amount == 0)
		{
			for (int y = 0; y < 256; ++y)
				for (int x = 0; x < 256; ++x)
					if (world.map.getResource(x, y).type == WHEAT)
						world.map.setNoResource(x, y, 1);
			assert(!definition.validateWorld(world, verify).empty());
		}
	}
	// A worker can stand inside a hypothetical future building rectangle. Circulation
	// may instead start at reachable gathering faces of its existing swarm.
	auto futureRoom = compact;
	futureRoom.seed = 100111;
	futureRoom.nbWorkers = 1;
	futureRoom.options["wooded-neck-thickness"] = 7;
	futureRoom.options["neutral-clearing-size"] = 24;
	futureRoom.options["wheat-amount"] = 300;
	futureRoom.options["wood-amount"] = 200;
	futureRoom.options["stone-amount"] = 250;
	futureRoom.options["algae-amount"] = 75;
	futureRoom.options["fruit-amount"] = 275;
	Game roomRegression(nullptr);
	assert(service.generate(roomRegression, futureRoom));
	// Sparse woods on the smallest map still need useful destinations for both homes.
	auto sparse = compact;
	sparse.seed = 2;
	sparse.nbWorkers = 4;
	sparse.options["wood-amount"] = 0;
	Game sparseRegression(nullptr);
	assert(service.generate(sparseRegression, sparse));
	sparse.seed = 100260;
	sparse.nbWorkers = 3;
	sparse.options["wooded-neck-thickness"] = 7;
	sparse.options["neutral-clearing-size"] = 22;
	sparse.options["wheat-amount"] = 175;
	sparse.options["stone-amount"] = 50;
	sparse.options["algae-amount"] = 75;
	sparse.options["fruit-amount"] = 75;
	Game scarceRegression(nullptr);
	assert(service.generate(scarceRegression, sparse));
	// At full colony density, many alternate bars and thick necks need a longer
	// bounded search while retaining the same useful-shortcut requirement.
	auto dense = request;
	dense.nbTeams = 8;
	dense.seed = 100801;
	dense.options["sandbar-connections"] = 90;
	dense.options["wooded-neck-thickness"] = 9;
	dense.options["neutral-clearing-size"] = 16;
	dense.options["wheat-amount"] = 250;
	dense.options["wood-amount"] = 125;
	dense.options["stone-amount"] = 75;
	dense.options["algae-amount"] = 275;
	dense.options["fruit-amount"] = 275;
	Game denseRegression(nullptr);
	assert(service.generate(denseRegression, dense));
	// Fully occupied rectangular maps need the same bounded tail as dense squares.
	auto rectangle = request;
	rectangle.wDec = 7;
	rectangle.nbWorkers = 7;
	rectangle.seed = 101727;
	rectangle.options["sandbar-connections"] = 100;
	rectangle.options["wooded-neck-thickness"] = 9;
	rectangle.options["neutral-clearing-size"] = 24;
	rectangle.options["wood-amount"] = 0;
	rectangle.options["stone-amount"] = 50;
	rectangle.options["algae-amount"] = 200;
	Game rectangularRegression(nullptr);
	assert(service.generate(rectangularRegression, rectangle));
	Game cold(nullptr);
	assert(service.generate(cold, request));
	// The worker-sensitive compact request evicted the cache; reconstruction is identical.
	Game original(nullptr);
	assert(service.generate(original, request));
	assert(mapFingerprint(cold) == fingerprint && mapFingerprint(original) == fingerprint);
	puts("PASS Drowned Forest: envelope, repeatability, cache, growth containment, neck damage, "
		 "compact workers");
}

inline void bastionKeysContracts()
{
	D request;
	request.setMethodDefaults(GeneratorRegistry::builtins().idOf("bastion-keys"));
	const auto &definition = GeneratorRegistry::builtins().at(request.method);
	request.wDec = request.hDec = 7;
	request.nbTeams = 1;
	request.nbWorkers = 8;
	for (int size : {14, 18})
		for (int amount : {0, 300})
		{
			request.options["plantation-size"] = size;
			for (const char *key :
				 {"wheat-amount", "wood-amount", "stone-amount", "algae-amount", "fruit-amount"})
				request.options[key] = amount;
			Game game(nullptr);
			const auto result = GenerationService().generate(game, request, true);
			if (!result)
				std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
			assert(result);
			GenerationContext check(request);
			assert(definition.validateWorld(game, check).empty());
			assert(countBuildings(game, 0, "swimmingpool") == 1);
			if (amount == 0)
			{
				setSyncRandSeed(917);
				for (int tick = 0; tick < 20000; ++tick)
					game.map.growResources();
				assert(definition.validateWorld(game, check).empty());
			}
			// A missing permanent rampart is an invalid map, even when the rest is playable.
			int wall = -1;
			for (int i = 0; i < 128 * 128; ++i)
				if (game.map.getResource(i).type == STONE)
				{
					wall = i;
					break;
				}
			assert(wall >= 0);
			game.map.setNoResource(wall % 128, wall / 128, 1);
			assert(!definition.validateWorld(game, check).empty());
		}
	request.nbTeams = 2;
	assert(!definition.validateRequest(request).empty());
	request.wDec = 8;
	assert(definition.validateRequest(request).empty());
	// Exercise all fort styles/facings over held-out seeds, both rectangular orientations,
	// scarce/dense crops, open-water isolation and the full twelve-colony envelope.
	for (int seed : {207, 208, 211, 307})
		for (int shape = 0; shape < 3; ++shape)
		{
			request.setMethodDefaults(GeneratorRegistry::builtins().idOf("bastion-keys"));
			request.seed = seed;
			request.wDec = shape == 0 ? 7 : 9;
			request.hDec = shape == 1 ? 7 : 9;
			request.nbTeams = shape == 2 ? 12 : 4;
			request.nbWorkers = seed % 2 ? 1 : 8;
			request.options["home-size"] = seed % 2 ? 13 : 15;
			request.options["plantation-size"] = seed % 2 ? 14 : 18;
			request.options["outer-islands"] = seed % 2 ? 1 : 5;
			Game game(nullptr);
			const auto result = GenerationService().generate(game, request, true);
			if (!result)
				std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
			assert(result);
			assert(coloniesApart(game.map, request.nbTeams, "without swimming").empty());
		}
	request.setMethodDefaults(GeneratorRegistry::builtins().idOf("bastion-keys"));
	request.wDec = 8;
	request.hDec = 7;
	request.nbTeams = 2;
	Game bridged(nullptr);
	assert(GenerationService().generate(bridged, request));
	assert(coloniesApart(bridged.map, 2, "without swimming").empty());
	const Torus t(bridged.map);
	TerrainSketch landBridge(t.size());
	for (int i = 0; i < t.size(); ++i)
	{
		const int corner = bridged.map.getUMTerrain(i % t.w, i / t.w);
		landBridge[i] = corner == WATER ? SAND : corner;
	}
	writeUndermap(bridged.map, landBridge);
	GenerationContext check(request);
	assert(definition.validateWorld(bridged, check).find("walking connection") !=
		   std::string::npos);
	puts("PASS Bastion Keys: envelope, variants, rectangles, twelve colonies, swimming isolation, "
		 "land-bridge corruption, external crops, "
		 "growth, pools and rampart corruption");
}

inline void generatorContracts()
{
	bastionKeysContracts();
	drownedForestContracts();
	portageLakesContracts();
	faultedCityContracts();
	encircledKingdomContracts();
	combContracts();
	evenGroundContracts();
	marchlandContracts();
	eatenMapContracts();
	rebuiltLandscapeContracts();
	savannahContracts();
	locustFoodChecks();
	hedgerowContracts();
	breachableHighlandsContracts();
	braidedDeltaChecks();
	fortsContracts();
	emojiContracts();
	honeycombIsleContracts();
	karstTowersContracts();
	bajadaContracts();
}
} // namespace GeneratorContracts
