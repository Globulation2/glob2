// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "Race.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"

#include "Building.h"

#include "FixedPoint.h"
#include "MapInternal.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include <climits>

void Unit::handleMovement(void)
{
	// Release any clearing-area claim from a prior tick before this unit decides
	// what to do this tick. Note: distance is intentionally NOT reset here — it
	// must survive the per-tick reset (see Unit.h declaration comment).
	if (previousClearingArea)
	{
		owner->map->setClearingAreaUnclaimed(previousClearingArea->x, previousClearingArea->y, owner->teamNumber);
		previousClearingArea.reset();
	}

	if (tryClaimClearingAreaForHarvesting())
		return;

	switch (displacement)
	{
		case DIS_REMOVING_BLACK_AROUND:
			handleMovementRemovingBlackAround();
			break;
		case DIS_ATTACKING_AROUND:
			handleMovementAttackingAround();
			break;
		case DIS_CLEARING_RESOURCES:
			handleMovementClearingResources();
			break;
		case DIS_RANDOM:
			handleMovementRandom();
			break;
		case DIS_GOING_TO_FLAG:
		case DIS_GOING_TO_BUILDING:
			handleMovementGoingToFlagOrBuilding();
			break;
		case DIS_ENTERING_BUILDING:
			handleMovementEnteringBuilding();
			break;
		case DIS_INSIDE:
			handleMovementInside();
			break;
		case DIS_EXITING_BUILDING:
			handleMovementExitingBuilding();
			break;
		case DIS_GOING_TO_RESOURCE:
			handleMovementGoingToResource();
			break;
		case DIS_HARVESTING:
			handleMovementHarvesting();
			break;
		case DIS_FILLING_BUILDING:
			handleMovementFillingBuilding();
			break;
		default:
			assert(false);
			break;
	}
}

bool Unit::tryClaimClearingAreaForHarvesting()
{
	// Idle painted-area clearing completes the same harvest action as a local
	// interruption, but does not require permission to interrupt another job.
	if ((hasCapability(UnitRuntimeTraits::AdjacentClearInterrupt)
		|| (displacement==DIS_RANDOM && hasCapability(UnitRuntimeTraits::ClearIdle)))
		&& hasCapability(UnitRuntimeTraits::Clear) && performance[HARVEST] &&
		medical == MED_FREE &&
		(displacement == DIS_RANDOM
		|| displacement == DIS_GOING_TO_FLAG
		|| displacement == DIS_GOING_TO_RESOURCE
		|| displacement == DIS_GOING_TO_BUILDING))
	{
		Map *map = owner->map;
		// The preceding harvest action has finished; settle its resource removal
		// and self-damage before claiming the next adjacent painted target.
		if (movement == MOV_HARVESTING)
		{
			const Resource clearedBefore = map->getResource(posX + dx, posY + dy);
			recordLethalDamage(race->getUnitType(typeNum, level[HARVEST])->harvestDamage,
							   GameplayMeasurements::CLEARING);
			map->decResource(posX + dx, posY + dy);
			if (clearedBefore != map->getResource(posX + dx, posY + dy))
				++owner->stats.events().cleared[materialIndex(
					map->resourcePropertiesByIndex(clearedBefore.type).primaryMaterial)];
			hp=int(std::max<Sint64>(INT_MIN,Sint64(hp)-race->getUnitType(typeNum, level[HARVEST])->harvestDamage));
		}
		const bool farmAreas = map->farmAreasEnabled();
		for (int tdx = -1; tdx <= 1; tdx++)
			for (int tdy = -1; tdy <= 1; tdy++)
			{
				int x = (posX + tdx) & map->wMask;
				int y = (posY + tdy) & map->hMask;
				const size_t index = (y << map->wDec) + x;
				if (map->isClearingTarget(index, owner->me, farmAreas)
					&& (performance[FLY] || !(map->areaState()[index].forbidden & owner->me)))
				{
					owner->map->setClearingAreaClaimed(posX+tdx, posY+tdy, owner->teamNumber, gid);
					previousClearingArea = ClearingAreaClaim{
						static_cast<Uint32>((posX+tdx) & map->wMask),
						static_cast<Uint32>((posY+tdy) & map->hMask)
					};
					dx = tdx;
					dy = tdy;
					movement = MOV_HARVESTING;
					return true;
				}
			}
	}
	return false;
}

void Unit::handleMovementRemovingBlackAround()
{
	if (attachedBuilding)
	{
		movement=MOV_GOING_DX_DY;
		int bposX=attachedBuilding->posX;
		int bposY=attachedBuilding->posY;

		int ldx=bposX-posX;
		int ldy=bposY-posY;
		int cdx, cdy;
		simplifyDirection(ldx, ldy, &cdx, &cdy);

		dx=-cdy;
		dy=cdx;
		if (!owner->map->isMapDiscovered(posX+4*cdx, posY+4*cdy, owner->sharedVisionOther))
		{
			dx=cdx;
			dy=cdy;
		}
	}
	else if ((movement!=MOV_GOING_DX_DY)||((entityRandom.nextU32()&0xFF)<0xEF))
	{
		// "c" is the center of the unit, "x" are the sample spots:
		// oxoooxo
		//ooooooooo
		//xooooooox
		//ooooooooo
		//oooocoooo
		//ooooooooo
		//xooooooox
		//ooooooooo
		// oxoooxo
		bool found = false;
		const int dxTab[8] = {-4, -2, +2, +4, +4, +2, -2, -4};
		const int dyTab[8] = {-2, -4, -4, -2, +2, +4, +4, +2};
		int tab[8];
		for (int i = 0; i < 8; i++)
		{
			tab[i] = owner->map->getExplored(posX + dxTab[i], posY + dyTab[i], owner->teamNumber);
			//also move around enemy towers:
			if(locationIsInEnemyGuardTowerRange(posX + dxTab[i], posY + dyTab[i]))tab[i]=1;
		}
		for (int di = 0; di < 8; di++)
		{
			int d = (di + direction + 4) % 8;
			//Move in a direction in which you circle counter-clockwise
			//about explored area, while exploring.
			if ((tab[d] > 0) && (tab[(d + 1) % 8] == 0) && (tab[(d + 2) % 8] == 0))
			{
				direction = (d + 1) % 8;
				dxDyFromDirection();
				movement = MOV_GOING_DX_DY;
				found = true;
				break;
			}
		}
		if (!found)
		{
			int scoreX = 0;
			int scoreY = 0;
                                        /* The next line should really be calculated only once per game.  How to do this?  The point is to avoid wrapping around the torus in considering what area is closer to us. */
                                        int maxRange = (std::min(owner->map->getW(), owner->map->getH())) / 2;
                                        /* We sample cells at various
                                           distances to decide in what
                                           direction there is more
                                           unexplored territory. */
                                        for (int range = 1; range <= maxRange; range *= 2)
                                          {
                                            for (int delta = -3; delta <= 3; delta++)
                                              {
					scoreX += owner->map->getExplored(posX - (4*range), posY + (delta*range), owner->teamNumber);
					scoreX -= owner->map->getExplored(posX + (4*range), posY + (delta*range), owner->teamNumber);
					scoreY += owner->map->getExplored(posX + (delta*range), posY - (4*range), owner->teamNumber);
					scoreY -= owner->map->getExplored(posX + (delta*range), posY + (4*range), owner->teamNumber);
                                              }
                                          }
			int cdx, cdy;
			simplifyDirection(scoreX, scoreY, &cdx, &cdy);

			if (cdx == 0 && cdy == 0)
				movement = MOV_RANDOM_FLY;
			else
			{
				dx = cdx;
				dy = cdy;
				directionFromDxDy();
				movement = MOV_GOING_DX_DY;
			}
		}
	}
	const bool flying=performance[FLY]!=0;
	const bool canWalk=performance[WALK]>0;
	const bool canSwim=performance[SWIM]>0;
	if (flying) {
		if (movement!=MOV_GOING_DX_DY || !owner->map->isFreeForMovement(posX+dx,posY+dy,AIR_MOVEMENT_CLASS,owner->me)) movement=MOV_RANDOM_FLY;
	} else if (movement!=MOV_GOING_DX_DY
		|| (!canWalk && (!canSwim || !owner->map->terrainPropertiesAt(posX+dx,posY+dy).swimmable))
		|| !owner->map->isFreeForGroundUnit(posX+dx,posY+dy,canSwim,owner->me)) movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
}

void Unit::handleMovementAttackingAround()
{
	int quality=INT_MAX; // Smaller is better.
	movement=MOV_RANDOM_GROUND;

	if (hasCapability(UnitRuntimeTraits::Melee) && performance[ATTACK_SPEED]) {
	///Don't change targets if we still have a valid target
	if (auto off = owner->map->doesUnitTouchEnemy(this))
	{
		dx = off->dx;
		dy = off->dy;
		targetX = posX+dx;
		targetY = posY+dy;
		movement=MOV_ATTACKING_TARGET;
	}
	else
	{
		// we look for the best target to attack around us
		const int radius=runtimeTraits().attackSearchRadius;
		for (int x=-radius; x<=radius; x++)
		{
			for (int y=-radius; y<=radius; y++)
			{
				if (owner->map->isFOWDiscovered(posX+x, posY+y, owner->sharedVisionOther))
				{
					if (attachedBuilding &&
						owner->map->warpDistSquare(posX+x, posY+y, attachedBuilding->posX, attachedBuilding->posY)
							>((int)attachedBuilding->unitStayRange*(int)attachedBuilding->unitStayRange))
						continue;
					Uint16 gid;
					gid=owner->map->getBuilding(posX+x, posY+y);
					if (gid!=NOGBID)
					{
						int team=Building::GIDtoTeam(gid);
						if (owner->attackableTeams() & (Team::teamNumberToMask(team)))
						{
							int id=Building::GIDtoID(gid);
							int newQuality=((x*x+y*y)<<Q8_FIXED_POINT_SHIFT);
							Building *b=owner->game->teams[team]->myBuildings[id];
							int shootDamage=b->runtime->damage(typeNum);
							newQuality/=(1+shootDamage);
							tryAcquireAttackTarget(x, y, newQuality, quality);
						}
					}
					gid=owner->map->getGroundUnit(posX+x, posY+y);
					if (gid!=NOGUID)
					{
						int team=Unit::GIDtoTeam(gid);
						Uint32 tm=(Team::teamNumberToMask(team));
						if (owner->attackableTeams() & tm)
						{
							int id=Building::GIDtoID(gid);
							Unit *u=owner->game->teams[team]->myUnits[id];
							if (((owner->sharedVisionExchange & tm)==0))
							{
								int attackStrength=u->getRealAttackStrength();
								int newQuality=Uint32((x*x+y*y)<<Q8_FIXED_POINT_SHIFT)/(Uint32(attackStrength)+1u);
								tryAcquireAttackTarget(x, y, newQuality, quality);
							}
						}
					}
				}
			}
		}
	}

	}

	// if we haven't found anything satisfactory, follow guard area gradients
	if (movement == MOV_RANDOM_GROUND)
	{

		// Under guard-area balancing a crowded area's tiles sit below the goal
		// value, so "inside" is the painted bit there.
		const bool balancing = owner->game->gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing);
		auto insideGuardArea = [&](int x, int y)
		{
			if (balancing || performance[FLY])
				return owner->map->isGuardArea(x, y, owner->me);
			return owner->map->getGuardAreasGradient(owner->teamNumber, movementClass())[owner->map->coordToIndex(x, y)] == GRADIENT_AT_GOAL;
		};
		if (!attachedBuilding && performance[FLY] && !owner->map->hasAirRoutingConstraints()
            && !balancing && !insideGuardArea(posX,posY)) {
            const auto* field=owner->map->getGuardAreasGradient(owner->teamNumber,AIR_MOVEMENT_CLASS);
            Sint32 goalX=0,goalY=0;
            setValidTarget(owner->map->getGlobalGradientDestination(field,posX,posY,&goalX,&goalY));
            if (validTarget()) { targetX=goalX; targetY=goalY; }
            movement=validTarget()?MOV_FLYING_TARGET:MOV_RANDOM_FLY;
            return;
        }
		if (!attachedBuilding && findDisplacement([&](int* pathDx, int* pathDy) { return owner->map->pathfindArea(entityRandom, Map::AreaKind::Guard, owner->teamNumber, movementClass(), posX, posY, pathDx, pathDy); }))
		{
			directionFromDxDy();
			movement = MOV_GOING_DX_DY;
			// get the target position of guard area for display
			owner->map->getGlobalGradientDestination(owner->map->getGuardAreasGradient(owner->teamNumber, movementClass()), posX, posY, &targetX, &targetY);
			setValidTarget(true);
		}
		else if (attachedBuilding || insideGuardArea(posX, posY))
		{
			// are we into the guard area or war flag, and we have to go to the least known area.
			int bestExplored = 3*EXPLORED_FRESH;
			int bestDirection = -1;
			const bool flying=performance[FLY]!=0;
			const bool canWalk=performance[WALK]>0;
			const bool canSwim=performance[SWIM]>0;
			for (int di = 0; di < 8; di++)
			{
				int d = (direction + di) & UNIT_DIRECTION_MASK;
				int cdx, cdy;
				dxDyFromDirection(d, &cdx, &cdy);
				if (flying ? !owner->map->isFreeForMovement(posX+cdx,posY+cdy,AIR_MOVEMENT_CLASS,owner->me)
					: ((!canWalk && (!canSwim || !owner->map->terrainPropertiesAt(posX+cdx,posY+cdy).swimmable))
						|| !owner->map->isFreeForGroundUnit(posX+cdx,posY+cdy,canSwim,owner->me)))
					continue;
				if (attachedBuilding)
				{
					if (owner->map->warpDistSquare(posX + cdx, posY + cdy, attachedBuilding->posX, attachedBuilding->posY)
						> ((int)attachedBuilding->unitStayRange * (int)attachedBuilding->unitStayRange))
						continue;
				}
				else
				{
					if (!insideGuardArea(posX + cdx, posY + cdy))
						continue;
				}
				int explored = owner->map->getExplored(posX + 2*cdx, posY + 2*cdy, owner->teamNumber);
				explored += owner->map->getExplored(posX + 2*cdx - cdy, posY + 2*cdy + cdx, owner->teamNumber);
				explored += owner->map->getExplored(posX + 2*cdx + cdy, posY + 2*cdy - cdx, owner->teamNumber);
				if (bestExplored > explored)
				{
					bestExplored = explored;
					bestDirection = d;
				}
			}
			if (bestDirection >= 0)
			{
				direction = bestDirection;
				dxDyFromDirection();
				movement = MOV_GOING_DX_DY;
				setValidTarget(false);
			}
			else
			{
				movement = MOV_RANDOM_GROUND;
				setValidTarget(false);
			}
		}
		else
		{
			// this case happens when no movement could be found because of busy places or because we are in a guard area or because there is no guard area
			movement = MOV_RANDOM_GROUND;
			setValidTarget(false);
		}
	}
	if (performance[FLY] && movement==MOV_RANDOM_GROUND) movement=MOV_RANDOM_FLY;
}

void Unit::tryAcquireAttackTarget(int x, int y, int newQuality, int& quality)
{
	if (newQuality >= quality)
		return;
    bool pathfind = findDisplacement([&](int* pathDx,int* pathDy) {
        return owner->map->pathfindPointToPoint(posX,posY,posX+x,posY+y,pathDx,pathDy,
            movementClass(),owner->me,performance[FLY] ? Map::GRADIENT_COST_LIMIT/GRADIENT_STEP : GOING_TARGET_MAX_PATH_LENGTH);
    });
	if (!pathfind)
		return;
	if (abs(x)<=1 && abs(y)<=1)
	{
		movement=MOV_ATTACKING_TARGET;
		dx=x;
		dy=y;
	}
	else
	{
		movement=performance[FLY]?MOV_FLYING_TARGET:MOV_GOING_TARGET;
	}
	targetX=posX+x;
	targetY=posY+y;
	setValidTarget(true);
	quality=newQuality;
}

void Unit::handleMovementClearingResources()
{
	if (!hasCapability(UnitRuntimeTraits::Clear) || !performance[HARVEST]) {
		stopAttachedForBuilding(false); movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND; return;
	}
	Map *map=owner->map;
	if (movement==MOV_HARVESTING)
	{
		const Resource clearedBefore = map->getResource(posX + dx, posY + dy);
		recordLethalDamage(race->getUnitType(typeNum, level[HARVEST])->harvestDamage,
						   GameplayMeasurements::CLEARING);
		map->decResource(posX + dx, posY + dy);
		if (clearedBefore != map->getResource(posX + dx, posY + dy))
			++owner->stats.events().cleared[materialIndex(
				map->resourcePropertiesByIndex(clearedBefore.type).primaryMaterial)];
		hp=int(std::max<Sint64>(INT_MIN,Sint64(hp)-race->getUnitType(typeNum, level[HARVEST])->harvestDamage));
	}

	int bx=attachedBuilding->posX;
	int by=attachedBuilding->posY;
	int usr=attachedBuilding->unitStayRange;
	int usr2=usr*usr;
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
		{
			int x=posX+tdx;
			int y=posY+tdy;
			if (map->warpDistSquare(x, y, bx, by)<=usr2 && map->isClearableResourceForMaterials(x, y, attachedBuilding->clearingMaterialMask) && (performance[FLY] || !map->isForbidden(x, y, owner->me)))
			{
				dx=tdx;
				dy=tdy;
				movement=MOV_HARVESTING;
				return;
			}
		}

	assert(attachedBuilding);
    if (performance[FLY] && !map->hasAirRoutingConstraints()) {
        bool source=validTarget() && map->warpDistSquare(targetX,targetY,bx,by)<=usr2
            && map->isClearableResourceForMaterials(targetX,targetY,attachedBuilding->clearingMaterialMask);
        if (!source) {
            const auto* field=map->buildingGradient(attachedBuilding,AIR_MOVEMENT_CLASS,BuildingRoute::Clearing);
            source=field && map->getGlobalGradientDestination(field,posX,posY,&targetX,&targetY);
        }
        setValidTarget(source);
        if (source) movement=MOV_FLYING_TARGET;
        else { stopAttachedForBuilding(false); movement=MOV_RANDOM_FLY; }
        return;
    }
	if (findDisplacement([&](int* pathDx, int* pathDy) { return map->pathfindBuilding(entityRandom, attachedBuilding, movementClass(), posX, posY, pathDx, pathDy, BuildingRoute::Clearing); }))
	{
		directionFromDxDy();
		movement=MOV_GOING_DX_DY;
	}
	else if (attachedBuilding->anyResourceToClear[swimAccessVariant(movementClass())]==2)
	{
		stopAttachedForBuilding(false);
		movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
	}
	else
		movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
}

void Unit::handleMovementRandom()
{
	Map *map=owner->map;
	std::optional<Offset> enemyOff;
	if ((hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies) || (hasCapability(UnitRuntimeTraits::Melee) && hasCapability(UnitRuntimeTraits::CombatInterrupt))) && performance[ATTACK_SPEED] && medical==MED_FREE)
		enemyOff = map->doesUnitTouchEnemy(this);
	if (enemyOff)
	{
		dx = enemyOff->dx;
		dy = enemyOff->dy;
		movement=MOV_ATTACKING_TARGET;
	}

	else if (!performance[FLY] && (map->getForbidden(posX, posY)&owner->me))
	{
		if (findDisplacement([&](int* pathDx, int* pathDy) { return map->pathfindForbidden(NULL, owner->teamNumber, movementClass(), posX, posY, pathDx, pathDy); }))
			directionFromDxDy();
		else
		{
			dx=0;
			dy=0;
			direction=UNIT_DIRECTION_NONE;
		}
		movement=MOV_GOING_DX_DY;
	}
	else if(hasCapability(UnitRuntimeTraits::ClearIdle) && hasCapability(UnitRuntimeTraits::Clear) && performance[HARVEST])
	{
		// g==0: on obstacle. g==1: no clearing area reachable from this cell.
		// Both cases mean "nothing found".
		const Uint16 *clearAreasGradient = owner->map->getClearAreasGradient(owner->teamNumber, movementClass());
		Uint16 g = clearAreasGradient[owner->map->coordToIndex(posX, posY)];
		int distance = gradientTiles(g);
		if(g > GRADIENT_UNREACHABLE && distance < foodStepsLeft(trigHungry) && medical == MED_FREE)
		{
			int tempTargetX, tempTargetY;
			bool path = owner->map->getGlobalGradientDestination(clearAreasGradient, posX, posY, &tempTargetX, &tempTargetY);
			int guid = owner->map->isClearingAreaClaimed(tempTargetX, tempTargetY, owner->teamNumber);
			int other_distance = INT_MAX;
			if(guid != NOGUID)
			{
				Unit* unit = owner->myUnits[GIDtoID(guid)];
				if(unit)
					other_distance = unit->previousClearingAreaDistance;
			}
			if(path && distance < other_distance)
			{
				dx=0;
				dy=0;
                const bool direct=performance[FLY] && !map->hasAirRoutingConstraints();
                if (!direct)
                    findDisplacement([&](int* pathDx, int* pathDy) { return owner->map->pathfindArea(entityRandom, Map::AreaKind::Clear, owner->teamNumber, movementClass(), posX, posY, pathDx, pathDy); });

				targetX = tempTargetX;
				targetY = tempTargetY;
				previousClearingArea = ClearingAreaClaim{
					static_cast<Uint32>(tempTargetX),
					static_cast<Uint32>(tempTargetY)
				};
				previousClearingAreaDistance = distance;

				if(guid != NOGUID)
				{
					Unit* unit = owner->myUnits[GIDtoID(guid)];
					if(unit)
					{
						unit->previousClearingArea.reset();
						unit->previousClearingAreaDistance=UNIT_CLEAR_AREA_DISTANCE_NONE;
					}
				}

				//Find clearing resource
				directionFromDxDy();
				movement = MOV_GOING_DX_DY;
				owner->map->setClearingAreaClaimed(targetX, targetY, owner->teamNumber, gid);
				setValidTarget(true);
                if (direct) movement=MOV_FLYING_TARGET;
			}
			else
				movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
		}
		else
			movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
	}
	else
		movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
}

void Unit::handleMovementGoingToFlagOrBuilding()
{
	Map *map=owner->map;

	std::optional<Offset> enemyOff;
	if ((hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies) || (hasCapability(UnitRuntimeTraits::Melee) && hasCapability(UnitRuntimeTraits::CombatInterrupt))) && performance[ATTACK_SPEED] && medical==MED_FREE)
		enemyOff = map->doesUnitTouchEnemy(this);
	if (enemyOff)
	{
		dx = enemyOff->dx;
		dy = enemyOff->dy;
		movement=MOV_ATTACKING_TARGET;
	}

	else if (performance[FLY] && !map->hasAirRoutingConstraints())
    {
        movement=MOV_FLYING_TARGET;
    }
	else if (findDisplacement([&](int* pathDx, int* pathDy) { return map->pathfindBuilding(entityRandom, targetBuilding, movementClass(), posX, posY, pathDx, pathDy,
		activity == ACT_FLAG ? (jobPurpose == UnitJobPurpose::Clear ? BuildingRoute::Clearing : BuildingRoute::Combat) : BuildingRoute::Footprint); }))
	{
		movement=MOV_GOING_DX_DY;
	}
	else
	{
		stopAttachedForBuilding(true);
		movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
	}
}

void Unit::handleMovementEnteringBuilding()
{
	movement=MOV_ENTERING_BUILDING;
}

void Unit::handleMovementInside()
{
	movement=MOV_INSIDE;
}

void Unit::handleMovementExitingBuilding()
{
	bool exitFound;
	if (performance[FLY])
		exitFound=findDisplacement([&](int* pathDx, int* pathDy) { return attachedBuilding->findAirExit(&posX, &posY, pathDx, pathDy); });
	else
		exitFound=findDisplacement([&](int* pathDx, int* pathDy) { return attachedBuilding->findGroundExit(&posX, &posY, pathDx, pathDy, performance[SWIM],performance[WALK]>0); });
	if (exitFound)
	{
		jobPurpose=UnitJobPurpose::None;
		activity=ACT_RANDOM;
		movement=MOV_EXITING_BUILDING;
		attachedBuilding->removeUnitFromInside(this);
		attachedBuilding->updateConstructionState();
		attachedBuilding=NULL;
		setTargetBuilding(NULL);
		assert(ownExchangeBuilding==NULL);
		assert(needToRecheckMedical());
	}
	else
	{
		movement=MOV_INSIDE;
	}
}

void Unit::handleMovementGoingToResource()
{
	Map *map=owner->map;

	if (performance[FLY] && !map->hasAirRoutingConstraints()) {
        // Reuse the shared destination query only when the old source expires;
        // ordinary travel neither follows nor refreshes a movement field.
        const auto sourceAvailable=[&] {
            if (map->isMaterialTakeableSlot(targetX,targetY,destinationPurpose)) return true;
            if (!attachedBuilding) return false;
            const unsigned modes=map->materialSupplyModesSlot(attachedBuilding,destinationPurpose);
            const auto eligible=[&](const Building* supplier) {
                return map->stockSupplierEligibleSlot(supplier,attachedBuilding,destinationPurpose,modes);
            };
            const auto supplier=map->getBuilding(targetX,targetY);
            if (supplier!=NOGBID && Building::GIDtoTeam(supplier)==owner->teamNumber
                && eligible(owner->myBuildings[Building::GIDtoID(supplier)])) return true;
            // Overlay providers have no building tile. Inspect their small
            // instance lists, retaining the shared query's supplier policies.
            const auto overlay=[&](const Building* b) {
                return !b->type->semantics.occupiesGround
                    && ((targetX-b->posX+map->getW()) & (map->getW()-1))<b->type->width
                    && ((targetY-b->posY+map->getH()) & (map->getH()-1))<b->type->height
                    && eligible(b);
            };
            if (modes&1) for (const Building* b:owner->stockSuppliers) if (overlay(b)) return true;
            if (modes&2) for (const Building* b:owner->directStockSuppliers) if (overlay(b)) return true;
            return false;
        };
        if ((!validTarget() || !sourceAvailable()) && !findMaterialDestination(destinationPurpose,&targetX,&targetY,nullptr,
                attachedBuilding && attachedBuilding->fetchesFromMarkets(),attachedBuilding)) {
            stopAttachedForBuilding(false);
            movement=MOV_RANDOM_FLY;
            return;
        }
        // A published field may still point to a depleted source. Wait for its
        // scheduled refresh rather than repeatedly steering toward a dead goal.
        if (!sourceAvailable()) {
            setValidTarget(false);
            movement=MOV_RANDOM_FLY;
            return;
        }
        setValidTarget(true);
        movement=MOV_FLYING_TARGET;
        return;
    }
	int teamNumber=owner->teamNumber;
	int swim=movementClass();
	bool stopWork;
	const bool withMarkets=attachedBuilding && attachedBuilding->fetchesFromMarkets();
	if (findDisplacement([&](int* pathDx, int* pathDy) { return map->pathfindMaterial(entityRandom, teamNumber, destinationPurpose, swim, posX, posY, pathDx, pathDy, &stopWork, attachedBuilding, withMarkets); }))
	{
		directionFromDxDy();
		movement=MOV_GOING_DX_DY;
		// targetX/Y (also the debug path line, hotkey T) were set once, by
		// ascending a gradient, when the fetch task started. pathfindMaterial
		// above re-reads the resource gradient that governs the step fresh
		// every action, and that field can be rebuilt while the unit is still
		// walking. Re-ascend from here whenever the stored target has stopped
		// being a peak of that same gradient; isGradientPeak is a cheap check
		// to run every action, the ascent itself only when it actually goes stale.
		const Uint16 *gradient = map->getMaterialGradientSlot(teamNumber, destinationPurpose, swim, withMarkets, attachedBuilding);
		if (!map->isGradientPeak(gradient, targetX, targetY))
			map->getGlobalGradientDestination(gradient, posX, posY, &targetX, &targetY);
	}
	else
	{
		if (stopWork)
			stopAttachedForBuilding(false);
		movement=performance[FLY]?MOV_RANDOM_FLY:MOV_RANDOM_GROUND;
	}
}

void Unit::handleMovementHarvesting()
{
	movement=MOV_HARVESTING;
}

void Unit::handleMovementFillingBuilding()
{
	movement=MOV_FILLING;
}
