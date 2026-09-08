// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
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
	Uint16 *gradient = forbiddenGradient[teamNumber][swimClass];
	assert(gradient);
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	bool canSwim = swimClass > 0;

	// Seed: free cells are goals, forbidden interiors are placeholders (promoted
	// to GRADIENT_FORBIDDEN_BORDER in the second pass if they border a free cell),
	// all other blockers (resources, buildings, water, immobileUnits) are obstacles.
	for (size_t i=0; i<size; i++)
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
	for (int c=0; c<SWIM_CLASS_COUNT; c++)
		if (forbiddenGradient[teamNumber][c])
			updateForbiddenGradient(teamNumber, c);
}

void Map::updateForbiddenGradient()
{
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateForbiddenGradient(i);
}


void Map::updateGuardAreasGradient(int teamNumber, int swimClass)
{
	Uint16 *gradient = guardAreasGradient[teamNumber][swimClass];
	assert(gradient);
	bool canSwim = swimClass > 0;

	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
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

	propagateGradient(gradient, swimClass);
}

void Map::updateGuardAreasGradient(int teamNumber)
{
	for (int c=0; c<SWIM_CLASS_COUNT; c++)
		if (guardAreasGradient[teamNumber][c])
			updateGuardAreasGradient(teamNumber, c);
}

void Map::updateGuardAreasGradient()
{
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateGuardAreasGradient(i);
}


void Map::updateClearAreasGradient(int teamNumber, int swimClass)
{
	Uint16 *gradient = clearAreasGradient[teamNumber][swimClass];
	assert(gradient);
	bool canSwim = swimClass > 0;

	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
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

	propagateGradient(gradient, swimClass);
}

void Map::updateClearAreasGradient(int teamNumber)
{
	for (int c=0; c<SWIM_CLASS_COUNT; c++)
		if (clearAreasGradient[teamNumber][c])
			updateClearAreasGradient(teamNumber, c);
}

void Map::updateClearAreasGradient()
{
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateClearAreasGradient(i);
}
