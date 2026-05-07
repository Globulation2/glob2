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

// Auto-seed wrapper for caller-supplied gradient buffers (Castor/Warrush AIs).
// Just runs the chamfer pass with GT_UNDEFINED — chamfer reads sources from
// the buffer directly (cells with value >= 3), so no listedAddr is needed.
void Map::updateGlobalGradient(Uint8 *gradient)
{
	updateGlobalGradient<Uint32>(gradient, GT_UNDEFINED, true);
}


void Map::updateRessourcesGradient(int teamNumber, Uint8 ressourceType, bool canSwim)
{
	Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
	assert(gradient);

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
				gradient[i]=255;
		}
		else
			gradient[i]=0;
	}

	updateGlobalGradient<Uint32>(gradient, GT_RESOURCE, canSwim);
}

