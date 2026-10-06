// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingGradientBuild.h"
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>

// Observational only. Fresh fields and event counters never enter saved simulation state.
struct BuildingGradientImpact
{
	std::ofstream decisions, outcomes;
	std::uint64_t index = 0, tick = 0, deliveries = 0;
	std::string kind;
	int building = -1, unit = -1;
	std::uint32_t tieRoll = 0;
	std::function<std::uint32_t(int, bool)> identity;
	struct Journey
	{
		std::uint32_t identity = 0, buildingIdentity = 0, started = 0;
		int building = -1, resource = -1, x = 0, y = 0, dx = 0, dy = 0, carried = -1;
		unsigned distance = 0, reversals = 0;
	};
	std::map<int, Journey> journeys;
	struct MissedHire
	{
		std::uint32_t unitIdentity, buildingIdentity, started;
		int resource;
		bool selected = true;
	};
	using HiringEpisodeKey = std::tuple<int, int, bool, std::uint32_t, std::uint32_t, int>;
	std::map<HiringEpisodeKey, MissedHire> missedHires;
	std::map<int, std::pair<std::uint32_t, bool>> buildings;
	std::ofstream ticks;
	std::uint64_t lastTickNs = 0, counterfactualTick = ~std::uint64_t(0), counterfactualEvent = 0;
	bool counterfactualApplied = false;
	std::shared_ptr<building_gradient::Terrain> terrain;
	std::map<std::pair<int, int>, building_gradient::Result> oracle;
	std::map<std::tuple<int, int, int>, std::vector<std::uint16_t>> publishedFields;
	std::map<std::tuple<int, int, int>, std::vector<std::uint16_t>> resourceOracle;
	GradientWorkspace scratch;
	explicit BuildingGradientImpact(const std::string &prefix)
		: decisions(prefix + "-decisions.csv"), outcomes(prefix + "-outcomes.csv"),
		  ticks(prefix + "-ticks.csv")
	{
		if (!decisions || !outcomes || !ticks)
			throw std::runtime_error("cannot open building gradient impact telemetry");
		decisions
			<< "tick,event,kind,building,unit,live_choice,fresh_choice,live_resource,fresh_"
			   "resource,live_score,fresh_score,changed,harm_cost,live_age,pending,live_"
			   "reason,fresh_reason,building_identity,unit_identity,live_identity,fresh_identity\n";
		outcomes << "tick,kind,building,unit,resource,elapsed_ticks,distance,reversals,censored,x,"
					"y,building_identity,unit_identity,event,censor_reason,source_building,source_building_identity\n";
		ticks << "tick,tick_ns,buildings,units,unfilled_slots,deliveries_total,construction_"
				 "completions,hungry_units,deaths_total\n";
	}
	void begin(std::uint64_t step, const char *category, int gid, int uid)
	{
		tick = step;
		kind = category;
		building = gid;
		unit = uid;
		++index;
		oracle.clear();
		publishedFields.clear();
		resourceOracle.clear();
		terrain.reset();
	}
	void record(int live, int fresh, int liveResource, int freshResource, int liveScore,
				int freshScore, int harm, unsigned age, bool pending, int liveReason = -1,
				int freshReason = -1)
	{
		decisions << tick << ',' << index << ',' << kind << ',' << building << ',' << unit << ','
				  << live << ',' << fresh << ',' << liveResource << ',' << freshResource << ','
				  << liveScore << ',' << freshScore << ','
				  << (live != fresh || liveResource != freshResource) << ',' << harm << ',' << age
				  << ',' << pending << ',' << liveReason << ',' << freshReason;
		const bool hiring = kind == "hiring" || kind == "hiring_candidate",
				   market = kind == "resource";
		decisions << ',' << identity(building, true) << ',' << identity(unit, false) << ','
				  << (hiring || market ? identity(live, market) : 0) << ','
				  << (hiring || market ? identity(fresh, market) : 0) << '\n';
	}
	void outcome(std::uint32_t step, const char *category, int gid, int uid, int resource,
				 unsigned elapsed, unsigned distance, unsigned reversals, bool censored, int x = -1,
				 int y = -1, std::uint32_t unitIdentity = 0, std::uint32_t buildingIdentity = 0,
				 const char *censorReason = "", int sourceBuilding = -1,
				 std::uint32_t sourceIdentity = 0)
	{
		outcomes << step << ',' << category << ',' << gid << ',' << uid << ',' << resource << ','
				 << elapsed << ',' << distance << ',' << reversals << ',' << censored << ',' << x
				 << ',' << y << ',' << (buildingIdentity ? buildingIdentity : identity(gid, true))
				 << ',' << (unitIdentity ? unitIdentity : identity(uid, false)) << ',' << index << ',' << censorReason
				 << ',' << sourceBuilding << ',' << sourceIdentity << '\n';
	}
};
