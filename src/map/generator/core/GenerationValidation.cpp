#include "GenerationResult.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include <PerformanceTelemetry.h>
#include "GenerationValidation.h"
#include <string>
#include "GenerationWorld.h"
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
std::string validateGenerationRequest(const GenerationRequest &r, const GeneratorDefinition &d)
{
	// A request is valid at any size either screen offers: the editor's range includes the lobby's.
	for (const auto &shared : sharedGeneratorControls())
	{
		const GeneratorControl c = editorSizeControl(shared);
		if (c.get(r) != c.normalize(c.get(r)))
			return "Invalid " + c.id;
	}
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
	try { return d.validateRequest ? d.validateRequest(r) : std::string{}; }
    catch(const ScriptGenerationFailure &error) { return error.what(); }
}
std::string validateGeneratedWorld(const GenerationWorld &g, const GenerationRequest &r,
								   const GeneratorDefinition &d)
{
	PERF_SCOPE_TIME(Validation);
	if (g.map.getW() != (1 << r.wDec) || g.map.getH() != (1 << r.hDec))
		return "Incorrect map dimensions";
	if (g.teamsCount() != (d.hasStartingColonies ? r.nbTeams : 1))
		return "Incorrect colony count";
	// No generated map may disable resource growth. The engine's saved canResourcesGrow flag
	// is for hand-made scenarios such as the tutorial; a generated map contains its crops with
	// terrain, or it does not contain them. Inspect the scenario flag itself, not
	// canResourcesGrow(), which also includes the terrain's natural growth permission.
	for (int y = 0; y < g.map.getH(); ++y)
		for (int x = 0; x < g.map.getW(); ++x)
			if (!g.map.resourceState()[g.map.coordToIndex(x, y)].mayGrow)
				return "Generated maps may not disable resource growth (no-growth zone at " +
					   std::to_string(x) + "," + std::to_string(y) + ")";
	// Normally the shared worker count is per colony; detached settlements may grant it per swarm.
	if (d.hasStartingColonies)
		for (int i = 0; i < r.nbTeams; ++i)
		{
			if (!g.hasTeam(i))
				return "Missing colony";
			const GenerationTeamStart start = g.teamStart(i);
			if (start.x < 0 || start.x >= g.map.getW() || start.y < 0 || start.y >= g.map.getH())
				return "Starting position outside map bounds";
			int workers = 0;
			int swarms = 0;
			for (const GenerationUnit &unit : g.teamUnits(i))
				if (unit.typeNum == GenerationUnit::Worker)
					++workers;
			const int startingType = g.startingBuildingType().typeNum;
			for (const GenerationBuilding &building : g.teamBuildings(i))
				if (building.typeNum == startingType)
					++swarms;
			const int expectedWorkers = r.nbWorkers * (d.workersPerStartingSwarm ? swarms : 1);
			if (workers != expectedWorkers || !swarms)
				return "Incomplete starting colony";
		}
	return {};
}
