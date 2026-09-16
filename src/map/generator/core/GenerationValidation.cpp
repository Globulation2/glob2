// SPDX-License-Identifier: GPL-3.0-or-later
#include <PerformanceTelemetry.h>
#include "GenerationValidation.h"
#include <string>
#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
#include "IntBuildingType.h"
#include "Unit.h"
std::string validateGenerationRequest(const GenerationRequest &r, const GeneratorDefinition &d)
{
	for (const auto &c : sharedGeneratorControls())
		if (c.get(r) != c.normalize(c.get(r)))
			return "Invalid " + c.id;
	if (r.terrainType < WATER || r.terrainType > GRASS)
		return "Invalid terrain";
	for (const auto &c : d.controls)
	{
		auto it = r.options.find(c.id);
		if (it == r.options.end() || c.normalize(it->second) != it->second)
			return "Invalid " + c.id;
	}
	for (const auto &v : r.options)
	{
		bool found = false;
		for (const auto &c : d.controls)
			found |= c.id == v.first;
		if (!found)
			return "Unknown option " + v.first;
	}
	for (int amount : r.resourceAmounts)
		if (amount < 0 || amount > 100)
			return "Invalid legacy resource amount";
	if (!r.hasTerrainWeight(d.controls))
		return "Give at least one terrain type a nonzero weight.";
	return d.validateRequest ? d.validateRequest(r) : std::string{};
}
std::string validateGeneratedWorld(const Game &g, const GenerationRequest &r,
								   const GeneratorDefinition &d)
{
	PERF_SCOPE_TIME(Validation);
	if (g.map.getW() != (1 << r.wDec) || g.map.getH() != (1 << r.hDec))
		return "Incorrect map dimensions";
	if (g.mapHeader.getNumberOfTeams() != (d.hasStartingColonies ? r.nbTeams : 1))
		return "Incorrect colony count";
	// No generated map may disable resource growth. The engine's saved canResourcesGrow flag
	// is for hand-made scenarios such as the tutorial; a generated map contains its crops with
	// terrain, or it does not contain them. Using the flag froze farmland and hid overgrowth
	// the design should have solved (2026-09-16).
	for (int y = 0; y < g.map.getH(); ++y)
		for (int x = 0; x < g.map.getW(); ++x)
			if (!g.map.canResourcesGrow(x, y))
				return "Generated maps may not disable resource growth (no-growth zone at " +
					   std::to_string(x) + "," + std::to_string(y) + ")";
	// Every colony starts with the lobby's shared "Starting workers" value.
	const int expectedWorkers = r.nbWorkers;
	if (d.hasStartingColonies)
		for (int i = 0; i < r.nbTeams; ++i)
		{
			const Team *team = g.teams[i];
			if (!team)
				return "Missing colony";
			if (team->startPosX < 0 || team->startPosX >= g.map.getW() || team->startPosY < 0 ||
				team->startPosY >= g.map.getH())
				return "Starting position outside map bounds";
			int workers = 0;
			bool swarm = false;
			for (int slot = 0; slot < Unit::MAX_COUNT; ++slot)
				if (const auto *unit = team->myUnits[slot]; unit && unit->typeNum == WORKER)
					++workers;
			for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
				if (const auto *building = team->myBuildings[slot];
					building && building->type->shortTypeNum == IntBuildingType::SWARM_BUILDING)
					swarm = true;
			if (workers != expectedWorkers || !swarm)
				return "Incomplete starting colony";
		}
	return {};
}
