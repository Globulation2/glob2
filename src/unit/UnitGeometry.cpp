// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "Team.h"
#include "Map.h"

#include "Building.h"

#include "Utilities.h"
#include "gradient/DirectFlightDirection.h"

void Unit::setNewValidDirectionGround(void)
{
	assert(!performance[FLY]);
	int i = 0;
	Uint32 me=owner->me;
	while (i < 8 && !owner->map->canTraverse(posX, posY, dx, dy, swimClass(), me))
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
	while ( i<8 && !owner->map->isFreeForMovement(posX+dx,posY+dy,AIR_MOVEMENT_CLASS,owner->me))
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

// Preserve the original angular bearing and deterministic avoidance order on
// unconstrained maps. Destination selection remains in the shared query layer.
void Unit::flyToTarget()
{
    assert(performance[FLY]);
    const auto step=direct_flight::select(posX,posY,targetX,targetY,owner->map->getW(),owner->map->getH(),
        [&](int x,int y){return owner->map->isFreeForMovement(x,y,AIR_MOVEMENT_CLASS,owner->me);});
    findDisplacement([&](int* x,int* y){*x=step.dx;*y=step.dy;return true;});
    directionFromDxDy();
    if(directGradientDecision) {
        const auto key=*directGradientDecision;directGradientDecision.reset();
        const auto* cells=*owner->map->gradientFieldDestination(key);
        owner->map->directGradientMovement(key,posX,posY,entityRandom,cells,dx||dy,true,false);
    }

}

void Unit::escapeGroundTarget()
{
	int ldx=posX-targetX;
	int ldy=posY-targetY;
	findDisplacement([&](int* pathDx, int* pathDy) { simplifyDirection(ldx, ldy, pathDx, pathDy); return true; });
	directionFromDxDy();
	Map *map=owner->map;
	if (map->canTraverse(posX, posY, dx, dy, swimClass(), 0))
		return;
	int cDirection=direction;
	direction=(cDirection+1)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->canTraverse(posX, posY, dx, dy, swimClass(), 0))
		return;
	direction=(cDirection+7)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->canTraverse(posX, posY, dx, dy, swimClass(), 0))
		return;
	direction=(cDirection+2)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->canTraverse(posX, posY, dx, dy, swimClass(), 0))
		return;
	direction=(cDirection+6)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->canTraverse(posX, posY, dx, dy, swimClass(), 0))
		return;
	direction=(cDirection+3)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->canTraverse(posX, posY, dx, dy, swimClass(), 0))
		return;
	direction=(cDirection+5)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->canTraverse(posX, posY, dx, dy, swimClass(), 0))
		return;
	direction=(cDirection+4)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->canTraverse(posX, posY, dx, dy, swimClass(), 0))
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
	findDisplacement([&](int* pathDx, int* pathDy) { dxDyFromDirection(direction,pathDx,pathDy); return true; });
}

int Unit::swimClass() const
{
	return Map::swimClass(performance[WALK], performance[SWIM]);
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
    const auto step=direct_flight::bearing(ldx,ldy,owner->map->getW(),owner->map->getH());
    *cdx=step.dx;*cdy=step.dy;

}

int Unit::movementClass() const
{
    return performance[FLY]>0 ? AIR_MOVEMENT_CLASS : swimClass();
}
