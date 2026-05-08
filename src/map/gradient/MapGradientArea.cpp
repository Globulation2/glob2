// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Unit.h"
#include "MapInternal.h"

#include <algorithm>
#include <valarray>
#include <Stream.h>
#include <queue>


// Forbidden / Guard area / Clear area gradients

void Map::updateForbiddenGradient(int teamNumber, bool canSwim)
{
	Uint8 *gradient = forbiddenGradient[teamNumber][canSwim];
	assert(gradient);
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);

	// We set the obstacle and free places
	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.ressource.type!=NO_RES_TYPE)
			gradient[i] = 0;
		else if (c.building!=NOGBID)
			gradient[i] = 0;
		else if (!canSwim && isWater(i))
			gradient[i] = 0;
		else if(immobileUnits[i] != 255)
			gradient[i]=0;
		else if (c.forbidden&teamMask)
			gradient[i] = 1;  // forbidden interior; bumped to 254 below if it borders a free cell
		else
			gradient[i] = 255;
	}

	// Forbidden cells bordering free cells become 254 sources so the gradient
	// fades outward into the forbidden zone.
	for (size_t i=0; i<size; i++)
	{
		if (gradient[i] != 1)
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
			if (gradient[deltaAddrC[ci]] == 255)
			{
				gradient[i] = 254;
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
			gradient[i] = 0;
		else if(immobileUnits[i] != 255)
			gradient[i]=0;
		else if (c.ressource.type != NO_RES_TYPE)
			gradient[i] = 0;
		else if (c.building != NOGBID && (1<<Building::GIDtoTeam(c.building)) & (game->teams[teamNumber]->allies))
			gradient[i] = 0;
		else if (!canSwim && isWater(i))
			gradient[i] = 0;
		else if (c.guardArea & teamMask)
			gradient[i] = 255;
		else
			gradient[i] = 1;
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
			gradient[i] = 0;
		else if(c.clearArea & teamMask && c.ressource.type != NO_RES_TYPE && globalContainer->ressourcesTypes.get(c.ressource.type)->clearable)
			gradient[i] = 255;
		else if(immobileUnits[i] != 255)
			gradient[i]=0;
		else if (c.ressource.type != NO_RES_TYPE)
			gradient[i] = 0;
		else if (c.building != NOGBID)
			gradient[i] = 0;
		else if (!canSwim && isWater(i))
			gradient[i] = 0;
		else
			gradient[i] = 1;
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


