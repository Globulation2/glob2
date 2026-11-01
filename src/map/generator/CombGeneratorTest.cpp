// SPDX-License-Identifier: GPL-3.0-or-later
// Contracts and a controlled firing probe on unmodified generated Comb terrain.
#include "EngineFixtures.h"
#include <array>
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "GameGUIKeyActions.h"
#include "MapEditKeyActions.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "GenerationContext.h"
#include "FileManager.h"
#include "Unit.h"
#include "Race.h"
#include "Building.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "Grid.h"
#include "Pipeline.h"
#include "Roads.h"
#include "Contact.h"
#include "Walls.h"
#include "Growth.h"
#include "Planting.h"
#include "Room.h"
#include "Topology.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
using namespace MapGeneration;
namespace
{
void require(bool ok, const std::string &message)
{
	GLOB2_REQUIRE(ok, message);
}
std::string serialized(Game &game)
{
	auto *memory = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream out(memory);
	game.save(&out, false, "The Comb contracts");
	out.flush();
	memory->seekFromEnd(0);
	return std::string(memory->getBuffer(), memory->getPosition());
}
GenerationRequest request(unsigned seed = 7)
{
	GenerationRequest r;
	r.setMethodDefaults(GeneratorRegistry::builtins().idOf("comb"));
	r.wDec = r.hDec = 8;
	r.nbTeams = 4;
	r.seed = seed;
	return r;
}
void contracts()
{
	auto r = request();
	GameGUI first, second, third;
	auto a = GenerationService().generate(first.game, r, false);
	auto b = GenerationService().generate(second.game, r, true);
	auto c = GenerationService().generate(third.game, r, false);
	require(bool(a), a.diagnostic());
	require(bool(b), b.diagnostic());
	require(bool(c), c.diagnostic());
	serialized(first.game);
	serialized(second.game);
	serialized(third.game);
	const auto bytes = serialized(first.game);
	require(bytes == serialized(second.game) && bytes == serialized(third.game),
			"full-save repeatability and telemetry neutrality");
	GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
	in.seekFromStart(0);
	GameGUI restored;
	require(restored.game.load(&in), "generated save loads");
	for (int y = 0; y < 256; ++y)
		for (int x = 0; x < 256; ++x)
			require(first.game.map.vertexTerrainAt(x, y) == restored.game.map.vertexTerrainAt(x, y) &&
						first.game.map.getResource(x, y).getUint32() ==
							restored.game.map.getResource(x, y).getUint32(),
					"terrain and resources survive save/load");
	const auto &def = GeneratorRegistry::builtins().at(r.method);
	// Natural inlet crops are allowed, but town construction ground stays clear.
	// A new seed on open construction ground must still be rejected.
	const Torus t{256, 256};
	const auto reserve = buildAnchors(t, potentialBuildingTiles(second.game.map), 6);
	TerrainSketch terrain(t.size());
	for (int i = 0; i < t.size(); ++i)
		terrain[i] = TerrainType(second.game.map.vertexTerrainAt(i % t.w, i / t.w));
	const auto fertility = cropGrowthField(terrain, t);
	const auto envelope = fertileCropEnvelope(second.game.map, fertility);
	const int home = unitTilesByTeam(second.game.map, 4).front().front();
	int stray = -1, nearest = 999999;
	for (int i = 0; i < t.size(); ++i)
		if (reserve[i] && !envelope[i] && clearGround(second.game.map, i % t.w, i / t.w))
		{
			const int distance = t.dist2(i % 256, i / 256, home % 256, home / 256);
			if (distance < nearest)
			{
				nearest = distance;
				stray = i;
			}
		}
	require(stray >= 0, "unseeded construction ground exists");
	second.game.map.setResourceByIndex(stray % 256, stray / 256, WHEAT, 1);
	require(second.game.map.getResource(stray % 256, stray / 256).type == WHEAT,
			"stray crop planted");
	GenerationContext strayContext(r);
	require(!def.validateWorld(second.game, strayContext).empty(),
			"crop in protected construction ground rejected");
	GenerationContext context(r);
	for (int y = 0; y < 256; ++y)
		for (int x = 0; x < 256; ++x)
			if (first.game.map.getResource(x, y).type == WHEAT)
				first.game.map.setNoResource(x, y, 1);
	require(!def.validateWorld(first.game, context).empty(), "missing food is rejected");
	for (auto shape : {std::array{7, 8, 2}, std::array{8, 7, 2}, std::array{8, 8, 5},
					   std::array{9, 8, 6}, std::array{9, 9, 9}, std::array{8, 8, 1}})
	{
		auto bad = r;
		bad.wDec = shape[0];
		bad.hDec = shape[1];
		bad.nbTeams = shape[2];
		Game world(nullptr);
		require(GenerationService().generate(world, bad).error == GenerationError::InvalidRequest &&
					world.teamsCount() == 0,
				"unsupported request rejected before construction");
	}
	puts("PASS Comb full-save repeatability, telemetry, save/load, food-loss and invalid requests");
}
void naturalShores(int wDec, int hDec)
{
	for (const auto amounts : {std::array{100, 100}, std::array{0, 100}, std::array{100, 0}})
	{
		auto r = request();
		r.wDec = wDec;
		r.hDec = hDec;
		r.nbTeams = wDec == 9 && hDec == 9 ? 8 : 4;
		r.options["wheat-amount"] = amounts[0];
		r.options["wood-amount"] = amounts[1];
		Game world(nullptr);
		auto result = GenerationService().generate(world, r, true);
		require(bool(result), result.diagnostic());
		int wheat = -1, wood = -1;
		for (const auto &record : result.telemetry.records())
		{
			if (record.key == "comb.shore.wheat")
				wheat = int(std::get<std::int64_t>(record.value));
			if (record.key == "comb.shore.wood")
				wood = int(std::get<std::int64_t>(record.value));
		}
		require((amounts[0] ? wheat > 0 : wheat == 0) &&
				(amounts[1] ? wood > 0 : wood == 0), "inlet crops follow their own abundance controls");
		if (amounts[0] && amounts[1])
		{
			// Revision 5 planted 101 inlet tiles on the short comb and 544 on
			// long combs at this seed, all wheat. Allow geometry/rounding headroom.
			const int previous = std::max(wDec, hDec) == 9 ? 544 : 101;
			require(wheat + wood >= 3 * previous && wheat + wood <= 5 * previous,
					"substantially fuller mixed inlet on every supported map shape");
		}
		const Torus t(world.map);
		TerrainSketch terrain(t.size());
		for (int i = 0; i < t.size(); ++i)
			terrain[i] = TerrainType(world.map.vertexTerrainAt(i % t.w, i / t.w));
		const auto unbounded = cropSpreadEnvelope(world.map);
		const auto actual = fertileCropEnvelope(world.map, cropGrowthField(terrain, t));
		for (const auto &units : unitTilesByTeam(world.map, r.nbTeams))
		{
			require(!units.empty(), "town has starting workers");
			const int home = units.front();
			require(unbounded.steps[home] >= 0, "coastal crops have open grass margins into the mainland");
			require(!actual[home], "natural fertility keeps crop growth outside the town");
		}
	}
	puts("PASS mixed inlet planting, independent crop controls, open farm margins and dry towns");
}
void feedingCourts()
{
	for (unsigned seed : {7u, 29u, 401u})
		for (int amount : {0, 300})
		{
			auto r = request(seed);
			r.options["wheat-amount"] = amount;
			r.options["peninsulas"] = 4;
			Game world(nullptr);
			auto result = GenerationService().generate(world, r);
			require(bool(result), result.diagnostic());
			const Torus t{256, 256};
			const auto anchors = buildAnchors(t, potentialBuildingTiles(world.map), 3);
			const auto open = groundUnitTiles(world.map);
			for (const auto &units : unitTilesByTeam(world.map, 4))
			{
				const auto walk = stepsFrom(t, tileMask(t, units), open);
				bool found = false;
				for (int i = 0; i < t.size() && !found; ++i)
				{
					if (!anchors[i] || walk[i] < 0 || walk[i] > 24)
						continue;
					int cluster[2]{}, edge[2]{};
					const int x = i % 256, y = i / 256;
					for (int dy = -5; dy <= 6; ++dy)
						for (int dx = -5; dx <= 6; ++dx)
							if (world.map.getResource(x + dx, y + dy).type == WHEAT)
							{
								const int parity = (x + dx + y + dy) & 1;
								++cluster[parity];
								if (dx >= -1 && dx <= 3 && dy >= -1 && dy <= 3)
									++edge[parity];
							}
					found = cluster[0] >= 5 && cluster[1] >= 5 && edge[0] && edge[1];
				}
				require(found, "opening has a nearby grown inn court beside a viable grain patch");
			}
		}
	puts("PASS feeding courts at wheat 0/300, both harvest parities, three seeds");
}
void firing(const std::filesystem::path &output, bool buildingTarget)
{
	auto r = request();
	GameGUI gui;
	auto &game = gui.game;
	auto result = GenerationService().generate(game, r);
	require(bool(result), result.diagnostic());
	const Torus t{256, 256};
	const auto open = groundUnitTiles(game.map);
	const auto anchors = buildAnchors(t, potentialBuildingTiles(game.map), 2);
	int site = -1, target = -1;
	// Cut each candidate orientation at both inlet ends. Only a true cross-shore pair
	// becomes disconnected, so nearby targets on the tower's own bank cannot pass.
	for (bool horizontal : {false, true})
	{
		auto cut = open;
		for (int i = 0; i < t.size(); ++i)
		{
			int u = horizontal ? i % 256 : i / 256;
			if (u <= 32 || u >= 223)
				cut[i] = 0;
		}
		const auto labels = connectedRegions(cut, 256, 256, true, GridNeighbors::Eight);
		for (int i = 0; i < t.size() && site < 0; ++i)
		{
			const int x = i % 256, y = i / 256;
			if (!anchors[i] || labels[i] < 0 || x < 55 || x > 200 || y < 55 || y > 200)
				continue;
			for (int dy = -7; dy <= 8 && site < 0; ++dy)
				for (int dx = -7; dx <= 8; ++dx)
				{
					const int j = t.at(x + dx, y + dy);
					if (labels[j] >= 0 && labels[j] != labels[i] &&
						game.map.isFreeForGroundUnit(j % 256, j / 256, false, 0) &&
						(!buildingTarget || anchors[j]))
					{
						site = i;
						target = j;
						break;
					}
				}
		}
		if (site >= 0)
			break;
	}
	require(site >= 0, "range-7 firing pair on opposite shores exists");
	const int type = globalContainer->buildingsTypes.getTypeNum("defencetower", 1, false);
	require(game.checkRoomForBuilding(site % 256, site / 256,
									  globalContainer->buildingsTypes.get(type), 0, false),
			"tower fits original terrain");
	auto *tower = game.addBuilding(site % 256, site / 256, type, 0);
	require(tower != nullptr, "test tower placed");
	game.teams[0]->addToStaticAbilitiesLists(tower);
	game.map.setBuilding(site % 256, site / 256, 2, 2, tower->gid);
	Unit *worker = nullptr;
	Building *enemyTower = nullptr;
	if (buildingTarget)
	{
		const int enemyType = globalContainer->buildingsTypes.getTypeNum("defencetower", 0, false);
		enemyTower = game.addBuilding(target % 256, target / 256, enemyType, 1);
		require(enemyTower != nullptr, "opposing tower fits original terrain");
		game.teams[1]->addToStaticAbilitiesLists(enemyTower);
		game.map.setBuilding(target % 256, target / 256, 2, 2, enemyTower->gid);
	}
	else
	{
		worker = game.addUnit(target % 256, target / 256, 1, WORKER, 1, 0, 0, 0);
		require(worker != nullptr, "target worker placed");
		worker->speed = 1;
		worker->delta = 0;
	}
	game.teams[0]->allies &= ~game.teams[1]->me;
	game.teams[0]->enemies |= game.teams[1]->me;
	tower->bullets = 0;
	tower->setMaterial(STONE, 0);
	for (unsigned tick = 0; tick < 256; ++tick)
		tower->turretStep(tick);
	require(tower->bullets == 0, "unfed tower cannot shoot");
	tower->setMaterial(STONE, 1);
	glob2test::prepareTurretTargets(*tower);
	tower->turretStep(256);
	require(tower->material(STONE) == 0 &&
				tower->bullets == tower->type->multiplierStoneToBullets - 1,
			"test stock becomes ammunition and a cross-channel shot");
	const auto path =
		output / (buildingTarget ? "cross-channel-building.game" : "cross-channel-unit.game");
	FILE *file = std::fopen(path.string().c_str(), "wb");
	require(file, "scenario artifact opens");
	GAGCore::BinaryOutputStream disk(new GAGCore::FileStreamBackend(file));
	game.save(&disk, false, "Comb cross-channel firing");
	const int before = buildingTarget ? enemyTower->hp : worker->hp;
	// This fixture drives a running match directly, without a network wait phase.
	game.anyPlayerWaited = false;
	const auto beforeStep = game.stepCounter;
	for (int tick = 0; tick < 32; ++tick)
		game.step(0);
	require(game.stepCounter == beforeStep + 32, "controlled combat simulation advances");
	require((buildingTarget ? enemyTower->hp : worker->hp) < before,
			"cross-channel projectile damages target");
	if (buildingTarget)
	{
		// A second probe starts empty and hires trained workers through normal
		// building updates. Only native quarry harvests can supply further fire.
		tower->setMaterial(STONE, 0);
		tower->bullets = 0;
		tower->maxUnitWorking = 3;
		tower->update();
		enemyTower->hp = enemyTower->type->hpInit;
		int added = 0;
		for (int dy = -6; dy <= 6 && added < 3; ++dy)
			for (int dx = -6; dx <= 6 && added < 3; ++dx)
			{
				const int x = site % 256 + dx, y = site / 256 + dy;
				if (game.map.isFreeForGroundUnit(x, y, false, 0) &&
					game.addUnit(x, y, 0, WORKER, 1, 0, 0, 0))
					++added;
			}
		require(added == 3, "supply workers fit near tower");
		auto &m = game.teams[0]->stats.editMeasurements();
		const auto shots = m.shots[GameplayMeasurements::TOWER];
		int ticks = 0;
		for (; ticks < 12000 &&
			   (m.delivered[STONE] == 0 || m.shots[GameplayMeasurements::TOWER] == shots);
			 ++ticks)
			game.step(0);
		std::printf("SUPPLY ticks=%d harvested=%llu delivered=%llu new-shots=%llu\n", ticks,
					(unsigned long long)m.harvested[STONE], (unsigned long long)m.delivered[STONE],
					(unsigned long long)(m.shots[GameplayMeasurements::TOWER] - shots));
		require(m.harvested[STONE] > 0 && m.delivered[STONE] > 0 &&
					m.shots[GameplayMeasurements::TOWER] > shots,
				"native quarry workers resupply cross-channel fire");
	}
	std::printf("PASS cross-channel fire tower=(%d,%d) target=(%d,%d) range=7 stone-consumed=1 "
				"shots=1; empty-ammo control=pass damage=pass target=%s\n",
				site % 256, site / 256, target % 256, target / 256,
				buildingTarget ? "building" : "unit");
}
} // namespace
}

TEST_SUITE("CombGenerator")
{
	TEST_CASE("contracts") { glob2test::HeadlessGlobals globals; contracts(); }
	TEST_CASE("feeding courts") { glob2test::HeadlessGlobals globals; feedingCourts(); }
	TEST_CASE("natural shores")
	{
		glob2test::HeadlessGlobals globals;
		for (const auto shape : {std::array{8, 8}, std::array{8, 9}, std::array{9, 8}, std::array{9, 9}})
			naturalShores(shape[0], shape[1]);
	}
	TEST_CASE("cross-channel firing [artifacts]")
	{
		glob2test::HeadlessGlobals globals;
		firing(glob2test::artifactDir(), false);
		firing(glob2test::artifactDir(), true);
	}
	TEST_CASE("generation timing benchmark [benchmark][slow]")
	{
		glob2test::HeadlessGlobals globals;
		for (bool telemetry : {false, true})
		{
					const int count = 60;
					auto r = request();
					r.wDec = r.hDec = 9;
					r.nbTeams = 8;
					const auto start = std::chrono::steady_clock::now();
					const auto cpuStart = std::clock();
					for (int i = 0; i < count; ++i)
					{
						Game world(nullptr);
						r.seed = 101 + i;
						auto result = GenerationService().generate(world, r, telemetry);
						require(bool(result), result.diagnostic());
					}
					const double ms =
						std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
							.count();
					const double cpuMs = 1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC;
					std::printf("Comb benchmark maps=%d telemetry=%d wall-ms/map=%.3f cpu-ms/map=%.3f\n", count,
								telemetry, ms / count, cpuMs / count);
		}
	}
}

TEST_SUITE("GeneratorGroundConnectivity")
{
	TEST_CASE("legal shore roads retain water under chopping and ford policies")
	{
		glob2test::HeadlessGlobals globals;
		for (int waterCost : {-1, 25})
		{
			CAPTURE(waterCost);
			glob2test::HeadlessGame world(
				{.wDec = 4, .hDec = 4, .teams = 2, .loadDefaultRace = true, .header = true});
			REQUIRE(world.addBuilding("swarm", 1, 1, 0, 0));
			REQUIRE(world.addBuilding("swarm", 9, 1, 0, 1));
			auto &map = world.game.map;
			const Torus t(map);
			map.fillTerrain(WATER);
			for (int y = 0; y <= 6; ++y)
				for (int x = 0; x < t.w; ++x)
					map.setVertexTerrain(x, y, GRASS);
			std::vector<unsigned char> keep(t.size());
			for (int x : {6, 14})
				for (int y = 0; y < t.h; ++y)
				{
					keep[t.at(x, y)] = y != 6;
					if (map.terrainAllowsMovementAt(x, y, 0))
					{
						const int resource = map.terrainPropertiesAt(x, y).shoreline ? ALGA : WOOD;
						REQUIRE(map.terrainSupportsResourceAtByIndex(x, y, resource));
						map.setResourceByIndex(x, y, resource, 1);
						REQUIRE(map.resourceBlocksGround(t.at(x, y)));
					}
				}
			GenerationRequest request;
			request.wDec = request.hDec = 4;
			request.nbTeams = 2;
			GenerationContext context(request);
			context.bootX[0] = context.bootY[0] = context.bootY[1] = 1;
			context.bootX[1] = 9;
			std::vector<TerrainType> before(t.size());
			for (int i = 0; i < t.size(); ++i)
				before[i] = map.vertexTerrainAt(t.remainderX(i), i / t.w);
			REQUIRE(
				openColonyRoutes(map, context, t, StepCosts{1, 3, -1, waterCost, -1}, 0, &keep));
			for (int i = 0; i < t.size(); ++i)
				CHECK(map.vertexTerrainAt(t.remainderX(i), i / t.w) == before[i]);
			const auto open = groundUnitTiles(map);
			const auto distance =
				stepsFrom(map, tileMask(t, colonyGroundExits(world.game, 0, open)), open);
			bool reached = false;
			for (int i : colonyGroundExits(world.game, 1, open))
				reached |= distance[i] >= 0;
			CHECK(reached);
		}
	}

	TEST_CASE("Anthill minimum tunnels stay connected with and without dirt roads")
	{
		glob2test::HeadlessGlobals globals;
		const auto &definition =
			GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("anthill"));
		for (unsigned seed : {1u, 3u})
			for (int roads : {0, 1})
			{
				CAPTURE(seed);
				CAPTURE(roads);
				GenerationRequest request;
				request.setMethodDefaults(definition.legacyId);
				request.wDec = request.hDec = 8;
				request.nbTeams = 4;
				request.seed = seed;
				request.options["tunnel-width"] = 2;
				request.options["sand-roads"] = roads;
				Game world(nullptr);
				const auto result = GenerationService().generate(world, request, false);
				REQUIRE_MESSAGE(bool(result), result.diagnostic());
				CHECK(walkFromFirstColony(world.map, request.nbTeams, "anthill",
										  "through minimum tunnels")
						  .error.empty());
				GenerationContext check(request);
				CHECK(definition.validateWorld(world, check).empty());
			}
	}

	TEST_CASE("road clearing respects side obstacles and fords create complete walking cells")
	{
		glob2test::HeadlessGlobals globals;
		{
			glob2test::HeadlessGame world({.wDec = 4, .hDec = 4, .header = true});
			auto &map = world.game.map;
			const Torus t(map);
			map.fillTerrain(OUTCROP);
			for (int y = 4; y <= 5; ++y)
				for (int x = 4; x <= 5; ++x)
					map.paintCell(x, y, GRASS);
			map.setResourceByIndex(5, 4, WOOD, 1);
			map.setResourceByIndex(4, 5, WOOD, 1);
			CHECK_FALSE(map.canTraverse(4, 4, 1, 1, 0, 0));
			REQUIRE(openRoad(map, t, {t.at(4, 4)}, tileMask(t, {t.at(5, 5)})));
			CHECK(stepsFrom(map, tileMask(t, {t.at(4, 4)}), groundUnitTiles(map))[t.at(5, 5)] >= 0);
			CHECK((map.getResource(5, 4).type == NO_RES_TYPE ||
				   map.getResource(4, 5).type == NO_RES_TYPE));
		}
		{
			glob2test::HeadlessGame world(
				{.wDec = 4, .hDec = 4, .teams = 2, .loadDefaultRace = true, .header = true});
			REQUIRE(world.addBuilding("swarm", 1, 1, 0, 0));
			REQUIRE(world.addBuilding("swarm", 9, 1, 0, 1));
			auto &map = world.game.map;
			const Torus t(map);
			map.fillTerrain(WATER);
			for (int y = 0; y <= 5; ++y)
				for (int x = 0; x <= 5; ++x)
				{
					map.paintCell(x, y, GRASS);
					map.paintCell(x + 8, y, GRASS);
				}
			GenerationRequest request;
			request.wDec = request.hDec = 4;
			request.nbTeams = 2;
			GenerationContext context(request);
			context.bootX[0] = context.bootY[0] = context.bootY[1] = 1;
			context.bootX[1] = 9;
			std::vector<TerrainType> before(t.size());
			for (int i = 0; i < t.size(); ++i)
				before[i] = map.vertexTerrainAt(t.remainderX(i), i / t.w);
			const auto start = colonyGroundExits(world.game, 0, groundUnitTiles(map));
			const auto targets = colonyGroundExits(world.game, 1, groundUnitTiles(map));
			const auto disconnected = stepsFrom(map, tileMask(t, start), groundUnitTiles(map));
			for (int target : targets)
				CHECK(disconnected[target] < 0);
			REQUIRE(openColonyRoutes(map, context, t, StepCosts{1, 3, -1, 25, -1}));
			const auto connected =
				stepsFrom(map, tileMask(t, colonyGroundExits(world.game, 0, groundUnitTiles(map))),
						  groundUnitTiles(map));
			bool reached = false;
			for (int target : colonyGroundExits(world.game, 1, groundUnitTiles(map)))
				reached |= connected[target] >= 0;
			CHECK(reached);
			for (int i = 0; i < t.size(); ++i)
				if (before[i] == GRASS)
					CHECK(map.vertexTerrainAt(t.remainderX(i), i / t.w) == GRASS);
		}
	}

	TEST_CASE(
		"physical floods reject a diagonal rock seam while geometric masks stay eight connected")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec = 4, .hDec = 4, .header = true});
		auto &map = world.game.map;
		const Torus t(map);
		map.fillTerrain(OUTCROP);
		map.paintCell(4, 4, GRASS);
		map.paintCell(5, 5, GRASS);
		const auto open = groundUnitTiles(map);
		REQUIRE(open[t.at(4, 4)]);
		REQUIRE(open[t.at(5, 5)]);
		REQUIRE_FALSE(open[t.at(5, 4)]);
		REQUIRE_FALSE(open[t.at(4, 5)]);
		const auto source = tileMask(t, {t.at(4, 4)});
		CHECK(stepsFrom(t, source, open)[t.at(5, 5)] == 1);
		CHECK(stepsFrom(map, source, open)[t.at(5, 5)] == -1);
		const auto regions = groundRegions(map, open, GridNeighbors::Eight);
		CHECK(regions[t.at(4, 4)] != regions[t.at(5, 5)]);
		CHECK(costsFrom(map, t, {t.at(4, 4)}, StepCosts::walking())[t.at(5, 5)] == -1);
		CHECK_FALSE(openRoad(map, t, {t.at(4, 4)}, tileMask(t, {t.at(5, 5)})));
		CHECK(floodFrom(map, source, open, 1).visited == std::vector<int>{t.at(4, 4)});
		const auto reached = reachFrom(map, {t.at(4, 4)}, open, 1);
		CHECK(reached.tiles == std::vector<int>{t.at(4, 4)});
		CHECK(reached.steps == std::vector<int>{0});
	}

	TEST_CASE("physical gate partition rejects diagonal-only contact and clearing masks preserve "
			  "clearance")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec = 4, .hDec = 4, .header = true});
		auto &map = world.game.map;
		const Torus t(map);
		const int a = t.at(4, 4), gate = t.at(5, 5), b = t.at(6, 6);
		const auto open = tileMask(t, {a, gate, b});
		std::vector<int> owners(t.size(), -1);
		owners[a] = 0;
		owners[b] = 1;
		const std::vector<TileGate> gates{{{0, 1}, {gate}}};
		CHECK(checkGatePartition(t, open, owners, gates).badGate == -1);
		CHECK(checkGatePartition(map, open, owners, gates).badGate == 0);
		// Side deposits obstruct a real walk but a caller may explicitly model
		// their removal when evaluating a chopping shortcut.
		map.setResourceByIndex(5, 4, WOOD, 1);
		map.setResourceByIndex(4, 5, WOOD, 1);
		auto physical = groundUnitTiles(map);
		CHECK(floodFrom(map, tileMask(t, {a}), physical, 1).steps[gate] == -1);
		physical[t.at(5, 4)] = physical[t.at(4, 5)] = 1;
		CHECK(floodFrom(map, tileMask(t, {a}), physical, 1).steps[gate] == 1);
		CHECK(costsFrom(map, t, {a}, StepCosts::walking())[gate] > 1);
		CHECK(costsFrom(map, t, {a}, StepCosts::chopping())[gate] == 1);
	}

	TEST_CASE("colony reach sources use actual cardinal building exits")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world(
			{.wDec = 4, .hDec = 4, .loadDefaultRace = true, .header = true});
		auto *swarm = world.addBuilding("swarm", 4, 4);
		REQUIRE(swarm);
		auto &map = world.game.map;
		const Torus t(map);
		map.fillTerrain(OUTCROP);
		for (int y = swarm->posY; y < swarm->posY + swarm->type->height; ++y)
			for (int x = swarm->posX; x < swarm->posX + swarm->type->width; ++x)
				map.paintCell(x, y, GRASS);
		map.paintCell(swarm->posX - 1, swarm->posY - 1, GRASS);
		REQUIRE(map.terrainAllowsMovementAt(swarm->posX - 1, swarm->posY - 1, 0));
		CHECK(colonyGroundExits(world.game, 0, groundUnitTiles(map)).empty());
		map.paintCell(swarm->posX, swarm->posY - 1, GRASS);
		const auto exits = colonyGroundExits(world.game, 0, groundUnitTiles(map));
		CHECK(exits == std::vector<int>{t.at(swarm->posX, swarm->posY - 1)});
	}

	TEST_CASE("physical floods follow dry shore edges across wrapped seams")
	{
		glob2test::HeadlessGlobals globals;
		for (bool vertical : {false, true})
		{
			CAPTURE(vertical);
			glob2test::HeadlessGame world({.wDec = 4, .hDec = 4, .header = true});
			auto &map = world.game.map;
			const Torus t(map);
			for (int y = 0; y < t.h; ++y)
				for (int x = 0; x < t.w; ++x)
					map.setVertexTerrain(x, y, (vertical ? y : x) % 2 ? WATER : GRASS);
			const int a = t.at(4, 4), b = vertical ? t.at(4, 5) : t.at(5, 4);
			auto open = tileMask(t, {a, b});
			const auto source = tileMask(t, {a});
			CHECK(stepsFrom(t, source, open)[b] == 1);
			CHECK(stepsFrom(map, source, open)[b] == -1); // shared edge is entirely water
			CHECK(stepsFrom(map, source, open, true)[b] == 1);
			CHECK(groundRegions(map, open, GridNeighbors::Cardinal)[a] !=
				  groundRegions(map, open, GridNeighbors::Cardinal)[b]);
			const int seamA = vertical ? t.at(4, 15) : t.at(15, 4);
			const int seamB = vertical ? t.at(4, 0) : t.at(0, 4);
			open = tileMask(t, {seamA, seamB});
			CHECK(stepsFrom(map, tileMask(t, {seamA}), open)[seamB] == 1); // dry wrap edge
			for (int i = 0; i < t.w; ++i)
			{
				map.setVertexTerrain(vertical ? i : 15, vertical ? 15 : i, GRASS);
				map.setVertexTerrain(vertical ? i : 0, vertical ? 0 : i, WATER);
				map.setVertexTerrain(vertical ? i : 1, vertical ? 1 : i, GRASS);
			}
			REQUIRE(groundUnitTiles(map)[seamA]);
			REQUIRE(groundUnitTiles(map)[seamB]);
			CHECK(stepsFrom(map, tileMask(t, {seamA}), open)[seamB] == -1);
			CHECK(stepsFrom(map, tileMask(t, {seamB}), open)[seamA] == -1);
			CHECK(floodFrom(map, tileMask(t, {seamA}), open, 1, true).steps[seamB] == 1);
		}
	}
}
