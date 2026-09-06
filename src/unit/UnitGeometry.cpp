// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "Team.h"
#include "Map.h"

#include "Building.h"

#include "Utilities.h"

void Unit::setNewValidDirectionGround(void)
{
	assert(!performance[FLY]);
	int i=0;
	bool swim=(performance[SWIM]>0);
	Uint32 me=owner->me;
	while ( i<8 && !owner->map->isFreeForGroundUnit(posX+dx, posY+dy, swim, me))
	{
		direction=(direction+1)&UNIT_DIRECTION_MASK;
		dxDyFromDirection();
		i++;
	}
	if (i==UNIT_DIRECTION_COUNT)
	{
		direction=UNIT_DIRECTION_NONE;
		dxDyFromDirection();
	}
}

void Unit::setNewValidDirectionAir(void)
{
	assert(performance[FLY]);
	int i=0;
	while ( i<8 && !owner->map->isFreeForAirUnit(posX+dx, posY+dy))
	{
		direction=(direction+1)&UNIT_DIRECTION_MASK;
		dxDyFromDirection();
		i++;
	}
	if (i==UNIT_DIRECTION_COUNT)
	{
		direction=UNIT_DIRECTION_NONE;
		dx=0;
		dy=0;
	}
}

void Unit::flyToTarget()
{
	assert(performance[FLY]);
	int ldx=targetX-posX;
	int ldy=targetY-posY;
	simplifyDirection(ldx, ldy, &dx, &dy);
	directionFromDxDy();
	Map *map=owner->map;
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	int cDirection=direction;
	direction=(cDirection+1)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	direction=(cDirection+7)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	direction=(cDirection+2)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	direction=(cDirection+6)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	direction=(cDirection+3)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	direction=(cDirection+5)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	direction=(cDirection+4)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	dx=0;
	dy=0;
	direction=UNIT_DIRECTION_NONE;
	if (verbose)
		printf("guid=(%d) flyto failed pos=(%d, %d) \n", gid, posX, posY);
}


void Unit::escapeGroundTarget()
{
	int ldx=posX-targetX;
	int ldy=posY-targetY;
	simplifyDirection(ldx, ldy, &dx, &dy);
	directionFromDxDy();
	bool canSwim=performance[SWIM];
	Map *map=owner->map;
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	int cDirection=direction;
	direction=(cDirection+1)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	direction=(cDirection+7)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	direction=(cDirection+2)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	direction=(cDirection+6)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	direction=(cDirection+3)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	direction=(cDirection+5)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	direction=(cDirection+4)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	dx=0;
	dy=0;
	direction=UNIT_DIRECTION_NONE;
	if (verbose)
		printf("guid=(%d) escapeGroundTarget failed pos=(%d, %d) \n", gid, posX, posY);
}

void Unit::endOfAction(void)
{
	handleMedical();
	if (isDead)
		return;
	handleActivity();
	handleDisplacement();
	handleMovement();
	handleAction();
}

// NOTE : position 0 is top left (-1, -1) then run clockwise

void Unit::directionFromDxDy(void)
{
	const int tab[3][3]={	{0, 1, 2},
							{7, 8, 3},
							{6, 5, 4} };
	assert(dx>=-1);
	assert(dx<=1);
	assert(dy>=-1);
	assert(dy<=1);
	direction=tab[dy+1][dx+1];
}

void Unit::dxDyFromDirection(void)
{
	dxDyFromDirection(direction,&dx,&dy);
}

int Unit::directionFromDxDy(int dx, int dy)
{
	const int tab[3][3]={	{0, 1, 2},
							{7, 8, 3},
							{6, 5, 4} };
	assert(dx>=-1);
	assert(dx<=1);
	assert(dy>=-1);
	assert(dy<=1);
	return tab[dy+1][dx+1];
}

void Unit::simplifyDirection(int ldx, int ldy, int *cdx, int *cdy)
{
	int mapW=owner->map->getW();
	int mapH=owner->map->getH();
	if (ldx>(mapW>>1))
		ldx-=mapW;
	else if (ldx<-(mapW>>1))
		ldx+=mapW;
	if (ldy>(mapH>>1))
		ldy-=mapH;
	else if (ldy<-(mapH>>1))
		ldy+=mapH;

        /* We consider a cell to be vertical or horizontal in
           direction (rather than diagonal) if it is 2.41 times more
           vertical than horizontal, or vice versa.  This is because
           the halfway point between 45 degrees and 90 degrees is 67.5
           degrees and sin(67.5 deg) / cos(67.5 deg) =
           2.41421356237. */
	if ((100 * abs(ldx)) > (241 * abs(ldy)))
	{
		*cdx=SIGN(ldx);
		*cdy=0;
	}
	else if ((100 * abs(ldy)) > (241 * abs(ldx)))
	{
		*cdx=0;
		*cdy=SIGN(ldy);
	}
	else
	{
		*cdx=SIGN(ldx);
		*cdy=SIGN(ldy);
	}
}
