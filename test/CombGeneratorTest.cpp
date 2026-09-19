// SPDX-License-Identifier: GPL-3.0-or-later
// Contracts and a controlled firing probe on unmodified generated Comb terrain.
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
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
#include "Growth.h"
#include "Room.h"
#include "Topology.h"
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
using namespace MapGeneration;
GlobalContainer *globalContainer = nullptr;
namespace
{
void require(bool ok, const std::string &message)
{
	if (!ok)
	{
		std::fprintf(stderr, "FAIL: %s\n", message.c_str());
		std::exit(1);
	}
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
			require(first.game.map.getTerrain(x, y) == restored.game.map.getTerrain(x, y) &&
						first.game.map.getResource(x, y).getUint32() ==
							restored.game.map.getResource(x, y).getUint32(),
					"terrain and resources survive save/load");
	const auto &def = GeneratorRegistry::builtins().at(r.method);
	// Decorative crops are legal only inside their sand-contained pockets.
	// A new seed on open construction ground must still be rejected.
	const Torus t{256, 256};
	const auto reserve = buildAnchors(t, potentialBuildingTiles(second.game.map), 6);
	const auto envelope = cropSpreadEnvelope(second.game.map);
	const int home = unitTilesByTeam(second.game.map, 4).front().front();
	int stray = -1, nearest = 999999;
	for (int i = 0; i < t.size(); ++i)
		if (reserve[i] && envelope.steps[i] < 0)
		{
			const int distance = t.dist2(i % 256, i / 256, home % 256, home / 256);
			if (distance < nearest)
			{
				nearest = distance;
				stray = i;
			}
		}
	require(stray >= 0, "unseeded construction ground exists");
	second.game.map.setResource(stray % 256, stray / 256, WHEAT, 1);
	require(second.game.map.getResource(stray % 256, stray / 256).type == WHEAT,
			"stray crop planted");
	GenerationContext strayContext(r);
	require(!def.validateWorld(second.game, strayContext).empty(),
			"uncontained decorative crop rejected");
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
	tower->resources[STONE] = 0;
	for (unsigned tick = 0; tick < 256; ++tick)
		tower->turretStep(tick);
	require(tower->bullets == 0, "unfed tower cannot shoot");
	tower->resources[STONE] = 1;
	tower->turretStep(256);
	require(tower->resources[STONE] == 0 &&
				tower->bullets == tower->type->multiplierStoneToBullets - 1,
			"test stock becomes ammunition and a cross-channel shot");
	const auto path =
		output / (buildingTarget ? "cross-channel-building.game" : "cross-channel-unit.game");
	FILE *file = std::fopen(path.string().c_str(), "wb");
	require(file, "scenario artifact opens");
	GAGCore::BinaryOutputStream disk(new GAGCore::FileStreamBackend(file));
	game.save(&disk, false, "Comb cross-channel firing");
	const int before = buildingTarget ? enemyTower->hp : worker->hp;
	for (int tick = 0; tick < 32; ++tick)
		game.syncStep(0);
	require((buildingTarget ? enemyTower->hp : worker->hp) < before,
			"cross-channel projectile damages target");
	if (buildingTarget)
	{
		// A second probe starts empty and hires trained workers through normal
		// building updates. Only native quarry harvests can supply further fire.
		tower->resources[STONE] = 0;
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
		auto &m = game.teams[0]->stats.measurements;
		const auto shots = m.shots[GameplayMeasurements::TOWER];
		int ticks = 0;
		for (; ticks < 12000 &&
			   (m.delivered[STONE] == 0 || m.shots[GameplayMeasurements::TOWER] == shots);
			 ++ticks)
			game.syncStep(0);
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
int main(int argc, char **argv)
{
	require(argc == 4 || argc == 6,
			"usage: CombGeneratorTest PROFILE ROOT OUTPUT [--benchmark COUNT]");
	SDL_SetMainReady();
	GlobalContainer globals(argv[1]);
	globals.fileManager->addDir(argv[2]);
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();
	GameGUIKeyActions::init();
	MapEditKeyActions::init();
	std::filesystem::create_directories(argv[3]);
	if (argc == 6)
	{
		const bool telemetry = std::string(argv[4]) == "--benchmark-trace";
		require(telemetry || std::string(argv[4]) == "--benchmark", "known benchmark mode");
		const int count = std::atoi(argv[5]);
		require(count > 0 && count <= 10000, "bounded benchmark");
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
		return 0;
	}
	contracts();
	feedingCourts();
	firing(argv[3], false);
	firing(argv[3], true);
	return 0;
}
