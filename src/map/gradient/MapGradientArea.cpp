// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "MapInternal.h"



// Forbidden / Guard area / Clear area gradients

void Map::updateForbiddenGradient(int teamNumber, bool canSwim)
{
	Uint8 *gradient = forbiddenGradient[teamNumber][canSwim];
	assert(gradient);
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);

	// Seed: free cells are sources (255), forbidden interiors are placeholder 1
	// (promoted to 254 in the second pass if they border a free cell), all other
	// blockers (resources, buildings, water, immobileUnits) are obstacles.
	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.ressource.type!=NO_RES_TYPE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.building!=NOGBID)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (!canSwim && isWater(i))
			gradient[i] = GRADIENT_FORBIDDEN;
		else if(immobileUnits[i] != 255)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.forbidden&teamMask)
			gradient[i] = GRADIENT_UNREACHABLE;  // promoted to GRADIENT_FORBIDDEN_BORDER below if it borders a free cell
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
		size_t yu = ((y - 1) & hMask);
		size_t yd = ((y + 1) & hMask);
		size_t xl = ((x - 1) & wMask);
		size_t xr = ((x + 1) & wMask);
		size_t deltaAddrC[8] = {
			(yu << wDec) | xl,
			(yu << wDec) | x ,
			(yu << wDec) | xr,
			(y  << wDec) | xr,
			(yd << wDec) | xr,
			(yd << wDec) | x ,
			(yd << wDec) | xl,
			(y  << wDec) | xl,
		};
		for (int ci=0; ci<8; ci++)
		{
			if (gradient[deltaAddrC[ci]] == GRADIENT_AT_GOAL)
			{
				gradient[i] = GRADIENT_FORBIDDEN_BORDER;
				break;
			}
		}
	}

	updateGlobalGradient(gradient);
}

void Map::updateForbiddenGradient(int teamNumber)
{
	for (int i=0; i<2; i++)
		updateForbiddenGradient(teamNumber, i);
}

void Map::updateForbiddenGradient()
{
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateForbiddenGradient(i);
}


void Map::updateGuardAreasGradient(int teamNumber, bool canSwim)
{
	Uint8 *gradient = guardAreasGradient[teamNumber][canSwim];
	assert(gradient);

	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.forbidden & teamMask)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if(immobileUnits[i] != 255)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.ressource.type != NO_RES_TYPE)
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

	updateGlobalGradient(gradient);
}

void Map::updateGuardAreasGradient(int teamNumber)
{
	for (int i=0; i<2; i++)
		updateGuardAreasGradient(teamNumber, i);
}

void Map::updateGuardAreasGradient()
{
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateGuardAreasGradient(i);
}


void Map::updateClearAreasGradient(int teamNumber, bool canSwim)
{
	Uint8 *gradient = clearAreasGradient[teamNumber][canSwim];
	assert(gradient);

	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.forbidden & teamMask)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if(c.clearArea & teamMask && c.ressource.type != NO_RES_TYPE && globalContainer->ressourcesTypes.get(c.ressource.type)->clearable)
			gradient[i] = GRADIENT_AT_GOAL;
		else if(immobileUnits[i] != 255)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.ressource.type != NO_RES_TYPE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.building != NOGBID)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (!canSwim && isWater(i))
			gradient[i] = GRADIENT_FORBIDDEN;
		else
			gradient[i] = GRADIENT_UNREACHABLE;
	}

	updateGlobalGradient(gradient);
}

void Map::updateClearAreasGradient(int teamNumber)
{
	for (int i=0; i<2; i++)
		updateClearAreasGradient(teamNumber, i);
}

void Map::updateClearAreasGradient()
{
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateClearAreasGradient(i);
}


