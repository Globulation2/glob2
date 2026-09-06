// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Diagnostic counters for the pathfinding subsystem. Non-simulation state:
// never read by game logic, so it cannot affect determinism. Printed at the
// end of a headless run when GLOB2_PATHFIND_STATS is set.

#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>

namespace PathfindStats
{
	constexpr int MAX_TEAMS = 32;

	struct Timer
	{
		std::uint64_t calls = 0;
		std::uint64_t ns = 0;
	};

	struct Counters
	{
		Timer globalChamfer;        // Map::updateGlobalGradient(Uint8*) — every full-map sweep
		std::uint64_t chamferPasses = 0;
		Timer localGradient;        // Map::updateLocalGradient (32x32)
		Timer buildingGlobal;       // Map::updateGlobalGradient(Building*) — seeding + chamfer
		Timer resourcesGradient;    // Map::updateResourcesGradient
		Timer areaGradient;         // forbidden / guard / clear gradients
		Timer pointToPoint;         // Map::pathfindPointToPoint (A*)
		Timer weightedField;        // Map::buildWeightedField (alternative pathfinder)
		std::uint64_t directionByCostCalls = 0;
		std::uint64_t composedFieldBuilds = 0;
		std::uint64_t pointToPointExpanded = 0;
		std::uint64_t minigradCalls = 0;
		std::uint64_t pathfindBuildingCalls = 0;
		std::uint64_t pathfindResourceCalls = 0;
		std::uint64_t pathfindResourceStuck = 0;   // no strictly-uphill free neighbour -> random step

		// Per-team behaviour, indexed by team number.
		std::uint64_t movesCardinal[MAX_TEAMS] = {};
		std::uint64_t movesDiagonal[MAX_TEAMS] = {};
		std::uint64_t movesWalk[MAX_TEAMS] = {};
		std::uint64_t movesSwim[MAX_TEAMS] = {};
		std::uint64_t movesRandomWhileWorking[MAX_TEAMS] = {};
		std::uint64_t deliveries[MAX_TEAMS] = {};
	};

	inline Counters& get()
	{
		static Counters c;
		return c;
	}

	class Scope
	{
	public:
		explicit Scope(Timer& t) : timer(t), start(std::chrono::steady_clock::now()) {}
		~Scope()
		{
			timer.calls++;
			timer.ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
		}
	private:
		Timer& timer;
		std::chrono::steady_clock::time_point start;
	};

	inline void printTimer(const char* name, const Timer& t)
	{
		std::printf("GLOB2_PF %s calls=%llu ms=%.1f avg_us=%.1f\n", name,
			(unsigned long long)t.calls, t.ns / 1e6, t.calls ? (t.ns / 1e3) / t.calls : 0.0);
	}

	inline void print(int numberOfTeams, double totalCpuMs)
	{
		const Counters& c = get();
		std::printf("GLOB2_PF total_cpu_ms=%.0f\n", totalCpuMs);
		printTimer("global_chamfer", c.globalChamfer);
		std::printf("GLOB2_PF chamfer_passes=%llu avg_passes=%.2f\n", (unsigned long long)c.chamferPasses,
			c.globalChamfer.calls ? (double)c.chamferPasses / c.globalChamfer.calls : 0.0);
		printTimer("resources_gradient", c.resourcesGradient);
		printTimer("area_gradient", c.areaGradient);
		printTimer("building_global", c.buildingGlobal);
		printTimer("local_gradient", c.localGradient);
		printTimer("point_to_point", c.pointToPoint);
		printTimer("weighted_field", c.weightedField);
		std::printf("GLOB2_PF direction_by_cost_calls=%llu composed_field_builds=%llu\n", (unsigned long long)c.directionByCostCalls, (unsigned long long)c.composedFieldBuilds);
		std::printf("GLOB2_PF point_to_point_expanded=%llu\n", (unsigned long long)c.pointToPointExpanded);
		std::printf("GLOB2_PF minigrad_calls=%llu pathfind_building_calls=%llu pathfind_resource_calls=%llu pathfind_resource_stuck=%llu\n",
			(unsigned long long)c.minigradCalls, (unsigned long long)c.pathfindBuildingCalls,
			(unsigned long long)c.pathfindResourceCalls, (unsigned long long)c.pathfindResourceStuck);
		for (int t = 0; t < numberOfTeams && t < MAX_TEAMS; t++)
			std::printf("GLOB2_PF_TEAM team=%d moves_cardinal=%llu moves_diagonal=%llu moves_walk=%llu moves_swim=%llu random_while_working=%llu deliveries=%llu\n",
				t, (unsigned long long)c.movesCardinal[t], (unsigned long long)c.movesDiagonal[t],
				(unsigned long long)c.movesWalk[t], (unsigned long long)c.movesSwim[t],
				(unsigned long long)c.movesRandomWhileWorking[t], (unsigned long long)c.deliveries[t]);
	}
}
