// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "Team.h"
#include "Map.h"

#include "Building.h"

#include "Utilities.h"

namespace
{
	// In MOV_RANDOM_FLY, we resample (dx,dy) up to this many times trying to find a
	// step that doesn't cross into an enemy guard tower's range. If all attempts
	// land inside tower range, the last sampled (dx,dy) is used as a fallback —
	// the unit takes the hit rather than stalling.
	constexpr int RANDOM_FLY_TOWER_AVOIDANCE_ATTEMPTS = 5;

	// GOING_TARGET_MAX_PATH_LENGTH lives in UnitConsts.h so UnitMovement.cpp's
	// tryAcquireAttackTarget can share the same path-budget value.
}

void Unit::wrapPosition()
{
	posX=(posX+dx)&(owner->map->getMaskW());
	posY=(posY+dy)&(owner->map->getMaskH());
}

void Unit::clearOccupiedMapSlot()
{
	if (performance[FLY])
		owner->map->setAirUnit(posX, posY, NOGUID);
	else
		owner->map->setGroundUnit(posX, posY, NOGUID);
}

void Unit::claimOccupiedMapSlot()
{
	if (performance[FLY])
	{
		assert(owner->map->getAirUnit(posX, posY)==NOGUID);
		owner->map->setAirUnit(posX, posY, gid);
	}
	else
	{
		assert(owner->map->getGroundUnit(posX, posY)==NOGUID);
		owner->map->setGroundUnit(posX, posY, gid);
	}
}

void Unit::handleActionRandomGround()
{
	assert(!performance[FLY]);
	clearOccupiedMapSlot();
	owner->map->pathfindRandom(this);
	wrapPosition();
	selectPreferredGroundMovement();
	speed=performance[action];
	claimOccupiedMapSlot();
}

void Unit::handleActionRandomFly()
{
	assert(performance[FLY]);
	clearOccupiedMapSlot();
	for(int q = 0; q < RANDOM_FLY_TOWER_AVOIDANCE_ATTEMPTS; ++q)
	{
		dx=-1+syncRand()%3;
		dy=-1+syncRand()%3;
		if(!locationIsInEnemyGuardTowerRange(posX + dx, posY + dy))
			break;
	}
	directionFromDxDy();
	setNewValidDirectionAir();
	wrapPosition();
	action=FLY;
	speed=performance[FLY];
	claimOccupiedMapSlot();
}

void Unit::handleActionGoingTarget()
{
	assert(!performance[FLY]);
	clearOccupiedMapSlot();
	owner->map->pathfindPointToPoint(posX, posY, targetX, targetY, &dx, &dy, performance[SWIM] > 0, owner->me, GOING_TARGET_MAX_PATH_LENGTH);
	directionFromDxDy();
	wrapPosition();

	if(dx == 0 && dy == 0)
		owner->map->markImmobileUnit(posX, posY, owner->teamNumber);

	selectPreferredGroundMovement();
	speed=performance[action];
	claimOccupiedMapSlot();
}

void Unit::handleActionFlyingTarget()
{
	// No assert(getAirUnit==NOGUID) on the final claim — two flyers can
	// converge on the same target tile, so the destination slot may be
	// non-empty. Direct setAirUnit calls preserve that, unlike claimOccupiedMapSlot().
	owner->map->setAirUnit(posX, posY, NOGUID);

	flyToTarget();

	wrapPosition();

	action=FLY;
	speed=performance[FLY];

	owner->map->setAirUnit(posX, posY, gid);
}

void Unit::handleActionGoingDxDy()
{
	clearOccupiedMapSlot();

	directionFromDxDy();

	wrapPosition();

	if(dx == 0 && dy == 0)
		owner->map->markImmobileUnit(posX, posY, owner->teamNumber);

	selectPreferredMovement();
	speed=performance[action];

	claimOccupiedMapSlot();

	if (verbose)
		printf("guid=(%d) MOV_GOING_DX_DY d=(%d, %d; %d).\n", gid, direction, dx, dy);
}

void Unit::handleActionEnteringBuilding()
{
	// NOTE : this is a hack : We don't delete the unit on the map
	// because we have to draw it while it is entering.
	wrapPosition();
	directionFromDxDy();
	selectPreferredMovement();
	speed=performance[action];
}

void Unit::handleActionExitingBuilding()
{
	directionFromDxDy();
	selectPreferredMovement();
	speed=performance[action];
	claimOccupiedMapSlot();
}

void Unit::handleActionFilling()
{
	owner->map->markImmobileUnit(posX, posY, owner->teamNumber);
	directionFromDxDy();
	action=BUILD;
	speed=performance[action];
}

void Unit::handleActionAttackingTarget()
{
	owner->map->markImmobileUnit(posX, posY, owner->teamNumber);
	directionFromDxDy();
	action=ATTACK_SPEED;
	speed=performance[action];
}

void Unit::handleActionHarvesting()
{
	owner->map->markImmobileUnit(posX, posY, owner->teamNumber);
	directionFromDxDy();
	action=HARVEST;
	speed=performance[action];
	assert(speed!=0);
}

void Unit::handleAction(void)
{
	owner->map->clearImmobileUnit(posX, posY);
	switch (movement)
	{
		case MOV_RANDOM_GROUND:     handleActionRandomGround();     break;
		case MOV_RANDOM_FLY:        handleActionRandomFly();        break;
		case MOV_GOING_TARGET:      handleActionGoingTarget();      break;
		case MOV_FLYING_TARGET:     handleActionFlyingTarget();     break;
		case MOV_GOING_DX_DY:       handleActionGoingDxDy();        break;
		case MOV_ENTERING_BUILDING: handleActionEnteringBuilding(); break;
		case MOV_EXITING_BUILDING:  handleActionExitingBuilding();  break;
		case MOV_INSIDE:                                            break;
		case MOV_FILLING:           handleActionFilling();          break;
		case MOV_ATTACKING_TARGET:  handleActionAttackingTarget();  break;
		case MOV_HARVESTING:        handleActionHarvesting();       break;
		default:                    assert(false);                  break;
	}
}
