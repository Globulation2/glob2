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

// updateGlobalGradientSlow + dispatcher (slow O(n^2) reference path)
void Map::updateGlobalGradientSlow(Uint8 *gradient)
{
	if (size <= 65536)
		updateGlobalGradientSlow<Uint16>(gradient);
	else
		updateGlobalGradientSlow<Uint32>(gradient);
}

template<typename Tint> void Map::updateGlobalGradientSlow(Uint8 *gradient)
{
	Tint *listedAddr = new Tint[size];
	size_t listCountWrite = 0;
	// make the first list:
	for (size_t i = 0; i < size; i++)
		if (gradient[i] >= 3)
			listedAddr[listCountWrite++] = i;
	updateGlobalGradient(gradient, listedAddr, listCountWrite, GT_UNDEFINED, true);
	delete[] listedAddr;
}


// updateRessourcesGradient + dispatcher
void Map::updateRessourcesGradient(int teamNumber, Uint8 ressourceType, bool canSwim)
{
	if (size <= 65536)
		updateRessourcesGradient<Uint16>(teamNumber, ressourceType, canSwim);
	else
		updateRessourcesGradient<Uint32>(teamNumber, ressourceType, canSwim);
}

template<typename Tint> void Map::updateRessourcesGradient(int teamNumber, Uint8 ressourceType, bool canSwim)
{
	Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
	assert(gradient);
	Tint *listedAddr = new Tint[size];
	size_t listCountWrite = 0;
	
	Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	assert(globalContainer);
	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.forbidden & teamMask)
			gradient[i]=0;
		else if(immobileUnits[i] != 255)
			gradient[i]=0;
		else if (c.ressource.type==NO_RES_TYPE)
		{
			if (c.building!=NOGBID)
				gradient[i]=0;
			else if (!canSwim && (c.terrain>=256 && c.terrain<16+256)) //!canSwim && isWater
				gradient[i]=0;
			else
				gradient[i]=1;
		}
		else if (c.ressource.type==ressourceType)
		{
			if (globalContainer->ressourcesTypes.get(ressourceType)->visibleToBeCollected && !(fogOfWar[i]&teamMask))
				gradient[i]=0;
			else
			{
				gradient[i]=255;
				listedAddr[listCountWrite++] = i;
			}
		}
		else
			gradient[i]=0;
	}
	
	updateGlobalGradient(gradient, (Tint *)listedAddr, listCountWrite, GT_RESOURCE, canSwim);
	delete[] listedAddr;
}

