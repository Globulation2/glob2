// SPDX-License-Identifier: GPL-3.0-or-later
#include "GenerationValidation.h"
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
	if (g.map.getW() != (1 << r.wDec) || g.map.getH() != (1 << r.hDec))
		return "Incorrect map dimensions";
	if (g.mapHeader.getNumberOfTeams() != (d.hasStartingColonies ? r.nbTeams : 1))
		return "Incorrect colony count";
	if (d.hasStartingColonies)
		for (int i = 0; i < r.nbTeams; ++i)
		{
			const Team *team = g.teams[i];
			if (!team)
				return "Missing colony";
			int workers = 0;
			bool swarm = false;
			for (int slot = 0; slot < Unit::MAX_COUNT; ++slot)
				if (const auto *unit = team->myUnits[slot]; unit && unit->typeNum == WORKER)
					++workers;
			for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
				if (const auto *building = team->myBuildings[slot];
					building && building->type->shortTypeNum == IntBuildingType::SWARM_BUILDING)
					swarm = true;
			if (workers != r.nbWorkers || !swarm)
				return "Incomplete starting colony";
		}
	return {};
}
