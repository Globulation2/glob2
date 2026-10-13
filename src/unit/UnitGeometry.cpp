// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "Game.h"
#include "unit/planning/MovementShadow.h"
#include "Team.h"
#include "Map.h"

#include "Building.h"

#include "Utilities.h"

void Unit::setNewValidDirectionGround(void)
{
	assert(!performance(FLY));
	int i=0;
	bool swim=(performance(SWIM)>0);
	Uint32 me=owner->me;
	while ( i<8 && ((!performance(WALK) && !owner->map->terrainPropertiesAt(core.posX+core.dx,core.posY+core.dy).swimmable) || !owner->map->isFreeForGroundUnit(core.posX+core.dx, core.posY+core.dy, swim, me)))
	{
		core.direction=(core.direction+1)&UNIT_DIRECTION_MASK;
		dxDyFromDirection();
		i++;
	}
	if (i==UNIT_DIRECTION_COUNT)
	{
		core.direction=UNIT_DIRECTION_NONE;
		dxDyFromDirection();
	}
}

void Unit::setNewValidDirectionAir(void)
{
	assert(performance(FLY));
	int i=0;
	while ( i<8 && !owner->map->isFreeForAirUnit(core.posX+core.dx, core.posY+core.dy))
	{
		core.direction=(core.direction+1)&UNIT_DIRECTION_MASK;
		dxDyFromDirection();
		i++;
	}
	if (i==UNIT_DIRECTION_COUNT)
	{
		core.direction=UNIT_DIRECTION_NONE;
		core.dx=0;
		core.dy=0;
	}
}

void Unit::flyToTarget()
{
	assert(performance(FLY));
    if (owner->map->hasAirTerrainConstraints())
    {
        core.dx = core.dy = 0;
        owner->map->pathfindAirPointToPoint(core.posX,core.posY,core.targetX,core.targetY,&core.dx,&core.dy);
        directionFromDxDy();
        return;
    }
	int ldx=core.targetX-core.posX;
	int ldy=core.targetY-core.posY;
	simplifyDirection(ldx, ldy, &core.dx, &core.dy);
	directionFromDxDy();
	Map *map=owner->map;
	if (map->isFreeForAirUnit(core.posX+core.dx, core.posY+core.dy))
		return;
	int cDirection=core.direction;
	core.direction=(cDirection+1)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(core.posX+core.dx, core.posY+core.dy))
		return;
	core.direction=(cDirection+7)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(core.posX+core.dx, core.posY+core.dy))
		return;
	core.direction=(cDirection+2)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(core.posX+core.dx, core.posY+core.dy))
		return;
	core.direction=(cDirection+6)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(core.posX+core.dx, core.posY+core.dy))
		return;
	core.direction=(cDirection+3)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(core.posX+core.dx, core.posY+core.dy))
		return;
	core.direction=(cDirection+5)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(core.posX+core.dx, core.posY+core.dy))
		return;
	core.direction=(cDirection+4)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if (map->isFreeForAirUnit(core.posX+core.dx, core.posY+core.dy))
		return;
	core.dx=0;
	core.dy=0;
	core.direction=UNIT_DIRECTION_NONE;
	if (verbose)
		printf("guid=(%d) flyto failed pos=(%d, %d) \n", core.gid, core.posX, core.posY);
}


void Unit::escapeGroundTarget()
{
	int ldx=core.posX-core.targetX;
	int ldy=core.posY-core.targetY;
	simplifyDirection(ldx, ldy, &core.dx, &core.dy);
	directionFromDxDy();
	bool canSwim=performance(SWIM);
	Map *map=owner->map;
	if ((performance(WALK) || map->terrainPropertiesAt(core.posX+core.dx,core.posY+core.dy).swimmable) && map->isFreeForGroundUnitNoForbidden(core.posX+core.dx, core.posY+core.dy, canSwim))
		return;
	int cDirection=core.direction;
	core.direction=(cDirection+1)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if ((performance(WALK) || map->terrainPropertiesAt(core.posX+core.dx,core.posY+core.dy).swimmable) && map->isFreeForGroundUnitNoForbidden(core.posX+core.dx, core.posY+core.dy, canSwim))
		return;
	core.direction=(cDirection+7)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if ((performance(WALK) || map->terrainPropertiesAt(core.posX+core.dx,core.posY+core.dy).swimmable) && map->isFreeForGroundUnitNoForbidden(core.posX+core.dx, core.posY+core.dy, canSwim))
		return;
	core.direction=(cDirection+2)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if ((performance(WALK) || map->terrainPropertiesAt(core.posX+core.dx,core.posY+core.dy).swimmable) && map->isFreeForGroundUnitNoForbidden(core.posX+core.dx, core.posY+core.dy, canSwim))
		return;
	core.direction=(cDirection+6)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if ((performance(WALK) || map->terrainPropertiesAt(core.posX+core.dx,core.posY+core.dy).swimmable) && map->isFreeForGroundUnitNoForbidden(core.posX+core.dx, core.posY+core.dy, canSwim))
		return;
	core.direction=(cDirection+3)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if ((performance(WALK) || map->terrainPropertiesAt(core.posX+core.dx,core.posY+core.dy).swimmable) && map->isFreeForGroundUnitNoForbidden(core.posX+core.dx, core.posY+core.dy, canSwim))
		return;
	core.direction=(cDirection+5)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if ((performance(WALK) || map->terrainPropertiesAt(core.posX+core.dx,core.posY+core.dy).swimmable) && map->isFreeForGroundUnitNoForbidden(core.posX+core.dx, core.posY+core.dy, canSwim))
		return;
	core.direction=(cDirection+4)&UNIT_DIRECTION_MASK;
	dxDyFromDirection();
	if ((performance(WALK) || map->terrainPropertiesAt(core.posX+core.dx,core.posY+core.dy).swimmable) && map->isFreeForGroundUnitNoForbidden(core.posX+core.dx, core.posY+core.dy, canSwim))
		return;
	core.dx=0;
	core.dy=0;
	core.direction=UNIT_DIRECTION_NONE;
	if (verbose)
		printf("guid=(%d) escapeGroundTarget failed pos=(%d, %d) \n", core.gid, core.posX, core.posY);
}

void Unit::endOfAction(void)
{
auto* diagnostic = owner->game->movementDiagnostics();
    if (!diagnostic) {
        handleMedical();
        if (core.isDead) return;
        handleActivity(); handleDisplacement(); handleMovement(); handleAction();
        return;
    }
    struct Completion { MovementPlanning::Shadow* diagnostic; Unit& unit;
        ~Completion() { if(diagnostic)diagnostic->completeAction(unit); } } completion{diagnostic,*this};
    MovementPlanning::DiagnosticBinding binding(diagnostic ? diagnostic->counters(*this) : nullptr);
    {
        MovementPlanning::PhaseScope phase(MovementPlanning::Phase::Transitions);
        handleMedical();
        if (core.isDead) return;
        handleActivity();
    }
    { MovementPlanning::PhaseScope phase(MovementPlanning::Phase::Displacement); handleDisplacement(); }
    { MovementPlanning::PhaseScope phase(MovementPlanning::Phase::Direction); handleMovement(); }
    if (diagnostic) diagnostic->observe(*this);
    { MovementPlanning::PhaseScope phase(MovementPlanning::Phase::Action); handleAction(); }
}

// NOTE : position 0 is top left (-1, -1) then run clockwise

void Unit::directionFromDxDy(void)
{
	const int tab[3][3]={	{0, 1, 2},
							{7, 8, 3},
							{6, 5, 4} };
	assert(core.dx>=-1);
	assert(core.dx<=1);
	assert(core.dy>=-1);
	assert(core.dy<=1);
	core.direction=tab[core.dy+1][core.dx+1];
}

void Unit::dxDyFromDirection(void)
{
	dxDyFromDirection(core.direction,&core.dx,&core.dy);
}

int Unit::swimClass() const
{
	return Map::swimClass(performance(WALK), performance(SWIM));
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
