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

	switch (core.displacement)
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
		|| (core.displacement==DIS_RANDOM && hasCapability(UnitRuntimeTraits::ClearIdle)))
		&& hasCapability(UnitRuntimeTraits::Clear) && performance(HARVEST) &&
		core.medical == MED_FREE &&
		(core.displacement == DIS_RANDOM
		|| core.displacement == DIS_GOING_TO_FLAG
		|| core.displacement == DIS_GOING_TO_RESOURCE
		|| core.displacement == DIS_GOING_TO_BUILDING))
	{
		Map *map = owner->map;
		// The preceding harvest action has finished; settle its resource removal
		// and self-damage before claiming the next adjacent painted target.
		if (core.movement == MOV_HARVESTING)
		{
			const Resource clearedBefore = map->getResource(core.posX + core.dx, core.posY + core.dy);
			recordLethalDamage(race->getUnitType(core.typeNum, detail.level[HARVEST])->harvestDamage,
							   GameplayMeasurements::CLEARING);
			map->decResource(core.posX + core.dx, core.posY + core.dy);
			if (clearedBefore != map->getResource(core.posX + core.dx, core.posY + core.dy))
				++owner->stats.measurements.cleared[materialIndex(
					map->resourcePropertiesByIndex(clearedBefore.type).primaryMaterial)];
			core.hp=int(std::max<Sint64>(INT_MIN,Sint64(core.hp)-race->getUnitType(core.typeNum, detail.level[HARVEST])->harvestDamage));
		}
		const bool farmAreas = map->farmAreasEnabled();
		for (int tdx = -1; tdx <= 1; tdx++)
			for (int tdy = -1; tdy <= 1; tdy++)
			{
				int x = (core.posX + tdx) & map->wMask;
				int y = (core.posY + tdy) & map->hMask;
				const size_t index = (y << map->wDec) + x;
				if (map->isClearingTarget(index, owner->me, farmAreas)
					&& !(map->areaState()[index].forbidden & owner->me))
				{
					owner->map->setClearingAreaClaimed(core.posX+tdx, core.posY+tdy, owner->teamNumber, core.gid);
					previousClearingArea = ClearingAreaClaim{
						static_cast<Uint32>((core.posX+tdx) & map->wMask),
						static_cast<Uint32>((core.posY+tdy) & map->hMask)
					};
					core.dx = tdx;
					core.dy = tdy;
					core.movement = MOV_HARVESTING;
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
		core.movement=MOV_GOING_DX_DY;
		int bposX=attachedBuilding->posX;
		int bposY=attachedBuilding->posY;

		int ldx=bposX-core.posX;
		int ldy=bposY-core.posY;
		int cdx, cdy;
		simplifyDirection(ldx, ldy, &cdx, &cdy);

		core.dx=-cdy;
		core.dy=cdx;
		if (!owner->map->isMapDiscovered(core.posX+4*cdx, core.posY+4*cdy, owner->sharedVisionOther))
		{
			core.dx=cdx;
			core.dy=cdy;
		}
	}
	else if ((core.movement!=MOV_GOING_DX_DY)||((detail.entityRandom.nextU32()&0xFF)<0xEF))
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
			tab[i] = owner->map->getExplored(core.posX + dxTab[i], core.posY + dyTab[i], owner->teamNumber);
			//also move around enemy towers:
			if(locationIsInEnemyGuardTowerRange(core.posX + dxTab[i], core.posY + dyTab[i]))tab[i]=1;
		}
		for (int di = 0; di < 8; di++)
		{
			int d = (di + core.direction + 4) % 8;
			//Move in a direction in which you circle counter-clockwise
			//about explored area, while exploring.
			if ((tab[d] > 0) && (tab[(d + 1) % 8] == 0) && (tab[(d + 2) % 8] == 0))
			{
				core.direction = (d + 1) % 8;
				dxDyFromDirection();
				core.movement = MOV_GOING_DX_DY;
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
					scoreX += owner->map->getExplored(core.posX - (4*range), core.posY + (delta*range), owner->teamNumber);
					scoreX -= owner->map->getExplored(core.posX + (4*range), core.posY + (delta*range), owner->teamNumber);
					scoreY += owner->map->getExplored(core.posX + (delta*range), core.posY - (4*range), owner->teamNumber);
					scoreY -= owner->map->getExplored(core.posX + (delta*range), core.posY + (4*range), owner->teamNumber);
                                              }
                                          }
			int cdx, cdy;
			simplifyDirection(scoreX, scoreY, &cdx, &cdy);

			if (cdx == 0 && cdy == 0)
				core.movement = MOV_RANDOM_FLY;
			else
			{
				core.dx = cdx;
				core.dy = cdy;
				directionFromDxDy();
				core.movement = MOV_GOING_DX_DY;
			}
		}
	}
	const bool flying=performance(FLY)!=0;
	const bool canWalk=performance(WALK)>0;
	const bool canSwim=performance(SWIM)>0;
	if (flying) {
		if (core.movement!=MOV_GOING_DX_DY || !owner->map->isFreeForAirUnit(core.posX+core.dx,core.posY+core.dy)) core.movement=MOV_RANDOM_FLY;
	} else if (core.movement!=MOV_GOING_DX_DY
		|| (!canWalk && (!canSwim || !owner->map->terrainPropertiesAt(core.posX+core.dx,core.posY+core.dy).swimmable))
		|| !owner->map->isFreeForGroundUnit(core.posX+core.dx,core.posY+core.dy,canSwim,owner->me)) core.movement=MOV_RANDOM_GROUND;
}

void Unit::handleMovementAttackingAround()
{
	int quality=INT_MAX; // Smaller is better.
	core.movement=MOV_RANDOM_GROUND;

	if (hasCapability(UnitRuntimeTraits::Melee) && performance(ATTACK_SPEED)) {
	///Don't change targets if we still have a valid target
	if (auto off = owner->map->doesUnitTouchEnemy(this))
	{
		core.dx = off->dx;
		core.dy = off->dy;
		core.targetX = core.posX+core.dx;
		core.targetY = core.posY+core.dy;
		core.movement=MOV_ATTACKING_TARGET;
	}
	else
	{
		// we look for the best target to attack around us
		const int radius=runtimeTraits().attackSearchRadius;
		for (int x=-radius; x<=radius; x++)
		{
			for (int y=-radius; y<=radius; y++)
			{
				if (owner->map->isFOWDiscovered(core.posX+x, core.posY+y, owner->sharedVisionOther))
				{
					if (attachedBuilding &&
						owner->map->warpDistSquare(core.posX+x, core.posY+y, attachedBuilding->posX, attachedBuilding->posY)
							>((int)attachedBuilding->unitStayRange*(int)attachedBuilding->unitStayRange))
						continue;
					Uint16 gid;
					gid=owner->map->getBuilding(core.posX+x, core.posY+y);
					if (gid!=NOGBID)
					{
						int team=Building::GIDtoTeam(gid);
						if (owner->attackableTeams() & (Team::teamNumberToMask(team)))
						{
							int id=Building::GIDtoID(gid);
							int newQuality=((x*x+y*y)<<Q8_FIXED_POINT_SHIFT);
							Building *b=owner->game->teams[team]->myBuildings[id];
							int shootDamage=b->runtime->damage(core.typeNum);
							newQuality/=(1+shootDamage);
							tryAcquireAttackTarget(x, y, newQuality, quality);
						}
					}
					gid=owner->map->getGroundUnit(core.posX+x, core.posY+y);
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
	if (core.movement == MOV_RANDOM_GROUND)
	{
		if (performance(FLY) && !attachedBuilding && !owner->map->isGuardArea(core.posX,core.posY,owner->me)) {
			core.validTarget=findAirGuardDestination(&core.targetX,&core.targetY);
			core.movement=core.validTarget?MOV_FLYING_TARGET:MOV_RANDOM_FLY;
			return;
		}
		// Under guard-area balancing a crowded area's tiles sit below the goal
		// value, so "inside" is the painted bit there.
		const bool balancing = owner->game->gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing);
		auto insideGuardArea = [&](int x, int y)
		{
			if (balancing || performance(FLY))
				return owner->map->isGuardArea(x, y, owner->me);
			return owner->map->getGuardAreasGradient(owner->teamNumber, swimClass())[owner->map->coordToIndex(x, y)] == GRADIENT_AT_GOAL;
		};
		if (!performance(FLY) && !attachedBuilding && owner->map->pathfindArea(detail.entityRandom, Map::AreaKind::Guard, owner->teamNumber, swimClass(), core.posX, core.posY, &core.dx, &core.dy))
		{
			directionFromDxDy();
			core.movement = MOV_GOING_DX_DY;
			// get the target position of guard area for display
			owner->map->getGlobalGradientDestination(owner->map->getGuardAreasGradient(owner->teamNumber, swimClass()), core.posX, core.posY, &core.targetX, &core.targetY);
			core.validTarget=true;
		}
		else if (attachedBuilding || insideGuardArea(core.posX, core.posY))
		{
			// are we into the guard area or war flag, and we have to go to the least known area.
			int bestExplored = 3*EXPLORED_FRESH;
			int bestDirection = -1;
			const bool flying=performance(FLY)!=0;
			const bool canWalk=performance(WALK)>0;
			const bool canSwim=performance(SWIM)>0;
			for (int di = 0; di < 8; di++)
			{
				int d = (core.direction + di) & UNIT_DIRECTION_MASK;
				int cdx, cdy;
				dxDyFromDirection(d, &cdx, &cdy);
				if (flying ? !owner->map->isFreeForAirUnit(core.posX+cdx,core.posY+cdy)
					: ((!canWalk && (!canSwim || !owner->map->terrainPropertiesAt(core.posX+cdx,core.posY+cdy).swimmable))
						|| !owner->map->isFreeForGroundUnit(core.posX+cdx,core.posY+cdy,canSwim,owner->me)))
					continue;
				if (attachedBuilding)
				{
					if (owner->map->warpDistSquare(core.posX + cdx, core.posY + cdy, attachedBuilding->posX, attachedBuilding->posY)
						> ((int)attachedBuilding->unitStayRange * (int)attachedBuilding->unitStayRange))
						continue;
				}
				else
				{
					if (!insideGuardArea(core.posX + cdx, core.posY + cdy))
						continue;
				}
				int explored = owner->map->getExplored(core.posX + 2*cdx, core.posY + 2*cdy, owner->teamNumber);
				explored += owner->map->getExplored(core.posX + 2*cdx - cdy, core.posY + 2*cdy + cdx, owner->teamNumber);
				explored += owner->map->getExplored(core.posX + 2*cdx + cdy, core.posY + 2*cdy - cdx, owner->teamNumber);
				if (bestExplored > explored)
				{
					bestExplored = explored;
					bestDirection = d;
				}
			}
			if (bestDirection >= 0)
			{
				core.direction = bestDirection;
				dxDyFromDirection();
				core.movement = MOV_GOING_DX_DY;
				core.validTarget = false;
			}
			else
			{
				core.movement = MOV_RANDOM_GROUND;
				core.validTarget = false;
			}
		}
		else
		{
			// this case happens when no movement could be found because of busy places or because we are in a guard area or because there is no guard area
			core.movement = MOV_RANDOM_GROUND;
			core.validTarget = false;
		}
	}
	if (performance(FLY) && core.movement==MOV_RANDOM_GROUND) core.movement=MOV_RANDOM_FLY;
}

void Unit::tryAcquireAttackTarget(int x, int y, int newQuality, int& quality)
{
	if (newQuality >= quality)
		return;
	bool pathfind = performance(FLY) ? owner->map->pathfindAirPointToPoint(core.posX,core.posY,core.posX+x,core.posY+y,&core.dx,&core.dy) : owner->map->pathfindPointToPoint(core.posX, core.posY, core.posX+x, core.posY+y, &core.dx, &core.dy, swimClass(), owner->me, GOING_TARGET_MAX_PATH_LENGTH);
	if (!pathfind)
		return;
	if (abs(x)<=1 && abs(y)<=1)
	{
		core.movement=MOV_ATTACKING_TARGET;
		core.dx=x;
		core.dy=y;
	}
	else
	{
		core.movement=performance(FLY)?MOV_FLYING_TARGET:MOV_GOING_TARGET;
	}
	core.targetX=core.posX+x;
	core.targetY=core.posY+y;
	core.validTarget=true;
	quality=newQuality;
}

void Unit::handleMovementClearingResources()
{
	if (!hasCapability(UnitRuntimeTraits::Clear) || !performance(HARVEST)) {
		stopAttachedForBuilding(false); core.movement=performance(FLY)?MOV_RANDOM_FLY:MOV_RANDOM_GROUND; return;
	}
	Map *map=owner->map;
	if (core.movement==MOV_HARVESTING)
	{
		const Resource clearedBefore = map->getResource(core.posX + core.dx, core.posY + core.dy);
		recordLethalDamage(race->getUnitType(core.typeNum, detail.level[HARVEST])->harvestDamage,
						   GameplayMeasurements::CLEARING);
		map->decResource(core.posX + core.dx, core.posY + core.dy);
		if (clearedBefore != map->getResource(core.posX + core.dx, core.posY + core.dy))
			++owner->stats.measurements.cleared[materialIndex(
				map->resourcePropertiesByIndex(clearedBefore.type).primaryMaterial)];
		core.hp=int(std::max<Sint64>(INT_MIN,Sint64(core.hp)-race->getUnitType(core.typeNum, detail.level[HARVEST])->harvestDamage));
	}

	int bx=attachedBuilding->posX;
	int by=attachedBuilding->posY;
	int usr=attachedBuilding->unitStayRange;
	int usr2=usr*usr;
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
		{
			int x=core.posX+tdx;
			int y=core.posY+tdy;
			if (map->warpDistSquare(x, y, bx, by)<=usr2 && map->isClearableResourceForMaterials(x, y, attachedBuilding->clearingMaterials) && !(owner->map->isForbidden(x, y, owner->me)))
			{
				core.dx=tdx;
				core.dy=tdy;
				core.movement=MOV_HARVESTING;
				return;
			}
		}
	if (performance(FLY)) {
		int distance;
		if (findAirClearingDestination(attachedBuilding,&core.targetX,&core.targetY,&distance)) {
			core.validTarget=true; core.movement=MOV_FLYING_TARGET;
		} else { stopAttachedForBuilding(false); core.movement=MOV_RANDOM_FLY; }
		return;
	}
	assert(attachedBuilding);
	if (map->pathfindBuilding(detail.entityRandom, attachedBuilding, swimClass(), core.posX, core.posY, &core.dx, &core.dy, BuildingRoute::Clearing))
	{
		directionFromDxDy();
		core.movement=MOV_GOING_DX_DY;
	}
	else if (attachedBuilding->anyResourceToClear[swimAccessVariant(swimClass())]==2)
	{
		stopAttachedForBuilding(false);
		core.movement=MOV_RANDOM_GROUND;
	}
	else
		core.movement=MOV_RANDOM_GROUND;
}

void Unit::handleMovementRandom()
{
	Map *map=owner->map;
	std::optional<Offset> enemyOff;
	if ((hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies) || (hasCapability(UnitRuntimeTraits::Melee) && hasCapability(UnitRuntimeTraits::CombatInterrupt))) && performance(ATTACK_SPEED) && core.medical==MED_FREE)
		enemyOff = map->doesUnitTouchEnemy(this);
	if (enemyOff)
	{
		core.dx = enemyOff->dx;
		core.dy = enemyOff->dy;
		core.movement=MOV_ATTACKING_TARGET;
	}
	else if (performance(FLY)) {
		int distance;
		if (core.medical==MED_FREE && hasCapability(UnitRuntimeTraits::ClearIdle) && hasCapability(UnitRuntimeTraits::Clear)
			&& performance(HARVEST) && findAirClearingDestination(nullptr,&core.targetX,&core.targetY,&distance)
			&& distance<foodStepsLeft(detail.trigHungry)) {
			const int claimant=map->isClearingAreaClaimed(core.targetX,core.targetY,owner->teamNumber);
			if (claimant!=NOGUID && claimant!=core.gid) {
				if (Unit* other=owner->myUnits[GIDtoID(claimant)]) { other->previousClearingArea.reset(); other->previousClearingAreaDistance=UNIT_CLEAR_AREA_DISTANCE_NONE; }
			}
			previousClearingArea=ClearingAreaClaim{Uint32(core.targetX),Uint32(core.targetY)};
			previousClearingAreaDistance=distance;
			map->setClearingAreaClaimed(core.targetX,core.targetY,owner->teamNumber,core.gid);
			core.validTarget=true; core.movement=MOV_FLYING_TARGET;
		} else core.movement=MOV_RANDOM_FLY;
	}
	else if (map->getForbidden(core.posX, core.posY)&owner->me)
	{
		if (map->pathfindForbidden(NULL, owner->teamNumber, swimClass(), core.posX, core.posY, &core.dx, &core.dy))
			directionFromDxDy();
		else
		{
			core.dx=0;
			core.dy=0;
			core.direction=UNIT_DIRECTION_NONE;
		}
		core.movement=MOV_GOING_DX_DY;
	}
	else if(hasCapability(UnitRuntimeTraits::ClearIdle) && hasCapability(UnitRuntimeTraits::Clear) && performance(HARVEST))
	{
		// g==0: on obstacle. g==1: no clearing area reachable from this cell.
		// Both cases mean "nothing found".
		const Uint16 *clearAreasGradient = owner->map->getClearAreasGradient(owner->teamNumber, swimClass());
		Uint16 g = clearAreasGradient[owner->map->coordToIndex(core.posX, core.posY)];
		int distance = gradientTiles(g);
		if(g > GRADIENT_UNREACHABLE && distance < foodStepsLeft(detail.trigHungry) && core.medical == MED_FREE)
		{
			int tempTargetX, tempTargetY;
			bool path = owner->map->getGlobalGradientDestination(clearAreasGradient, core.posX, core.posY, &tempTargetX, &tempTargetY);
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
				core.dx=0;
				core.dy=0;
				owner->map->pathfindArea(detail.entityRandom, Map::AreaKind::Clear, owner->teamNumber, swimClass(), core.posX, core.posY, &core.dx, &core.dy);

				core.targetX = tempTargetX;
				core.targetY = tempTargetY;
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
				core.movement = MOV_GOING_DX_DY;
				owner->map->setClearingAreaClaimed(core.targetX, core.targetY, owner->teamNumber, core.gid);
				core.validTarget=true;
			}
			else
				core.movement=MOV_RANDOM_GROUND;
		}
		else
			core.movement=MOV_RANDOM_GROUND;
	}
	else
		core.movement=MOV_RANDOM_GROUND;
}

void Unit::handleMovementGoingToFlagOrBuilding()
{
	Map *map=owner->map;

	std::optional<Offset> enemyOff;
	if ((hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies) || (hasCapability(UnitRuntimeTraits::Melee) && hasCapability(UnitRuntimeTraits::CombatInterrupt))) && performance(ATTACK_SPEED) && core.medical==MED_FREE)
		enemyOff = map->doesUnitTouchEnemy(this);
	if (enemyOff)
	{
		core.dx = enemyOff->dx;
		core.dy = enemyOff->dy;
		core.movement=MOV_ATTACKING_TARGET;
	}
	else if (performance(FLY))
	{
		core.movement=MOV_FLYING_TARGET;
	}
	else if (map->pathfindBuilding(detail.entityRandom, targetBuilding, swimClass(), core.posX, core.posY, &core.dx, &core.dy,
		core.activity == ACT_FLAG ? (core.jobPurpose == UnitJobPurpose::Clear ? BuildingRoute::Clearing : BuildingRoute::Combat) : BuildingRoute::Footprint))
	{
		core.movement=MOV_GOING_DX_DY;
	}
	else
	{
		stopAttachedForBuilding(true);
		core.movement=MOV_RANDOM_GROUND;
	}
}

void Unit::handleMovementEnteringBuilding()
{
	core.movement=MOV_ENTERING_BUILDING;
}

void Unit::handleMovementInside()
{
	core.movement=MOV_INSIDE;
}

void Unit::handleMovementExitingBuilding()
{
	bool exitFound;
	if (performance(FLY))
		exitFound=attachedBuilding->findAirExit(&core.posX, &core.posY, &core.dx, &core.dy);
	else
		exitFound=attachedBuilding->findGroundExit(&core.posX, &core.posY, &core.dx, &core.dy, performance(SWIM),performance(WALK)>0);
	if (exitFound)
	{
		core.jobPurpose=UnitJobPurpose::None;
		core.activity=ACT_RANDOM;
		core.movement=MOV_EXITING_BUILDING;
		attachedBuilding->removeUnitFromInside(this);
		attachedBuilding->updateConstructionState();
		attachedBuilding=NULL;
		setTargetBuilding(NULL);
		assert(ownExchangeBuilding==NULL);
		assert(detail.needToRecheckMedical);
	}
	else
	{
		core.movement=MOV_INSIDE;
	}
}

void Unit::handleMovementGoingToResource()
{
	Map *map=owner->map;
	if (performance(FLY)) {
		int distance;
		if (!core.validTarget || (!map->isMaterialTakeableSlot(core.targetX,core.targetY,core.destinationPurpose) && map->getBuilding(core.targetX,core.targetY)==NOGBID))
			if (!findMaterialDestination(core.destinationPurpose,&core.targetX,&core.targetY,&distance,attachedBuilding && attachedBuilding->fetchesFromMarkets(),attachedBuilding)) {
				stopAttachedForBuilding(false); core.movement=MOV_RANDOM_FLY; return;
			}
		core.validTarget=true; core.movement=MOV_FLYING_TARGET; return;
	}
	int teamNumber=owner->teamNumber;
	int swim=swimClass();
	bool stopWork;
	const bool withMarkets=attachedBuilding && attachedBuilding->fetchesFromMarkets();
	if (map->pathfindMaterial(detail.entityRandom, teamNumber, core.destinationPurpose, swim, core.posX, core.posY, &core.dx, &core.dy, &stopWork, attachedBuilding, withMarkets))
	{
		directionFromDxDy();
		core.movement=MOV_GOING_DX_DY;
		// targetX/Y (also the debug path line, hotkey T) were set once, by
		// ascending a gradient, when the fetch task started. pathfindMaterial
		// above re-reads the resource gradient that governs the step fresh
		// every action, and that field can be rebuilt while the unit is still
		// walking. Re-ascend from here whenever the stored target has stopped
		// being a peak of that same gradient; isGradientPeak is a cheap check
		// to run every action, the ascent itself only when it actually goes stale.
		const Uint16 *gradient = map->getMaterialGradientSlot(teamNumber, core.destinationPurpose, swim, withMarkets, attachedBuilding);
		if (!map->isGradientPeak(gradient, core.targetX, core.targetY))
			map->getGlobalGradientDestination(gradient, core.posX, core.posY, &core.targetX, &core.targetY);
	}
	else
	{
		if (stopWork)
			stopAttachedForBuilding(false);
		core.movement=MOV_RANDOM_GROUND;
	}
}

void Unit::handleMovementHarvesting()
{
	core.movement=MOV_HARVESTING;
}

void Unit::handleMovementFillingBuilding()
{
	core.movement=MOV_FILLING;
}
