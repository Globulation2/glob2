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


// Ressource pathfinding for units (pathfindRessource, pathfindLocalRessource, pathfindRandom)

bool Map::pathfindRessource(int teamNumber, Uint8 ressourceType, bool canSwim, int x, int y, int *dx, int *dy, bool *stopWork, bool verbose)
{
	pathToRessourceCountTot++;
	if (verbose)
		printf("pathfindingRessource...\n");
	assert(ressourceType<MAX_RESSOURCES);
	const Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
	assert(gradient);
	Uint8 max=gradient[x+y*w];
	Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	if (max==0)
	{
		if (verbose)
			printf("...pathfindedRessource pathfindForbidden() v1\n");
		pathToRessourceCountFailure++;
		*stopWork=true;
		return pathfindForbidden(gradient, teamNumber, canSwim, x, y, dx, dy, verbose);
	}
	if (max<2)
	{
		if (verbose)
			printf("...pathfindedRessource failure v2\n");
		pathToRessourceCountFailure++;
		*stopWork=true;
		return false;
	}
	
	if (directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient, true, verbose))
	{
		pathToRessourceCountSuccess++;
		if (verbose)
			printf("...pathfindedRessource success v3\n");
		return true;
	}
	else
	{
		pathToRessourceCountFailure++;
		if (verbose)
			printf("...pathfindedRessource failure locked v4\n");
		//printf("locked at (%d, %d) for r=%d, max=%d\n", x, y, ressourceType, max);
		fprintf(logFile, "locked at (%d, %d) for r=%d, max=%d\n", x, y, ressourceType, max);
		*stopWork=false;
		return false;
	}
}


#ifndef YOG_SERVER_ONLY
void Map::pathfindRandom(Unit *unit, bool verbose)
{
	if (verbose)
		printf("pathfindRandom()\n");
	int x=unit->posX;
	int y=unit->posY;
	if ((cases[x+(y<<wDec)].forbidden)&unit->owner->me)
	{
		if (verbose)
			printf(" forbidden\n");
		if (pathfindForbidden(NULL, unit->owner->teamNumber, (unit->performance[SWIM]>0), x, y, &unit->dx, &unit->dy, verbose))
		{
			if (verbose)
				printf(" success\n");
			unit->directionFromDxDy();
		}
		else
		{
			if (verbose)
				printf(" failed\n");
			unit->dx=0;
			unit->dy=0;
			unit->direction=8;
		}
	}
	else
	{
		bool da[8];
		int count=0;
		for (int di=0; di<8; di++)
		{
			int tx=(x+tabClose[di][0])&wMask;
			int ty=(y+tabClose[di][1])&hMask;
			if (isFreeForGroundUnit(tx, ty, (unit->performance[SWIM]>0), unit->owner->me))
			{
				da[di]=true;
				count++;
			}
			else
				da[di]=false;
		}
		if (verbose)
		{
			printf("count=%d\n", count);
			for (int di=0; di<8; di++)
				printf("da[%d]=%d\n", di, da[di]);
		}
		if (count==0)
		{
			unit->dx=0;
			unit->dy=0;
			unit->direction=8;
			return;
		}
		int dir=syncRand()%count;
		if (verbose)
			printf(" dir=%d\n", dir);
		for (int di=0; di<8; di++)
			if (da[di] && dir--==0)
			{
				unit->dx=tabClose[di][0];
				unit->dy=tabClose[di][1];
				unit->direction=di;
				if (verbose)
					printf("d=(%d, %d), d=%d\n", unit->dx, unit->dy, unit->direction);
				return;
			}
		assert(false);
	}
}
#endif  // !YOG_SERVER_ONLY

bool Map::pathfindLocalRessource(Building *building, bool canSwim, int x, int y, int *dx, int *dy)
{
	pathfindLocalRessourceCount++;
	assert(building);
	assert(building->type);
	assert(building->type->isVirtual);
	//printf("pathfindingLocalRessource[%d] (gbid=%d)...\n", canSwim, building->gid);
	
	int bx=building->posX;
	int by=building->posY;
	Uint32 teamMask=building->owner->me;
	
	Uint8 *gradient=building->localRessources[canSwim];
	if (gradient==NULL)
	{
		if (!updateLocalRessources(building, canSwim))
			return false;
		gradient=building->localRessources[canSwim];
	}
	assert(gradient);
	//HACK: I have no idea what is going on or why isInLocalGradient(x, y, bx, by) was asserted and why isInLocalGradient(x, y, bx, by) checks for the rectangle it is checking for, but this fixes a rare crash.
	if(!isInLocalGradient(x, y, bx, by))
		return false;
//	assert(isInLocalGradient(x, y, bx, by));
	
	int lx=(x-bx+15+32)&31;
	int ly=(y-by+15+32)&31;
	int max=0;
	Uint8 currentg=gradient[lx+(ly<<5)];
	bool found=false;
	bool gradientUsable=false;
	
	if (currentg==1 && (building->localRessourcesCleanTime[canSwim]+=16)<128)
	{
		// This mean there are still ressources, but they are unreachable.
		// We wait 5[s] before recomputing anything.
		if (verbose)
			printf("...pathfindedLocalRessource v0 failure waiting\n");
		pathfindLocalRessourceCountWait++;
		return false;
	}
	
	if (currentg>1 && currentg!=255)
	{
		for (int sd=0; sd<=1; sd++)
			for (int d=sd; d<8; d+=2)
			{
				int ddx, ddy;
				Unit::dxDyFromDirection(d, &ddx, &ddy);
				int lxddx=clip_0_31(lx+ddx);
				int lyddy=clip_0_31(ly+ddy);
				Uint8 g=gradient[lxddx+(lyddy<<5)];
				if (!gradientUsable && g>currentg && isHardSpaceForGroundUnit(x+ddx, y+ddy, canSwim, teamMask))
					gradientUsable=true;
				if (g>=max && isFreeForGroundUnit(x+ddx, y+ddy, canSwim, teamMask))
				{
					max=g;
					*dx=ddx;
					*dy=ddy;
					found=true;
				}
			}

		if (gradientUsable)
		{
			if (found)
			{
				pathfindLocalRessourceCountSuccessBase++;
				//printf("...pathfindedLocalRessource v1\n");
				return true;
			}
			else
			{
				*dx=0;
				*dy=0;
				pathfindLocalRessourceCountSuccessLocked++;
				if (verbose)
					printf("...pathfindedLocalRessource v2 locked\n");
				return true;
			}
		}
	}

	updateLocalRessources(building, canSwim);
	
	max=0;
	currentg=gradient[lx+(ly<<5)];
	found=false;
	gradientUsable=false;
	
	if (currentg==1)
	{
		pathfindLocalRessourceCountFailureNone++;
		//printf("...pathfindedLocalRessource v3 No ressource\n");
		return false;
	}
	else if ((currentg!=0) && (currentg!=255))
	{
		for (int sd=0; sd<=1; sd++)
			for (int d=sd; d<8; d+=2)
			{
				int ddx, ddy;
				Unit::dxDyFromDirection(d, &ddx, &ddy);
				int lxddx=clip_0_31(lx+ddx);
				int lyddy=clip_0_31(ly+ddy);
				Uint8 g=gradient[lxddx+(lyddy<<5)];
				if (!gradientUsable && g>currentg && isHardSpaceForGroundUnit(x+ddx, y+ddy, canSwim, teamMask))
					gradientUsable=true;
				if (g>=max && isFreeForGroundUnit(x+ddx, y+ddy, canSwim, teamMask))
				{
					max=g;
					*dx=ddx;
					*dy=ddy;
					found=true;
				}
			}

		if (gradientUsable)
		{
			if (found)
			{
				pathfindLocalRessourceCountSuccessUpdate++;
				//printf("...pathfindedLocalRessource v3\n");
				return true;
			}
			else
			{
				*dx=0;
				*dy=0;
				pathfindLocalRessourceCountSuccessUpdateLocked++;
				if (verbose)
					printf("...pathfindedLocalRessource v4 locked\n");
				return true;
			}
		}
		else
		{
			pathfindLocalRessourceCountFailureUnusable++;
			fprintf(logFile, "lr-a- failed to pathfind localRessource bgid=%d@(%d, %d) p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			if (verbose)
				printf("lr-a- failed to pathfind localRessource bgid=%d@(%d, %d) p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
	}
	else
	{
		pathfindLocalRessourceCountFailureBad++;
		fprintf(logFile, "lr-b- failed to pathfind localRessource bgid=%d@(%d, %d) p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
		if (verbose)
			printf("lr-b- failed to pathfind localRessource bgid=%d@(%d, %d) p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
		return false;
	}
}


