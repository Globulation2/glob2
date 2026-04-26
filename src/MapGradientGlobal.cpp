/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/


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

