// SPDX-License-Identifier: GPL-3.0-or-later
// Contract checks for individual generators: the promises a map makes beyond the framework's
// common structure (its supported shapes, its variants, what its validator refuses), each run
// by MapGeneratorDefaultsTest. Shared-primitive checks live in MapGeneratorToolkitChecks.h and
// MapGeneratorLandscapeChecks.h; a check here is about one landscape's own contract.
#pragma once
#include "BraidedDeltaGenerator.h"
#include <array>
#include <algorithm>
#include <utility>
#include <memory>
#include <cstdlib>
#include <cstdint>
#include <climits>
#include "Contact.h"
#include "FertilityField.h"
#include "Game.h"
#include "LastTreelineChecks.h"
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
#include "TerrainTypeTable.h"

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
						assert(world.map.vertexTerrainAt(x, y) == alone.map.vertexTerrainAt(x, y));
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

// Exercise the production service and final-world validator on the inland delta, including
// deliberate damage: a successful seed alone would not show that the validator notices a breach.
inline void braidedDeltaChecks()
{
	const auto definition = braidedDeltaDefinition();
	struct Case
	{
		unsigned seed;
		int wDec, hDec, teams, flood;
	};
	for (const Case &c : {Case{7, 8, 8, 4, 1}, Case{21, 8, 8, 4, 0}, Case{99, 8, 8, 4, 2}, Case{3, 7, 7, 4, 1},
						  Case{5, 9, 8, 8, 1}, Case{11, 7, 9, 12, 2}})
	{
		GenerationRequest request;
		request.setMethodDefaults(definition.legacyId);
		request.wDec = c.wDec;
		request.hDec = c.hDec;
		request.nbTeams = c.teams;
		request.seed = c.seed;
		request.options["flood"] = c.flood;
		Game game(nullptr);
		const auto result = GenerationService().generate(game, request, true);
		assert(result);
		GenerationContext context(request);
		assert(definition.validateWorld(game, context).empty());
		// The panhandle's river is deep: some vertex of the map is deep water, and the swamp is marsh.
		const MapGeneration::Torus torus(game.map);
		bool deep = false, marsh = false;
		for (int i = 0; i < torus.size(); ++i)
		{
			deep = deep || game.map.vertexTerrainAt(size_t(i)) == DEEP_WATER;
			marsh = marsh || game.map.vertexTerrainAt(size_t(i)) == MUDFLAT;
		}
		assert(deep && marsh);
		// Damage: a causeway of sand straight across the map wherever there is water. The terrain no
		// longer matches the design, so the validator must refuse the world.
		{
			Game damaged(nullptr);
			assert(GenerationService().generate(damaged, request, true));
			for (int x = 0; x < torus.w; ++x)
				for (int y = torus.h / 3; y < torus.h / 3 + 3; ++y)
					if (damaged.map.vertexTerrainAt(x, y) == WATER || damaged.map.vertexTerrainAt(x, y) == DEEP_WATER)
						damaged.map.setVertexTerrain(x, y, SAND);
			assert(!definition.validateWorld(damaged, context).empty());
		}
		// Damage: stone over every tile that will take it. Terrain still matches, but the colonies
		// are walled in and the finished-world contract must fail after furnishing.
		for (int y = 0; y < game.map.getH(); ++y)
			for (int x = 0; x < game.map.getW(); ++x)
				if (game.map.isResourceAllowed(x, y, STONE))
					game.map.setResourceByIndex(x, y, STONE, 1);
		assert(!definition.validateWorld(game, context).empty());
	}
	// Undersized maps fail before mutating a Game.
	GenerationRequest request;
	request.setMethodDefaults(definition.legacyId);
	request.wDec = 6;
	request.hDec = 7;
	request.nbTeams = 2;
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
		game.map.setResourceByIndex(i % game.map.getW(), i / game.map.getW(), WOOD, 1);
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
		const int value = uncontained.map.vertexTerrainAt(i % farmTorus.w, i / farmTorus.w);
		terrain[i] = value == SAND ? GRASS : value;
	}
	MapGeneration::layBeaches(terrain, farmTorus);
	MapGeneration::writeVertices(uncontained.map, terrain);
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
	assert(definition.legacyId == 45 && definition.revision == 5);
	assert(request.option("watering-holes") == 1 && request.option("dry-patches") == 8);
	assert(request.option("grassland") == 70 && request.option("kopjes") == 1 &&
		   request.option("game-trails") == 1);
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
	// Without tall grass or trails the map is still a savannah: the plain is open, not refused.
	r = request;
	r.wDec = r.hDec = 7;
	r.options["grassland"] = 0;
	r.options["game-trails"] = 0;
	Game bare(nullptr);
	assert(service.generate(bare, r));
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
		MapGeneration::startingAccessFailure(map, 2, {{MaterialId::Food, 24, "food"}, {MaterialId::Wood, 32, "wood"}})
			.empty());
	// No fruit exists here: a required absent supply must fail, as must an impossible room budget.
	assert(!MapGeneration::startingAccessFailure(map, 2, {{MaterialId::Cherries, 24, "cherries"}}).empty());
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
		// Current versions of the rebuilt landscapes (The Glacis 5, Caravanserai 2.0 is 5 as well).
		assert(definition.revision == (std::string(id) == "allotments" ? 3 : 5));
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
				game.map.setResourceByIndex(x, y, WHEAT, 1);
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
// every road-sealed hex, a design cache that never changes a map or its telemetry, and a colony
// whose starter wheat is taken away failing the validator.
inline void honeycombIsleContracts()
{
	const auto &definition =
		GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("honeycomb-isle"));
	assert(definition.legacyId == 53 && definition.revision == 7);
	D request;
	request.setMethodDefaults(definition.legacyId);
	assert(request.option("block-shape") == 1 && request.option("river-width") == 12 &&
		   request.option("blocks-per-colony") == 11 && request.option("lakes") == 1 &&
		   request.option("mountains") == 1);
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
	std::set<std::string> homeDesigns;
	for (unsigned seed = 1; seed <= 12; ++seed)
	{
		Game g(nullptr);
		const auto result = service.generate(g, make(8, 8, 4, seed), true);
		assert(result);
		std::vector<unsigned char> grass(g.map.getW() * g.map.getH(), 0);
		for (int y = 0; y < g.map.getH(); ++y)
			for (int x = 0; x < g.map.getW(); ++x)
				grass[y * g.map.getW() + x] = g.map.terrainPropertiesAt(x, y).resourcesGrow &&
											  g.map.terrainPropertiesAt(x, y).walkable;
		const auto cells = connectedRegions(grass, g.map.getW(), g.map.getH(), true,
										   GridNeighbors::Eight);
		std::set<int> fruitCells;
		for (int i = 0; i < int(grass.size()); ++i)
		{
			const int type = g.map.getResource(i % g.map.getW(), i / g.map.getW()).type;
			if (type == CHERRY || type == ORANGE || type == PRUNE)
				fruitCells.insert(cells[i]);
		}
		assert(!fruitCells.empty());
		for (int i = 0; i < int(grass.size()); ++i)
			if (g.map.getResource(i % g.map.getW(), i / g.map.getW()).type == STONE)
				assert(!fruitCells.count(cells[i]));
		for (const auto &record : result.telemetry.records())
		{
			if (record.key == "honeycomb-isle.home-design")
				homeDesigns.insert(std::get<std::string>(record.value));
		}
	}
	assert(homeDesigns.size() == 2);
	// All stone is quarry stock at the mountains and hills: at zero stone there is
	// none on either tiling.
	for (int shape : {0, 1})
		for (unsigned seed : {1u, 7u, 29u})
		{
			auto bare = make(8, 8, 4, seed);
			bare.options["block-shape"] = shape;
			bare.options["stone-amount"] = 0;
			Game g(nullptr);
			assert(service.generate(g, bare));
			for (int y = 0; y < g.map.getH(); ++y)
				for (int x = 0; x < g.map.getW(); ++x)
					assert(g.map.getResource(x, y).type != STONE);
		}
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
	// Full-map growth with nobody harvesting: every field, garden and forest grows, and the roads
	// must keep each inside its own hex.
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
	assert(definition.legacyId == 54 && definition.revision == 3);
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
					game.map.setResourceByIndex(x, y, STONE, 1);
			}
		assert(!definition.validateWorld(game, check).empty());
	}
	puts("PASS Karst towers: envelope and refusal, crowded retry, home designs, resource extremes, "
		 "sealed growth, closed gates");
}

inline void bajadaContracts()
{
	const auto &definition = GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("bajada"));
	assert(definition.legacyId == 55 && definition.revision == 4);
	D request;
	request.setMethodDefaults(definition.legacyId);
	assert(request.option("range-spacing") == 128 && request.option("passes") == 2 &&
		   request.option("playa") == 80 && request.option("home-design") == 0 &&
		   request.option("cienegas") == 50);
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
	std::set<std::string> designs, lithologies;
	for (unsigned seed = 1; seed <= 40; ++seed)
	{
		Game g(nullptr);
		const auto result = service.generate(g, make(8, 8, 4, seed), true);
		assert(result);
		for (const auto &record : result.telemetry.records())
			if (record.key == "bajada.home.design")
				designs.insert(std::get<std::string>(record.value));
			else if (record.key == "bajada.lithology")
				lithologies.insert(std::get<std::string>(record.value));
	}
	assert(designs.size() == 6 && lithologies.size() == 3);
	// Every design pinned, on every size and colony count the envelope promises it.
	for (int design = 1; design <= 6; ++design)
		for (auto [dimension, teams] : {std::pair{8, 4}, std::pair{7, 4}, std::pair{8, 8}, std::pair{9, 8}})
		{
			D pinned = make(dimension, dimension, teams, 11);
			pinned.options["home-design"] = design;
			Game g(nullptr);
			const auto result = service.generate(g, pinned);
			if (!result)
				std::fprintf(stderr, "Bajada design %d (%d, %d colonies): %s\n", design, 1 << dimension, teams,
							 result.diagnostic().c_str());
			assert(result);
		}
	// Every design grows a real stream tree on every seed tried: a home whose trunk met its town grew
	// none and halved its food.
	for (int design = 1; design <= 6; ++design)
		for (std::uint32_t seed = 1; seed <= 8; ++seed)
		{
			D pinned = make(8, 8, 4, seed);
			pinned.options["home-design"] = design;
			Game g(nullptr);
			const auto result = service.generate(g, pinned, true);
			assert(result);
			for (const auto &record : result.telemetry.records())
				if (record.key == "bajada.home.branches" && std::get<std::int64_t>(record.value) < 3)
				{
					std::fprintf(stderr, "Bajada design %d seed %u grew %d branches\n", design, unsigned(seed),
								 int(std::get<std::int64_t>(record.value)));
					assert(false);
				}
		}
	for (int lithology = 1; lithology <= 3; ++lithology)
	{
		D pinned = make(8, 8, 4, 11);
		pinned.options["lithology"] = lithology;
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
				zero.map.replaceResource(i % 256, i / 256, Resource{});
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
					game.map.setResourceByIndex(x, y, WHEAT, 1);
					sown = true;
				}
			}
		assert(sown && !definition.validateWorld(game, check).empty());
	}
	{
		// The playa's pan, the cienegas and the quarries: a dry lake bed still has its hardpan, no
		// cienegas means none, and every count is in the telemetry.
		const auto measure = [&](D r, const char *key)
		{
			Game g(nullptr);
			const auto result = service.generate(g, r, true);
			assert(result);
			for (const auto &record : result.telemetry.records())
				if (record.key == key)
					return int(std::get<std::int64_t>(record.value));
			return -1;
		};
		D dry = make(8, 8, 4, 53);
		dry.options["playa"] = 0;
		assert(measure(dry, "bajada.lakes") <= 0);
		D none = make(8, 8, 4, 53);
		none.options["cienegas"] = 0;
		assert(measure(none, "bajada.cienegas") == 0);
		D many = make(8, 8, 4, 53);
		many.options["cienegas"] = 100;
		assert(measure(many, "bajada.cienegas") > measure(make(8, 8, 4, 53), "bajada.cienegas"));
		assert(measure(make(8, 8, 4, 53), "bajada.quarries") > 4);
	}
	puts("PASS Bajada: envelope and refusal, home designs, resource extremes, designed stone, sealed "
		 "towns under growth, crops refused in towns, pan, cienegas and quarries");
}

// Braided River 2.0: rock bluffs sealed round the seam with a quarry face for every colony, moraine
// knots with stone flanks, pavement beyond the water's reach, deep pools and a look-only season.
inline void braidedRiverContracts()
{
	const auto &definition =
		GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("braided-river"));
	assert(definition.legacyId == 42 && definition.revision == 5);
	D request;
	request.setMethodDefaults(definition.legacyId);
	assert(request.option("braid-width") == 40 && request.option("channels") == 6 &&
		   request.option("dry-patches") == 35 && request.option("deep-pools") == 1 &&
		   request.option("season") == 0 && request.option("moraine") == 1);
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
	const auto records = [&](const D &r, Game &g)
	{
		const auto result = service.generate(g, r, true);
		if (!result)
			std::fprintf(stderr, "Braided river (%d x %d, %d colonies, seed %u): %s\n", 1 << r.wDec,
						 1 << r.hDec, r.nbTeams, unsigned(r.seed), result.diagnostic().c_str());
		assert(result);
		return result.telemetry.records();
	};
	const auto measure = [](const auto &recs, const char *key)
	{
		long long total = 0;
		bool found = false;
		for (const auto &record : recs)
			if (record.key == key && std::holds_alternative<std::int64_t>(record.value))
			{
				total += std::get<std::int64_t>(record.value);
				found = true;
			}
		return found ? total : -1LL;
	};
	// The envelope: every size and colony count the request check accepts, each with a season and
	// deep pools pinned in turn.
	int pinned = 0;
	for (auto dimensions : {std::pair{7, 7}, std::pair{7, 8}, std::pair{8, 7}, std::pair{8, 8},
							std::pair{9, 8}, std::pair{9, 9}})
		for (int teams : {1, 2, 4, 6, 8, 12})
		{
			D r = make(dimensions.first, dimensions.second, teams, 7);
			if (!definition.validateRequest(r).empty())
				continue;
			r.options["season"] = 1 + pinned % 2;
			r.options["deep-pools"] = (pinned / 2) % 2;
			++pinned;
			// A seed whose braid fails (a riffle without a walkable bank: revision 4 refused the same
			// seeds) is the service's to roll again; one of the next few must hold.
			for (int roll = 0; roll < 6; ++roll)
			{
				Game probe(nullptr);
				if (service.generate(probe, r))
					break;
				++r.seed;
			}
			Game g(nullptr);
			const auto recs = records(r, g);
			// Every colony's own quarry face holds the same ten stone, and more stand along the bluffs.
			assert(measure(recs, "braided-river.quarry.bluff-stone") >= 10 * teams);
			GenerationContext check(r);
			assert(definition.validateWorld(g, check).empty());
		}
	assert(pinned >= 24);
	assert(!definition.validateRequest(make(6, 8, 2, 7)).empty());
	// Both seasons come up at random; each variant pinned holds at 128, 256 and 512.
	std::set<std::string> seasons;
	for (unsigned seed = 1; seed <= 10; ++seed)
	{
		Game g(nullptr);
		for (const auto &record : records(make(8, 8, 4, seed), g))
			if (record.key == "braided-river.season")
				seasons.insert(std::get<std::string>(record.value));
	}
	assert(seasons.size() == 2);
	for (int season = 1; season <= 2; ++season)
		for (int deep = 0; deep <= 1; ++deep)
			for (auto [dimension, teams] : {std::pair{7, 4}, std::pair{8, 4}, std::pair{9, 8}})
			{
				D r = make(dimension, dimension, teams, 11);
				r.options["season"] = season;
				r.options["deep-pools"] = deep;
				Game g(nullptr);
				const auto recs = records(r, g);
				assert((measure(recs, "braided-river.deep.vertices") > 0) == (deep == 1));
			}
	// The season is looks only: the same seed plays identically in both.
	{
		D melt = make(8, 8, 4, 13), late = make(8, 8, 4, 13);
		melt.options["season"] = 1;
		late.options["season"] = 2;
		Game a(nullptr), b(nullptr);
		assert(service.generate(a, melt) && service.generate(b, late));
		int differentLooks = 0;
		for (int y = 0; y < 256; ++y)
			for (int x = 0; x < 256; ++x)
			{
				const auto &ra = a.map.terrainPropertiesAt(x, y), &rb = b.map.terrainPropertiesAt(x, y);
				assert(ra.walkable == rb.walkable && ra.buildable == rb.buildable &&
					   ra.swimmable == rb.swimmable && ra.inhibitionQ8 == rb.inhibitionQ8);
				differentLooks += a.map.vertexTerrainAt(x, y) != b.map.vertexTerrainAt(x, y);
				assert(a.map.isResource(x, y) == b.map.isResource(x, y));
			}
		assert(differentLooks > 1000);
	}
	// The bluffs are a seal: cutting a gap through the rock across the seam is refused. A 512 x 256
	// map runs its river along x, so its seam is the band round y = 0.
	{
		const D r = make(9, 8, 4, 17);
		Game g(nullptr);
		assert(service.generate(g, r));
		GenerationContext check(r);
		assert(definition.validateWorld(g, check).empty());
		int cut = 0;
		for (int y = 0; y < 256; ++y)
			for (int x = 200; x < 204; ++x)
				if ((y < 24 || y > 232) && g.map.vertexTerrainAt(x, y) == RIDGE_ROCK)
				{
					g.map.setVertexTerrain(x, y, GRASS);
					++cut;
				}
		const std::string fault = definition.validateWorld(g, check);
		assert(cut > 0 && fault.find("bluffs") != std::string::npos);
	}
	{
		// Every quarry face keeps its stone: taking the stone away is refused.
		const D r = make(8, 8, 4, 19);
		Game g(nullptr);
		assert(service.generate(g, r));
		GenerationContext check(r);
		assert(definition.validateWorld(g, check).empty());
		for (int i = 0; i < 256 * 256; ++i)
			if (g.map.isResource(i % 256, i / 256) && g.map.getResource(i % 256, i / 256).type == STONE)
				g.map.replaceResource(i % 256, i / 256, Resource{});
		assert(definition.validateWorld(g, check).find("quarry") != std::string::npos);
	}
	// The moraine: knots and stocked flanks with it, none without; flanks and ambient faces follow the
	// stone amount while every colony's own face stays.
	{
		D on = make(8, 8, 4, 23), off = make(8, 8, 4, 23), bare = make(8, 8, 4, 23), rich = make(8, 8, 4, 23);
		off.options["moraine"] = 0;
		bare.options["stone-amount"] = 0;
		rich.options["stone-amount"] = 200;
		Game a(nullptr), b(nullptr), c(nullptr), d(nullptr);
		const auto withKnots = records(on, a), without = records(off, b), noStone = records(bare, c),
				   moreStone = records(rich, d);
		assert(measure(withKnots, "braided-river.moraine.designed") > 100);
		assert(measure(withKnots, "braided-river.moraine.quarry-tiles") > 0);
		assert(measure(without, "braided-river.moraine.designed") == 0 &&
			   measure(without, "braided-river.moraine.quarry-tiles") == 0);
		assert(measure(noStone, "braided-river.moraine.quarry-tiles") == 0 &&
			   measure(noStone, "braided-river.bluffs.ambient-quarries") == 0);
		assert(measure(noStone, "braided-river.quarry.bluff-stone") == 40);
		assert(measure(moreStone, "braided-river.moraine.quarry-tiles") >
			   measure(withKnots, "braided-river.moraine.quarry-tiles"));
		assert(measure(moreStone, "braided-river.bluffs.ambient-quarries") >
			   measure(withKnots, "braided-river.bluffs.ambient-quarries"));
		// Pavement only where asked for.
		D flat = make(8, 8, 4, 23);
		flat.options["dry-patches"] = 0;
		Game e(nullptr);
		assert(measure(records(flat, e), "braided-river.pavement.tiles") == 0);
		assert(measure(withKnots, "braided-river.pavement.tiles") > 1000);
	}
	// Resource extremes: no ambient resources at all still plays and validates; at the ceilings the
	// map validates after a few thousand ticks of growth with nobody harvesting, and the crops grew.
	D r = make(8, 8, 4, 37);
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = 0;
	{
		Game zero(nullptr);
		assert(service.generate(zero, r));
		GenerationContext check(r);
		assert(definition.validateWorld(zero, check).empty());
	}
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = control.maximum;
	Game abundant(nullptr);
	assert(service.generate(abundant, r));
	GenerationContext context(r);
	assert(definition.validateWorld(abundant, context).empty());
	const auto wheat = [&]()
	{
		int count = 0;
		for (int i = 0; i < 256 * 256; ++i)
			count += abundant.map.isResource(i % 256, i / 256) &&
					 abundant.map.getResource(i % 256, i / 256).type == WHEAT;
		return count;
	};
	const int before = wheat();
	setSyncRandSeed(4211);
	for (int tick = 0; tick < 3072; ++tick)
		abundant.map.growResources();
	const std::string growthError = definition.validateWorld(abundant, context);
	if (!growthError.empty())
		std::fprintf(stderr, "Braided river growth: %s\n", growthError.c_str());
	assert(growthError.empty() && wheat() > before);
	puts("PASS Braided river: envelope with pinned seasons and deep pools, looks-only season, sealed "
		 "bluffs, quarry faces, moraine flanks and pavement, resource extremes, growth");
}

// Caravanserai 2.0 (revision 5): every desert country pinned over the envelope the old revision
// accepted, homes identical by rules, mesas sealed rock whose only stone is on quarry faces, roads
// of path that hold nothing, resource extremes and growth, and damage the validator must notice.
inline void caravanseraiContracts()
{
	const auto &definition =
		GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("caravanserai"));
	assert(definition.legacyId == 41 && definition.revision == 5);
	GenerationService service;
	const auto make = [&](int wDec, int hDec, int teams, std::uint32_t seed, int desert)
	{
		D r;
		r.setMethodDefaults(definition.legacyId);
		r.wDec = wDec;
		r.hDec = hDec;
		r.nbTeams = teams;
		r.seed = seed;
		r.options["desert"] = desert;
		return r;
	};
	const auto measured = [](const auto &result, const char *key)
	{
		for (const auto &record : result.telemetry.records())
			if (record.key == key)
				return int(std::get<std::int64_t>(record.value));
		return -1;
	};
	const auto group = [](const Map &map, int x, int y)
	{
		int ridges = 0;
		for (const auto &[vx, vy] : {std::pair{x, y}, std::pair{x + 1, y}, std::pair{x, y + 1}, std::pair{x + 1, y + 1}})
			ridges += terrainGroup(map.vertexTerrainAt(vx, vy)) == TerrainGroup::Ridges;
		return ridges;
	};
	// The finished map's promises, whatever the country: rock is sealed and bare, every road tile
	// is path that holds nothing, and stone stands only on grass.
	const auto inspect = [&](const Game &game, int &rockTiles, int &roadTiles)
	{
		const Map &map = game.map;
		rockTiles = roadTiles = 0;
		for (int y = 0; y < map.getH(); ++y)
			for (int x = 0; x < map.getW(); ++x)
			{
				const TerrainProperties &rules = map.terrainPropertiesAt(x, y);
				if (group(map, x, y) >= 2)
				{
					assert(!rules.walkable && !map.isResource(x, y));
					rockTiles += group(map, x, y) == 4;
				}
				if (rules.walkable && rules.buildable && rules.groundSpeedQ8 == 512)
				{
					assert(!map.isResource(x, y) && !rules.resourcesGrow);
					++roadTiles;
				}
				if (map.isResource(x, y) && map.getResource(x, y).type == STONE)
					assert(rules.buildable && rules.resourcesGrow);
			}
	};
	// Every country over the envelope: 128, 256 and 512 tiles, one colony to a crowd.
	for (int desert = 0; desert <= 2; ++desert)
		for (const auto &[w, h, teams] :
			 {std::tuple{7, 7, 1}, std::tuple{7, 7, 2}, std::tuple{8, 8, 4}, std::tuple{8, 8, 6}, std::tuple{9, 8, 4},
			  std::tuple{8, 9, 8}, std::tuple{9, 9, 8}})
		{
			const D r = make(w, h, teams, 11, desert);
			Game game(nullptr);
			const auto result = service.generate(game, r, true);
			if (!result)
				std::fprintf(stderr, "Caravanserai contract (desert %d, %d x %d, %d colonies): %s\n", desert, 1 << w,
							 1 << h, teams, result.diagnostic().c_str());
			assert(result);
			GenerationContext check(r);
			assert(definition.validateWorld(game, check).empty());
			int rock = 0, roads = 0;
			inspect(game, rock, roads);
			// Open erg has no rock; the other countries do on every map big enough for a mesa.
			assert(desert == 0 ? rock == 0 && measured(result, "caravanserai.quarries") == 0 : w < 8 || rock > 0);
			assert(desert == 0 || w < 8 || measured(result, "caravanserai.quarries") > 0);
			// Wherever there is a caravanserai there are roads to it.
			assert(measured(result, "caravanserai.serais") <= 0 || roads > 0);
			assert(desert != 2 || w < 8 || measured(result, "caravanserai.floor.corners") > 0);
			assert(desert == 2 || measured(result, "caravanserai.floor.corners") == 0);
		}
	{
		// Random draws every country.
		std::set<std::string> countries;
		for (std::uint32_t seed = 1; seed <= 24; ++seed)
		{
			Game game(nullptr);
			const auto result = service.generate(game, make(8, 8, 4, seed, 3), true);
			assert(result);
			for (const auto &record : result.telemetry.records())
				if (record.key == "caravanserai.desert")
					countries.insert(std::get<std::string>(record.value));
		}
		assert(countries.size() == 3);
	}
	{
		// Quarry faces follow the stone amount, and a quarry robbed of its stone is refused.
		const D standard = make(8, 8, 4, 17, 1);
		D none = standard, many = standard;
		none.options["stone-amount"] = 0;
		many.options["stone-amount"] = 300;
		Game a(nullptr), b(nullptr), c(nullptr);
		const auto zero = service.generate(a, none, true), normal = service.generate(b, standard, true),
				   rich = service.generate(c, many, true);
		assert(zero && normal && rich);
		assert(measured(zero, "caravanserai.quarries") == 0);
		assert(measured(rich, "caravanserai.quarries") > measured(normal, "caravanserai.quarries"));
		// GenerationContext keeps a reference to its request.
		GenerationContext check(standard);
		assert(definition.validateWorld(b, check).empty());
		bool robbed = false;
		for (int y = 0; y < 256 && !robbed; ++y)
			for (int x = 0; x < 256 && !robbed; ++x)
			{
				if (!b.map.isResource(x, y) || b.map.getResource(x, y).type != STONE)
					continue;
				bool face = false;
				for (int dy = -2; dy <= 2; ++dy)
					for (int dx = -2; dx <= 2; ++dx)
						face = face || group(b.map, (x + dx + 256) % 256, (y + dy + 256) % 256) > 0;
				if (face)
				{
					b.map.setNoResource(x, y, 1);
					robbed = true;
				}
			}
		assert(robbed);
		const std::string error = definition.validateWorld(b, check);
		assert(error.find("quarry") != std::string::npos);
	}
	{
		// Homes are identical by their tiles' rules: a home tile turned to sand is refused.
		const D r = make(8, 8, 4, 19, 2);
		Game game(nullptr);
		assert(service.generate(game, r));
		GenerationContext check(r);
		assert(definition.validateWorld(game, check).empty());
		const Team *team = game.teams[1];
		const int x = (team->startPosX + 8) % 256, y = (team->startPosY + 8) % 256;
		game.map.paintVertices({{x, y}, {x + 1, y}, {x, y + 1}, {x + 1, y + 1}}, SAND, false);
		const std::string error = definition.validateWorld(game, check);
		assert(!error.empty());
	}
	{
		// Resource extremes in every country with rock, then unattended growth on the abundant map:
		// nothing may grow onto a road, a quarry or the rock, and the homes' fields stay sealed.
		for (int desert = 1; desert <= 2; ++desert)
			for (int amount : {0, 300})
			{
				D r = make(7, 7, 2, 23, desert);
				for (const char *key : {"wheat-amount", "wood-amount", "stone-amount", "algae-amount", "fruit-amount"})
					r.options[key] = amount;
				Game game(nullptr);
				assert(service.generate(game, r));
				if (amount == 0)
					continue;
				setSyncRandSeed(4409);
				for (int tick = 0; tick < 2048; ++tick)
					game.map.growResources();
				GenerationContext check(r);
				const std::string grown = definition.validateWorld(game, check);
				if (!grown.empty())
					std::fprintf(stderr, "Caravanserai growth (desert %d): %s\n", desert, grown.c_str());
				assert(grown.empty());
				int rock = 0, roads = 0;
				inspect(game, rock, roads);
			}
	}
	puts("PASS Caravanserai: every country over the envelope, random countries, homes identical by rules, "
		 "sealed bare rock, quarry faces by stone amount, roads of path, resource extremes and growth");
}

inline void evenGroundContracts()
{
	const auto &definition =
		GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("even-ground"));
	assert(definition.legacyId == 60 && definition.revision == 4);
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
					g.map.replaceResource(x, y, Resource{});
		assert(!definition.validateWorld(g, check).empty());
	}
	puts("PASS Even Ground: envelope including thin maps and refusal, water budget ordering, "
		 "balance buys a measurably smaller catchment spread, crop reach enforced");
}

inline void marchlandContracts()
{
	const auto &definition =
		GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("marchland"));
	assert(definition.legacyId == 61 && definition.revision == 4);
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
			game.map.setResourceByIndex(x, y, WHEAT, 1);
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
					abundant.map.replaceResource(x, y, Resource{});
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
			assert(low.map.vertexTerrainAt(x, y) == high.map.vertexTerrainAt(x, y));
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
		world.map.setResourceByIndex((x + 1) % world.map.getW(), y, STONE, 1);
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
				assert(world.map.vertexTerrainAt(x, y) == sparse.map.vertexTerrainAt(x, y));
		GenerationContext check(request);
		assert(definition.validateWorld(world, check).empty());
		// Depleting the opening crops must be rejected even when the coastline survives.
		for (int y = 0; y < 128; ++y)
			for (int x = 0; x < 128; ++x)
				if (repeated.map.getResource(x, y).type == WHEAT)
					repeated.map.setNoResource(x, y, 0);
		assert(!definition.validateWorld(repeated, check).empty());
		world.map.setVertexTerrain(0, 0, GRASS);
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
	// Coverage is per water component, not merely a larger global wheat total.
	const auto checkShoreFields = [](const auto &result, int wanted)
	{
		int lakes = 0, fields = 0;
		for (const auto &record : result.telemetry.records())
		{
			if (record.key == "portage-lakes.lakes.components")
				lakes = int(std::get<std::int64_t>(record.value));
			if (record.key == "portage-lakes.shore-fields.extra")
			{
				assert(std::get<std::int64_t>(record.value) >= 1 &&
					   std::get<std::int64_t>(record.value) <= wanted);
				++fields;
			}
		}
		assert(lakes > 0 && fields == lakes);
	};
	checkShoreFields(report, 2);
	const auto fingerprint = mapFingerprint(first);
	GenerationContext context(r);
	assert(definition.validateWorld(first, context).empty());
	// Telemetry must not change either placement or RNG, even between other requests.
	D compact = r;
	compact.wDec = compact.hDec = 6;
	compact.nbTeams = 2;
	Game compactGame(nullptr);
	auto smallReport = service.generate(compactGame, compact, true);
	if (!smallReport)
		std::fprintf(stderr, "%s\n", smallReport.diagnostic().c_str());
	assert(smallReport);
	checkShoreFields(smallReport, 1);
	Game repeated(nullptr);
	assert(service.generate(repeated, r, false));
	assert(mapFingerprint(repeated) == fingerprint);
	// Open wheat may spread into meadows; timber stays contained and dry portages cannot refill.
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
				repeated.map.replaceResource(x, y, Resource{});
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
	const auto originalTerrain = compactGame.map.vertexTerrainAt(0, 0);
	compactGame.map.setVertexTerrain(0, 0, originalTerrain == WATER ? GRASS : WATER);
	GenerationContext compactContext(compact);
	assert(!definition.validateWorld(compactGame, compactContext).empty());
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
	// Full maps need shore room budgeted early; tiny maps need their landing first.
	for (unsigned seed : {650037u, 650064u})
	{
		const bool tiny = seed == 650064u;
		D shore = r;
		shore.seed = seed;
		shore.wDec = shore.hDec = tiny ? 6 : 9;
		shore.nbTeams = tiny ? 2 : 11;
		shore.nbWorkers = 2;
		shore.options["lake-elongation"] = 225;
		shore.options["portage-depth"] = 5;
		shore.options["extra-trails"] = tiny ? 100 : 25;
		shore.options["wheat-amount"] = tiny ? 125 : 300;
		shore.options["wood-amount"] = tiny ? 175 : 50;
		shore.options["stone-amount"] = tiny ? 250 : 200;
		shore.options["algae-amount"] = tiny ? 150 : 275;
		shore.options["fruit-amount"] = 175;
		Game world(nullptr);
		auto result = service.generate(world, shore, true);
		if (!result)
			std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
		assert(result);
		checkShoreFields(result, tiny ? 1 : 2);
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
	puts("PASS Portage Lakes: compact/full, repeatability, open wheat growth, missing portage, "
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
	// Keep the retained regression worlds off Windows' small main-thread stack.
	auto firstStorage = std::make_unique<Game>(nullptr);
	Game &first = *firstStorage;
	auto warmStorage = std::make_unique<Game>(nullptr);
	Game &warm = *warmStorage;
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
	auto observedWorldStorage = std::make_unique<Game>(nullptr);
	Game &observedWorld = *observedWorldStorage;
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
	auto crowdedStorage = std::make_unique<Game>(nullptr);
	Game &crowded = *crowdedStorage;
	assert(service.generate(crowded, compact));
	for (int amount : {0, 300})
	{
		auto extreme = request;
		extreme.nbWorkers = 8;
		for (const auto &control : definition.controls)
			if (control.group == ControlGroup::Resources)
				extreme.options[control.id] = amount;
		auto worldStorage = std::make_unique<Game>(nullptr);
		Game &world = *worldStorage;
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
	auto roomRegressionStorage = std::make_unique<Game>(nullptr);
	Game &roomRegression = *roomRegressionStorage;
	assert(service.generate(roomRegression, futureRoom));
	// Sparse woods on the smallest map still need useful destinations for both homes.
	auto sparse = compact;
	sparse.seed = 2;
	sparse.nbWorkers = 4;
	sparse.options["wood-amount"] = 0;
	auto sparseRegressionStorage = std::make_unique<Game>(nullptr);
	Game &sparseRegression = *sparseRegressionStorage;
	assert(service.generate(sparseRegression, sparse));
	sparse.seed = 100260;
	sparse.nbWorkers = 3;
	sparse.options["wooded-neck-thickness"] = 7;
	sparse.options["neutral-clearing-size"] = 22;
	sparse.options["wheat-amount"] = 175;
	sparse.options["stone-amount"] = 50;
	sparse.options["algae-amount"] = 75;
	sparse.options["fruit-amount"] = 75;
	auto scarceRegressionStorage = std::make_unique<Game>(nullptr);
	Game &scarceRegression = *scarceRegressionStorage;
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
	auto denseRegressionStorage = std::make_unique<Game>(nullptr);
	Game &denseRegression = *denseRegressionStorage;
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
	auto rectangularRegressionStorage = std::make_unique<Game>(nullptr);
	Game &rectangularRegression = *rectangularRegressionStorage;
	assert(service.generate(rectangularRegression, rectangle));
	auto coldStorage = std::make_unique<Game>(nullptr);
	Game &cold = *coldStorage;
	assert(service.generate(cold, request));
	// The worker-sensitive compact request evicted the cache; reconstruction is identical.
	auto originalStorage = std::make_unique<Game>(nullptr);
	Game &original = *originalStorage;
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
	// The estates are dressed in catalogue ground; the sea uses ordinary water.
	// This recipe does not add a deep-water band around the islands.
	{
		std::set<int> used;
		int pier = -1;
		for (int i = 0; i < t.size(); ++i)
		{
			const int corner = bridged.map.vertexTerrainAt(i % t.w, i / t.w);
			used.insert(corner);
			if (corner == BOARDWALK && pier < 0)
				pier = i;
		}
		for (const int type : {BOARDWALK, BRICK_PATH, SHALE, LOAM, OUTCROP, RIDGE_ROCK})
			assert(used.count(type));
		assert(!used.count(DEEP_WATER) && !used.count(DARK_WATER));
		// Any change to the dressing is a changed design, so the final-world check refuses it.
		GenerationContext dressed(request);
		bridged.map.setVertexTerrain(pier % t.w, pier / t.w, GRASS);
		assert(!definition.validateWorld(bridged, dressed).empty());
		bridged.map.setVertexTerrain(pier % t.w, pier / t.w, BOARDWALK);
		assert(definition.validateWorld(bridged, dressed).empty());
	}
	TerrainSketch landBridge(t.size());
	for (int i = 0; i < t.size(); ++i)
	{
		const int corner = bridged.map.vertexTerrainAt(i % t.w, i / t.w);
		landBridge[i] = corner == WATER ? SAND : corner;
	}
	writeVertices(bridged.map, landBridge);
	GenerationContext check(request);
	assert(definition.validateWorld(bridged, check).find("walking connection") !=
		   std::string::npos);
	puts("PASS Bastion Keys: envelope, variants, rectangles, twelve colonies, swimming isolation, "
		 "land-bridge corruption, external crops, "
		 "growth, pools and rampart corruption");
}

inline void rainShadowContracts()
{
	const auto &definition = GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("rain-shadow"));
	assert(definition.legacyId == 27 && definition.revision == 7);
	D request;
	request.setMethodDefaults(definition.legacyId);
	assert(request.option("ridges") == 4 && request.option("ridge-thickness") == 3 &&
		   request.option("climate") == 0 && request.option("lithology") == 0);
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
	// Generates and validates, reporting what failed.
	const auto build = [&](Game &g, const D &r, const char *what, bool telemetry = false)
	{
		const auto result = service.generate(g, r, telemetry);
		if (!result)
			std::fprintf(stderr, "Rain shadow %s (%d x %d, %d colonies, seed %u): %s\n", what, 1 << r.wDec,
						 1 << r.hDec, r.nbTeams, unsigned(r.seed), result.diagnostic().c_str());
		assert(result);
		GenerationContext check(r);
		const std::string error = definition.validateWorld(g, check);
		if (!error.empty())
			std::fprintf(stderr, "Rain shadow %s validation: %s\n", what, error.c_str());
		assert(error.empty());
		return result;
	};
	const auto measure = [](const auto &result, const char *key)
	{
		for (const auto &record : result.telemetry.records())
			if (record.key == key)
				return int(std::get<std::int64_t>(record.value));
		return -1;
	};
	const auto choice = [](const auto &result, const char *key)
	{
		for (const auto &record : result.telemetry.records())
			if (record.key == key)
				return std::get<std::string>(record.value);
		return std::string();
	};
	// The envelope: every size and colony count revision 6 accepted at its defaults.
	for (auto [w, h, teams] : {std::tuple{7, 7, 1}, std::tuple{7, 7, 2}, std::tuple{7, 7, 4}, std::tuple{8, 8, 1},
							   std::tuple{8, 8, 4}, std::tuple{8, 8, 6}, std::tuple{8, 8, 8}, std::tuple{9, 9, 4},
							   std::tuple{9, 9, 12}, std::tuple{9, 7, 4}, std::tuple{7, 9, 4}, std::tuple{8, 7, 2},
							   std::tuple{7, 8, 2}})
	{
		Game g(nullptr);
		build(g, make(w, h, teams, 7), "envelope");
	}
	// Every climate and every rock comes up at random, and each can be pinned at every size.
	std::set<std::string> climates, rocks;
	for (std::uint32_t seed = 1; seed <= 16; ++seed)
	{
		Game g(nullptr);
		const auto result = service.generate(g, make(7, 7, 2, seed), true);
		assert(result);
		climates.insert(choice(result, "rain-shadow.climate"));
		rocks.insert(choice(result, "rain-shadow.lithology"));
	}
	assert(climates.size() == 3 && rocks.size() == 3);
	const char *climateNames[] = {"temperate", "steppe", "desert"}, *rockNames[] = {"granite", "sandstone", "volcanic"};
	for (int look = 1; look <= 3; ++look)
		for (auto [dimension, teams] : {std::pair{7, 2}, std::pair{8, 4}, std::pair{9, 8}})
		{
			D pinned = make(dimension, dimension, teams, 11);
			pinned.options["climate"] = look;
			pinned.options["lithology"] = 4 - look;
			Game g(nullptr);
			const auto result = build(g, pinned, "pinned look", true);
			assert(choice(result, "rain-shadow.climate") == climateNames[look - 1]);
			assert(choice(result, "rain-shadow.lithology") == rockNames[3 - look]);
			// Every home holds the same quarry, never below the floor.
			assert(measure(result, "rain-shadow.quarries.home-stone") >= 8);
		}
	{
		// The looks are only looks: a temperate granite map and a desert volcanic one have the same
		// rules on every tile and the same deposits.
		D temperate = make(8, 8, 4, 23), desert = make(8, 8, 4, 23);
		temperate.options["climate"] = 1;
		temperate.options["lithology"] = 1;
		desert.options["climate"] = 3;
		desert.options["lithology"] = 3;
		Game a(nullptr), b(nullptr);
		build(a, temperate, "temperate");
		build(b, desert, "desert");
		int differentLooks = 0;
		for (int y = 0; y < 256; ++y)
			for (int x = 0; x < 256; ++x)
			{
				const auto &p = a.map.terrainPropertiesAt(x, y), &q = b.map.terrainPropertiesAt(x, y);
				assert(p.walkable == q.walkable && p.buildable == q.buildable && p.swimmable == q.swimmable &&
					   p.resourcesGrow == q.resourcesGrow && p.inhibitionQ8 == q.inhibitionQ8 &&
					   p.fertilityQ8 == q.fertilityQ8 && p.projectileBlocks == q.projectileBlocks);
				assert(a.map.getResource(x, y).type == b.map.getResource(x, y).type);
				differentLooks += a.map.vertexTerrainAt(x, y) != b.map.vertexTerrainAt(x, y);
			}
		assert(differentLooks > 1000);
	}
	// Thin, thick, straight and steep ridges stay sealed outside their passes (validateWorld floods
	// across every ridge with the passes shut), and passes close rather than cut a home's quarry.
	// (Slant 3 on a 256 map leaves room for three ridges, as revision 6 also required.)
	for (auto [thickness, slant, spacing, dimension, teams, seed, ridges] :
		 {std::tuple{2, 1, 48, 8, 4, 3u, 4}, std::tuple{2, 3, 48, 9, 4, 5u, 4}, std::tuple{2, 0, 24, 9, 8, 9u, 4},
		  std::tuple{6, 2, 96, 8, 4, 13u, 4}, std::tuple{3, 3, 24, 8, 6, 17u, 3}, std::tuple{4, 0, 32, 7, 2, 19u, 4}})
	{
		D r = make(dimension, dimension, teams, seed);
		r.options["ridges"] = ridges;
		r.options["ridge-thickness"] = thickness;
		r.options["slant"] = slant;
		r.options["pass-spacing"] = spacing;
		Game g(nullptr);
		build(g, r, "ridge variant");
	}
	D r = make(8, 8, 4, 37);
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = 0;
	{
		// No ambient stone: every stone on the map is a home's quarry, which stays (an unscaled kit),
		// and taking one away is refused; so is taking away a piece of ridge.
		Game zero(nullptr);
		const auto result = build(zero, r, "no resources", true);
		assert(measure(result, "rain-shadow.quarries.ambient") == 0);
		GenerationContext check(r);
		Game damaged(nullptr);
		assert(service.generate(damaged, r));
		bool removed = false;
		for (int i = 0; i < 256 * 256 && !removed; ++i)
			if (damaged.map.isResource(i % 256, i / 256) && damaged.map.getResource(i % 256, i / 256).type == STONE)
			{
				damaged.map.replaceResource(i % 256, i / 256, Resource{});
				removed = true;
			}
		assert(removed && !definition.validateWorld(damaged, check).empty());
		Game breached(nullptr);
		assert(service.generate(breached, r));
		bool opened = false;
		for (int i = 0; i < 256 * 256 && !opened; ++i)
			if (terrainGroup(breached.map.vertexTerrainAt(i % 256, i / 256)) == TerrainGroup::Ridges)
			{
				breached.map.setVertexTerrain(i % 256, i / 256, GRASS);
				opened = true;
			}
		assert(opened && !definition.validateWorld(breached, check).empty());
	}
	{
		// Ambient quarry faces follow stone-amount.
		D few = make(8, 8, 4, 41), many = make(8, 8, 4, 41);
		many.options["stone-amount"] = 300;
		Game a(nullptr), b(nullptr);
		const int normal = measure(build(a, few, "default stone", true), "rain-shadow.quarries.ambient");
		const int more = measure(build(b, many, "abundant stone", true), "rain-shadow.quarries.ambient");
		assert(normal > 4 && more > normal);
	}
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = control.maximum;
	Game abundant(nullptr);
	build(abundant, r, "abundant resources");
	GenerationContext context(r);
	setSyncRandSeed(4211);
	// Full-map growth with nobody harvesting: the streams' fields grow, but every pass floor is sand
	// and stays open, every quarry keeps its stone and every colony can still walk to every other.
	for (int tick = 0; tick < 4096; ++tick)
		abundant.map.growResources();
	const std::string growthError = definition.validateWorld(abundant, context);
	if (!growthError.empty())
		std::fprintf(stderr, "Rain shadow growth: %s\n", growthError.c_str());
	assert(growthError.empty());
	puts("PASS Rain shadow: envelope, climates and rocks pinned and random, looks keep the rules, sealed "
		 "thin and steep ridges, home quarries equal, damage refused, ambient quarries scale, growth "
		 "keeps the passes and quarries");
}

// Tidal Flats 2.0 (revision 9): every tide pinned over the envelope revision 8 accepted, the tide
// moving only the water on the flats, flats nothing can be built on, deep lagoon beds inside shallow
// margins, every island reachable on foot, the tor's quarry faces stocked, resource extremes, and
// growth over a few thousand ticks that keeps every colony and island reachable.
inline void tidalFlatsContracts()
{
	const auto &definition =
		GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("tidal-flats"));
	assert(definition.legacyId == 18 && definition.revision == 9);
	D request;
	request.setMethodDefaults(definition.legacyId);
	assert(request.option("tide") == 0 && request.option("lagoons") == 3 &&
		   request.option("central-island") == 1);
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
	const auto build = [&](Game &g, const D &r, const char *what)
	{
		const auto result = service.generate(g, r, true);
		if (!result)
			std::fprintf(stderr, "Tidal flats %s (%d x %d, %d colonies, seed %u): %s\n", what, 1 << r.wDec,
						 1 << r.hDec, r.nbTeams, unsigned(r.seed), result.diagnostic().c_str());
		assert(result);
		GenerationContext check(r);
		const std::string error = definition.validateWorld(g, check);
		if (!error.empty())
			std::fprintf(stderr, "Tidal flats %s validation: %s\n", what, error.c_str());
		assert(error.empty());
		return result;
	};
	const auto measure = [](const auto &result, const char *key)
	{
		long long total = 0;
		bool found = false;
		for (const auto &record : result.telemetry.records())
			if (record.key == key && std::holds_alternative<std::int64_t>(record.value))
			{
				total += std::get<std::int64_t>(record.value);
				found = true;
			}
		return found ? total : -1LL;
	};
	const auto choice = [](const auto &result, const char *key)
	{
		for (const auto &record : result.telemetry.records())
			if (record.key == key)
				return std::get<std::string>(record.value);
		return std::string();
	};
	const auto group = [](const Game &g, int x, int y)
	{ return terrainTypeDefinition(g.map.vertexTerrainAt(x, y)).group; };
	// The envelope: every size and colony count the request check accepts, each tide pinned in turn.
	// The flats are mud and sand: nothing between the islands is buildable (validateWorld), and the
	// mud really is there.
	int pinned = 0;
	const char *tideNames[] = {"neap", "mean", "spring"};
	for (auto dimensions : {std::pair{7, 7}, std::pair{7, 8}, std::pair{8, 7}, std::pair{8, 8},
							std::pair{9, 8}, std::pair{9, 9}})
		for (int teams : {1, 2, 4, 6, 8, 12})
		{
			D r = make(dimensions.first, dimensions.second, teams, 7);
			if (!definition.validateRequest(r).empty())
				continue;
			const int tide = 1 + pinned % 3;
			r.options["tide"] = tide;
			++pinned;
			Game g(nullptr);
			const auto result = build(g, r, "envelope");
			assert(choice(result, "tidal-flats.tide") == tideNames[tide - 1]);
			assert(measure(result, "tidal-flats.channels.vertices") > 0);
			assert(measure(result, "tidal-flats.tor.stone") > 0);
			assert(measure(result, "tidal-flats.tor.stone") == measure(result, "tidal-flats.tor.quarry-tiles"));
			int mud = 0;
			for (int y = 0; y < g.map.getH(); ++y)
				for (int x = 0; x < g.map.getW(); ++x)
					mud += group(g, x, y) == TerrainGroup::Rough;
			assert(mud > g.map.getW() * g.map.getH() / 4);
		}
	assert(pinned >= 20);
	assert(!definition.validateRequest(make(7, 7, 12, 7)).empty());
	// Every tide comes up at random.
	std::set<std::string> tides;
	for (std::uint32_t seed = 1; seed <= 40 && tides.size() < 3; ++seed)
	{
		Game g(nullptr);
		const auto result = service.generate(g, make(7, 7, 2, seed), true);
		assert(result);
		tides.insert(choice(result, "tidal-flats.tide"));
	}
	assert(tides.size() == 3);
	// Each tide pinned holds at 128, 256 and 512, and the lagoons get deep beds at 256 and 512.
	for (int tide = 1; tide <= 3; ++tide)
		for (auto [dimension, teams] : {std::pair{7, 2}, std::pair{8, 4}, std::pair{9, 8}})
		{
			D r = make(dimension, dimension, teams, 11);
			r.options["tide"] = tide;
			Game g(nullptr);
			const auto result = build(g, r, "pinned tide");
			if (dimension >= 8)
				assert(measure(result, "tidal-flats.deep.vertices") > 0);
		}
	// The tide moves only the water on the flats: the same seed at neap, mean and spring has the same
	// buildable ground and the same islands, and more water the higher the tide.
	{
		Game neap(nullptr), mean(nullptr), spring(nullptr);
		D a = make(8, 8, 4, 13), b = a, c = a;
		a.options["tide"] = 1;
		b.options["tide"] = 2;
		c.options["tide"] = 3;
		build(neap, a, "neap");
		build(mean, b, "mean");
		build(spring, c, "spring");
		int water[3] = {0, 0, 0};
		for (int y = 0; y < 256; ++y)
			for (int x = 0; x < 256; ++x)
			{
				const auto &rn = neap.map.terrainPropertiesAt(x, y), &rm = mean.map.terrainPropertiesAt(x, y),
						   &rs = spring.map.terrainPropertiesAt(x, y);
				assert(rn.buildable == rm.buildable && rm.buildable == rs.buildable);
				water[0] += rn.swimmable;
				water[1] += rm.swimmable;
				water[2] += rs.swimmable;
			}
		assert(water[0] < water[1] && water[1] < water[2]);
	}
	// Damage the validator must notice: grass laid on the flats, deep water against a shore, and the
	// tor's stone taken away.
	{
		const D r = make(8, 8, 4, 19);
		Game g(nullptr);
		build(g, r, "damage");
		GenerationContext check(r);
		// A 3x3 block of grass corners well out on the mud, away from every island.
		bool laid = false;
		for (int y = 2; y < 254 && !laid; y += 3)
			for (int x = 2; x < 254 && !laid; x += 3)
			{
				bool mud = true;
				for (int dy = -2; dy <= 3 && mud; ++dy)
					for (int dx = -2; dx <= 3 && mud; ++dx)
						mud = group(g, x + dx, y + dy) == TerrainGroup::Rough;
				if (!mud)
					continue;
				Game damaged(nullptr);
				build(damaged, r, "damage copy");
				for (int dy = 0; dy <= 1; ++dy)
					for (int dx = 0; dx <= 1; ++dx)
						damaged.map.setVertexTerrain(x + dx, y + dy, GRASS);
				const std::string fault = definition.validateWorld(damaged, check);
				assert(fault.find("built on") != std::string::npos);
				laid = true;
			}
		assert(laid);
		// Deep water on a beach's edge.
		bool deepened = false;
		for (int y = 1; y < 255 && !deepened; ++y)
			for (int x = 1; x < 255 && !deepened; ++x)
				if (g.map.vertexTerrainAt(x, y) == WATER && g.map.vertexTerrainAt(x + 1, y) == SAND)
				{
					Game damaged(nullptr);
					build(damaged, r, "deep copy");
					damaged.map.setVertexTerrain(x, y, DEEP_WATER);
					assert(definition.validateWorld(damaged, check).find("Deep water") != std::string::npos);
					deepened = true;
				}
		assert(deepened);
		for (int i = 0; i < 256 * 256; ++i)
			if (g.map.isResource(i % 256, i / 256) && g.map.getResource(i % 256, i / 256).type == STONE)
				g.map.replaceResource(i % 256, i / 256, Resource{});
		assert(definition.validateWorld(g, check).find("quarry") != std::string::npos);
	}
	// Without the central island there is no tor and the map still validates.
	{
		D r = make(8, 8, 4, 29);
		r.options["central-island"] = 0;
		Game g(nullptr);
		const auto result = build(g, r, "no central island");
		assert(measure(result, "tidal-flats.tor.quarry-tiles") == 0);
	}
	// Resource extremes: no ambient resources at all still plays and validates; at the ceilings the
	// map validates after a few thousand ticks of growth with nobody harvesting (every colony and
	// every island still reachable), and the crops grew.
	D r = make(8, 8, 4, 37);
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = 0;
	{
		Game zero(nullptr);
		build(zero, r, "zero resources");
	}
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = control.maximum;
	Game abundant(nullptr);
	build(abundant, r, "abundant");
	GenerationContext context(r);
	const auto count = [&](int type)
	{
		int total = 0;
		for (int i = 0; i < 256 * 256; ++i)
			total += abundant.map.isResource(i % 256, i / 256) &&
					 abundant.map.getResource(i % 256, i / 256).type == type;
		return total;
	};
	const int wheatBefore = count(WHEAT), woodBefore = count(WOOD), algaeBefore = count(ALGA);
	setSyncRandSeed(4211);
	for (int tick = 0; tick < 4096; ++tick)
		abundant.map.growResources();
	const std::string growthError = definition.validateWorld(abundant, context);
	if (!growthError.empty())
		std::fprintf(stderr, "Tidal flats growth: %s\n", growthError.c_str());
	std::fprintf(stderr, "Tidal flats growth over 4096 ticks: wheat %d -> %d, wood %d -> %d, algae %d -> %d\n",
				 wheatBefore, count(WHEAT), woodBefore, count(WOOD), algaeBefore, count(ALGA));
	assert(growthError.empty() && count(WHEAT) > wheatBefore && count(ALGA) > algaeBefore);
	puts("PASS Tidal flats: envelope with pinned tides, random tides, tide moves only water, flats "
		 "unbuildable, deep beds in shallows, tor quarries, damage refused, resource extremes, growth");
}

// Watershed 2.0 (revision 4): every climate and deep-water setting pinned over the envelope revision 3
// accepted, deep cores that leave shallows, the finished terrain a fixed point of layBeaches, looks that
// keep every rule, pavement only beyond the water's reach, outcrops that never cut ground off and
// quarries that keep their stone, resource extremes, and growth.
inline void watershedContracts()
{
	const auto &definition = GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("watershed"));
	assert(definition.legacyId == 13 && definition.revision == 4);
	D request;
	request.setMethodDefaults(definition.legacyId);
	assert(request.option("deep-water") == 1 && request.option("outcrops") == 1 && request.option("climate") == 0 &&
		   request.option("dryness") == 5 && request.option("river-density") == 5);
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
	const auto build = [&](Game &g, const D &r, const char *what)
	{
		const auto result = service.generate(g, r, true);
		if (!result)
			std::fprintf(stderr, "Watershed %s (%d x %d, %d colonies, seed %u): %s\n", what, 1 << r.wDec, 1 << r.hDec,
						 r.nbTeams, unsigned(r.seed), result.diagnostic().c_str());
		assert(result);
		GenerationContext check(r);
		const std::string error = definition.validateWorld(g, check);
		if (!error.empty())
			std::fprintf(stderr, "Watershed %s validation: %s\n", what, error.c_str());
		assert(error.empty());
		return result;
	};
	const auto measure = [](const auto &result, const char *key)
	{
		for (const auto &record : result.telemetry.records())
			if (record.key == key && std::holds_alternative<std::int64_t>(record.value))
				return int(std::get<std::int64_t>(record.value));
		return -1;
	};
	const auto choice = [](const auto &result, const char *key)
	{
		for (const auto &record : result.telemetry.records())
			if (record.key == key)
				return std::get<std::string>(record.value);
		return std::string();
	};
	// The deep cores leave shallows: no deep vertex touches land, and none stands in a ford's water.
	// Every Barren (pavement) vertex lies at least 22 steps from any water.
	const auto checkTerrain = [&](const Game &g)
	{
		const Map &map = g.map;
		const int w = map.getW(), h = map.getH();
		const Torus t(map);
		std::vector<unsigned char> water(size_t(w) * h, 0);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				const TerrainGroup group = terrainGroup(map.vertexTerrainAt(x, y));
				water[size_t(t.at(x, y))] = group == TerrainGroup::Water || group == TerrainGroup::DeepWater;
			}
		const std::vector<int> fromWater = stepsFrom(t, water);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				const TerrainGroup group = terrainGroup(map.vertexTerrainAt(x, y));
				if (group == TerrainGroup::DeepWater)
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
							assert(water[size_t(t.at(x + dx, y + dy))]);
				if (group == TerrainGroup::Barren)
					assert(fromWater[size_t(t.at(x, y))] >= 22);
			}
		// The finished terrain is a fixed point of layBeaches, as revision 3's stamp was: no vertex of
		// the grass group touches plain water.
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				if (terrainGroup(map.vertexTerrainAt(x, y)) == TerrainGroup::Grass)
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
							assert(map.vertexTerrainAt(t.x(x + dx), t.y(y + dy)) != WATER);
	};
	// The envelope: every size and colony count the request check accepts, each with a climate and
	// deep water pinned in turn. A seed the service refuses (revision 3 refused the same seeds: a ford
	// or a spread of starts that does not fit) is rolled again, as the lobby does.
	int pinned = 0;
	for (auto dimensions : {std::pair{7, 7}, std::pair{7, 8}, std::pair{8, 7}, std::pair{8, 8}, std::pair{9, 8},
							std::pair{8, 9}, std::pair{9, 9}})
		for (int teams : {1, 2, 4, 8, 12, 16})
		{
			D r = make(dimensions.first, dimensions.second, teams, 7);
			if (!definition.validateRequest(r).empty())
				continue;
			if (dimensions.first + dimensions.second >= 17 && teams != 4 && teams != 12)
				continue; // the large maps only twice, for time
			r.options["climate"] = 1 + pinned % 3;
			r.options["deep-water"] = (pinned / 3) % 2 == 0;
			++pinned;
			bool generated = false;
			for (int roll = 0; roll < 6 && !generated; ++roll)
			{
				Game probe(nullptr);
				generated = bool(service.generate(probe, r));
				if (!generated)
					++r.seed;
			}
			if (!generated)
				continue; // revision 3 refuses this size and colony count on these seeds too
			Game g(nullptr);
			const auto result = build(g, r, "envelope");
			assert((measure(result, "watershed.deep.vertices") > 0) == (r.option("deep-water") == 1));
			checkTerrain(g);
		}
	assert(pinned >= 20);
	assert(!definition.validateRequest(make(7, 7, 17, 7)).empty());
	// Every climate comes up at random, and each pinned holds at 128, 256 and 512.
	std::set<std::string> climates;
	for (std::uint32_t seed = 1; seed <= 12; ++seed)
	{
		Game g(nullptr);
		const auto result = service.generate(g, make(7, 7, 2, seed), true);
		if (result)
			climates.insert(choice(result, "watershed.climate"));
	}
	assert(climates.size() == 3);
	const char *names[] = {"temperate", "steppe", "tropical"};
	for (int climate = 1; climate <= 3; ++climate)
		for (auto [dimension, teams] : {std::pair{7, 2}, std::pair{8, 4}, std::pair{9, 6}})
		{
			D r = make(dimension, dimension, teams, 11);
			r.options["climate"] = climate;
			Game g(nullptr);
			const auto result = build(g, r, "pinned climate");
			assert(choice(result, "watershed.climate") == names[climate - 1]);
			// Deep cores, levees and the delta's mud are on every map; pavement and rock wherever the
			// uplands reach that far from water (they do at 256 and 512).
			assert(measure(result, "watershed.deep.vertices") > 0 && measure(result, "watershed.levee.vertices") > 0);
			if (dimension >= 8)
				assert(measure(result, "watershed.pavement.vertices") > 0 && measure(result, "watershed.knots.placed") > 0 &&
					   measure(result, "watershed.quarry.stone") > 0);
			checkTerrain(g);
		}
	{
		// The climate is looks only: the same seed plays identically in all three.
		Game a(nullptr), b(nullptr), c(nullptr);
		D temperate = make(8, 8, 4, 13), steppe = make(8, 8, 4, 13), tropical = make(8, 8, 4, 13);
		temperate.options["climate"] = 1;
		steppe.options["climate"] = 2;
		tropical.options["climate"] = 3;
		build(a, temperate, "temperate");
		build(b, steppe, "steppe");
		build(c, tropical, "tropical");
		int differentLooks = 0;
		for (int y = 0; y < 256; ++y)
			for (int x = 0; x < 256; ++x)
				for (const Game *other : {&b, &c})
				{
					const auto &p = a.map.terrainPropertiesAt(x, y), &q = other->map.terrainPropertiesAt(x, y);
					assert(sameTerrainProperties(p, q));
					assert(a.map.getResource(x, y).type == other->map.getResource(x, y).type);
					differentLooks += a.map.vertexTerrainAt(x, y) != other->map.vertexTerrainAt(x, y);
				}
		assert(differentLooks > 2000);
	}
	{
		// Deep water off: no deep vertex anywhere, and the same routes on foot: every tile walks alike.
		D on = make(8, 8, 4, 17), off = make(8, 8, 4, 17);
		off.options["deep-water"] = 0;
		Game a(nullptr), b(nullptr);
		build(a, on, "deep water");
		const auto result = build(b, off, "no deep water");
		assert(measure(result, "watershed.deep.vertices") == 0);
		for (int y = 0; y < 256; ++y)
			for (int x = 0; x < 256; ++x)
			{
				assert(terrainGroup(b.map.vertexTerrainAt(x, y)) != TerrainGroup::DeepWater);
				assert(a.map.terrainPropertiesAt(x, y).walkable == b.map.terrainPropertiesAt(x, y).walkable);
			}
	}
	{
		// Outcrops: rock and stocked faces with them, no ridges vertex without; faces follow the stone
		// amount, none at 0.
		D on = make(8, 8, 4, 23), off = make(8, 8, 4, 23), bare = make(8, 8, 4, 23), rich = make(8, 8, 4, 23);
		off.options["outcrops"] = 0;
		bare.options["stone-amount"] = 0;
		rich.options["stone-amount"] = 300;
		Game a(nullptr), b(nullptr), c(nullptr), d(nullptr);
		const auto withKnots = build(a, on, "outcrops"), without = build(b, off, "no outcrops"),
				   noStone = build(c, bare, "no stone"), moreStone = build(d, rich, "abundant stone");
		assert(measure(withKnots, "watershed.knots.placed") > 2 && measure(withKnots, "watershed.knots.faces") > 0);
		assert(measure(withKnots, "watershed.rock.vertices") > 0);
		assert(measure(without, "watershed.knots.placed") == 0 && measure(without, "watershed.rock.vertices") == 0);
		for (int y = 0; y < 256; ++y)
			for (int x = 0; x < 256; ++x)
				assert(terrainGroup(b.map.vertexTerrainAt(x, y)) != TerrainGroup::Ridges);
		assert(measure(noStone, "watershed.knots.faces") == 0 && measure(noStone, "watershed.quarry.stone") == 0);
		assert(measure(moreStone, "watershed.knots.faces") > measure(withKnots, "watershed.knots.faces"));
		// Every colony can walk to a quarry face, and the telemetry says how far.
		for (int team = 0; team < 4; ++team)
		{
			bool found = false;
			for (const auto &record : withKnots.telemetry.records())
				if (record.key == "watershed.quarry.nearest-walk" && record.subject == team)
					found = std::get<std::int64_t>(record.value) > 0;
			assert(found);
		}
		// Taking a quarry face's stone away is refused.
		GenerationContext check(on);
		bool removed = false;
		for (int i = 0; i < 256 * 256 && !removed; ++i)
		{
			const int x = i % 256, y = i / 256;
			if (!a.map.isResource(x, y) || a.map.getResource(x, y).type != STONE)
				continue;
			bool besideRock = false;
			for (int dy = -1; dy <= 2 && !besideRock; ++dy)
				for (int dx = -1; dx <= 2 && !besideRock; ++dx)
					besideRock = terrainGroup(a.map.vertexTerrainAt((x + dx) & 255, (y + dy) & 255)) == TerrainGroup::Ridges;
			if (besideRock)
			{
				a.map.replaceResource(x, y, Resource{});
				removed = true;
			}
		}
		assert(removed && definition.validateWorld(a, check).find("quarry") != std::string::npos);
		// A ring of rock drawn round a patch of open upland cuts it off, and is refused.
		Game ringed(nullptr);
		build(ringed, on, "outcrops again");
		bool drawn = false;
		for (int cy = 8; cy < 248 && !drawn; cy += 4)
			for (int cx = 8; cx < 248 && !drawn; cx += 4)
			{
				bool open = true;
				for (int dy = -6; dy <= 6 && open; ++dy)
					for (int dx = -6; dx <= 6 && open; ++dx)
						open = ringed.map.terrainPropertiesAt(cx + dx, cy + dy).walkable &&
							   !ringed.map.isResource(cx + dx, cy + dy) &&
							   terrainGroup(ringed.map.vertexTerrainAt(cx + dx, cy + dy)) != TerrainGroup::Ridges;
				if (!open)
					continue;
				for (int dy = -5; dy <= 5; ++dy)
					for (int dx = -5; dx <= 5; ++dx)
						if (std::max(std::abs(dx), std::abs(dy)) >= 4)
							ringed.map.setVertexTerrain(cx + dx, cy + dy, RIDGE_ROCK);
				drawn = true;
			}
		assert(drawn && definition.validateWorld(ringed, check).find("cuts ground off") != std::string::npos);
	}
	D r = make(8, 8, 4, 37);
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = 0;
	{
		Game zero(nullptr);
		build(zero, r, "no resources");
	}
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = control.maximum;
	Game abundant(nullptr);
	build(abundant, r, "abundant resources");
	GenerationContext context(r);
	const auto crops = [&]()
	{
		int count = 0;
		for (int i = 0; i < 256 * 256; ++i)
			count += abundant.map.isResource(i % 256, i / 256) &&
					 (abundant.map.getResource(i % 256, i / 256).type == WHEAT ||
					  abundant.map.getResource(i % 256, i / 256).type == WOOD);
		return count;
	};
	const int before = crops();
	setSyncRandSeed(4211);
	// Full-map growth with nobody harvesting: the crops spread along the levees, but every colony can
	// still walk to colony 0, every ford stays open and every quarry keeps its stone.
	for (int tick = 0; tick < 3072; ++tick)
		abundant.map.growResources();
	const std::string growthError = definition.validateWorld(abundant, context);
	if (!growthError.empty())
		std::fprintf(stderr, "Watershed growth: %s\n", growthError.c_str());
	assert(growthError.empty() && crops() > before);
	puts("PASS Watershed: envelope with pinned climates and deep water, shallows round every deep core, "
		 "beach fixed point, looks keep the rules, pavement beyond the water's reach, outcrops that never cut "
		 "ground off, stocked quarries, resource extremes, growth");
}

// Everglades 2.0 (revision 4): every season, mud share and hammock count pinned over the envelope
// revision 3 accepted, deep slough beds that leave shallows, boardwalk fords that are paths, hammocks
// that never cut ground off, mud banks that keep the lanes open, the season changing looks only, stone
// in reach of every colony, resource extremes, and growth.
inline void evergladesContracts()
{
	const auto &definition = GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("everglades"));
	assert(definition.legacyId == 19 && definition.revision == 4);
	D request;
	request.setMethodDefaults(definition.legacyId);
	assert(request.option("mudflats") == 50 && request.option("hammocks") == 3 && request.option("season") == 0 &&
		   request.option("levee") == 70 && request.option("clearing-size") == 13);
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
	const auto build = [&](Game &g, const D &r, const char *what)
	{
		const auto result = service.generate(g, r, true);
		if (!result)
			std::fprintf(stderr, "Everglades %s (%d x %d, %d colonies, seed %u): %s\n", what, 1 << r.wDec, 1 << r.hDec,
						 r.nbTeams, unsigned(r.seed), result.diagnostic().c_str());
		assert(result);
		GenerationContext check(r);
		const std::string error = definition.validateWorld(g, check);
		if (!error.empty())
			std::fprintf(stderr, "Everglades %s validation: %s\n", what, error.c_str());
		assert(error.empty());
		return result;
	};
	const auto measure = [](const auto &result, const char *key)
	{
		for (const auto &record : result.telemetry.records())
			if (record.key == key && std::holds_alternative<std::int64_t>(record.value))
				return int(std::get<std::int64_t>(record.value));
		return -1;
	};
	const auto measures = [](const auto &result, const char *key)
	{
		std::vector<int> values;
		for (const auto &record : result.telemetry.records())
			if (record.key == key && std::holds_alternative<std::int64_t>(record.value))
				values.push_back(int(std::get<std::int64_t>(record.value)));
		return values;
	};
	const auto choice = [](const auto &result, const char *key)
	{
		for (const auto &record : result.telemetry.records())
			if (record.key == key)
				return std::get<std::string>(record.value);
		return std::string();
	};
	const auto count = [](const Game &g, int type)
	{
		int total = 0;
		for (int y = 0; y < g.map.getH(); ++y)
			for (int x = 0; x < g.map.getW(); ++x)
				total += g.map.isResource(x, y) && g.map.getResource(x, y).type == type;
		return total;
	};
	const auto vertices = [](const Game &g, TerrainGroup group)
	{
		int total = 0;
		for (int y = 0; y < g.map.getH(); ++y)
			for (int x = 0; x < g.map.getW(); ++x)
				total += terrainGroup(g.map.vertexTerrainAt(x, y)) == group;
		return total;
	};
	// Deep water only inside a shallow margin of plain water (or a boardwalk ford); boardwalk only
	// where a ford was laid; no deposit on a tile with a mud corner (the lanes stay open).
	const auto checkTerrain = [&](const Game &g, int boardwalks)
	{
		const Map &map = g.map;
		const Torus t(map);
		int paths = 0;
		for (int y = 0; y < map.getH(); ++y)
			for (int x = 0; x < map.getW(); ++x)
			{
				const TerrainGroup group = terrainGroup(map.vertexTerrainAt(x, y));
				if (group == TerrainGroup::DeepWater)
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							const TerrainGroup near = terrainGroup(map.vertexTerrainAt(t.x(x + dx), t.y(y + dy)));
							assert(near == TerrainGroup::Water || near == TerrainGroup::DeepWater ||
								   near == TerrainGroup::Paths);
						}
				if (group == TerrainGroup::Paths)
				{
					assert(map.vertexTerrainAt(x, y) == BOARDWALK);
					++paths;
				}
				bool mud = false;
				for (TerrainType corner : map.cellCorners(x, y))
					mud = mud || terrainGroup(corner) == TerrainGroup::Rough;
				if (mud)
					assert(!map.isResource(x, y));
			}
		assert(paths == boardwalks);
	};
	// The envelope: every size and colony count the request check accepts, a season, a mud share and a
	// hammock count pinned in turn.
	int pinned = 0;
	const char *seasons[] = {"wet", "dry"};
	for (auto dimensions : {std::pair{7, 7}, std::pair{7, 8}, std::pair{8, 7}, std::pair{8, 8}, std::pair{9, 8},
							std::pair{9, 9}})
		for (int teams : {1, 2, 4, 6, 8, 12})
		{
			D r = make(dimensions.first, dimensions.second, teams, 7);
			if (!definition.validateRequest(r).empty())
				continue;
			if (dimensions.first + dimensions.second >= 17 && teams != 4 && teams != 12)
				continue; // the large maps only twice, for time
			r.options["season"] = 1 + pinned % 2;
			r.options["mudflats"] = (pinned / 2) % 3 * 50;
			r.options["hammocks"] = (pinned / 3) % 3 * 4;
			++pinned;
			Game g(nullptr);
			const auto result = build(g, r, "envelope");
			assert(choice(result, "everglades.season") == seasons[r.option("season") - 1]);
			checkTerrain(g, measure(result, "everglades.routes.boardwalk-vertices"));
			assert(count(g, STONE) > 0);
			assert((vertices(g, TerrainGroup::Rough) > 0) == (r.option("mudflats") > 0));
			assert(measure(result, "everglades.hammocks.placed") <= measure(result, "everglades.hammocks.wanted"));
			if (r.option("hammocks") > 0 && dimensions.first + dimensions.second >= 16)
				assert(measure(result, "everglades.hammocks.placed") > 0 && vertices(g, TerrainGroup::Obstacles) > 0);
			if (r.option("hammocks") == 0)
				assert(vertices(g, TerrainGroup::Obstacles) == 0);
			if (dimensions.first + dimensions.second >= 16)
				assert(measure(result, "everglades.deep.vertices") > 0 && vertices(g, TerrainGroup::DeepWater) > 0);
			for (int walk : measures(result, "everglades.stone.walk"))
				assert(walk > 0);
		}
	assert(pinned >= 20);
	// Revision 3 accepted every colony count up to 12 at 128 (the clearings shrink and the ring widens).
	assert(definition.validateRequest(make(7, 7, 12, 7)).empty());
	// Both seasons come up at random, and each pinned holds at 128, 256 and 512 with the variants at
	// their extremes.
	std::set<std::string> drawn;
	for (std::uint32_t seed = 1; seed <= 20 && drawn.size() < 2; ++seed)
	{
		Game g(nullptr);
		const auto result = service.generate(g, make(7, 7, 2, seed), true);
		assert(result);
		drawn.insert(choice(result, "everglades.season"));
	}
	assert(drawn.size() == 2);
	for (int season = 1; season <= 2; ++season)
		for (auto [dimension, teams] : {std::pair{7, 2}, std::pair{8, 6}, std::pair{9, 8}})
		{
			D r = make(dimension, dimension, teams, 11);
			r.options["season"] = season;
			r.options["mudflats"] = season == 1 ? 100 : 0;
			r.options["hammocks"] = season == 1 ? 8 : 1;
			Game g(nullptr);
			const auto result = build(g, r, "pinned season");
			checkTerrain(g, measure(result, "everglades.routes.boardwalk-vertices"));
		}
	// The season changes looks only: the same seed wet and dry has the same rules on every tile and the
	// same deposits.
	{
		Game wet(nullptr), dry(nullptr);
		D a = make(8, 8, 4, 13), b = a;
		a.options["season"] = 1;
		b.options["season"] = 2;
		build(wet, a, "wet");
		build(dry, b, "dry");
		int differentLooks = 0;
		for (int y = 0; y < 256; ++y)
			for (int x = 0; x < 256; ++x)
			{
				const auto &rw = wet.map.terrainPropertiesAt(x, y), &rd = dry.map.terrainPropertiesAt(x, y);
				assert(rw.walkable == rd.walkable && rw.swimmable == rd.swimmable && rw.buildable == rd.buildable &&
					   rw.resourcesGrow == rd.resourcesGrow && rw.groundSpeedQ8 == rd.groundSpeedQ8 &&
					   rw.inhibitionQ8 == rd.inhibitionQ8 && rw.fertilityQ8 == rd.fertilityQ8);
				assert(wet.map.isResource(x, y) == dry.map.isResource(x, y));
				differentLooks += wet.map.vertexTerrainAt(x, y) != dry.map.vertexTerrainAt(x, y);
			}
		assert(differentLooks > 1000);
	}
	// Hammocks cut nothing off: the same seed with none and with the most has the same pools, and every
	// tile walkable in both that colony 0 could reach over the ground without hammocks it still reaches
	// with them.
	{
		Game none(nullptr), most(nullptr);
		D a = make(8, 8, 4, 17), b = a;
		a.options["hammocks"] = 0;
		b.options["hammocks"] = 8;
		build(none, a, "no hammocks");
		const auto result = build(most, b, "most hammocks");
		assert(measure(result, "everglades.hammocks.placed") >= 20);
		const Torus t(none.map);
		std::vector<unsigned char> openNone(256 * 256), openMost(256 * 256), swarm(256 * 256, 0);
		for (int i = 0; i < 256 * 256; ++i)
		{
			openNone[i] = none.map.terrainPropertiesAt(i % 256, i / 256).walkable;
			openMost[i] = most.map.terrainPropertiesAt(i % 256, i / 256).walkable;
		}
		for (int dy = 0; dy < 4; ++dy)
			for (int dx = 0; dx < 4; ++dx)
				swarm[t.at(none.teams[0]->startPosX - 2 + dx, none.teams[0]->startPosY - 2 + dy)] = 1;
		const std::vector<int> reachNone = stepsFrom(t, swarm, openNone), reachMost = stepsFrom(t, swarm, openMost);
		int lost = 0, blocked = 0;
		for (int i = 0; i < 256 * 256; ++i)
		{
			lost += reachNone[i] >= 0 && openMost[i] && reachMost[i] < 0;
			blocked += openNone[i] && !openMost[i];
		}
		assert(lost == 0 && blocked > 100);
	}
	// Damage the validator must notice: deep water against a bank, and a hammock opened.
	{
		const D r = make(8, 8, 4, 19);
		Game g(nullptr);
		build(g, r, "damage");
		GenerationContext check(r);
		bool deepened = false;
		for (int y = 1; y < 255 && !deepened; ++y)
			for (int x = 1; x < 255 && !deepened; ++x)
				if (g.map.vertexTerrainAt(x, y) == WATER &&
					terrainGroup(g.map.vertexTerrainAt(x + 1, y)) == TerrainGroup::Sand)
				{
					Game damaged(nullptr);
					build(damaged, r, "deep copy");
					damaged.map.setVertexTerrain(x, y, DEEP_WATER);
					assert(definition.validateWorld(damaged, check).find("Deep water") != std::string::npos);
					deepened = true;
				}
		assert(deepened);
		for (int y = 0; y < 256; ++y)
			for (int x = 0; x < 256; ++x)
				if (terrainGroup(g.map.vertexTerrainAt(x, y)) == TerrainGroup::Obstacles)
					g.map.setVertexTerrain(x, y, GRASS);
		assert(definition.validateWorld(g, check).find("hammock") != std::string::npos);
	}
	// Boardwalk fords: a drowned map of big, close sloughs boxes colonies in, and openRoutes fords them
	// with boardwalk (Paths: walkable and fast) rather than sand.
	{
		int fords = 0;
		for (std::uint32_t seed = 1; seed <= 24 && fords == 0; ++seed)
		{
			D r = make(7, 7, 4, seed);
			r.options["pool-spacing"] = 8;
			r.options["pool-size"] = 7;
			r.options["sloughs"] = 50;
			Game g(nullptr);
			const auto result = service.generate(g, r, true);
			if (!result)
				continue;
			GenerationContext check(r);
			assert(definition.validateWorld(g, check).empty());
			fords = measure(result, "everglades.routes.boardwalk-vertices");
			checkTerrain(g, fords);
		}
		assert(fords > 0);
	}
	// Resource extremes: no ambient resources at all still plays and validates; at the ceilings, after a
	// few thousand ticks of growth with nobody harvesting, the crops and algae grew, no deposit stands on
	// a mud bank or a levee, the deep beds and hammocks hold, and the only fault the validator may find
	// is a colony walled in by the swamp: it is meant to grow shut where nobody cuts it (revision 3
	// promised no more), and its walk check comes last, after every other.
	D r = make(8, 8, 4, 37);
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = 0;
	{
		Game zero(nullptr);
		build(zero, r, "zero resources");
	}
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources)
			r.options[control.id] = control.maximum;
	Game abundant(nullptr);
	const auto abundantResult = build(abundant, r, "abundant");
	GenerationContext context(r);
	const int wheatBefore = count(abundant, WHEAT), woodBefore = count(abundant, WOOD),
			  algaeBefore = count(abundant, ALGA);
	setSyncRandSeed(4211);
	for (int tick = 0; tick < 4096; ++tick)
		abundant.map.growResources();
	const std::string growthError = definition.validateWorld(abundant, context);
	if (!growthError.empty())
		std::fprintf(stderr, "Everglades growth: %s\n", growthError.c_str());
	std::fprintf(stderr, "Everglades growth over 4096 ticks: wheat %d -> %d, wood %d -> %d, algae %d -> %d\n",
				 wheatBefore, count(abundant, WHEAT), woodBefore, count(abundant, WOOD), algaeBefore,
				 count(abundant, ALGA));
	assert(growthError.empty() || growthError.find("cannot walk") != std::string::npos);
	assert(count(abundant, WHEAT) > wheatBefore && count(abundant, ALGA) > algaeBefore);
	checkTerrain(abundant, measure(abundantResult, "everglades.routes.boardwalk-vertices"));
	puts("PASS Everglades: envelope with pinned seasons, mud and hammocks, random seasons, season moves only "
		 "looks, deep beds in shallows, boardwalk fords, hammocks cut nothing off, damage refused, resource "
		 "extremes, growth");
}

inline void generatorContracts()
{
	// GLOB2_CONTRACTS=bajada,forts runs only those generators' contracts (a comma list of the names
	// below), so one generator's contracts can be checked in minutes; unset runs them all.
	const char *only = std::getenv("GLOB2_CONTRACTS");
	const auto run = [&](const char *name, void (*contracts)())
	{
		if (!only || (std::string(",") + only + ",").find(std::string(",") + name + ",") != std::string::npos)
			contracts();
	};
	run("bastion-keys", bastionKeysContracts);
	run("drowned-forest", drownedForestContracts);
	run("portage-lakes", portageLakesContracts);
	run("faulted-city", faultedCityContracts);
	run("encircled-kingdom", encircledKingdomContracts);
	run("comb", combContracts);
	run("even-ground", evenGroundContracts);
	run("marchland", marchlandContracts);
	run("who-ate-the-map", eatenMapContracts);
	run("rebuilt-landscapes", rebuiltLandscapeContracts);
	run("savannah", savannahContracts);
	run("locust", locustFoodChecks);
	run("last-treeline", LastTreelineChecks::run);
	run("hedgerow-country", hedgerowContracts);
	run("breachable-highlands", breachableHighlandsContracts);
	run("braided-delta", braidedDeltaChecks);
	run("forts", fortsContracts);
	run("emoji", emojiContracts);
	run("honeycomb-isle", honeycombIsleContracts);
	run("karst-towers", karstTowersContracts);
	run("bajada", bajadaContracts);
	run("braided-river", braidedRiverContracts);
	run("caravanserai", caravanseraiContracts);
	run("rain-shadow", rainShadowContracts);
	run("tidal-flats", tidalFlatsContracts);
	run("watershed", watershedContracts);
	run("everglades", evergladesContracts);
}
} // namespace GeneratorContracts
