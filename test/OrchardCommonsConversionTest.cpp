// SPDX-License-Identifier: GPL-3.0-or-later
// Controlled mechanism test on unmodified generated terrain, not an AI balance test.
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
#include "FileManager.h"
#include "Unit.h"
#include "Race.h"
#include "Building.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <SDL.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

GlobalContainer *globalContainer = nullptr;
namespace
{
void require(bool ok, const char *message)
{
	if (!ok)
	{
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}
}
// Compare full saves (after their header/content hash has been established),
// including starting colony state rather than only a visual terrain hash.
std::string serialized(Game &game)
{
	auto *memory = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream stream(memory);
	game.save(&stream, false, "Orchard Commons generation contracts");
	stream.flush();
	memory->seekFromEnd(0);
	return std::string(memory->getBuffer(), memory->getPosition());
}
void contracts(unsigned seed)
{
	GenerationRequest request;
	request.setMethodDefaults(GeneratorRegistry::builtins().idOf("orchard-commons"));
	request.wDec = request.hDec = 8;
	request.nbTeams = 4;
	request.seed = seed;
	GenerationService service;
	GameGUI first, observed, repeated;
	auto initial = service.generate(first.game, request, false);
	require(bool(initial), initial.diagnostic().c_str());
	auto measured = service.generate(observed.game, request, true);
	require(bool(measured), measured.diagnostic().c_str());
	require(initial.telemetry.records().empty() && !measured.telemetry.records().empty(),
			"telemetry switch actually changes recorded observations");
	// Intervening generation must not perturb the pinned request's output.
	require(bool(service.generate(repeated.game, request, false)), "repeat generation succeeds");
	serialized(first.game);
	serialized(observed.game);
	serialized(repeated.game);
	const std::string bytes = serialized(first.game);
	require(bytes == serialized(observed.game),
			"telemetry leaves complete generated save unchanged");
	require(bytes == serialized(repeated.game), "same seed reproduces complete generated save");
	GAGCore::BinaryInputStream reader(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
	reader.seekFromStart(0);
	GameGUI restored;
	require(restored.game.load(&reader), "unmodified generated map save loads");
	require(restored.game.map.getW() == first.game.map.getW() &&
				restored.game.map.getH() == first.game.map.getH(),
			"save preserves map dimensions");
	for (int y = 0; y < first.game.map.getH(); ++y)
		for (int x = 0; x < first.game.map.getW(); ++x)
		{
			require(first.game.map.getResource(x, y).getUint32() ==
						restored.game.map.getResource(x, y).getUint32(),
					"save preserves every resource kind, amount, variety and animation");
			require(first.game.map.getTerrain(x, y) == restored.game.map.getTerrain(x, y) &&
						first.game.map.getUMTerrain(x, y) == restored.game.map.getUMTerrain(x, y),
					"save preserves generated terrain");
		}
	// Unsupported user requests must fail explicitly, before placing colonies.
	const int invalid[][3] = {{6, 8, 4}, {8, 6, 4}, {10, 9, 4}, {9, 10, 4}, {7, 9, 4}, {9, 7, 4},
							  {8, 8, 1}, {8, 8, 9}, {7, 7, 5},  {7, 8, 8},  {8, 7, 8}};
	for (const auto &dimensions : invalid)
	{
		auto bad = request;
		bad.wDec = dimensions[0];
		bad.hDec = dimensions[1];
		bad.nbTeams = dimensions[2];
		GameGUI rejected;
		auto result = service.generate(rejected.game, bad);
		require(result.error == GenerationError::InvalidRequest && !result.detail.empty(),
				"unsupported shape or colony count returns an explicit request error");
		require(rejected.game.teamsCount() == 0, "invalid request places no colonies");
	}
	std::printf("generation-contracts seed=%u repeatability=pass telemetry-neutral=pass "
				"terrain-resource-save-load=pass invalid-requests=11\n",
				seed);
}
// Repeated fixed-world work for sampling profilers; Game lifetime is outside
// the measured interval, matching the existing generator profiling fixture.
int benchmark(unsigned count)
{
	GenerationRequest request;
	request.setMethodDefaults(GeneratorRegistry::builtins().idOf("orchard-commons"));
	request.wDec = request.hDec = 9;
	request.nbTeams = 8;
	request.seed = 2;
	GenerationService service;
	unsigned totalFailures = 0;
	for (bool telemetry : {false, true})
	{
		double totalMs = 0;
		unsigned failures = 0;
		std::printf("benchmark-start seed=2 size=512x512 teams=8 telemetry=%s count=%u\n",
					telemetry ? "on" : "off", count);
		std::fflush(stdout);
		for (unsigned run = 0; run < count; ++run)
		{
			Game game(nullptr);
			const auto begin = std::chrono::steady_clock::now();
			const auto result = service.generate(game, request, telemetry);
			const auto end = std::chrono::steady_clock::now();
			totalMs += std::chrono::duration<double, std::milli>(end - begin).count();
			if (!result)
			{
				++failures;
				std::fprintf(stderr, "benchmark failure telemetry=%s run=%u: %s\n",
							 telemetry ? "on" : "off", run, result.diagnostic().c_str());
			}
		}
		totalFailures += failures;
		std::printf("benchmark seed=2 size=512x512 teams=8 telemetry=%s count=%u failures=%u "
					"total_ms=%.3f mean_ms=%.3f\n",
					telemetry ? "on" : "off", count, failures, totalMs, totalMs / count);
		std::fflush(stdout);
	}
	return totalFailures ? 1 : 0;
}
unsigned parseUnsigned(const char *text, unsigned minimum, unsigned maximum)
{
	unsigned value = 0;
	const char *end = text + std::strlen(text);
	const auto parsed = std::from_chars(text, end, value);
	require(parsed.ec == std::errc() && parsed.ptr == end && value >= minimum && value <= maximum,
			"numeric argument must be an integer in its documented range");
	return value;
}
struct Scenario
{
	const char *name;
	int friendlyFruit, rivalFruit;
	bool advertising, wheat, converts;
};
void run(const Scenario &scenario, const std::filesystem::path &output, unsigned seed)
{
	GameGUI gui;
	Game &game = gui.game;
	GenerationRequest request;
	request.setMethodDefaults(GeneratorRegistry::builtins().idOf("orchard-commons"));
	request.wDec = request.hDec = 8;
	request.nbTeams = 4;
	request.seed = seed;
	const auto result = GenerationService().generate(game, request);
	require(bool(result), result.diagnostic().c_str());
	const int type = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
	require(type >= 0, "inn definition exists");
	// Locate actual clear terrain beside a fruit grove. Do not clear resources or
	// replace terrain: this proves two competing outposts fit the generated map.
	std::vector<std::pair<int, int>> fruit;
	for (int y = 0; y < game.map.getH(); ++y)
		for (int x = 0; x < game.map.getW(); ++x)
		{
			int resource;
			if (game.map.isResource(x, y, &resource) && resource >= CHERRY && resource <= PRUNE)
				fruit.emplace_back(x, y);
		}
	int siteX = -1, siteY = -1, nearest = 100000;
	for (int y = 8; y < game.map.getH() - 16; ++y)
		for (int x = 8; x < game.map.getW() - 20; ++x)
			if (game.map.isFreeForBuilding(x, y, 14, 8))
				for (const auto &p : fruit)
				{
					const int distance = game.map.warpDistSquare(x + 7, y + 4, p.first, p.second);
					if (distance < nearest)
					{
						nearest = distance;
						siteX = x;
						siteY = y;
					}
				}
	require(siteX >= 0 && nearest <= 24 * 24, "two-inn clearing exists within 24 tiles of fruit");
	auto addInn = [&](int x, int team, int varieties, bool wheat)
	{
		Building *inn = game.addBuilding(x, siteY + 2, type, team);
		require(inn != nullptr, "inn fits generated clearing");
		game.map.setBuilding(x, siteY + 2, inn->type->width, inn->type->height, inn->gid);
		inn->resources[WHEAT] = wheat ? std::min(10, inn->type->maxResource[WHEAT]) : 0;
		const int kinds[] = {CHERRY, ORANGE, PRUNE};
		for (int i = 0; i < 3; ++i)
			inn->resources[kinds[i]] =
				i < varieties ? std::min(10, inn->type->maxResource[kinds[i]]) : 0;
		for (int r = 0; r < MAX_RESOURCES; ++r)
			require(inn->resources[r] <= inn->type->maxResource[r], "stock fits inn capacity");
		// Public building ticks honor its normal conversion cooldown.
		for (int i = 0; i < 256; ++i)
			inn->step();
		inn->updateCallLists();
		return inn;
	};
	Building *friendly = addInn(siteX + 1, 0, scenario.friendlyFruit, true);
	Building *rival = addInn(siteX + 9, 1, scenario.rivalFruit, scenario.wheat);
	game.teams[1]->sharedVisionFood = scenario.advertising ? game.teams[0]->me : 0;
	game.teams[1]->allies &= ~game.teams[0]->me;
	Unit *worker = game.addUnit(siteX + 7, siteY + 3, 0, WORKER, 0, 0, 0, 0);
	require(worker != nullptr, "hungry worker fits between inns");
	worker->hungry = worker->trigHungry;
	worker->medical = Unit::MED_HUNGRY;
	worker->needToRecheckMedical = true;
	Building *chosen = game.teams[0]->findNearestFood(worker);
	require(chosen == (scenario.converts ? rival : friendly),
			"normal food routing selects expected inn");
	const Uint16 workerId = worker->gid;
	auto *bytes = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream writer(bytes);
	game.save(&writer, false, scenario.name);
	{
		const auto path = output / (std::string(scenario.name) + ".game");
		FILE *file = std::fopen(path.string().c_str(), "wb");
		require(file != nullptr, "scenario artifact opens");
		GAGCore::BinaryOutputStream disk(new GAGCore::FileStreamBackend(file));
		game.save(&disk, false, scenario.name);
	}
	auto *copy = new GAGCore::MemoryStreamBackend(*bytes);
	copy->seekFromStart(0);
	GAGCore::BinaryInputStream reader(copy);
	GameGUI restored;
	require(restored.game.load(&reader), "scenario save loads");
	worker = restored.game.teams[0]->myUnits[Unit::GIDtoID(workerId)];
	require(worker != nullptr, "worker survives save/load");
	// Loading rebuilds service lists and restarts the normal feeding cooldown.
	// Mature those public building ticks before comparing eligible choices.
	for (int team = 0; team < restored.game.teamsCount(); ++team)
		for (int b = 0; b < Building::MAX_COUNT; ++b)
			if (auto *inn = restored.game.teams[team]->myBuildings[b])
				for (int tick = 0; tick < 256; ++tick)
					inn->step();
	chosen = restored.game.teams[0]->findNearestFood(worker);
	require(chosen && chosen->owner->teamNumber == (scenario.converts ? 1 : 0),
			"food choice survives save/load");
	for (int tick = 0; tick < 64; ++tick)
		restored.game.syncStep(0);
	require(worker->owner->teamNumber == (scenario.converts ? 1 : 0),
			"normal game ticks realize expected ownership");
	require(restored.game.teams[1]->stats.measurements.conversionsIn[WORKER] ==
				(scenario.converts ? 1u : 0u),
			"conversion telemetry records expected result");
	std::printf("%s seed=%u size=256x256 teams=4 clearing=%d,%d fruit_distance_squared=%d owner=%d "
				"ticks=64 loaded_cooldown_ticks=256 eligible_save_load=pass\n",
				scenario.name, seed, siteX, siteY, nearest, worker->owner->teamNumber);
}
} // namespace
int main(int argc, char **argv)
{
	SDL_SetMainReady();
	require(argc == 4 || argc == 5 || (argc == 6 && std::string(argv[4]) == "--benchmark"),
			"usage: OrchardCommonsConversionTest PROFILE ROOT ARTIFACT_DIR [SEED | --benchmark "
			"COUNT(1..1000)]");
	const bool benchmarking = argc == 6;
	const unsigned count = benchmarking ? parseUnsigned(argv[5], 1, 1000) : 0;
	const unsigned seed = argc == 5 ? parseUnsigned(argv[4], 0, UINT32_MAX) : 2;
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
	if (benchmarking)
		return benchmark(count);
	std::filesystem::create_directories(argv[3]);
	const Scenario scenarios[] = {
		{"superior-variety", 1, 3, true, true, true},
		{"equal-friendly-diet", 3, 3, true, true, false},
		{"no-advertising", 1, 3, false, true, false},
		{"fruit-supply-lost", 1, 0, true, true, false},
		{"wheat-supply-lost", 1, 3, true, false, false},
	};
	contracts(seed);
	for (const auto &scenario : scenarios)
		run(scenario, argv[3], seed);
	return 0;
}
