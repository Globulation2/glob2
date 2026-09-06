// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "BuildingType.h"
#include "Unit.h"
#include "MapInternal.h"



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

std::optional<Offset> Map::doesUnitTouchBuilding(Unit *unit, Uint16 gbid) const
{
	int x=unit->posX;
	int y=unit->posY;

	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (getBuilding(x+tdx, y+tdy)==gbid)
				return Offset{tdx, tdy};
	return std::nullopt;
}

std::optional<Offset> Map::doesPosTouchBuilding(int x, int y, Uint16 gbid) const
{
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (getBuilding(x+tdx, y+tdy)==gbid)
				return Offset{tdx, tdy};
	return std::nullopt;
}

std::optional<Offset> Map::doesUnitTouchRessource(Unit *unit) const
{
	int x=unit->posX;
	int y=unit->posY;
	Uint32 me=unit->owner->me;
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (isRessource(x+tdx, y+tdy) && ((getForbidden(x+tdx, y+tdy)&me)==0))
				return Offset{tdx, tdy};
	return std::nullopt;
}

std::optional<Offset> Map::doesUnitTouchRessource(Unit *unit, int ressourceType) const
{
	int x=unit->posX;
	int y=unit->posY;
	Uint32 me=unit->owner->me;
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (isRessourceTakeable(x+tdx, y+tdy, ressourceType) && ((getForbidden(x+tdx, y+tdy)&me)==0))
				return Offset{tdx, tdy};
	return std::nullopt;
}

std::optional<Offset> Map::doesPosTouchRessource(int x, int y, int ressourceType) const
{
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (isRessourceTakeable(x+tdx, y+tdy, ressourceType))
				return Offset{tdx, tdy};
	return std::nullopt;
}

//! Picks a direction for a warrior to hit. Prefers turrets, then units, then
//! other buildings. Returns nullopt if no enemy is touching.
//!
//! Tie-break rules (preserve carefully — they affect replay determinism):
//!   * Shooter (`shootingRange`): unconditional `bestTime=ENEMY_TOUCH_SCORE_SHOOTER`
//!     write. So if the 3x3 contains two shooters, the later-iterated one wins.
//!   * Non-shooter enemy building: `else if (bestTime>ENEMY_TOUCH_SCORE_BUILDING_FALLBACK)`
//!     — only set if no candidate has been seen yet (initial bestTime is
//!     ENEMY_TOUCH_BEST_TIME_NONE). Once any candidate exists, subsequent
//!     non-shooters are ignored.
//!   * Unit: strict `<`, so a unit with score 0 (e.g. delta=255 speed=2) does
//!     NOT displace an already-found shooter at the same score.
std::optional<Offset> Map::doesUnitTouchEnemy(Unit *unit) const
{
	int x=unit->posX;
	int y=unit->posY;
	int bestTime=ENEMY_TOUCH_BEST_TIME_NONE;//Shorter is better
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
							// Unconditional write — later shooter wins ties.
							bdx=tdx;
							bdy=tdy;
							bestTime=ENEMY_TOUCH_SCORE_SHOOTER;
						}
						else if (bestTime>ENEMY_TOUCH_SCORE_BUILDING_FALLBACK)
						{
							// Only fall back to a non-shooting enemy building
							// when no other candidate has been seen yet.
							bdx=tdx;
							bdy=tdy;
							bestTime=ENEMY_TOUCH_SCORE_BUILDING_FALLBACK;
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
						// Strict `<` — a unit never displaces a tied candidate.
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

	if (bestTime<ENEMY_TOUCH_BEST_TIME_NONE)
		return Offset{bdx, bdy};

	return std::nullopt;
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
	immobileUnits[coordToIndex(x, y)] = IMMOBILE_UNIT_NONE;
}


bool Map::isImmobileUnit(int x, int y) const
{
	return immobileUnits[coordToIndex(x, y)] != IMMOBILE_UNIT_NONE;
}



Uint8 Map::getImmobileUnit(int x, int y) const
{
	return immobileUnits[coordToIndex(x, y)];
}




