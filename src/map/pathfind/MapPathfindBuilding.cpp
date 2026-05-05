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


// Building pathfinding (buildingAvailable, pathfindBuilding, dirtyLocalGradient)

bool Map::buildingAvailable(Building *building, bool canSwim, int x, int y, int *dist)
{
	buildingAvailableCountTot++;
	assert(building);
	int bx=building->posX;
	int by=building->posY;
	x&=wMask;
	y&=hMask;
	assert(x>=0);
	assert(y>=0);
	
	Uint8 *gradient=building->localGradient[canSwim];
	
	if (isInLocalGradient(x, y, bx, by))
	{
		buildingAvailableCountClose++;
		int lx=(x-bx+15+32)&31;
		int ly=(y-by+15+32)&31;
		if (!building->dirtyLocalGradient[canSwim])
		{
			Uint8 currentg=gradient[lx+(ly<<5)];
			if (currentg>1)
			{
				buildingAvailableCountCloseSuccessFast++;
				*dist=255-currentg;
				return true;
			}
			else
			{
				for (int d=0; d<8; d++)
				{
					int ddx, ddy;
					Unit::dxDyFromDirection(d, &ddx, &ddy);
					int lxddx=clip_0_31(lx+ddx);
					int lyddy=clip_0_31(ly+ddy);
					Uint8 g=gradient[lxddx+(lyddy<<5)];
					if (g>1)
					{
						buildingAvailableCountCloseSuccessAround++;
						*dist=255-g;
						return true;
					}
				}
			}
		}
		
		updateLocalGradient(building, canSwim);
		if (building->locked[canSwim])
		{
			buildingAvailableCountCloseFailureLocked++;
			//printf("ba-a- local gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			//fprintf(logFile, "ba-a- local gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			
			return false;
		}
		
		Uint8 currentg=gradient[lx+ly*32];
		
		if (currentg>1)
		{
			buildingAvailableCountCloseSuccessUpdate++;
			*dist=255-currentg;
			return true;
		}
		else
		{
			for (int d=0; d<8; d++)
			{
				int ddx, ddy;
				Unit::dxDyFromDirection(d, &ddx, &ddy);
				int lxddx=clip_0_31(lx+ddx);
				int lyddy=clip_0_31(ly+ddy);
				Uint8 g=gradient[lxddx+(lyddy<<5)];
				if (g>1)
				{
					buildingAvailableCountCloseSuccessUpdateAround++;
					*dist=255-g;
					return true;
				}
			}
		}
		buildingAvailableCountCloseFailureEnd++;
		return false;
	}
	else
		buildingAvailableCountIsFar++;
	buildingAvailableCountFar++;
	
	
	gradient=building->globalGradient[canSwim];
	if (gradient==NULL)
	{
		buildingAvailableCountFarNew++;
		gradient=new Uint8[size];
		fprintf(logFile, "ba- allocating globalGradient for gbid=%d (%p)\n", building->gid, gradient);
		building->globalGradient[canSwim]=gradient;
	}
	else
	{
		buildingAvailableCountFarOld++;
		if (building->locked[canSwim])
		{
			buildingAvailableCountFarOldFailureLocked++;
			//printf("ba-b- global gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			//fprintf(logFile, "ba-b- global gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
		Uint8 currentg=gradient[coordToIndex(x, y)];
		if (currentg>1)
		{
			buildingAvailableCountFarOldSuccessFast++;
			*dist=255-currentg;
			return true;
		}
		else
		{
			for (int d=0; d<8; d++)
			{
				int ddx, ddy;
				Unit::dxDyFromDirection(d, &ddx, &ddy);
				Uint8 g=gradient[coordToIndex(x + ddx, y + ddy)];
				if (g>1)
				{
					buildingAvailableCountFarOldSuccessAround++;
					*dist=255-g;
					return true;
				}
			}
			buildingAvailableCountFarOldFailureEnd++;
			//printf("ba-c- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			//fprintf(logFile, "ba-c- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
	}
	
	updateGlobalGradient(building, canSwim);
	if (building->locked[canSwim])
	{
		buildingAvailableCountFarNewFailureLocked++;
		//printf("ba-d- global gradient to building bgid=%d@(%d, %d) failed, locked.\n", building->gid, building->posX, building->posY);
		fprintf(logFile, "ba-d- global gradient to building bgid=%d@(%d, %d) failed, locked.\n", building->gid, building->posX, building->posY);
		return false;
	}
	
	Uint8 currentg=gradient[coordToIndex(x, y)];
	if (currentg>1)
	{
		buildingAvailableCountFarNewSuccessFast++;
		*dist=255-currentg;
		return true;
	}
	else
	{
		for (int d=0; d<8; d++)
		{
			int ddx, ddy;
			Unit::dxDyFromDirection(d, &ddx, &ddy);
			Uint8 g=gradient[coordToIndex(x + ddx, y + ddy)];
			if (g>1)
			{
				buildingAvailableCountFarNewSuccessClosely++;
				*dist=255-g;
				return true;
			}
		}
		if (building->type->isVirtual)
		{
			//printf("ba-e- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			//fprintf(logFile, "ba-e- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			buildingAvailableCountFarNewFailureVirtual++;
		}
		else
		{
			if (building->verbose)
				printf("ba-f- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			fprintf(logFile, "ba-f- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			buildingAvailableCountFarNewFailureEnd++;
		}
		return false;
	}
}


bool Map::pathfindBuilding(Building *building, bool canSwim, int x, int y, int *dx, int *dy, bool verbose)
{
	pathToBuildingCountTot++;
	assert(building);
	if (verbose)
		printf("pathfindingBuilding (gbid=%d)...\n", building->gid);
	int bx=building->posX;
	int by=building->posY;
	assert(x>=0);
	assert(y>=0);
	Uint32 teamMask=building->owner->me;
	if (((cases[x+y*w].forbidden) & teamMask)!=0)
	{
		int teamNumber=building->owner->teamNumber;
		if (verbose)
			printf(" ...pathfindForbidden(%d, %d, %d, %d)\n", teamNumber, canSwim, x, y);
		return pathfindForbidden(building->globalGradient[canSwim], teamNumber, canSwim, x, y, dx, dy, verbose);
	}
	Uint8 *gradient=building->localGradient[canSwim];
	if (isInLocalGradient(x, y, bx, by))
	{
		pathToBuildingCountClose++;
		int lx=(x-bx+15+32)&31;
		int ly=(y-by+15+32)&31;
		Uint8 currentg=gradient[lx+(ly<<5)];
		
		if (!building->dirtyLocalGradient[canSwim] && currentg==255)
		{
			*dx=0;
			*dy=0;
			pathToBuildingCountCloseSuccessStand++;
			if (verbose)
				printf("...pathfindedBuilding v1\n");
			return true;
		}

		if (!building->dirtyLocalGradient[canSwim] && currentg>1)
		{
			if (directionByMinigrad(teamMask, canSwim, x, y, bx, by, dx, dy, gradient, true, verbose))
			{
				pathToBuildingCountCloseSuccessBase++;
				if (verbose)
					printf("...pathfindedBuilding v2\n");
				return true;
			}
		}

		updateLocalGradient(building, canSwim);
		if (building->locked[canSwim])
		{
			pathToBuildingCountCloseFailureLocked++;
			if (verbose)
				printf("a- local gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			fprintf(logFile, "a- local gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}

		currentg=gradient[lx+ly*32];
		if (currentg>1)
		{
			if (directionByMinigrad(teamMask, canSwim, x, y, bx, by, dx, dy, gradient, true, verbose))
			{
				pathToBuildingCountCloseSuccessUpdated++;
				if (verbose)
					printf("...pathfindedBuilding v4\n");
				return true;
			}
		}
		pathToBuildingCountCloseFailureEnd++;
	}
	else
		pathToBuildingCountIsFar++;
	pathToBuildingCountFar++;
	//Here the "local-32*32-cases-gradient-pathfinding-system" has failed, then we look for a full size gradient.
	
	gradient=building->globalGradient[canSwim];
	if (gradient==NULL)
	{
		pathToBuildingCountFarIsNew++;
		gradient=new Uint8[size];
		if (verbose)
			printf("allocating globalGradient for gbid=%d (%p)\n", building->gid, gradient);
		fprintf(logFile, "allocating globalGradient for gbid=%d (%p)\n", building->gid, gradient);
		building->globalGradient[canSwim]=gradient;
	}
	else
	{
		bool found=false;
		Uint8 currentg=gradient[coordToIndex(x, y)];
		if (building->locked[canSwim])
		{
			pathToBuildingCountFarOldFailureLocked++;
			if (verbose)
				printf("b- global gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			fprintf(logFile, "b- global gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
		else if (currentg==1)
		{
			pathToBuildingCountFarOldFailureBad++;
			if (verbose)
				printf("c- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			fprintf(logFile, "c- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
		else
			found=directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient, true, verbose);

		//printf("found=%d, d=(%d, %d)\n", found, *dx, *dy);
		if (found)
		{
			pathToBuildingCountFarOldSuccess++;
			if (verbose)
				printf("...pathfindedBuilding v6\n");
			return true;
		}
		else if (building->lastGlobalGradientUpdateStepCounter[canSwim]+128>game->stepCounter) // not faster than 5.12s
		{
			pathToBuildingCountFarOldFailureRepeat++;
			if (verbose)
				printf("d- global gradient to building bgid=%d@(%d, %d) failed, repeat.\n", building->gid, building->posX, building->posY);
			return directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient, false, verbose);
		}
		else
		{
			pathToBuildingCountFarOldFailureUnusable++;
		}
	}
	
	updateGlobalGradient(building, canSwim);
	building->lastGlobalGradientUpdateStepCounter[canSwim]=game->stepCounter;
	
	if (building->locked[canSwim])
	{
		pathToBuildingCountFarUpdateFailureLocked++;
		if (verbose)
			printf("e- global gradient to building bgid=%d@(%d, %d) failed, locked.\n", building->gid, building->posX, building->posY);
		fprintf(logFile, "e- global gradient to building bgid=%d@(%d, %d) failed, locked.\n", building->gid, building->posX, building->posY);
		return false;
	}
	
	Uint8 currentg=gradient[coordToIndex(x, y)];
	if (currentg>1)
	{
		if (directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient, true, verbose))
		{
			pathToBuildingCountFarUpdateSuccess++;
			if (verbose)
				printf("...pathfindedBuilding v7\n");
			return true;
		}
	}
	
	if (building->type->isVirtual)
	{
		pathToBuildingCountFarUpdateFailureVirtual++;
		if (verbose)
			printf("f- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
		//fprintf(logFile, "f- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
	}
	else
	{
		pathToBuildingCountFarUpdateFailureBad++;
		// TODO: find why this happend so often
		if (verbose)
			printf("g- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d), canSwim=%d\n", building->gid, building->posX, building->posY, x, y, canSwim);
		fprintf(logFile, "g- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d), canSwim=%d\n", building->gid, building->posX, building->posY, x, y, canSwim);
	}
	return false;
}


void Map::dirtyLocalGradient(int x, int y, int wl, int hl, int teamNumber)
{
	y &= hMask;
	x &= wMask;
	fprintf(logFile, "Map::dirtyLocalGradient(%d, %d, %d, %d, %d)\n", x, y, wl, hl, teamNumber);
	for (int hi=0; hi<hl; hi++)
	{
		for (int wi=0; wi<wl; wi++)
		{
			int bgid=cases[coordToIndex(x + wi, y + hi)].building;
			if (bgid!=NOGBID)
				if (Building::GIDtoTeam(bgid)==teamNumber)
				{
					fprintf(logFile, "dirtying-LocalGradient bgid=%d\n", bgid);
					Building *b=game->teams[teamNumber]->myBuildings[Building::GIDtoID(bgid)];
					for (int canSwim=0; canSwim<2; canSwim++)
					{
						b->dirtyLocalGradient[canSwim]=true;
						b->locked[canSwim]=false;
						if (b->localRessources[canSwim])
						{
							delete b->localRessources[canSwim];
							b->localRessources[canSwim]=NULL;
						}
					}
				}
		}
	}
}


