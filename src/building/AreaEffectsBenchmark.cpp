// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AreaEffects.h"
#include "BuildingType.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdio>
#include <array>
#include <algorithm>
#include <string_view>
#ifndef WIN32
#include <sys/resource.h>
#endif

namespace
{
using Clock = std::chrono::steady_clock;
long long ns(Clock::time_point start)
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
long long median(std::vector<long long> times)
{
	std::sort(times.begin(), times.end());
	return times[times.size() / 2];
}
Uint64 peakRss()
{
#ifdef WIN32
	return 0; // Not measured by this benchmark on Windows.
#else
	rusage usage{};
	getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
	return usage.ru_maxrss;
#else
	return Uint64(usage.ru_maxrss) * 1024;
#endif
#endif
}

} // namespace
TEST_SUITE("BuildingAreaEffectsBenchmark")
{
	TEST_CASE("dense fields measure steady pulses rebuilds and memory [benchmark]")
	{
		glob2test::HeadlessGlobals globals;
		std::printf("aura_fields,width,teams,emitters,phase,median_ns,field_bytes,rebuild_chunks,"
					"emitter_visits,field_allocations,process_peak_rss_bytes\n");
		for (int shift : {8, 9, 10})
			for (int teamCount : {1, 4, Team::MAX_COUNT})
				for (int count : {0, 32, 128, 512})
				{
					glob2test::HeadlessGame world({.wDec = shift,
												   .hDec = shift,
												   .teams = teamCount,
												   .loadDefaultRace = true,
												   .header = true,
												   .seed = 713});
					auto catalog = nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
					const int id = world.game.buildingsTypes.getTypeNum("stonewall", 0, false);
					if (count)
						catalog["variants"][id]["semantics"]["areaEffects"] = {
							{"radius", 8},
							{"healingQ8", 128},
							{"damageQ8", 128},
							{"feedingQ8", 128},
							{"attackBuffBps", 1000},
							{"armorBuffBps", 1000},
							{"attackWeaknessBps", 1000},
							{"armorWeaknessBps", 1000},
							{"fertilityBuffBps", 1000},
							{"cost", {{"food", 1}}}};
					world.game.buildingsTypes.loadSnapshotJson(catalog.dump());
					world.game.configureBuildingCatalog();
					std::vector<Building *> buildings;
					for (int n = 0; n < count; ++n)
					{
						const int x = 4 + (n % 32) * 4, y = 4 + (n / 32) * 4;
						auto *b = world.addBuilding("stonewall", x, y, 0, n % teamCount);
						b->materials[WHEAT] = 100000;
						buildings.push_back(b);
					}
					for (int t = 0; t < teamCount; ++t)
					{
						world.game.teams[t]->allies = 1u << t;
						world.game.teams[t]->enemies = ~(1u << t);
					}
					auto &runtime = world.game.areaEffects;
					const auto report = [&](const char *phase, const std::vector<long long> &times)
					{
						std::printf("aura_fields,%d,%d,%d,%s,%lld,%zu,%llu,%llu,%llu,%llu\n",
									1 << shift, teamCount, count, phase, median(times),
									runtime.fieldBytes(),
									(unsigned long long)runtime.metrics.rebuiltChunks,
									(unsigned long long)runtime.metrics.emitterVisits,
									(unsigned long long)runtime.metrics.allocations,
									(unsigned long long)peakRss());
					};
					auto start = Clock::now();
					runtime.beginTick(world.game);
					report("initial", {ns(start)});
					std::vector<long long> steady, pulses;
					const auto visits = runtime.metrics.emitterVisits,
							   allocations = runtime.metrics.allocations;
					for (int tick = 1; tick <= 256; ++tick)
					{
						world.game.stepCounter = tick;
						start = Clock::now();
						runtime.beginTick(world.game);
						((tick & 15) ? steady : pulses).push_back(ns(start));
					}
					CHECK(runtime.metrics.emitterVisits == visits);
					CHECK(runtime.metrics.allocations == allocations);
					report("steady", steady);
					report("pulse", pulses);
					if (count)
					{
						if (teamCount > 1)
						{
							for (int t = 0; t < teamCount; ++t)
							{
								world.game.teams[t]->allies = (1u << teamCount) - 1;
								world.game.teams[t]->enemies = 0;
							}
							world.game.stepCounter = 257;
							start = Clock::now();
							runtime.beginTick(world.game);
							report("diplomacy", {ns(start)});
						}
						for (auto *b : buildings)
							b->kill();
						world.game.stepCounter = 258;
						start = Clock::now();
						runtime.beginTick(world.game);
						report("mass_removal", {ns(start)});
					}
				}
	}
	TEST_CASE("populated matches compare disabled and stationary aura tick cost [benchmark]")
	{
		glob2test::HeadlessGlobals globals;
		std::printf("aura_ticks,emitters,enabled,repeat,steady_median_ns,pulse_median_ns,field_"
					"bytes,units,entity_digest,next_random\n");
		const char *selected = SDL_getenv_unsafe("GLOB2_TEST_AREA_BENCH_MODE");
		const std::string_view mode = selected ? selected : "";
		for (int count : {0, 32, 128, 512})
			for (int repeat = 0; repeat < 5; ++repeat)
				for (bool enabled : {false, true})
				{
					if ((mode == "disabled" && enabled) || (mode == "enabled" && !enabled))
						continue;
					glob2test::HeadlessGame world({.wDec = 8,
												   .hDec = 8,
												   .loadDefaultRace = true,
												   .header = true,
												   .seed = 713});
					auto catalog = nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
					const int id = world.game.buildingsTypes.getTypeNum("stonewall", 0, false);
					if (enabled)
						catalog["variants"][id]["semantics"]["areaEffects"] = {
							{"radius", 8},          {"healingQ8", 128},
							{"feedingQ8", 128},     {"attackBuffBps", 1000},
							{"armorBuffBps", 1000}, {"fertilityBuffBps", 1000}};
					world.game.buildingsTypes.loadSnapshotJson(catalog.dump());
					world.game.configureBuildingCatalog();
					for (int n = 0; n < count; ++n)
						world.addBuilding("stonewall", 4 + (n % 32) * 4, 4 + (n / 32) * 4);
					for (int n = 0; n < 256; ++n)
					{
						auto *u = world.addUnit(WORKER, 5 + 2 * (n % 64), 5 + 2 * (n / 64));
						u->hungriness = 0;
					}
					world.team->createLists();
					world.game.setWaitingOnMask(0);
					world.step(64);
					std::vector<long long> steady, pulses;
					for (int n = 0; n < 256; ++n)
					{
						const auto tick = world.game.stepCounter;
						const auto start = Clock::now();
						world.step();
						((tick & 15) ? steady : pulses).push_back(ns(start));
					}
					int units = 0;
					for (int n = 0; n < Unit::MAX_COUNT; ++n)
						units += world.team->myUnits[n] != nullptr;
					std::vector<Uint32> states, unitStates;
					world.game.checkSum(nullptr, &states, &unitStates, true);
					states.insert(states.end(), unitStates.begin(), unitStates.end());
					Uint32 digest = 2166136261u;
					for (auto state : states)
						digest = (digest ^ state) * 16777619u;
					auto random = world.game.syncRandom;
					std::printf("aura_ticks,%d,%d,%d,%lld,%lld,%zu,%d,%u,%u\n", count, int(enabled),
								repeat, median(steady), median(pulses),
								world.game.areaEffects.fieldBytes(), units, digest, random());
				}
	}
}
