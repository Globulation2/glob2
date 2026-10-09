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

#include <algorithm>
#include <array>
#include "SeedCells.h"
#include <atomic>



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
	std::array<Uint8, ResourceRegistry::Capacity> blocksGround;
	const auto& properties=resourceRegistry().propertyTable();
	for(size_t id=0;id<properties.size();++id) blocksGround[id]=properties[id].blocksGround;

	// Seed: free cells are goals, forbidden interiors are placeholders (promoted
	// to GRADIENT_FORBIDDEN_BORDER in the second pass if they border a free cell),
	// all other blockers (resources, buildings, water, immobileUnits) are obstacles.
	initializeGradientCells([&](size_t begin, size_t end) {
	for (size_t i=begin; i<end; i++)
	{
		if (resourceCells[i].resource.type!=NO_RES_TYPE && blocksGround[resourceCells[i].resource.type])
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (occupancyCells[i].building!=NOGBID)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (!terrainPropertiesAt(i).walkable && !(canSwim && terrainPropertiesAt(i).swimmable))
			gradient[i] = GRADIENT_FORBIDDEN;
		else if(occupancyCells[i].immobileUnit != IMMOBILE_UNIT_NONE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (areaCells[i].forbidden&teamMask)
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

	{
		PERF_SCOPE_TIME(PropagationArea);
		propagateGradient(gradient, swimClass);
	}
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


// Guard-area balancing. Warrior crowding for the guard gradient: every tile
// ends up holding the number of the team's warriors within GUARD_CROWD_RADIUS
// tiles of it. Warrior positions are splatted onto a map-sized grid and
// box-summed. Warriors inside buildings are not counted; they are not guarding
// anything.
bool Map::computeWarriorCrowding(int teamNumber, Uint16 *out) const
{
	// Gather positions first: a team without warriors costs one pass over its
	// unit slots and nothing over the map.
	auto &crowdPositions = gradientRuntime->workspaces[compute.slot()].crowding.positions;
	crowdPositions.clear();
	const Team *team = game->teams[teamNumber];
	for (int i = 0; i < Unit::MAX_COUNT; i++)
	{
		const Unit *u = team->myUnits[i];
		if (!u || u->isDead || u->typeNum != WARRIOR || u->displacement == Unit::DIS_INSIDE)
			continue;
		crowdPositions.push_back(coordToIndex(u->posX, u->posY));
	}
	if (crowdPositions.empty())
		return false;
	std::fill(out, out + size, 0);
	for (size_t i : crowdPositions)
		out[i]++;
	boxSumInPlace(out);
	return true;
}

// Box sum over a (2r+1)-square window, in place. A box filter is separable: sum
// each row over the window, then sum those row sums down each column, and every
// cell holds the sum of the whole square. Each pass is a sliding window that
// adds the cell entering and subtracts the one leaving, so the cost is a few
// operations per cell whatever the radius. Both passes walk memory in row
// order; the column pass carries one running sum per column across the rows
// rather than striding down each column. The map is a torus and its sides are
// powers of two, so the window wraps by masking the index, with no edge cases.
void Map::boxSumInPlace(Uint16 *grid) const
{
	gradient_preparation::boxSum(grid, w, h, gradientRuntime->workspaces[compute.slot()].crowding);
}

// Guard-area balancing: painted tiles are the seeds, but not at cost zero. Each
// carries a crowding cost, warriors nearby per painted tile nearby
// (MapInternal.h), so Dijkstra gives every tile the best of "distance to an
// area plus that area's crowding". Two areas then settle where their crowding
// difference matches the walking distance between them, a larger painted area
// costs less per warrior and so takes more of them, and an area that is
// over-full has another area's field running over its painted tiles, which is
// what lets its warriors find the way out (Map::pathfindArea).
void Map::seedGuardAreaCrowding(int teamNumber, Uint16 *gradient) const
{
	// Seeds are the cells the plain seeding left at the goal value: painted and
	// not blocked. A team with no warriors keeps its painted tiles at cost zero.
	auto &scratch = gradientRuntime->workspaces[compute.slot()].crowding;
	auto &guardSeeds = scratch.seeds;
	auto &crowdCells = scratch.warriors;
	auto &paintCells = scratch.paint;
	guardSeeds.clear();
	for (size_t i = 0; i < size; i++)
		if (gradient[i] == GRADIENT_AT_GOAL)
			guardSeeds.push_back(i);
	if (guardSeeds.empty())
		return;
	crowdCells.resize(size);
	if (!computeWarriorCrowding(teamNumber, crowdCells.data()))
		return;
	paintCells.assign(size, 0);
	for (size_t i : guardSeeds)
		paintCells[i] = 1;
	boxSumInPlace(paintCells.data());
	for (size_t i : guardSeeds)
	{
		// Warriors per painted tile in the window, scaled so that a 25-tile
		// area pays the full per-warrior cost. paintCells[i] is at least 1: the
		// seed itself is painted.
		const int cost = (GUARD_CROWD_COST_PER_WARRIOR * crowdCells[i] * GUARD_CROWD_REFERENCE_AREA) / std::max<int>(1, paintCells[i]);
		gradient[i] = (Uint16)(GRADIENT_AT_GOAL - std::min(GUARD_CROWD_COST_MAX, cost));
	}
}

void Map::updateGuardAreasGradient(int teamNumber, int swimClass)
{
	PERF_SCOPE_TIME(AreaGradient);
	gradientRuntime->pipeline.invalidate(&guardAreasGradient[teamNumber][swimClass]);
	Uint16 *gradient = guardAreasGradient[teamNumber][swimClass];
	seedGuardAreasGradient(teamNumber, swimClass, gradient);
	{
		PERF_SCOPE_TIME(PropagationArea);
		propagateGradient(gradient, swimClass);
	}
}

void Map::seedGuardAreasGradient(int teamNumber, int swimClass, Uint16 *gradient)
{
    const bool painted=gradient_preparation::guardCells(liveCells, game->teams[teamNumber]->allies, teamNumber, swimClass, gradient,
        [this](auto fn) { initializeGradientCells(fn); });
    if (painted && game->gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing)) seedGuardAreaCrowding(teamNumber, gradient);
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
	{
		PERF_SCOPE_TIME(PropagationArea);
		propagateGradient(gradient, swimClass);
	}
}

void Map::seedClearAreasGradient(int teamNumber, int swimClass, Uint16 *gradient)
{
    gradient_preparation::clearCells(liveCells, farmAreasEnabled(), teamNumber, swimClass, gradient,
        [this](auto fn) { initializeGradientCells(fn); });
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

void Map::updateTeamAreaGradients(int teamNumber)
{
	updateForbiddenGradient(teamNumber);
	updateGuardAreasGradient(teamNumber);
	updateClearAreasGradient(teamNumber);
}
