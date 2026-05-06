// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "race.h"
#include "team.h"
#include "Map.h"
#include "Game.h"

#include "Building.h"
#include "Integrity.h"

#include "Utilities.h"
#include "GlobalContainer.h"
#include <Stream.h>
#include <set>
#include <climits>

void Unit::handleMovement(void)
{
	// This variable says whether the unit is going to a clearing area
	if(previousClearingAreaX != static_cast<unsigned int>(-1))
	{
		owner->map->setClearingAreaUnclaimed(previousClearingAreaX, previousClearingAreaY, owner->teamNumber);
		previousClearingAreaX = static_cast<unsigned int>(-1);
		previousClearingAreaY = static_cast<unsigned int>(-1);
	}


	// clearArea code, override behaviour locally
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
					previousClearingAreaX = (posX+tdx)  & map->wMask;
					previousClearingAreaY = (posY+tdy)  & map->hMask;
					dx = tdx;
					dy = tdy;
					movement = MOV_HARVESTING;
					return;
				}
			}
	}

	switch (displacement)
	{
		case DIS_REMOVING_BLACK_AROUND:
		{
			assert(performance[FLY]);
			if (verbose)
				printf("guid=(%d) DIS_REMOVING_BLACK_AROUND\n", gid);
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
				//printf("tab ");
				//for (int i = 0; i < 8; i++)
				//	printf("%3d; ", tab[i]);
				//printf("d=%d\n", direction);
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
                                                /*
                                                fprintf (stderr, "gid = %d; changed direction: direction = %d, dx = %d, dy = %d; tab = {", gid, direction, dx, dy);
                                                for (int i = 0; i < 8; i++) {
                                                  fprintf (stderr, "%d%s", tab[i], ((i < 7) ? ", " : "")); }
                                                fprintf (stderr, "}\n");
                                                */
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
					// fprintf(stderr, "gid = %d, maxRange = %d, score = (%2d, %2d), cd = (%d, %d)\n", gid, maxRange, scoreX, scoreY, cdx, cdy);
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
		break;

		case DIS_ATTACKING_AROUND:
		{
			assert(performance[ATTACK_SPEED]);
			int quality=INT_MAX; // Smaller is better.
			movement=MOV_RANDOM_GROUND;
			if (verbose)
				printf("guid=(%d) selecting movement\n", gid);

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
				Building *tempTargetBuilding=NULL;
				// we look for the best target to attack around us
				for (int x=-8; x<=8; x++)
				{
					for (int y=-8; y<=8; y++)
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
									int newQuality=((x*x+y*y)<<8);
									Building *b=owner->game->teams[team]->myBuildings[id];
									BuildingType *bt=b->type;
									int shootDamage=bt->shootDamage;
									newQuality/=(1+shootDamage);
									if (verbose)
										printf("guid=(%d) warrior found building with newQuality=%d\n", this->gid, newQuality);
									if (newQuality<quality)
									{
										bool pathfind = owner->map->pathfindPointToPoint(posX, posY, posX+x, posY+y, &dx, &dy, (performance[SWIM] > 0 ? true : false), owner->me, 12);
										if(pathfind)
										{
											if (abs(x)<=1 && abs(y)<=1)
											{
												movement=MOV_ATTACKING_TARGET;
												dx=x;
												dy=y;
											}
											else
											{
												movement=MOV_GOING_TARGET;
												tempTargetBuilding=b;
											}
											targetX=posX+x;
											targetY=posY+y;
											validTarget=true;
											quality=newQuality;
										}
									}
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
										int newQuality=((x*x+y*y)<<8)/(1+attackStrength);
										if (verbose)
											printf("guid=(%d) warrior found unit with newQuality=%d\n", this->gid, newQuality);
										if (newQuality<quality)
										{
											bool pathfind = owner->map->pathfindPointToPoint(posX, posY, posX+x, posY+y, &dx, &dy, (performance[SWIM] > 0 ? true : false), owner->me, 12);
											if(pathfind)
											{
												if (abs(x)<=1 && abs(y)<=1)
												{
													movement=MOV_ATTACKING_TARGET;
													dx=x;
													dy=y;
												}
												else
												{
													movement=MOV_GOING_TARGET;
													tempTargetBuilding=NULL;
												}
												targetX=posX+x;
												targetY=posY+y;
												validTarget=true;
												quality=newQuality;
											}
										}
									}
								}
							}
						}
					}
				}
			}

			// if we haven't find anything satisfactory, follow guard area gradients
			if (movement == MOV_RANDOM_GROUND)
			{
				if (!attachedBuilding && owner->map->pathfindGuardArea(owner->teamNumber, (performance[SWIM]>0), posX, posY, &dx, &dy))
				{
					directionFromDxDy();
					movement = MOV_GOING_DX_DY;
					// get the target position of guard area for display
					owner->map->getGlobalGradientDestination(owner->map->guardAreasGradient[owner->teamNumber][performance[SWIM]>0], posX, posY, &targetX, &targetY);
					validTarget=true;
				}
				else if (attachedBuilding || (owner->map->getGuardAreasGradient(posX, posY, performance[SWIM]>0, owner->teamNumber) == 255))
				{
					// are we into the guard area or war flag and we have to go to the least known area.
					int bestExplored = 3*255;
					int bestDirection = -1;
					for (int di = 0; di < 8; di++)
					{
						int d = (direction + di) & 7;
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
							if (owner->map->getGuardAreasGradient(posX + cdx, posY + cdy, performance[SWIM]>0, owner->teamNumber) != 255)
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
		break;

		case DIS_CLEARING_RESSOURCES:
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
		break;

		case DIS_RANDOM:
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
				if (map->pathfindForbidden(NULL, owner->teamNumber, (performance[SWIM]>0), posX, posY, &dx, &dy, verbose))
					directionFromDxDy();
				else
				{
					dx=0;
					dy=0;
					direction=8;
				}
				movement=MOV_GOING_DX_DY;
			}
			else if(performance[HARVEST])
			{
				///Value of 254 means nothing found
				int distance = 255-owner->map->getClearingGradient(owner->teamNumber,performance[SWIM]>0, posX, posY);
				if(distance < ((hungry-trigHungry) / race->hungryness) && distance < 254 && medical == MED_FREE)
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
						owner->map->pathfindClearArea(owner->teamNumber, (performance[SWIM]>0), posX, posY, &dx, &dy);

						targetX = tempTargetX;
						targetY = tempTargetY;
						previousClearingAreaX = tempTargetX;
						previousClearingAreaY = tempTargetY;
						previousClearingAreaDistance = distance;

						if(guid != NOGUID)
						{
							Unit* unit = owner->myUnits[GIDtoID(guid)];
							if(unit)
							{
								unit->previousClearingAreaX=static_cast<unsigned int>(-1);
								unit->previousClearingAreaY=static_cast<unsigned int>(-1);
								unit->previousClearingAreaDistance=static_cast<unsigned int>(-1);
							}
						}

						//Find clearing ressource
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
		break;

		case DIS_GOING_TO_FLAG:
		case DIS_GOING_TO_BUILDING:
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
			else if (map->pathfindBuilding(targetBuilding, canSwim, posX, posY, &dx, &dy, verbose))
			{
				if (verbose)
					printf("guid=(%d) Unit found path b pos=(%d, %d) to building %d, d=(%d, %d)\n", gid, posX, posY, attachedBuilding->gid, dx, dy);
				movement=MOV_GOING_DX_DY;
			}
			else
			{
				if (verbose)
					printf("guid=(%d) Unit failed path b pos=(%d, %d) to building %d, d=(%d, %d)\n", gid, posX, posY, attachedBuilding->gid, dx, dy);
				stopAttachedForBuilding(true);
				movement=MOV_RANDOM_GROUND;
			}
		}
		break;

		case DIS_ENTERING_BUILDING:
		{
			movement=MOV_ENTERING_BUILDING;
		}
		break;

		case DIS_INSIDE:
		{
			movement=MOV_INSIDE;
		}
		break;

		case DIS_EXITING_BUILDING:
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
		break;

		case DIS_GOING_TO_RESSOURCE:
		{
			Map *map=owner->map;
			int teamNumber=owner->teamNumber;
			bool canSwim=performance[SWIM]>0;
			bool stopWork;
			if (map->pathfindRessource(teamNumber, destinationPurpose, canSwim, posX, posY, &dx, &dy, &stopWork, verbose))
			{
				if (verbose)
					printf("guid=(%d) Unit found path r pos=(%d, %d) to ressource %d, d=(%d, %d)\n", gid, posX, posY, destinationPurpose, dx, dy);
				directionFromDxDy();
				movement=MOV_GOING_DX_DY;
			}
			else
			{
				if (verbose)
					printf("guid=(%d) Unit failed path r pos=(%d, %d) to ressource %d, aborting work.\n", gid, posX, posY, destinationPurpose);

				if (stopWork)
					stopAttachedForBuilding(false);
				movement=MOV_RANDOM_GROUND;
			}
		}
		break;

		case DIS_HARVESTING:
		{
			movement=MOV_HARVESTING;
		}
		break;

		case DIS_FILLING_BUILDING:
		{
			movement=MOV_FILLING;
		}
		break;

		default:
		{
			assert (false);
		}
		break;
	}
}
