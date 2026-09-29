// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/GradientRuntime.h"
#include "Game.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "MapInternal.h"



// Forbidden / Guard area / Clear area gradients

Uint16 *Map::getForbiddenGradient(int teamNumber, int swimClass)
{
	Uint16 *&gradient = forbiddenGradient[teamNumber][swimClass];
	if (gradient == NULL)
	{
		gradient = new Uint16[size];
		updateForbiddenGradient(teamNumber, swimClass);
	}
	return gradient;
}

Uint16 *Map::getGuardAreasGradient(int teamNumber, int swimClass)
{
	Uint16 *&gradient = guardAreasGradient[teamNumber][swimClass];
	if (gradient == NULL)
	{
		gradient = new Uint16[size];
		updateGuardAreasGradient(teamNumber, swimClass);
	}
	return gradient;
}

Uint16 *Map::getClearAreasGradient(int teamNumber, int swimClass)
{
	Uint16 *&gradient = clearAreasGradient[teamNumber][swimClass];
	if (gradient == NULL)
	{
		gradient = new Uint16[size];
		updateClearAreasGradient(teamNumber, swimClass);
	}
	return gradient;
}

void Map::updateForbiddenGradient(int teamNumber, int swimClass)
{
	PERF_SCOPE_TIME(AreaGradient);
	Uint16 *gradient = forbiddenGradient[teamNumber][swimClass];
	assert(gradient);
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	bool canSwim = swimClass > 0;

	// Seed: free cells are goals, forbidden interiors are placeholders (promoted
	// to GRADIENT_FORBIDDEN_BORDER in the second pass if they border a free cell),
	// all other blockers (resources, buildings, water, immobileUnits) are obstacles.
	initializeGradientCells([&](size_t begin, size_t end) {
	for (size_t i=begin; i<end; i++)
	{
		const Tile& c=tiles[i];
		if (c.resource.type!=NO_RES_TYPE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.building!=NOGBID)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (!canSwim && isWater(i))
			gradient[i] = GRADIENT_FORBIDDEN;
		else if(immobileUnits[i] != IMMOBILE_UNIT_NONE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.forbidden&teamMask)
			gradient[i] = GRADIENT_UNREACHABLE;
		else
			gradient[i] = GRADIENT_AT_GOAL;
	}
	});

	// Forbidden cells bordering free cells become GRADIENT_FORBIDDEN_BORDER sources
	// so the gradient fades outward into the forbidden zone.
	for (size_t i=0; i<size; i++)
	{
		if (gradient[i] != GRADIENT_UNREACHABLE)
			continue;
		size_t y = i >> wDec;
		size_t x = i & wMask;
		for (int d=0; d<8; d++)
		{
			size_t n = (((y + tabClose[d][1]) & hMask) << wDec) | ((x + tabClose[d][0]) & wMask);
			if (gradient[n] == GRADIENT_AT_GOAL)
			{
				gradient[i] = GRADIENT_FORBIDDEN_BORDER;
				break;
			}
		}
	}

	propagateGradient(gradient, swimClass);
}

void Map::updateForbiddenGradient(int teamNumber)
{
	PERF_SCOPE_TIME(AreaGradient);
	for (int c=0; c<SWIM_CLASS_COUNT; c++)
		if (forbiddenGradient[teamNumber][c])
			updateForbiddenGradient(teamNumber, c);
}

void Map::updateForbiddenGradient()
{
	PERF_SCOPE_TIME(AreaGradient);
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateForbiddenGradient(i);
}


void Map::updateGuardAreasGradient(int teamNumber, int swimClass)
{
	PERF_SCOPE_TIME(AreaGradient);
	gradientRuntime->pipeline.invalidate(&guardAreasGradient[teamNumber][swimClass]);
	Uint16 *gradient = guardAreasGradient[teamNumber][swimClass];
	seedGuardAreasGradient(teamNumber, swimClass, gradient);
	propagateGradient(gradient, swimClass);
}

void Map::seedGuardAreasGradient(int teamNumber, int swimClass, Uint16 *gradient)
{
	assert(gradient);
	bool canSwim = swimClass > 0;

	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	initializeGradientCells([&](size_t begin, size_t end) {
	for (size_t i=begin; i<end; i++)
	{
		const Tile& c=tiles[i];
		if (c.forbidden & teamMask)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if(immobileUnits[i] != IMMOBILE_UNIT_NONE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.resource.type != NO_RES_TYPE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.building != NOGBID && (1<<Building::GIDtoTeam(c.building)) & (game->teams[teamNumber]->allies))
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (!canSwim && isWater(i))
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.guardArea & teamMask)
			gradient[i] = GRADIENT_AT_GOAL;
		else
			gradient[i] = GRADIENT_UNREACHABLE;
	}
	});

}

void Map::updateGuardAreasGradient(int teamNumber)
{
	PERF_SCOPE_TIME(AreaGradient);
	for (int c=0; c<SWIM_CLASS_COUNT; c++)
		if (guardAreasGradient[teamNumber][c])
			updateGuardAreasGradient(teamNumber, c);
}

void Map::updateGuardAreasGradient()
{
	PERF_SCOPE_TIME(AreaGradient);
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateGuardAreasGradient(i);
}


void Map::updateClearAreasGradient(int teamNumber, int swimClass)
{
	PERF_SCOPE_TIME(AreaGradient);
	gradientRuntime->pipeline.invalidate(&clearAreasGradient[teamNumber][swimClass]);
	Uint16 *gradient = clearAreasGradient[teamNumber][swimClass];
	seedClearAreasGradient(teamNumber, swimClass, gradient);
	propagateGradient(gradient, swimClass);
}

void Map::seedClearAreasGradient(int teamNumber, int swimClass, Uint16 *gradient)
{
	assert(gradient);
	bool canSwim = swimClass > 0;

	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	initializeGradientCells([&](size_t begin, size_t end) {
	for (size_t i=begin; i<end; i++)
	{
		const Tile& c=tiles[i];
		if (c.forbidden & teamMask)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if(c.clearArea & teamMask && c.resource.type != NO_RES_TYPE && globalContainer->resourcesTypes.get(c.resource.type)->clearable)
			gradient[i] = GRADIENT_AT_GOAL;
		else if(immobileUnits[i] != IMMOBILE_UNIT_NONE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.resource.type != NO_RES_TYPE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.building != NOGBID)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (!canSwim && isWater(i))
			gradient[i] = GRADIENT_FORBIDDEN;
		else
			gradient[i] = GRADIENT_UNREACHABLE;
	}
	});

}

void Map::updateClearAreasGradient(int teamNumber)
{
	PERF_SCOPE_TIME(AreaGradient);
	for (int c=0; c<SWIM_CLASS_COUNT; c++)
		if (clearAreasGradient[teamNumber][c])
			updateClearAreasGradient(teamNumber, c);
}

void Map::updateClearAreasGradient()
{
	PERF_SCOPE_TIME(AreaGradient);
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateClearAreasGradient(i);
}

// These refreshes already share a simulation boundary. Only allocated fields
// participate; no worker changes cache ownership or refresh scheduling.
void Map::updateTeamAreaGradients(int teamNumber)
{
	if (!computeEnabled(ComputeAreas))
	{
		updateForbiddenGradient(teamNumber);
		updateGuardAreasGradient(teamNumber);
		updateClearAreasGradient(teamNumber);
		return;
	}
	std::vector<std::pair<int, int>> jobs;
	for (int kind = 0; kind < 3; ++kind)
		for (int swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
		{
			const auto field = kind == 0 ? forbiddenGradient[teamNumber][swim]
				: kind == 1 ? guardAreasGradient[teamNumber][swim] : clearAreasGradient[teamNumber][swim];
			if (field) jobs.emplace_back(kind, swim);
		}
	computeExecutor().run(jobs.size(), [&](size_t i) {
		const auto [kind, swim] = jobs[i];
		if (kind == 0) updateForbiddenGradient(teamNumber, swim);
		else if (kind == 1) updateGuardAreasGradient(teamNumber, swim);
		else updateClearAreasGradient(teamNumber, swim);
	});
}
