// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "Race.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"

#include "Building.h"

#include "Utilities.h"
#include "GlobalContainer.h"

void Unit::handleDisplacement(void)
{
	switch (activity)
	{
		case ACT_RANDOM:
		{
			if ((medical==MED_FREE)&&((displacement==DIS_RANDOM)||(displacement==DIS_REMOVING_BLACK_AROUND)||(displacement==DIS_ATTACKING_AROUND)))
			{
				if (performance[FLY])
					displacement=DIS_REMOVING_BLACK_AROUND;
				else if (performance[ATTACK_SPEED])
					displacement=DIS_ATTACKING_AROUND;
			}
			else
				displacement=DIS_RANDOM;
			validTarget=false;
		}
		break;

		case ACT_FILLING:
		{
			assert(attachedBuilding);
			assert(displacement!=DIS_RANDOM);

			if (verbose)
				printf("guid=(%d) handleDisplacement() ACT_FILLING, displacement=%d\n", gid, displacement);

			if (displacement==DIS_GOING_TO_RESSOURCE)
			{
				if (auto off = owner->map->doesUnitTouchRessource(this, destinationPurpose))
				{
					dx = off->dx;
					dy = off->dy;
					displacement=DIS_HARVESTING;
					validTarget=false;
				}
			}
			else if (displacement==DIS_HARVESTING)
			{
				// we got the ressource.
				carriedRessource=destinationPurpose;
				owner->map->decRessource(posX+dx, posY+dy, carriedRessource);
				assert(movement == MOV_HARVESTING);
				movement = MOV_RANDOM_GROUND; // we do this to avoid the handleMovement() to aditionaly decRessource() the same ressource.

				setTargetBuilding(attachedBuilding);
				if (auto off = owner->map->doesUnitTouchBuilding(this, attachedBuilding->gid))
				{
					dx = off->dx;
					dy = off->dy;
					displacement=DIS_FILLING_BUILDING;
					validTarget=false;
				}
				else
				{
					displacement=DIS_GOING_TO_BUILDING;
					targetX=targetBuilding->getMidX();
					targetY=targetBuilding->getMidY();
					validTarget=true;
				}
			}
			else if (displacement==DIS_GOING_TO_BUILDING)
			{
				assert(targetBuilding);
				if (auto off = owner->map->doesUnitTouchBuilding(this, targetBuilding->gid))
				{
					dx = off->dx;
					dy = off->dy;
					displacement=DIS_FILLING_BUILDING;
					validTarget=false;
				}
			}
			else if (displacement==DIS_FILLING_BUILDING)
			{
				bool loopMove=false;
				bool exchangeReady=false;
				assert(targetBuilding);
				if (targetBuilding==ownExchangeBuilding)
				{
					assert(targetBuilding);
					assert(ownExchangeBuilding);
					assert(targetBuilding->type->canExchange);
					assert(ownExchangeBuilding->type->canExchange);
					assert(owner==targetBuilding->owner);
					assert(owner==ownExchangeBuilding->owner);

					assert(attachedBuilding);
					assert(attachedBuilding->type->canFeedUnit);
					assert(destinationPurpose>=HAPPYNESS_BASE);

					// Let's grab the right ressource.

					if (targetBuilding->ressources[destinationPurpose]>0)
					{
						targetBuilding->removeRessourceFromBuilding(destinationPurpose);
						carriedRessource=destinationPurpose;

						setTargetBuilding(attachedBuilding);
						displacement=DIS_GOING_TO_BUILDING;
						targetX=targetBuilding->getMidX();
						targetY=targetBuilding->getMidY();
						validTarget=true;
						exchangeReady=true;
						if (verbose)
							printf("guid=(%d) took a foreign fruit in our exhange building to food\n", gid);
					}
				}
				else if ((carriedRessource>=0) && (targetBuilding->ressources[carriedRessource]<targetBuilding->type->maxRessource[carriedRessource]))
				{
					if (verbose)
						printf("guid=(%d) Giving ressource (%d) to building gbid=(%d) old-amount=(%d)\n", gid, destinationPurpose, targetBuilding->gid, targetBuilding->ressources[carriedRessource]);
					targetBuilding->addRessourceIntoBuilding(carriedRessource);
					carriedRessource=UNIT_CARRIED_RESSOURCE_NONE;
				}

				if (!loopMove && !exchangeReady)
				{
					//NOTE: if attachedBuilding has become NULL; it's beacause the building doesn't need me anymore.
					if (!attachedBuilding)
					{
						if (verbose)
							printf("guid=(%d) The building doesn't need me any more.\n", gid);
						activity=ACT_RANDOM;
						displacement=DIS_RANDOM;
						validTarget=false;
						assert(needToRecheckMedical);
					}
					else
					{
						///Find a ressource that the building wants and a location to get it from
						///The location may be a market, or the harvesting the ressource from the
						///map.
						int needs[MAX_NB_RESSOURCES];
						attachedBuilding->computeWishedRessources(needs);
						int teamNumber=owner->teamNumber;
						bool canSwim=performance[SWIM];
						int timeLeft = numberOfStepsLeftUntilHungry();
						if (timeLeft > 0)
						{
							int bestRessource=-1;
							int minValue=owner->map->getW()+owner->map->getW();
							bool takeInExchangeBuilding=false;
							Map* map=owner->map;
							for (int r=0; r<MAX_NB_RESSOURCES; r++)
							{
								int need=needs[r];
								if (need>0)
								{
									int distToRessource;
									if (map->ressourceAvailable(teamNumber, r, canSwim, posX, posY, &distToRessource))
									{
										if ((distToRessource<<1)>=timeLeft)
											continue; //We don't choose this ressource, because it won't have time to reach the ressource and bring it back.
										int value=distToRessource/need;
										if (value<minValue)
										{
											bestRessource=r;
											minValue=value;
											takeInExchangeBuilding=false;
										}
									}

									if (attachedBuilding->type->canFeedUnit)
										for (std::list<Building *>::iterator bi=owner->canExchange.begin(); bi!=owner->canExchange.end(); ++bi)
											if ((*bi)->ressources[r]>0)
											{
												int buildingDist;
												if (map->buildingAvailable(*bi, canSwim, posX, posY, &buildingDist))
												{
													// We increase the cost to get a ressource in an exchange building to reflect the costs to get the ressources to the exchange building.
													// increase is +5 as markets will in general be very close to fruits as they are the fruit teleporters.
													int value=(buildingDist+5)/need;
													if (value<minValue)
													{
														bestRessource=r;
														minValue=value;

														ownExchangeBuilding=*bi;
														setTargetBuilding(*bi);
														takeInExchangeBuilding=true;
													}
												}
											}
								}
							}

							if (verbose)
								printf("guid=(%d) bestRessource=%d, minValue=%d\n", gid, bestRessource, minValue);

							if (bestRessource>=0)
							{
								destinationPurpose=bestRessource;
								assert(activity==ACT_FILLING);
								if (takeInExchangeBuilding)
								{
									displacement=DIS_GOING_TO_BUILDING;
									targetX=targetBuilding->getMidX();
									targetY=targetBuilding->getMidY();
									targetBuilding->insertUnitToHarvesting(this);
									validTarget=true;
								}
								else
								{
									int dummyDist;
									if (auto off = owner->map->doesUnitTouchRessource(this, destinationPurpose))
									{
										dx = off->dx;
										dy = off->dy;
										displacement=DIS_HARVESTING;
										validTarget=false;
									}
									else if (map->ressourceAvailableUpdate(teamNumber, destinationPurpose, canSwim, posX, posY, &targetX, &targetY, &dummyDist))
									{
										displacement=DIS_GOING_TO_RESSOURCE;
										validTarget=true;
									}
									else
									{
										assert(false);//You can remove this assert(), but *do* notice me!
										stopAttachedForBuilding(false);
									}
								}
							}
							else
							{
								if (verbose)
									printf("guid=(%d) can't find any wished ressource, unsubscribing.\n", gid);
								stopAttachedForBuilding(false);
							}
						}
						else
						{
							if (verbose)
								printf("guid=(%d) not enough time for anything, unsubscribing.\n", gid);
							stopAttachedForBuilding(false);
						}
					}
				}
			}
			else
			{
				displacement=DIS_RANDOM;
				validTarget=false;
			}
		}
		break;

		case ACT_UPGRADING:
		{
			assert(attachedBuilding);

			if (displacement==DIS_GOING_TO_BUILDING)
			{
				if (auto off = owner->map->doesUnitTouchBuilding(this, attachedBuilding->gid))
				{
					dx = off->dx;
					dy = off->dy;
					displacement=DIS_ENTERING_BUILDING;
					validTarget=false;
				}
			}
			else if (displacement==DIS_ENTERING_BUILDING)
			{
				// The unit has already its room in the building,
				// then we are sure that the unit can enter.

				if (performance[FLY])
					owner->map->setAirUnit(posX-dx, posY-dy, NOGUID);
				else
					owner->map->setGroundUnit(posX-dx, posY-dy, NOGUID);
				displacement=DIS_INSIDE;
				validTarget=false;

				if (destinationPurpose==FEED)
				{
					insideTimeout=-attachedBuilding->type->timeToFeedUnit;
					speed=attachedBuilding->type->insideSpeed;
				}
				else if (destinationPurpose==HEAL)
				{
					//insideTimeout=-(attachedBuilding->type->timeToHealUnit*(performance[HP]-hp))/performance[HP];
					insideTimeout=-attachedBuilding->type->timeToHealUnit;
					speed=(attachedBuilding->type->insideSpeed*performance[HP])/(performance[HP]-hp);
				}
				else
				{
					int levelsToBeUpgraded=attachedBuilding->type->level+1-level[destinationPurpose];
					insideTimeout=-attachedBuilding->type->upgradeTime[destinationPurpose];
					speed=attachedBuilding->type->insideSpeed/levelsToBeUpgraded;
				}
			}
			else if (displacement==DIS_INSIDE)
			{
				// we stay inside while the unit upgrades.
				if (insideTimeout>=0)
				{
					displacement=DIS_EXITING_BUILDING;
					validTarget=false;

					if (destinationPurpose==FEED)
					{
						hungry=HUNGRY_MAX;
						fruitCount=attachedBuilding->eatOnce(&fruitMask);
						needToRecheckMedical=true;
					}
					else if (destinationPurpose==HEAL)
					{
						hp=performance[HP];
						needToRecheckMedical=true;
					}
					else
					{
						if (attachedBuilding->type->upgradeInParallel)
						{
							for (int ability = (int)WALK; ability < (int)ARMOR; ability++)
								if (canLearn[ability] && attachedBuilding->type->upgrade[ability])
								{
									level[ability] = attachedBuilding->type->level + 1;
									UnitType *ut = race->getUnitType(typeNum, level[ability]);
									performance[ability] = ut->performance[ability];
								}
						}
						else
						{
							assert(canLearn[destinationPurpose]);
							level[destinationPurpose] = attachedBuilding->type->level + 1;
							UnitType *ut = race->getUnitType(typeNum, level[destinationPurpose]);
							performance[destinationPurpose] = ut->performance[destinationPurpose];
						}


					}
				}
				else
				{
					insideTimeout++;
				}
			}
			else if (displacement==DIS_EXITING_BUILDING)
			{
				// we want to get out, so we still stay in displacement==DIS_EXITING_BUILDING.
			}
			else
			{
				displacement=DIS_RANDOM;
				validTarget=false;
			}
		}
		break;

		case ACT_FLAG:
		{
			assert(attachedBuilding);
			displacement=DIS_GOING_TO_FLAG;
			targetX=attachedBuilding->posX;
			targetY=attachedBuilding->posY;
			validTarget=true;
			int distance=owner->map->warpDistSquare(targetX, targetY, posX, posY);
			int usr=attachedBuilding->unitStayRange;
			int usr2=usr*usr;
			if (verbose)
				printf("guid=(%d) ACT_FLAG distance=%d, usr2=%d\n", gid, distance, usr2);

			if (distance<=usr2)
			{
				validTarget=false;
				if (typeNum==WORKER)
					displacement=DIS_CLEARING_RESSOURCES;
				else if (typeNum==EXPLORER)
					displacement=DIS_REMOVING_BLACK_AROUND;
				else if (typeNum==WARRIOR)
					displacement=DIS_ATTACKING_AROUND;
				else
					assert(false);
			}
			else if (typeNum==WORKER)
			{
				int usr2plus=1+(usr+1)*(usr+1);
				if (distance<=usr2plus)
				{
					Map *map=owner->map;
					for (int tdx=-1; tdx<=1; tdx++)
						for (int tdy=-1; tdy<=1; tdy++)
						{
							int x=posX+tdx;
							int y=posY+tdy;
							if (map->warpDistSquare(x, y, targetX, targetY)<=usr2
								&& map->isRessourceTakeable(x, y, attachedBuilding->clearingRessources))
							{
								dx=tdx;
								dy=tdy;
								validTarget=false;
								displacement=DIS_CLEARING_RESSOURCES;
								//movement=MOV_HARVESTING;
								return;
							}
						}
				}
			}
		}
		break;

		default:
		{
			assert(false);
			break;
		}
	}
}

bool Unit::locationIsInEnemyGuardTowerRange(int x, int y)const
{
	//TODO: totally fix this totally hacky implementation.
	for(int i=0;i<Team::MAX_COUNT;i++)
	{
		Team *t = owner->game->teams[i];
		if((t)&&(owner->enemies & t->me))
		{
			for(int j=0;j<Building::MAX_COUNT;j++)
			{
				Building *b = t->myBuildings[j];
				if((b)&&(b->shortTypeNum==IntBuildingType::DEFENSE_BUILDING)&&(owner->map->warpDistMax(b->posX,b->posY,posX,posY) <= b->type->shootingRange + 1))return true;
			}
		}
	}
	return false;
}
