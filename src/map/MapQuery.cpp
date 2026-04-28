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


// Spatial queries: isFree*, isHardSpace*, doesUnitTouch*, immobile units, clearing area

bool Map::isFreeForGroundUnit(int x, int y, bool canSwim, Uint32 teamMask) const
{
	if (isRessource(x, y))
		return false;
	if (getBuilding(x, y)!=NOGBID)
		return false;
	if (getGroundUnit(x, y)!=NOGUID)
		return false;
	if (!canSwim && isWater(x, y))
		return false;
	if (getForbidden(x, y)&teamMask)
		return false;
	return true;
}

bool Map::isFreeForGroundUnitNoForbidden(int x, int y, bool canSwim) const
{
	if (isRessource(x, y))
		return false;
	if (getBuilding(x, y)!=NOGBID)
		return false;
	if (getGroundUnit(x, y)!=NOGUID)
		return false;
	if (!canSwim && isWater(x, y))
		return false;
	return true;
}

bool Map::isFreeForBuilding(int x, int y) const
{
	if (isRessource(x, y))
		return false;
	if (getBuilding(x, y)!=NOGBID)
		return false;
	if (getGroundUnit(x, y)!=NOGUID)
		return false;
	if (isGrass(x, y))
		return true;
	else
		return false;
}

bool Map::isFreeForBuilding(int x, int y, int w, int h) const
{
	for (int yi=y; yi<y+h; yi++)
		for (int xi=x; xi<x+w; xi++)
			if (!isFreeForBuilding(xi, yi))
				return false;
	return true;
}

bool Map::isFreeForBuilding(int x, int y, int w, int h, Uint16 gid) const
{
	for (int yi=y; yi<y+h; yi++)
		for (int xi=x; xi<x+w; xi++)
			if (!isFreeForBuilding(xi, yi))
			{
				if (isRessource(xi, yi))
					return false;
				Uint16 buid=getBuilding(xi, yi);
				if (buid!=NOGBID && buid!=gid)
					return false;
				if (getGroundUnit(xi, yi)!=NOGUID)
					return false;
				if (!isGrass(xi, yi))
					return false;
			}
	return true;
}

bool Map::isHardSpaceForGroundUnit(int x, int y, bool canSwim, Uint32 me) const
{
	if (isRessource(x, y))
		return false;
	if (getBuilding(x, y)!=NOGBID)
		return false;
	if (!canSwim && isWater(x, y))
		return false;
	if (getForbidden(x, y)&me)
		return false;
	return true;
}

bool Map::isHardSpaceForBuilding(int x, int y) const
{
	if (isRessource(x, y))
		return false;
	if (getBuilding(x, y)!=NOGBID)
		return false;
	if (isGrass(x, y))
		return true;
	else
		return false;
}

bool Map::isHardSpaceForBuilding(int x, int y, int w, int h) const
{
	for (int yi=y; yi<y+h; yi++)
		for (int xi=x; xi<x+w; xi++)
			if (!isHardSpaceForBuilding(xi, yi))
				return false;
	return true;
}

bool Map::isHardSpaceForBuilding(int x, int y, int w, int h, Uint16 gid) const
{
	for (int yi=y; yi<y+h; yi++)
		for (int xi=x; xi<x+w; xi++)
		{
			if (isRessource(xi, yi))
				return false;
			Uint16 buid=getBuilding(xi, yi);
			if (buid!=NOGBID && buid!=gid)
				return false;
			if (!isGrass(xi, yi))
				return false;
		}
	return true;
}

bool Map::doesUnitTouchBuilding(Unit *unit, Uint16 gbid, int *dx, int *dy) const
{
	int x=unit->posX;
	int y=unit->posY;
	
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (getBuilding(x+tdx, y+tdy)==gbid)
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
	return false;
}

bool Map::doesPosTouchBuilding(int x, int y, Uint16 gbid) const
{
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (getBuilding(x+tdx, y+tdy)==gbid)
				return true;
	return false;
}

bool Map::doesPosTouchBuilding(int x, int y, Uint16 gbid, int *dx, int *dy) const
{
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (getBuilding(x+tdx, y+tdy)==gbid)
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
	return false;
}

bool Map::doesUnitTouchRessource(Unit *unit, int *dx, int *dy) const
{
	int x=unit->posX;
	int y=unit->posY;
	Uint32 me=unit->owner->me;
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (isRessource(x+tdx, y+tdy) && ((getForbidden(x+tdx, y+tdy)&me)==0))
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
	return false;
}

bool Map::doesUnitTouchRessource(Unit *unit, int ressourceType, int *dx, int *dy) const
{
	int x=unit->posX;
	int y=unit->posY;
	Uint32 me=unit->owner->me;
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (isRessourceTakeable(x+tdx, y+tdy, ressourceType) && ((getForbidden(x+tdx, y+tdy)&me)==0))
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
	return false;
}

bool Map::doesPosTouchRessource(int x, int y, int ressourceType, int *dx, int *dy) const
{
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (isRessourceTakeable(x+tdx, y+tdy,ressourceType))
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
	return false;
}

//! This method gives a good direction to hit for a warrior, and return false if nothing was found.
//! Currently, it chooses to hit any turret if available, then units, then other buildings.
bool Map::doesUnitTouchEnemy(Unit *unit, int *dx, int *dy) const
{
	int x=unit->posX;
	int y=unit->posY;
	int bestTime=256;//Shorter is better
	int bdx=0, bdy=0;

	Uint32 enemies=unit->owner->enemies;
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
		{
			Sint32 gbid=getBuilding(x+tdx, y+tdy);
			if (gbid!=NOGBID)
			{
				int otherTeam=Building::GIDtoTeam(gbid);
				Uint32 otherTeamMask=1<<otherTeam;
				if (enemies & otherTeamMask)
				{
					assert(game);
					assert(game->teams[otherTeam]);
					int otherID=Building::GIDtoID(gbid);
					Building *b=game->teams[otherTeam]->myBuildings[otherID];
					if (!b->type->defaultUnitStayRange)
					{
						if (b->type->shootingRange)
						{
							bdx=tdx;
							bdy=tdy;
							bestTime=0;
						}
						else if (bestTime>255)
						{
							bdx=tdx;
							bdy=tdy;
							bestTime=255;
						}
					}
				}
			}
			Sint32 guid=getGroundUnit(x+tdx, y+tdy);
			if (guid!=NOGUID)
			{
				int otherTeam=Unit::GIDtoTeam(guid);
				Uint32 otherTeamMask=1<<otherTeam;
				if (enemies & otherTeamMask)
				{
					assert(game);
					assert(game->teams[otherTeam]);
					int otherID=Unit::GIDtoID(guid);
					Unit *otherUnit=game->teams[otherTeam]->myUnits[otherID];
					if ((unit->owner->sharedVisionExchange & otherTeamMask)==0)
					{
						int time=(256-otherUnit->delta)/otherUnit->speed;
						if (time<bestTime)
						{
							bestTime=time;
							bdx=tdx;
							bdy=tdy;
						}
					}
				}
			}
			//TODO: can ground WARRIOR hit flying EXPLORER ?
		}
	
	if (bestTime<256)
	{
		*dx=bdx;
		*dy=bdy;
		return true;
	}

	return false;
}




void Map::setClearingAreaClaimed(int x, int y, int teamNumber, int gid)
{
	clearingAreaClaims[teamNumber][coordToIndex(x, y)] = gid;
}



void Map::setClearingAreaUnclaimed(int x, int y, int teamNumber)
{
	clearingAreaClaims[teamNumber][coordToIndex(x, y)]=NOGUID;
}



int Map::isClearingAreaClaimed(int x, int y, int teamNumber) const
{
	return clearingAreaClaims[teamNumber][coordToIndex(x, y)];
}



void Map::markImmobileUnit(int x, int y, int teamNumber)
{
	immobileUnits[coordToIndex(x, y)] = teamNumber;
}


void Map::clearImmobileUnit(int x, int y)
{
	immobileUnits[coordToIndex(x, y)] = 255;
}


bool Map::isImmobileUnit(int x, int y) const
{
	return immobileUnits[coordToIndex(x, y)] != 255;
}



Uint8 Map::getImmobileUnit(int x, int y) const
{
	return immobileUnits[coordToIndex(x, y)];
}




