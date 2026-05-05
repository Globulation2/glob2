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


#include "MapGradientImpl.h"

// Forbidden / Guard area / Clear area gradients

void Map::updateForbiddenGradient(int teamNumber, bool canSwim)
{
	if (size <= 65536)
		updateForbiddenGradient<Uint16>(teamNumber, canSwim);
	else
		updateForbiddenGradient<Uint32>(teamNumber, canSwim);
}

template<typename Tint> void Map::updateForbiddenGradient(int teamNumber, bool canSwim)
{
#define SIMONS_FORBIDDEN_GRADIENT_INIT

#if defined(TEST_FORBIDDEN_GRADIENT_INIT)
 #define SIMONS_FORBIDDEN_GRADIENT_INIT
 #define SIMPLE_FORBIDDEN_GRADIENT_INIT
#endif

	Tint *listedAddr = new Tint[size];
	size_t listCountWrite=0;
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);

#ifdef SIMON2_FORBIDDEN_GRADIENT_INIT
	Uint8 *gradient = forbiddenGradient[teamNumber][canSwim];
	assert(gradient);
	for (size_t i = 0; i < size; i++)
	{
		const Case& c = cases[i];
		if ((c.ressource.type != NO_RES_TYPE) || (c.building!=NOGBID) || (!canSwim && isWater(i)))
		{
			gradient[i] = 0;
		}
		else if ((c.forbidden) & teamMask)
		{
			// we compute the 8 addresses around i:
			// (a stands for address, u for up, d for down, l for left, r for right, m for middle)
			size_t aul = (i - 1 - w) & (size - 1);
			size_t aum = (i     - w) & (size - 1);
			size_t aur = (i + 1 - w) & (size - 1);
			size_t amr = (i + 1    ) & (size - 1);
			size_t adr = (i + 1 + w) & (size - 1);
			size_t adm = (i     + w) & (size - 1);
			size_t adl = (i - 1 + w) & (size - 1);
			size_t aml = (i - 1    ) & (size - 1);
			
			if( ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[aul].forbidden) &teamMask)
				|| (cases[aul].building!=NOGBID) || (!canSwim && isWater(aul))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[aum].forbidden) &teamMask)
			    || (cases[aum].building!=NOGBID) || (!canSwim && isWater(aum))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[aur].forbidden) &teamMask)
			    || (cases[aur].building!=NOGBID) || (!canSwim && isWater(aur))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[amr].forbidden) &teamMask)
			    || (cases[amr].building!=NOGBID) || (!canSwim && isWater(amr))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[adr].forbidden) &teamMask)
			    || (cases[adr].building!=NOGBID) || (!canSwim && isWater(adr))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[adm].forbidden) &teamMask)
			    || (cases[adm].building!=NOGBID) || (!canSwim && isWater(adm))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[adl].forbidden) &teamMask)
			    || (cases[adl].building!=NOGBID) || (!canSwim && isWater(adl))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[aml].forbidden) &teamMask)
			    || (cases[aml].building!=NOGBID) || (!canSwim && isWater(aml))) )
			{
				gradient[i]= 1;
			}
			else
			{
				gradient[i]=254;
				listedAddr[listCountWrite++] = i;
			}
		}
		else
		{
			gradient[i] = 255;
		}
	}
	// Then we propagate the gradient
	updateGlobalGradient(gradient, listedAddr, listCountWrite, GT_FORBIDDEN, canSwim);
#endif

#if defined(SIMONS_FORBIDDEN_GRADIENT_INIT)
	Uint8 *testgradient = forbiddenGradient[teamNumber][canSwim];
	assert(testgradient);
	size_t listCountWriteInit = 0;
	
	// We set the obstacle and free places
	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.ressource.type!=NO_RES_TYPE)
			testgradient[i] = 0;
		else if (c.building!=NOGBID)
			testgradient[i] = 0;
		else if (!canSwim && isWater(i))
			testgradient[i] = 0;
		else if(immobileUnits[i] != 255)
			testgradient[i]=0;
		else if (c.forbidden&teamMask)
		{
			testgradient[i]= 1;  // Later: check if we can set it to 254.
			listedAddr[listCountWriteInit++] = i;  // Remember this field.
		}
		else
			testgradient[i] = 255;
	}

	// Now check if the forbidden fields border free fields. 
	// If a field does, its gradient must be 254 and it can be used as a source
	// for the forbidden gradient.
	for (size_t listCountReadInit=0; listCountReadInit<listCountWriteInit; listCountReadInit++)
	{
		size_t i = listedAddr[listCountReadInit];
		size_t y = i >> wDec;               // Calculate the coordinates of
		size_t x = i & wMask;               // the current field and of the
		
		size_t yu = ((y - 1) & hMask);      // fields next to it.
		size_t yd = ((y + 1) & hMask);
		size_t xl = ((x - 1) & wMask);
		size_t xr = ((x + 1) & wMask);

		size_t deltaAddrC[8];
		deltaAddrC[0] = (yu << wDec) | xl;
		deltaAddrC[1] = (yu << wDec) | x ;
		deltaAddrC[2] = (yu << wDec) | xr;
		deltaAddrC[3] = (y  << wDec) | xr;
		deltaAddrC[4] = (yd << wDec) | xr;
		deltaAddrC[5] = (yd << wDec) | x ;
		deltaAddrC[6] = (yd << wDec) | xl;
		deltaAddrC[7] = (y  << wDec) | xl;
		for( int ci=0; ci<8; ci++)
		{
			if( testgradient[ deltaAddrC[ci] ] == 255 )
			{
				testgradient[i] = 254;
				listedAddr[listCountWrite++] = i;
				break;
			}
		}
	}
	
	// Then we propagate the gradient
	updateGlobalGradient(testgradient, listedAddr, listCountWrite, GT_FORBIDDEN, canSwim);
#endif	

#if defined(SIMPLE_FORBIDDEN_GRADIENT_INIT)
	listCountWrite = 0;
 #if defined(TEST_FORBIDDEN_GRADIENT_INIT)
	Uint8 *gradient = new Uint8[size];
 #else
	Uint8 *gradient = forbiddenGradient[teamNumber][canSwim];
	assert(gradient);
 #endif

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
			gradient[i]= 1;
		else
		{
			listedAddr[listCountWrite++] = i;
			gradient[i] = 255;
		}
	}
	updateGlobalGradient(gradient, listedAddr, listCountWrite, GT_FORBIDDEN, canSwim);
#endif
	delete[] listedAddr;
#if defined(TEST_FORBIDDEN_GRADIENT_INIT)
	assert (memcmp (testgradient, gradient, size) == 0);
#endif

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
	if (size <= 65536)
		updateGuardAreasGradient<Uint16>(teamNumber, canSwim);
	else
		updateGuardAreasGradient<Uint32>(teamNumber, canSwim);
}

template<typename Tint> void Map::updateGuardAreasGradient(int teamNumber, bool canSwim)
{
	Uint8 *gradient = guardAreasGradient[teamNumber][canSwim];
	assert(gradient);
	Tint *listedAddr = new Tint[size];
	size_t listCountWrite = 0;
	
	// We set the obstacle and free places
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
		{
			gradient[i] = 255;
			listedAddr[listCountWrite++] = i;
		}
		else
			gradient[i] = 1;
	}
	
	// Then we propagate the gradient
	updateGlobalGradient(gradient, listedAddr, listCountWrite, GT_GUARD_AREA, canSwim);
	delete[] listedAddr;
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
	if (size <= 65536)
		updateClearAreasGradient<Uint16>(teamNumber, canSwim);
	else
		updateClearAreasGradient<Uint32>(teamNumber, canSwim);
}

template<typename Tint> void Map::updateClearAreasGradient(int teamNumber, bool canSwim)
{
	Uint8 *gradient = clearAreasGradient[teamNumber][canSwim];
	assert(gradient);
	Tint *listedAddr = new Tint[size];
	size_t listCountWrite = 0;
	
	// We set the obstacle and free places
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.forbidden & teamMask)
			gradient[i] = 0;
		else if(c.clearArea & teamMask && c.ressource.type != NO_RES_TYPE && globalContainer->ressourcesTypes.get(c.ressource.type)->clearable)
		{
			gradient[i] = 255;
			listedAddr[listCountWrite++] = i;
		}
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
	
	// Then we propagate the gradient
	updateGlobalGradient(gradient, listedAddr, listCountWrite, GT_CLEAR_AREA, canSwim);
	delete[] listedAddr;
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


