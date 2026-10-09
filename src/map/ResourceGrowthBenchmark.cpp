// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ResourceGrowth.h"
#include "gradient/GradientRuntime.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <bit>
#include <sstream>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include "GenerationService.h"

namespace
{
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
Uint64 ns(Clock::time_point t)
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count();
}
void setup(Map &map, const std::string &scenario)
{
	auto doc = Json::parse(map.resourceRegistry().serialize());
	auto crop = doc["resources"][1];
	crop["key"] = "benchmark-crop";
	crop["properties"]["ecology"] = "uniform";
	if (scenario == "multi")
		crop["yields"]["paper"] = {{"capacity", 5},
								   {"initial", 2},
								   {"growthRate", ResourceRateScale},
								   {"consumption", "one"}};
	map.installResourceDefinitions(Json{{"schemaVersion", 1}, {"resources", {crop}}}.dump());
	const auto id = *map.resourceRegistry().find("benchmark-crop");
	const int stride = scenario == "sparse" ? 8 : 2;
	for (int y = 0; y < map.getH(); y += stride)
		for (int x = 0; x < map.getW(); x += stride)
		{
			map.setResource(x, y, id, 0);
			if (scenario == "saturated")
				map.setMaterialAmount(map.coordToIndex(x, y), MaterialId::Food, 5);
		}
	if (scenario == "blocked")
		for (int y = 0; y < map.getH(); ++y)
			for (int x = 0; x < map.getW(); ++x)
				if ((x | y) & 1)
					map.setResourcesGrow(x, y, false);
	map.game->gameHeader.setResourceGrowthDisabled(scenario == "disabled");
}
Uint64 stocks(Map &map)
{
	Uint64 result = 0;
	for (int y = 0; y < map.getH(); ++y)
		for (int x = 0; x < map.getW(); ++x)
			result += map.materialAmountAt(map.coordToIndex(x, y), MaterialId::Food);
	return result;
}
} // namespace
TEST_SUITE("ResourceGrowthBenchmark")
{
	TEST_CASE("player-free growth reconciles world stocks and team statistics")
	{
		glob2test::HeadlessGlobals globals;
		const char *output = std::getenv("GLOB2_GROWTH_PLAYER_FREE_OUTPUT");
		const unsigned seeds = output ? 20 : 2, ticks = output ? 512 : 64;
		const char *delaySetting = std::getenv("GLOB2_GROWTH_PLAYER_FREE_DELAY");
		const unsigned delay = delaySetting ? std::stoul(delaySetting) : 8;
		REQUIRE(delay >= 1);
		REQUIRE(delay <= 16);
		// Heavy checksums join pending work: this is correctness evidence, not timing.
		// The immediate reference uses its own RNG schedule and no full game ticks.
		Json report = {{"kind", "player-free-growth"}, {"ticks", ticks},
			{"map_side", output ? 64 : 32}, {"delay", delay}, {"samples", Json::array()}};
		for (const std::string scenario : {"dense", "sparse", "multi", "saturated", "blocked", "disabled"})
			for (unsigned randomSeed = 1; randomSeed <= seeds; ++randomSeed)
			{
				std::vector<Uint32> fallbackChecksums;
				Json fallbackStatistics;
				for (const std::string variant : {"immediate-reference", "zero-workers", "shared"})
				{
					INFO(scenario, " seed=", randomSeed, " variant=", variant);
					glob2test::HeadlessGame world({.wDec = output ? 6 : 5, .hDec = output ? 6 : 5,
						.teams = 2, .loadDefaultRace = true});
					// Passive teams receive statistics, but there are no player seats,
					// controllers, units or buildings to harvest or influence growth.
					GameHeader header;
					header.setNumberOfPlayers(0);
					header.setRandomSeed(randomSeed);
					world.game.setGameHeader(header, true);
					REQUIRE(world.game.gameHeader.getNumberOfPlayers() == 0);
					auto &map = world.game.map;
					setup(map, scenario);
					map.configureCompute(variant == "shared" ? 4 : 1);
					map.setResourceGrowthDelay(delay);
					auto scan = [&]()
					{
						std::array<Uint64, MaterialCount + 1> totals{};
						for (size_t cell = 0; cell < map.cellCount(); ++cell)
						{
							totals[MaterialCount] += map.getResource(cell).type != NO_RES_TYPE;
							for (unsigned material = 0; material < MaterialCount; ++material)
								totals[material] += map.materialAmountAtSlot(cell, material);
						}
						return totals;
					};
					const auto initial = scan();
					Json statistics = Json::array(), checkpoints = Json::array();
					for (unsigned tick = 0; tick < ticks; ++tick)
					{
						CAPTURE(tick);
						if (variant == "immediate-reference")
						{
							const auto random = world.game.bindRandom();
							map.growResources();
						}
						else
							world.step();
						const auto totals = scan();
						if ((tick + 1) % 128 == 0)
							checkpoints.push_back({{"tick", tick + 1}, {"stocks", totals}});
						const auto &a = world.game.teams[0]->stats.measurements;
						const auto &b = world.game.teams[1]->stats.measurements;
						Uint64 added = 0;
						Json global = Json::array();
						for (unsigned material = 0; material < MaterialCount; ++material)
						{
							REQUIRE(totals[material] == initial[material] + a.growthGlobal[1][material]);
							REQUIRE(a.growthGlobal[2][material] == 0);
							added += a.growthGlobal[1][material];
							for (unsigned kind = 0; kind < 3; ++kind)
							{
								REQUIRE(a.growthGlobal[kind][material] == b.growthGlobal[kind][material]);
								global.push_back(a.growthGlobal[kind][material]);
							}
							for (unsigned band = 0; band < GROWTH_COVERAGE_BANDS; ++band)
							{
								REQUIRE(a.growthTiles[band][material] == 0);
								REQUIRE(a.growthAmount[band][material] == 0);
								REQUIRE(a.growthReduction[band][material] == 0);
							}
						}
						statistics.push_back(global);
						if (variant != "immediate-reference")
						{
							const auto &metrics = map.resourceGrowthMetrics();
							const auto physicalTiles = totals[MaterialCount] - initial[MaterialCount];
							const auto id = *map.resourceRegistry().find("benchmark-crop");
							const auto &yields = map.resourceRegistry().yields(id);
							for (unsigned material = 0; material < MaterialCount; ++material)
								REQUIRE(a.growthGlobal[0][material] ==
									(yields[material].capacity && yields[material].initial ? physicalTiles : 0));
							REQUIRE(metrics.stockAdded == added);
							REQUIRE(metrics.tilesAdded == physicalTiles);
							REQUIRE(metrics.publishedProposals == metrics.accepted + metrics.rejected);
							const auto checksum = world.game.checkSum(nullptr, nullptr, nullptr, true);
							if (variant == "zero-workers") fallbackChecksums.push_back(checksum);
							else REQUIRE(checksum == fallbackChecksums[tick]);
						}
					}
					map.finishResourceGrowth(); // Compute outstanding work without publishing early.
					const auto final = scan();
					if (scenario == "disabled") REQUIRE(final == initial);
					else REQUIRE(final != initial);
					if (scenario == "blocked") REQUIRE(final[MaterialCount] == initial[MaterialCount]);
					if (variant == "zero-workers") fallbackStatistics = statistics;
					if (variant == "shared") REQUIRE(statistics == fallbackStatistics);
					report["samples"].push_back({{"scenario", scenario}, {"seed", randomSeed},
						{"variant", variant}, {"initial", initial}, {"final", scan()},
						{"statistics", statistics.back()}, {"checkpoints", checkpoints}});
					if (variant != "immediate-reference")
					{
						const auto metrics = map.resourceGrowthMetrics();
						auto &sample = report["samples"].back();
						sample["pipeline"] = {{"published", metrics.published},
							{"accepted", metrics.accepted}, {"rejected", metrics.rejected},
							{"clamped", metrics.clamped}, {"proposals", metrics.proposals},
							{"pending_batches", map.gradientRuntime->growth.count()},
							{"pending_proposals", metrics.proposals - metrics.publishedProposals}};
						// Diagnostic only: release all remaining batches in order without
						// calculating further growth. Normal endpoint stocks above remain
						// unchanged; this separates the final queue tail from lasting effects.
						map.gradientRuntime->growth.publish(map, world.game.stepCounter + delay);
						sample["terminal_flush"] = scan();
						REQUIRE(map.gradientRuntime->growth.count() == 0);
					}
				}
			}
		if (output)
		{
			std::ofstream out(output);
			out << report.dump(2);
			REQUIRE(out.good());
		}
	}


	TEST_CASE("generated landscapes preserve player-free growth accounting")
	{
		glob2test::HeadlessGlobals globals;
		const char *output = std::getenv("GLOB2_GROWTH_GENERATED_OUTPUT");
		const unsigned seeds = output ? 20 : 1;
		const char *firstSetting = std::getenv("GLOB2_GROWTH_GENERATED_SEED_BEGIN");
		const char *lastSetting = std::getenv("GLOB2_GROWTH_GENERATED_SEED_END");
		const unsigned firstSeed = firstSetting ? std::stoul(firstSetting) : 1;
		const unsigned lastSeed = lastSetting ? std::stoul(lastSetting) : seeds;
		REQUIRE(firstSeed >= 1);
		REQUIRE(lastSeed >= firstSeed);
		REQUIRE(lastSeed <= seeds);
		std::vector<unsigned> delays{8};
		if (const char *setting = std::getenv("GLOB2_GROWTH_GENERATED_DELAYS"))
		{
			delays.clear();
			std::istringstream values(setting);
			std::string value;
			while (std::getline(values, value, ','))
			{
				const auto delay = std::stoul(value);
				REQUIRE(delay >= 1);
				REQUIRE(delay <= 16);
				REQUIRE(std::find(delays.begin(), delays.end(), delay) == delays.end());
				delays.push_back(delay);
			}
			REQUIRE(!delays.empty());
		}
		const bool wide = std::getenv("GLOB2_GROWTH_GENERATED_WIDE") != nullptr;
		const char *selected = std::getenv("GLOB2_GROWTH_GENERATED_CASE");
		const char *masterInputs = std::getenv("GLOB2_GROWTH_GENERATED_INPUT");
		if (masterInputs) REQUIRE((selected && output));
		const char *tickSetting = std::getenv("GLOB2_GROWTH_GENERATED_TICKS");
		const unsigned ticks = tickSetting ? std::stoul(tickSetting) : (output ? 512 : 32);
		REQUIRE(ticks >= 1);
		REQUIRE(ticks <= 16384);
		Json report = {{"kind", "generated-player-free-growth"}, {"ticks", ticks},
			{"delays", delays}, {"samples", Json::array()}, {"generation_failures", Json::array()}};
		const auto directory = output ? std::filesystem::path(output).parent_path() : std::filesystem::path();
		if (output && !directory.empty()) std::filesystem::create_directories(directory);
		auto persist = [&]()
		{
			if (!output) return;
			std::ofstream file(output);
			file << report.dump(2);
			REQUIRE(file.good());
		};
		auto serialize = [](Game &game)
		{
			auto *bytes = new GAGCore::MemoryStreamBackend;
			GAGCore::BinaryOutputStream stream(bytes);
			game.save(&stream, false, "Generated growth comparison");
			stream.flush();
			bytes->seekFromEnd(0);
			return std::string(bytes->getBuffer(), bytes->getPosition());
		};
		auto artifact = [&](const std::string &name, const std::string &bytes)
		{
			if (!output) return;
			std::ofstream file(directory / (name + ".game"), std::ios::binary);
			file.write(bytes.data(), bytes.size());
			REQUIRE(file.good());
		};
		// Use the actual lobby generators and their default controls, never painted
		// approximations. Only colony entities are removed for the no-harvesting arm.
		struct Landscape { const char *id; int wDec, hDec; };
		std::vector<Landscape> landscapes{{"river", 7, 7}, {"swamp", 7, 7},
			{"crater-lakes", 8, 8}, {"islands", 8, 8}};
		if (wide)
			for (const auto &landscape : std::vector<Landscape>{{"rain-shadow", 8, 8},
				{"old-growth", 8, 8}, {"braided-river", 8, 8}, {"fjord-continent", 9, 8},
				{"stone-highlands", 8, 8}, {"tidal-flats", 8, 8}, {"canals", 8, 7},
				{"continents", 9, 9}})
				landscapes.push_back(landscape);
		bool matched = false;
		for (const auto &landscape : landscapes)
		{
			if (selected && landscape.id != std::string(selected)) continue;
			matched = true;
			for (unsigned seed = firstSeed; seed <= lastSeed; ++seed)
			{
				GenerationRequest request;
				request.setMethodDefaults(GeneratorRegistry::builtins().idOf(landscape.id));
				request.wDec = landscape.wDec;
				request.hDec = landscape.hDec;
				request.nbTeams = 2;
				request.seed = seed;
				GameGUI generated;
				GenerationResult result;
				Json fixture;
				if (masterInputs)
				{
					const auto path = std::filesystem::path(masterInputs) / landscape.id /
						("fixture-" + std::to_string(seed) + ".json");
					if (!std::filesystem::exists(path))
					{
						report["generation_failures"].push_back({{"generator", landscape.id},
							{"seed", seed}, {"diagnostic", "Master fixture unavailable"}});
						persist();
						continue;
					}
					std::ifstream file(path);
					file >> fixture;
					REQUIRE(fixture.at("generator") == landscape.id);
					REQUIRE(fixture.at("seed") == seed);
					REQUIRE(fixture.at("wDec") == request.wDec);
					REQUIRE(fixture.at("hDec") == request.hDec);
					result.generatorId = landscape.id;
					result.revision = fixture.at("revision");
					request.options = fixture.at("options").get<std::map<std::string, int>>();
					auto &map = generated.game.map;
					map.setSize(request.wDec, request.hDec, GRASS);
					map.setGame(&generated.game);
					for (unsigned team = 0; team < 2; ++team)
					{
						generated.game.addTeam();
						generated.game.teams[team]->race.loadDefault();
					}
					map.installResourceDefinitions(fixture.at("resource_registry").get<std::string>());
					REQUIRE(map.resourceRegistry().serialize() == fixture.at("resource_registry").get<std::string>());
					REQUIRE(fixture.at("vertices").size() == map.cellCount());
					for (size_t i = 0; i < map.cellCount(); ++i)
					{
						const unsigned vertex = fixture["vertices"][i];
						REQUIRE(vertex <= GRASS);
						map.setVertexTerrain(i, TerrainType(vertex));
					}
					for (const auto &row : fixture.at("resources"))
					{
						const auto stocks = row[3].get<std::array<Uint16, MaterialCount>>();
						map.replaceResource(row[0].get<size_t>(),
							Resource{row[1].get<Uint16>(), row[2].get<Uint8>(), 0, 0}, &stocks);
						const auto tile = row[0].get<size_t>();
						REQUIRE(map.getResource(tile).type == row[1].get<Uint16>());
						REQUIRE(map.getResource(tile).variety == row[2].get<Uint8>());
						REQUIRE(map.materialStocksAt(tile) == stocks);
					}
					// A save-format translation is not enough: require identical growth
					// permissions, habitat and ecology rates at EVERY cell before comparing.
					const auto types = fixture.at("growth_types").get<std::vector<unsigned>>();
					unsigned mismatches = 0;
					for (size_t i = 0; i < map.cellCount(); ++i)
					{
						std::vector<Uint32> query(1 + 2 * types.size());
						query[0] = map.canResourcesGrow(i % map.getW(), i / map.getW());
						for (unsigned type = 0; type < types.size(); ++type)
						{
							query[1 + type] = map.terrainSupportsResourceAt(i, static_cast<ResourceId>(types[type]));
							query[1 + types.size() + type] = map.resourceGrowthRateAt(i, types[type]);
						}
						mismatches += query != fixture["growth_queries"][i].get<std::vector<Uint32>>();
					}
					INFO("Master growth-input mismatches: ", mismatches);
					REQUIRE(mismatches == 0);
				}
				else result = GenerationService().generate(generated.game, request);
				INFO(result.diagnostic());
				if (!masterInputs && !result)
				{
					report["generation_failures"].push_back({{"generator", landscape.id}, {"seed", seed},
						{"diagnostic", result.diagnostic()}});
					persist();
					if (!wide) REQUIRE(bool(result));
					continue; // Report refused seeds; never substitute favourable ones.
				}
				const auto name = result.generatorId + "-" + std::to_string(seed);
				if (seed == 1 && !masterInputs) artifact(name + "-original-colonies", serialize(generated.game));
				GameHeader header;
				header.setNumberOfPlayers(0);
				header.setRandomSeed(seed);
				generated.game.setGameHeader(header, true);
				for (int t = 0; t < generated.game.teamsCount(); ++t)
					generated.game.teams[t]->playersMask = 0;
				generated.game.clearingUncontrolledTeams();
				if (masterInputs) generated.game.syncRandom.seed(seed);
				const auto startingBytes = serialize(generated.game);
				if (seed == 1) artifact(name + "-initial", startingBytes);
				std::vector<Uint32> checksums;
				Json fallbackStatistics;
				Uint32 initialChecksum = 0;
				std::vector<std::pair<std::string, unsigned>> runs;
				if (!masterInputs) runs.emplace_back("immediate-reference", 0);
				for (unsigned delay : delays)
				{
					runs.emplace_back("zero-workers", delay);
					runs.emplace_back("shared", delay);
				}
				for (const auto &[variant, delay] : runs)
				{
					INFO(name, " ", variant, " delay=", delay);
					if (variant == "zero-workers") checksums.clear();
					GameGUI gui;
					auto &game = gui.game;
					GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(
						startingBytes.data(), startingBytes.size()));
					input.seekFromStart(0);
					REQUIRE(game.load(&input));
					REQUIRE(game.gameHeader.getNumberOfPlayers() == 0);
					for (int t = 0; t < game.teamsCount(); ++t)
					{
						for (int i = 0; i < Unit::MAX_COUNT; ++i) REQUIRE(game.teams[t]->myUnits[i] == nullptr);
						for (int i = 0; i < Building::MAX_COUNT; ++i) REQUIRE(game.teams[t]->myBuildings[i] == nullptr);
					}
					const auto loadedChecksum = game.checkSum(nullptr, nullptr, nullptr, true);
					if (variant == runs.front().first && delay == runs.front().second) initialChecksum = loadedChecksum;
					else REQUIRE(loadedChecksum == initialChecksum);
					auto &map = game.map;
					map.configureCompute(variant == "shared" ? 4 : 1);
					map.setResourceGrowthDelay(delay ? delay : 8);
					auto scan = [&]()
					{
						std::array<std::array<Uint64, MaterialCount>, 2> counts{};
						for (size_t cell = 0; cell < map.cellCount(); ++cell)
						{
							const auto type = map.getResource(cell).type;
							if (type == NO_RES_TYPE) continue;
							for (unsigned mask = map.resourcePropertiesByIndex(type).materialMask;
								mask; mask &= mask - 1)
							{
								const auto material = std::countr_zero(mask);
								counts[0][material] += map.materialAmountAtSlot(cell, material);
								++counts[1][material];
							}
						}
						return counts;
					};
					const auto initial = scan();
					if (masterInputs) REQUIRE(Json(initial) == fixture.at("initial"));
					// Some real generators deliberately omit a material. Record that absence
					// in wide experiments instead of injecting resources into the map.
					if (!wide)
						for (auto material : {MaterialId::Food, MaterialId::Wood, MaterialId::Algae})
							REQUIRE(initial[0][materialIndex(material)] > 0);
					const auto baseline = game.teams[0]->stats.measurements;
					Json checkpoints = Json::array(), statistics;
					for (unsigned tick = 1; tick <= ticks; ++tick)
					{
						CAPTURE(tick);
						if (variant == "immediate-reference")
						{
							const auto random = game.bindRandom();
							map.growResources();
						}
						else game.syncStep(-1);
						const auto counts = scan();
						statistics = Json::array();
						const auto &a = game.teams[0]->stats.measurements;
						for (unsigned m = 0; m < MaterialCount; ++m)
						{
							REQUIRE(counts[0][m] - initial[0][m] ==
								a.growthGlobal[1][m] - baseline.growthGlobal[1][m]);
							REQUIRE(counts[1][m] - initial[1][m] ==
								a.growthGlobal[0][m] - baseline.growthGlobal[0][m]);
							REQUIRE(a.growthGlobal[2][m] == baseline.growthGlobal[2][m]);
							for (unsigned k = 0; k < 3; ++k)
							{
								REQUIRE(a.growthGlobal[k][m] == game.teams[1]->stats.measurements.growthGlobal[k][m]);
								statistics.push_back(a.growthGlobal[k][m]);
							}
						}
						if (tick % 128 == 0) checkpoints.push_back({{"tick", tick}, {"counts", counts}});
						if (variant != "immediate-reference")
						{
							const auto hash = game.checkSum(nullptr, nullptr, nullptr, true);
							if (variant == "zero-workers") checksums.push_back(hash);
							else REQUIRE(hash == checksums[tick - 1]);
						}
					}
					map.finishResourceGrowth();
					if (variant == "zero-workers") fallbackStatistics = statistics;
					if (variant == "shared") REQUIRE(fallbackStatistics == statistics);
					Json sample = {{"generator", result.generatorId}, {"revision", result.revision},
						{"seed", seed}, {"width", map.getW()}, {"height", map.getH()}, {"options", request.options},
						{"variant", variant}, {"delay", delay}, {"initial_checksum", initialChecksum},
						{"initial", initial}, {"final", scan()}, {"statistics", statistics},
						{"checkpoints", checkpoints}};
					if (seed == 1) artifact(name + "-" + variant + "-d" + std::to_string(delay), serialize(game));
					if (variant != "immediate-reference")
					{
						const auto &m = map.resourceGrowthMetrics();
						REQUIRE(m.publishedProposals == m.accepted + m.rejected);
						sample["pipeline"] = {{"proposals", m.proposals}, {"accepted", m.accepted},
							{"rejected", m.rejected}, {"clamped", m.clamped}, {"stock_added", m.stockAdded},
							{"tiles_added", m.tilesAdded}, {"pending", m.proposals - m.publishedProposals}};
						map.gradientRuntime->growth.publish(map, game.stepCounter + delay);
						sample["terminal_flush"] = scan();
						// Reconcile the diagnostic tail too; it is never folded into normal checkpoints.
						const auto flushed = scan();
						const auto &after = game.teams[0]->stats.measurements;
						for (unsigned material = 0; material < MaterialCount; ++material)
							REQUIRE(flushed[0][material] - initial[0][material] ==
								after.growthGlobal[1][material] - baseline.growthGlobal[1][material]);
						REQUIRE(map.gradientRuntime->growth.count() == 0);
					}
					report["samples"].push_back(sample);
					persist();
				}
			}
		}
		REQUIRE(matched);
		if (output) REQUIRE(!report["samples"].empty());
	}

	TEST_CASE("identical live and snapshot kernel inputs [benchmark][resources]")
	{
		glob2test::HeadlessGlobals globals;
		Json report = {{"samples", Json::array()}};
		for (int shift : {7, 8, 9})
			for (const std::string scenario : {"sparse", "dense", "saturated", "blocked", "multi"})
			{
				glob2test::HeadlessGame world(
					{.wDec = shift, .hDec = shift, .header = true, .seed = 713});
				auto &map = world.game.map;
				setup(map, scenario);
				map.resourceGrowthField();
				const auto captured = world.game.snapshotStore().captureBoundary(
					world.game, ResourceGrowth::Pipeline::requirements());
				const std::array<MapState::View, 2> views{map.stateView(), captured.view()};
				ResourceGrowth::Batch output[2];
				for (auto &batch : output)
					batch.proposals.reserve(map.cellCount());
				for (unsigned seed = 1; seed <= 32; ++seed)
				{
					MersenneTwister a(seed), b(seed);
					ResourceGrowth::calculate(views[0], a, output[0]);
					ResourceGrowth::calculate(views[1], b, output[1]);
					REQUIRE(output[0].sampled == output[1].sampled);
					REQUIRE(output[0].proposals.size() == output[1].proposals.size());
					for (size_t n = 0; n < output[0].proposals.size(); ++n)
					{
						const auto &x = output[0].proposals[n], &y = output[1].proposals[n];
						REQUIRE(std::tie(x.tile, x.type, x.material, x.delta, x.variety, x.kind, x.incrementMask) ==
								std::tie(y.tile, y.type, y.material, y.delta, y.variety, y.kind, y.incrementMask));
					}
					REQUIRE(a() == b());
				}
				for (int repeat = -1; repeat < 20; ++repeat)
					for (int order = 0; order < 2; ++order)
					{
						const int mode = (order + repeat + 1) % 2;
						std::vector<MersenneTwister> rngs;
						for (unsigned seed = 1; seed <= 128; ++seed)
							rngs.emplace_back(seed);
						// Equal warm-up of the selected immutable input, outside timing.
						MersenneTwister warm(713);
						ResourceGrowth::calculate(views[mode], warm, output[mode]);
						Uint64 proposals = 0, sampled = 0;
						const auto start = Clock::now();
						for (auto &rng : rngs)
						{
							ResourceGrowth::calculate(views[mode], rng, output[mode]);
							proposals += output[mode].proposals.size();
							sampled += output[mode].sampled;
						}
						const auto elapsed = ns(start);
						report["samples"].push_back({{"size", 1 << shift},
													 {"scenario", scenario},
													 {"repeat", repeat},
													 {"snapshot", mode == 1},
													 {"elapsed_ns", elapsed},
													 {"proposals", proposals},
													 {"sampled", sampled}});
					}
			}
		if (const char *path = std::getenv("GLOB2_GROWTH_KERNEL_OUTPUT"))
		{
			std::ofstream out(path);
			out << report.dump(2);
		}
	}

	TEST_CASE("paired legacy split and delayed growth component costs [benchmark][resources]")
	{
		glob2test::HeadlessGlobals globals;
		const bool sharedCapture = std::getenv("GLOB2_GROWTH_EXISTING_CAPTURE") != nullptr;
		Json report = {{"kind", "growth-component"},
					   {"existing_capture", sharedCapture},
					   {"samples", Json::array()}};
		for (int shift : {7, 8, 9})
			for (const std::string scenario :
				 {"sparse", "dense", "saturated", "harvested", "blocked", "multi", "disabled"})
				for (int repeat = -1; repeat < 10; ++repeat)
					for (int order = 0; order < 4; ++order)
					{
						const int variant = (order + (repeat + 1)) % 4;
						glob2test::HeadlessGame world(
							{.wDec = shift, .hDec = shift, .header = true, .seed = 713});
						auto &map = world.game.map;
						setup(map, scenario);
						map.configureCompute(variant == 3 ? 4 : 1);
						map.setResourceGrowthDelay(8);
						// Warm ecology before timing; snapshot capture remains inside timing.
						map.resourceGrowthField();
						map.rebuildGrowthCoverage();
						world.game.syncRandom.seed(713);
						Uint64 coldCaptureNs = 0;
						if (variant || sharedCapture)
						{
							const auto cold = Clock::now();
							world.game.snapshotStore().captureBoundary(
								world.game, sharedCapture
												? SimulationSnapshot::All
												: ResourceGrowth::Pipeline::requirements());
							coldCaptureNs = ns(cold);
							world.game.snapshotStore().metrics = {};
						}
						const auto before = stocks(map);
						const auto start = Clock::now();
						ResourceGrowth::Metrics immediate;
						ResourceGrowth::Batch immediateBatch;
						for (unsigned tick = 0; tick < 64; ++tick)
						{
							world.game.stepCounter = tick;
							if (scenario == "harvested" && tick % 8 == 0)
								for (int y = 0; y < map.getH(); y += 2)
									for (int x = 0; x < map.getW(); x += 2)
									{
										const auto i = map.coordToIndex(x, y);
										const auto amount =
											map.materialAmountAt(i, MaterialId::Food);
										if (amount > 1)
											map.setMaterialAmount(i, MaterialId::Food, amount - 1);
									}
							// Model the completed-tick capture already required by another
							// consumer. All placements pay for the same component union;
							// growth receives a projection of that capture, not another copy.
							SimulationSnapshot::Handle existing;
							if (sharedCapture)
							{
								if (variant > 1)
									map.gradientRuntime->growth.publish(map, tick);
								world.game.snapshotStore().invalidateBoundary();
								existing = world.game.snapshotStore().captureBoundary(
									world.game, SimulationSnapshot::All);
							}
							if (variant == 0)
								map.growResources();
							else if (variant == 1 && scenario != "disabled")
							{
								if (!sharedCapture)
									world.game.snapshotStore().invalidateBoundary();
								auto snapshot =
									sharedCapture
										? existing.project(ResourceGrowth::Pipeline::requirements())
										: world.game.snapshotStore().captureBoundary(
											  world.game, ResourceGrowth::Pipeline::requirements());
								MersenneTwister random(syncRand());
								const auto computing = Clock::now();
								ResourceGrowth::calculate(snapshot.view(), random, immediateBatch);
								immediate.computeNs += ns(computing);
								immediate.sampled += immediateBatch.sampled;
								immediate.proposals += immediateBatch.proposals.size();
								ResourceGrowth::apply(map, immediateBatch, immediate);
							}
							else if (variant > 1)
							{
								if (!sharedCapture)
									map.gradientRuntime->growth.publish(map, tick);
								map.stageResourceGrowth();
								if (sharedCapture)
									map.preparePendingWorld(existing);
								else
									map.preparePendingWorld();
							}
						}
						map.finishResourceGrowth();
						const auto elapsed = ns(start);
						const auto &m = variant == 1 ? immediate : map.resourceGrowthMetrics();
						Json row = {
							{"size", 1 << shift},
							{"scenario", scenario},
							{"repeat", repeat},
							{"variant", variant},
							{"elapsed_ns", elapsed},
							{"initial_food", before},
							{"final_food", stocks(map)},
							{"cold_capture_ns", coldCaptureNs},
							{"capture_ns", world.game.snapshotStore().metrics.captureNs},
							{"copied_bytes", world.game.snapshotStore().metrics.bytesCopied},
							{"compute_ns", m.computeNs},
							{"publication_ns", m.publicationNs},
							{"wait_ns", m.waitNs},
							{"proposals", m.proposals},
							{"accepted", m.accepted},
							{"sampled", m.sampled},
							{"clamped", m.clamped},
							{"stock_added", m.stockAdded},
							{"tiles_added", m.tilesAdded},
							{"max_pending", m.maxPending},
							{"proposal_bytes", m.maxProposalBytes},
							{"snapshot_peak_bytes",
							 world.game.snapshotStore().memoryMetrics().peakRetainedBytes},
							{"rejected", m.rejected}};
						report["samples"].push_back(row);
						std::cout << "GROWTH_COMPONENT " << row.dump() << '\n';
					}
		if (const char *path = std::getenv("GLOB2_GROWTH_BENCHMARK_OUTPUT"))
		{
			std::ofstream out(path);
			out << report.dump(2);
			REQUIRE(out.good());
		}
	}
	TEST_CASE("twenty seed ecology comparison [benchmark][resources]")
	{
		glob2test::HeadlessGlobals globals;
		Json report = {{"kind", "growth-ecology"}, {"samples", Json::array()}};
		for (unsigned seed = 1; seed <= 20; ++seed)
			for (bool deplete : {false, true})
				for (int variant = 0; variant < 2; ++variant)
				{
					glob2test::HeadlessGame world(
						{.wDec = 7, .hDec = 7, .header = true, .seed = seed});
					auto &map = world.game.map;
					setup(map, "dense");
					map.setResourceGrowthDelay(8);
					world.game.syncRandom.seed(seed);
					const auto initialFood = stocks(map);
					const Uint64 initialDeposits = Uint64(map.getW()) * map.getH() / 4;
					Uint64 harvested = 0, depleted = 0;
					for (unsigned tick = 0; tick < 512; ++tick)
					{
						world.game.stepCounter = tick;
						if (tick % 8 == 0)
							for (int y = 0; y < map.getH(); y += 2)
								for (int x = 0; x < map.getW(); x += 2)
								{
									const auto i = map.coordToIndex(x, y);
									auto amount = map.materialAmountAt(i, MaterialId::Food);
									if (amount > (deplete ? 0 : 1))
									{
										map.setMaterialAmount(i, MaterialId::Food, amount - 1);
										++harvested;
										depleted += map.getResource(i).type == NO_RES_TYPE;
									}
								}
						if (variant == 0)
							map.growResources();
						else
						{
							map.gradientRuntime->growth.publish(map, tick);
							map.stageResourceGrowth();
							map.preparePendingWorld();
						}
					}
					map.finishResourceGrowth();
					Uint64 deposits = 0;
					for (const auto &cell : map.cellView().resources)
						deposits += cell.resource.type != NO_RES_TYPE;
					// This fixture has one material and one-unit seeds. Conservation
					// separates replenishment from spread without instrumenting legacy growth.
					const auto food = stocks(map);
					REQUIRE(deposits + depleted >= initialDeposits);
					const auto seeds = deposits + depleted - initialDeposits;
					REQUIRE(food + harvested >= initialFood + seeds);
					report["samples"].push_back(
						{{"seed", seed},
						 {"harvest_policy", deplete ? "deplete" : "reserve"},
						 {"variant", variant},
						 {"food", food},
						 {"deposits", deposits},
						 {"depleted", depleted},
						 {"seeded", seeds},
						 {"replenished", food + harvested - initialFood - seeds},
						 {"harvested", harvested}});
				}
		if (const char *path = std::getenv("GLOB2_GROWTH_ECOLOGY_OUTPUT"))
		{
			std::ofstream out(path);
			out << report.dump(2);
			REQUIRE(out.good());
		}
		std::cout << "GROWTH_ECOLOGY " << report.dump() << '\n';
	}
}
