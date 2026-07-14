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
		case DIS_CLEARING_RESSOURCES:
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
		case DIS_GOING_TO_RESSOURCE:
			handleMovementGoingToRessource();
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
	// clearArea code, override behavior locally
	if (typeNum == WORKER &&
		medical == MED_FREE &&
		(displacement == DIS_RANDOM
		|| displacement == DIS_GOING_TO_FLAG
		|| displacement == DIS_GOING_TO_RESSOURCE
		|| displacement == DIS_GOING_TO_BUILDING))
	{
		Map *map = owner->map;
		// TODO : be sure this is the right thing to do and add a decent comment
		if (movement == MOV_HARVESTING)
		{
			map->decRessource(posX + dx, posY + dy);
			hp -= race->getUnitType(typeNum, level[HARVEST])->harvestDamage;
		}
		for (int tdx = -1; tdx <= 1; tdx++)
			for (int tdy = -1; tdy <= 1; tdy++)
			{
				int x = (posX + tdx) & map->wMask;
				int y = (posY + tdy) & map->hMask;
				Case mapCase = map->cases[(y << map->wDec) + x];
				if ((mapCase.clearArea & owner->me)
					&& (mapCase.ressource.type != NO_RES_TYPE)
					&& globalContainer->ressourcesTypes.get(mapCase.ressource.type)->clearable
					&& !(mapCase.forbidden & owner->me))
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
	assert(performance[FLY]);
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
	else if ((movement!=MOV_GOING_DX_DY)||((syncRand()&0xFF)<0xEF))
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
	if (movement!=MOV_GOING_DX_DY || owner->map->getAirUnit(posX+dx, posY+dy)!=NOGUID)
		movement=MOV_RANDOM_FLY;
}

void Unit::handleMovementAttackingAround()
{
	assert(performance[ATTACK_SPEED]);
	int quality=INT_MAX; // Smaller is better.
	movement=MOV_RANDOM_GROUND;

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
		for (int x=-UNIT_ATTACK_SEARCH_RADIUS; x<=UNIT_ATTACK_SEARCH_RADIUS; x++)
		{
			for (int y=-UNIT_ATTACK_SEARCH_RADIUS; y<=UNIT_ATTACK_SEARCH_RADIUS; y++)
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
						if (owner->enemies & (1<<team))
						{
							int id=Building::GIDtoID(gid);
							int newQuality=((x*x+y*y)<<Q8_FIXED_POINT_SHIFT);
							Building *b=owner->game->teams[team]->myBuildings[id];
							BuildingType *bt=b->type;
							int shootDamage=bt->shootDamage;
							newQuality/=(1+shootDamage);
							tryAcquireAttackTarget(x, y, newQuality, quality);
						}
					}
					gid=owner->map->getGroundUnit(posX+x, posY+y);
					if (gid!=NOGUID)
					{
						int team=Unit::GIDtoTeam(gid);
						Uint32 tm=(1<<team);
						if (owner->enemies & tm)
						{
							int id=Building::GIDtoID(gid);
							Unit *u=owner->game->teams[team]->myUnits[id];
							if (((owner->sharedVisionExchange & tm)==0))
							{
								int attackStrength=u->getRealAttackStrength();
								int newQuality=((x*x+y*y)<<Q8_FIXED_POINT_SHIFT)/(1+attackStrength);
								tryAcquireAttackTarget(x, y, newQuality, quality);
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
		if (!attachedBuilding && owner->map->pathfindArea(Map::AreaKind::Guard, owner->teamNumber, (performance[SWIM]>0), posX, posY, &dx, &dy))
		{
			directionFromDxDy();
			movement = MOV_GOING_DX_DY;
			// get the target position of guard area for display
			owner->map->getGlobalGradientDestination(owner->map->guardAreasGradient[owner->teamNumber][performance[SWIM]>0], posX, posY, &targetX, &targetY);
			validTarget=true;
		}
		else if (attachedBuilding || (owner->map->getGuardAreasGradient(posX, posY, performance[SWIM]>0, owner->teamNumber) == GRADIENT_AT_GOAL))
		{
			// are we into the guard area or war flag, and we have to go to the least known area.
			int bestExplored = 3*GRADIENT_AT_GOAL;
			int bestDirection = -1;
			for (int di = 0; di < 8; di++)
			{
				int d = (direction + di) & UNIT_DIRECTION_MASK;
				int cdx, cdy;
				dxDyFromDirection(d, &cdx, &cdy);
				if (!owner->map->isFreeForGroundUnit(posX + cdx, posY + cdy, performance[SWIM]>0, owner->me))
					continue;
				if (attachedBuilding)
				{
					if (owner->map->warpDistSquare(posX + cdx, posY + cdy, attachedBuilding->posX, attachedBuilding->posY)
						> ((int)attachedBuilding->unitStayRange * (int)attachedBuilding->unitStayRange))
						continue;
				}
				else
				{
					if (owner->map->getGuardAreasGradient(posX + cdx, posY + cdy, performance[SWIM]>0, owner->teamNumber) != GRADIENT_AT_GOAL)
						continue;
				}
				Uint8 explored = owner->map->getExplored(posX + 2*cdx, posY + 2*cdy, owner->teamNumber);
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
				validTarget = false;
			}
			else
			{
				movement = MOV_RANDOM_GROUND;
				validTarget = false;
			}
		}
		else
		{
			// this case happens when no movement could be found because of busy places or because we are in a guard area or because there is no guard area
			movement = MOV_RANDOM_GROUND;
			validTarget = false;
		}
	}
}

void Unit::tryAcquireAttackTarget(int x, int y, int newQuality, int& quality)
{
	if (newQuality >= quality)
		return;
	bool pathfind = owner->map->pathfindPointToPoint(posX, posY, posX+x, posY+y, &dx, &dy, (performance[SWIM] > 0 ? true : false), owner->me, GOING_TARGET_MAX_PATH_LENGTH);
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
		movement=MOV_GOING_TARGET;
	}
	targetX=posX+x;
	targetY=posY+y;
	validTarget=true;
	quality=newQuality;
}

void Unit::handleMovementClearingResources()
{
	Map *map=owner->map;
	if (movement==MOV_HARVESTING)
	{
		map->decRessource(posX+dx, posY+dy);
		hp -= race->getUnitType(typeNum, level[HARVEST])->harvestDamage;
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
			if (map->warpDistSquare(x, y, bx, by)<=usr2 && map->isRessourceTakeable(x, y, attachedBuilding->clearingRessources) && !(owner->map->isForbidden(x, y, owner->me)))
			{
				dx=tdx;
				dy=tdy;
				movement=MOV_HARVESTING;
				return;
			}
		}
	bool canSwim=performance[SWIM];
	assert(attachedBuilding);
	if (map->pathfindLocalRessource(attachedBuilding, canSwim, posX, posY, &dx, &dy))
	{
		directionFromDxDy();
		movement=MOV_GOING_DX_DY;
	}
	else if (attachedBuilding->anyRessourceToClear[canSwim]==2)
	{
		stopAttachedForBuilding(false);
		movement=MOV_RANDOM_GROUND;
	}
	else
		movement=MOV_RANDOM_GROUND;
}

void Unit::handleMovementRandom()
{
	Map *map=owner->map;
	std::optional<Offset> enemyOff;
	if (performance[ATTACK_SPEED] && medical==MED_FREE)
		enemyOff = map->doesUnitTouchEnemy(this);
	if (enemyOff)
	{
		dx = enemyOff->dx;
		dy = enemyOff->dy;
		movement=MOV_ATTACKING_TARGET;
	}
	else if (performance[FLY])
		movement=MOV_RANDOM_FLY;
	else if (map->getForbidden(posX, posY)&owner->me)
	{
		if (map->pathfindForbidden(NULL, owner->teamNumber, (performance[SWIM]>0), posX, posY, &dx, &dy))
			directionFromDxDy();
		else
		{
			dx=0;
			dy=0;
			direction=UNIT_DIRECTION_NONE;
		}
		movement=MOV_GOING_DX_DY;
	}
	else if(performance[HARVEST])
	{
		// g==0: on obstacle. g==1: chamfer never propagated here, so no clearing
		// area reachable from this cell. Both cases mean "nothing found".
		Uint8 g = owner->map->getClearingGradient(owner->teamNumber, performance[SWIM]>0, posX, posY);
		int distance = GRADIENT_AT_GOAL - g;
		if(g > GRADIENT_UNREACHABLE && distance < ((hungry-trigHungry) / race->hungryness) && medical == MED_FREE)
		{
			int tempTargetX, tempTargetY;
			bool path = owner->map->getGlobalGradientDestination(owner->map->clearAreasGradient[owner->teamNumber][performance[SWIM]>0], posX, posY, &tempTargetX, &tempTargetY);
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
				owner->map->pathfindArea(Map::AreaKind::Clear, owner->teamNumber, (performance[SWIM]>0), posX, posY, &dx, &dy);

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
				validTarget=true;
			}
			else
				movement=MOV_RANDOM_GROUND;
		}
		else
			movement=MOV_RANDOM_GROUND;
	}
	else
		movement=MOV_RANDOM_GROUND;
}

void Unit::handleMovementGoingToFlagOrBuilding()
{
	Map *map=owner->map;
	bool canSwim=performance[SWIM];

	std::optional<Offset> enemyOff;
	if (performance[ATTACK_SPEED] && medical==MED_FREE)
		enemyOff = map->doesUnitTouchEnemy(this);
	if (enemyOff)
	{
		dx = enemyOff->dx;
		dy = enemyOff->dy;
		movement=MOV_ATTACKING_TARGET;
	}
	else if (performance[FLY])
	{
		movement=MOV_FLYING_TARGET;
	}
	else if (map->pathfindBuilding(targetBuilding, canSwim, posX, posY, &dx, &dy))
	{
		movement=MOV_GOING_DX_DY;
	}
	else
	{
		stopAttachedForBuilding(true);
		movement=MOV_RANDOM_GROUND;
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
		exitFound=attachedBuilding->findAirExit(&posX, &posY, &dx, &dy);
	else
		exitFound=attachedBuilding->findGroundExit(&posX, &posY, &dx, &dy, performance[SWIM]);
	if (exitFound)
	{
		activity=ACT_RANDOM;
		movement=MOV_EXITING_BUILDING;
		attachedBuilding->removeUnitFromInside(this);
		attachedBuilding->updateConstructionState();
		attachedBuilding=NULL;
		setTargetBuilding(NULL);
		assert(ownExchangeBuilding==NULL);
		assert(needToRecheckMedical);
	}
	else
	{
		movement=MOV_INSIDE;
	}
}

void Unit::handleMovementGoingToRessource()
{
	Map *map=owner->map;
	int teamNumber=owner->teamNumber;
	bool canSwim=performance[SWIM]>0;
	bool stopWork;
	if (map->pathfindRessource(teamNumber, destinationPurpose, canSwim, posX, posY, &dx, &dy, &stopWork))
	{
		directionFromDxDy();
		movement=MOV_GOING_DX_DY;
	}
	else
	{
		if (stopWork)
			stopAttachedForBuilding(false);
		movement=MOV_RANDOM_GROUND;
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
