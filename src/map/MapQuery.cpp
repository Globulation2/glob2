// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "building_type.h"
#include "Unit.h"
#include "MapInternal.h"

#include <algorithm>
#include <valarray>
#include <Stream.h>
#include <queue>


// Spatial queries: isFree*, isHardSpace*, doesUnitTouch*, immobile units, clearing area

// Shared per-tile predicate that drives every isFree*/isHardSpace* overload.
// Each TileChecks flag toggles one occupancy/terrain test; ignoreGid lets the
// gid-tolerant building overloads accept a specific occupant gid.
// C++: Map.cpp historically inlined the same five tests in eight near-duplicate
// bodies — see the original isFree*/isHardSpace* implementations.
bool Map::checkTile(int x, int y, TileChecks c, bool canSwim,
                    Uint32 teamMask, Uint16 ignoreGid) const
{
	if (c.noRessource && isRessource(x, y))
		return false;
	Uint16 buid = getBuilding(x, y);
	if (buid != NOGBID && buid != ignoreGid)
		return false;
	if (c.noUnit && getGroundUnit(x, y) != NOGUID)
		return false;
	if (c.waterBlocks && !canSwim && isWater(x, y))
		return false;
	if (c.requireGrass && !isGrass(x, y))
		return false;
	if (c.checkForbidden && (getForbidden(x, y) & teamMask))
		return false;
	return true;
}

bool Map::isFreeForGroundUnit(int x, int y, bool canSwim, Uint32 teamMask) const
{
	return checkTile(x, y, {true, true, true, false, true}, canSwim, teamMask, NOGBID);
}

bool Map::isFreeForGroundUnitNoForbidden(int x, int y, bool canSwim) const
{
	return checkTile(x, y, {true, true, true, false, false}, canSwim, 0, NOGBID);
}

bool Map::isFreeForBuilding(int x, int y) const
{
	return checkTile(x, y, {true, true, false, true, false}, false, 0, NOGBID);
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
	const TileChecks checks{true, true, false, true, false};
	for (int yi=y; yi<y+h; yi++)
		for (int xi=x; xi<x+w; xi++)
			if (!checkTile(xi, yi, checks, false, 0, gid))
				return false;
	return true;
}

bool Map::isHardSpaceForGroundUnit(int x, int y, bool canSwim, Uint32 me) const
{
	return checkTile(x, y, {true, false, true, false, true}, canSwim, me, NOGBID);
}

bool Map::isHardSpaceForBuilding(int x, int y) const
{
	return checkTile(x, y, {true, false, false, true, false}, false, 0, NOGBID);
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
	const TileChecks checks{true, false, false, true, false};
	for (int yi=y; yi<y+h; yi++)
		for (int xi=x; xi<x+w; xi++)
			if (!checkTile(xi, yi, checks, false, 0, gid))
				return false;
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




