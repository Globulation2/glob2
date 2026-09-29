// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexTuning.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <set>
#include <limits>

namespace Cortex
{
	/// Name -> member binding for the loader, plus the lowest legal value (a
	/// degenerate knob like tierMidDiv=0 would divide by zero; a search must not
	/// be able to request it). Upper bounds are the SEARCH's business, not the
	/// loader's.
	struct TuningField
	{
		const char* name;
		int CortexTuning::*member;
		int minValue;
	};
	static const TuningField TUNING_FIELDS[] = {
		{ "expandWheatLo",        &CortexTuning::expandWheatLo,        1 },
		{ "swarmWorkerCap",       &CortexTuning::swarmWorkerCap,       1 },
		{ "swarmWheatRemHi",      &CortexTuning::swarmWheatRemHi,      1 },
		{ "wheatStarvedTiles",    &CortexTuning::wheatStarvedTiles,    0 },
		{ "expandWheatVeto",      &CortexTuning::expandWheatVeto,      0 },
		{ "expandDebounceCycles", &CortexTuning::expandDebounceCycles, 1 },
		{ "scoreSecondSwarmBase", &CortexTuning::scoreSecondSwarmBase, 1 },
		{ "scoreSecondSwarmStep", &CortexTuning::scoreSecondSwarmStep, 0 },
		{ "expandSeverityFloor",  &CortexTuning::expandSeverityFloor,  1 },
		{ "attackRangeBase",      &CortexTuning::attackRangeBase,      0 }, // 0 == gate disabled
		{ "attackRangePerWalkLevel", &CortexTuning::attackRangePerWalkLevel, 0 },
		{ "warPrepLevelMatch",    &CortexTuning::warPrepLevelMatch,    0 }, // 0/1
		{ "attackRangeGraceTicks", &CortexTuning::attackRangeGraceTicks, 0 }, // 0 == never waive
		{ "attackRangeUnscoutedWaiver", &CortexTuning::attackRangeUnscoutedWaiver, 0 }, // 0/1
		{ "landingStandoffTiles", &CortexTuning::landingStandoffTiles, 0 },
		{ "crossTimeoutTicks",    &CortexTuning::crossTimeoutTicks,    1 },
		{ "fleetReleaseArrived",  &CortexTuning::fleetReleaseArrived,  1 },
		{ "amphibiousMinSwimWarriors", &CortexTuning::amphibiousMinSwimWarriors, 0 },
		{ "forwardRallyPathDist", &CortexTuning::forwardRallyPathDist, 0 }, // 0 == disabled
		{ "tierMidDiv",           &CortexTuning::tierMidDiv,           1 },
		{ "workerRatioTier2",     &CortexTuning::workerRatioTier2,     1 },
	};
	static const int NUM_TUNING_FIELDS =
		static_cast<int>(sizeof(TUNING_FIELDS) / sizeof(TUNING_FIELDS[0]));

	/// A tuning file that fails to apply must kill the process, not fall back to
	/// defaults: a knob search whose configs silently didn't load would "measure"
	/// the default policy N times and pick a random winner.
	static void tuningFail(const std::string& msg)
	{
		std::cerr << "GLOB2_CORTEX_TUNING: " << msg << std::endl;
		std::exit(1);
	}

	static CortexTuning loadTuning()
	{
		CortexTuning tuning;
		const char* path = std::getenv("GLOB2_CORTEX_TUNING");
		if (!path || !path[0])
			return tuning;

		std::ifstream in(path);
		if (!in)
			tuningFail(std::string("cannot open '") + path
			           + "' (use an ABSOLUTE path; glob2 chdir()s at startup)");

		bool seen[NUM_TUNING_FIELDS] = {};
		std::string line;
		int lineNo = 0;
		while (std::getline(in, line))
		{
			lineNo++;
			const std::string::size_type hash = line.find('#');
			if (hash != std::string::npos)
				line.erase(hash);
			std::istringstream tokens(line);
			std::string key;
			if (!(tokens >> key))
				continue; // blank line or pure comment
			int value;
			std::string trailing;
			if (!(tokens >> value) || (tokens >> trailing))
				tuningFail("line " + std::to_string(lineNo) + ": expected 'key value', got '" + line + "'");
			int field = -1;
			for (int i = 0; i < NUM_TUNING_FIELDS; i++)
				if (key == TUNING_FIELDS[i].name)
					field = i;
			if (field < 0)
				tuningFail("line " + std::to_string(lineNo) + ": unknown key '" + key + "'");
			if (seen[field])
				tuningFail("line " + std::to_string(lineNo) + ": duplicate key '" + key + "'");
			if (value < TUNING_FIELDS[field].minValue)
				tuningFail("line " + std::to_string(lineNo) + ": " + key + "="
				           + std::to_string(value) + " is below the legal minimum "
				           + std::to_string(TUNING_FIELDS[field].minValue));
			seen[field] = true;
			tuning.*(TUNING_FIELDS[field].member) = value;
		}

		// Echo the effective vector once so every benchmark game log records the
		// exact config it ran (the search driver's audit trail).
		std::cerr << "CortexTuning '" << path << "':";
		for (int i = 0; i < NUM_TUNING_FIELDS; i++)
			std::cerr << ' ' << TUNING_FIELDS[i].name << '='
			          << tuning.*(TUNING_FIELDS[i].member);
		std::cerr << std::endl;
		return tuning;
	}

	static thread_local const CortexTuning* activeTuning = nullptr;
	TuningScope::TuningScope(const CortexTuning& tuning) : previous(activeTuning) { activeTuning = &tuning; }
	TuningScope::~TuningScope() { activeTuning = previous; }

	std::string tuningValues(const CortexTuning& tuning)
	{
		std::ostringstream out;
		for (const auto &field : TUNING_FIELDS)
			out << field.name << "=" << tuning.*(field.member) << "\n";
		return out.str();
	}
	bool applyTuning(CortexTuning& tuning, const std::string& values, std::string& error)
	{
		CortexTuning candidate = tuning;
		std::set<std::string> seen;
		std::istringstream in(values);
		std::string line;
		while (std::getline(in, line))
		{
			if (line.empty()) continue;
			const auto eq = line.find('=');
			const std::string key = line.substr(0, eq);
			bool found = false;
			for (const auto &field : TUNING_FIELDS)
				if (key == field.name && eq != std::string::npos && seen.insert(key).second)
				{
					std::istringstream value(line.substr(eq+1));
					int n; std::string trailing;
					if (!(value >> n) || (value >> trailing) || n < field.minValue)
					{ error = "invalid Cortex value: " + line; return false; }
					candidate.*(field.member) = n;
					found = true;
					break;
				}
			if (!found) { error = "unknown or duplicate Cortex parameter: " + key; return false; }
		}
		tuning = candidate;
		return true;
	}
	std::string tuningSchemaJson()
	{
		std::ostringstream out;
		out << '[';
		CortexTuning defaults;
		bool comma = false;
		for (const auto &field : TUNING_FIELDS)
		{
			if (comma) out << ',';
			comma = true;
			out << "{\"key\":\"" << field.name << "\",\"type\":\"int\",\"min\":"
				<< field.minValue << ",\"max\":" << std::numeric_limits<int>::max()
				<< ",\"default\":" << defaults.*(field.member) << '}';
		}
		out << ']'; return out.str();
	}
	const CortexTuning& cortexTuning()
	{
		if (activeTuning) return *activeTuning;
		static const CortexTuning tuning = loadTuning();
		return tuning;
	}
}
