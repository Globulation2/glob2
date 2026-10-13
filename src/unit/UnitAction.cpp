// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "Team.h"
#include "Map.h"

#include "Building.h"

#include "Utilities.h"
#include "UnitTiming.h"

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
	if (!performance(FLY) && !performance(WALK) && (core.dx || core.dy) && (!performance(SWIM) || !owner->map->terrainPropertiesAt(core.posX+core.dx,core.posY+core.dy).swimmable)) {
		core.dx=core.dy=0; core.direction=UNIT_DIRECTION_NONE;
	}
	core.posX=(core.posX+core.dx)&(owner->map->getMaskW());
	core.posY=(core.posY+core.dy)&(owner->map->getMaskH());
}

void Unit::clearOccupiedMapSlot()
{
	if (performance(FLY))
		owner->map->setAirUnit(core.posX, core.posY, NOGUID);
	else
		owner->map->setGroundUnit(core.posX, core.posY, NOGUID);
}

void Unit::claimOccupiedMapSlot()
{
	if (performance(FLY))
	{
		assert(owner->map->getAirUnit(core.posX, core.posY)==NOGUID);
		owner->map->setAirUnit(core.posX, core.posY, core.gid);
	}
	else
	{
		assert(owner->map->getGroundUnit(core.posX, core.posY)==NOGUID);
		owner->map->setGroundUnit(core.posX, core.posY, core.gid);
	}
}

void Unit::handleActionRandomGround()
{
	assert(!performance(FLY));
	if (!performance(WALK) && !performance(SWIM)) {
		core.dx=core.dy=0; core.direction=UNIT_DIRECTION_NONE; core.action=STOP_WALK;
		core.speed=std::max(1,performance(STOP_WALK)); return;
	}
	clearOccupiedMapSlot();
	owner->map->pathfindRandom(this);
	wrapPosition();
	selectPreferredGroundMovement();
	core.speed=unitTerrainMovementSpeed(performance(core.action), core.action == FLY
        ? owner->map->terrainPropertiesAt(core.posX,core.posY).airSpeedQ8
        : owner->map->terrainPropertiesAt(core.posX,core.posY).groundSpeedQ8, core.dx != 0 || core.dy != 0);
	claimOccupiedMapSlot();
}

void Unit::handleActionRandomFly()
{
	assert(performance(FLY));
	clearOccupiedMapSlot();
	if (owner->map->terrainPropertiesAt(core.posX,core.posY).airHealthQ8 < 0)
		owner->map->pathfindTerrainSafety(this);
	else
	{
		for(int q = 0; q < RANDOM_FLY_TOWER_AVOIDANCE_ATTEMPTS; ++q)
		{
			core.dx=-1+detail.entityRandom.nextU32()%3;
			core.dy=-1+detail.entityRandom.nextU32()%3;
			if(!locationIsInEnemyGuardTowerRange(core.posX + core.dx, core.posY + core.dy))
				break;
		}
		directionFromDxDy();
		setNewValidDirectionAir();
		// Direction repair can rotate onto a hazard, so check the final step.
		if (owner->map->terrainPropertiesAt(core.posX+core.dx,core.posY+core.dy).airHealthQ8 < 0) {
			core.dx=core.dy=0;
			directionFromDxDy();
		}
	}

	wrapPosition();
	core.action=FLY;
	core.speed=unitTerrainMovementSpeed(performance(FLY), owner->map->terrainPropertiesAt(core.posX,core.posY).airSpeedQ8, core.dx != 0 || core.dy != 0);
	claimOccupiedMapSlot();
}

void Unit::handleActionGoingTarget()
{
	assert(!performance(FLY));
	clearOccupiedMapSlot();
	owner->map->pathfindPointToPoint(core.posX, core.posY, core.targetX, core.targetY, &core.dx, &core.dy, swimClass(), owner->me, GOING_TARGET_MAX_PATH_LENGTH);
	directionFromDxDy();
	wrapPosition();

	if(core.dx == 0 && core.dy == 0)
		if (!performance(FLY)) owner->map->markImmobileUnit(core.posX, core.posY, owner->teamNumber);

	selectPreferredGroundMovement();
	core.speed=unitTerrainMovementSpeed(performance(core.action), core.action == FLY
        ? owner->map->terrainPropertiesAt(core.posX,core.posY).airSpeedQ8
        : owner->map->terrainPropertiesAt(core.posX,core.posY).groundSpeedQ8, core.dx != 0 || core.dy != 0);
	claimOccupiedMapSlot();
}

void Unit::handleActionFlyingTarget()
{
	// No assert(getAirUnit==NOGUID) on the final claim — two flyers can
	// converge on the same target tile, so the destination slot may be
	// non-empty. Direct setAirUnit calls preserve that, unlike claimOccupiedMapSlot().
	owner->map->setAirUnit(core.posX, core.posY, NOGUID);

	flyToTarget();

	wrapPosition();

	core.action=FLY;
	core.speed=unitTerrainMovementSpeed(performance(FLY), owner->map->terrainPropertiesAt(core.posX,core.posY).airSpeedQ8, core.dx != 0 || core.dy != 0);

	owner->map->setAirUnit(core.posX, core.posY, core.gid);
}

void Unit::handleActionGoingDxDy()
{
	clearOccupiedMapSlot();

	directionFromDxDy();

	wrapPosition();

	if(core.dx == 0 && core.dy == 0)
		if (!performance(FLY)) owner->map->markImmobileUnit(core.posX, core.posY, owner->teamNumber);

	selectPreferredMovement();
	core.speed=unitTerrainMovementSpeed(performance(core.action), core.action == FLY
        ? owner->map->terrainPropertiesAt(core.posX,core.posY).airSpeedQ8
        : owner->map->terrainPropertiesAt(core.posX,core.posY).groundSpeedQ8, core.dx != 0 || core.dy != 0);

	claimOccupiedMapSlot();

	if (verbose)
		printf("guid=(%d) MOV_GOING_DX_DY d=(%d, %d; %d).\n", core.gid, core.direction, core.dx, core.dy);
}

void Unit::handleActionEnteringBuilding()
{
	// NOTE : this is a hack : We don't delete the unit on the map
	// because we have to draw it while it is entering.
	wrapPosition();
	directionFromDxDy();
	selectPreferredMovement();
	core.speed=unitTerrainMovementSpeed(performance(core.action), core.action == FLY
        ? owner->map->terrainPropertiesAt(core.posX,core.posY).airSpeedQ8
        : owner->map->terrainPropertiesAt(core.posX,core.posY).groundSpeedQ8, core.dx != 0 || core.dy != 0);
}

void Unit::handleActionExitingBuilding()
{
	directionFromDxDy();
	selectPreferredMovement();
	core.speed=unitTerrainMovementSpeed(performance(core.action), core.action == FLY
        ? owner->map->terrainPropertiesAt(core.posX,core.posY).airSpeedQ8
        : owner->map->terrainPropertiesAt(core.posX,core.posY).groundSpeedQ8, core.dx != 0 || core.dy != 0);
	claimOccupiedMapSlot();
}

void Unit::handleActionFilling()
{
	if (!performance(FLY)) owner->map->markImmobileUnit(core.posX, core.posY, owner->teamNumber);
	directionFromDxDy();
	core.action=BUILD;
	core.speed=performance(core.action);
}

void Unit::handleActionAttackingTarget()
{
	if (!performance(FLY)) owner->map->markImmobileUnit(core.posX, core.posY, owner->teamNumber);
	directionFromDxDy();
	core.action=ATTACK_SPEED;
	core.speed=performance(core.action);
}

void Unit::handleActionHarvesting()
{
	if (!performance(FLY)) owner->map->markImmobileUnit(core.posX, core.posY, owner->teamNumber);
	directionFromDxDy();
	core.action=HARVEST;
	core.speed=performance(core.action);
	assert(core.speed!=0);
}

void Unit::handleAction(void)
{
	owner->map->clearImmobileUnit(core.posX, core.posY);
	switch (core.movement)
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
