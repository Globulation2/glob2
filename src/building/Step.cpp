// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <list>
#include <math.h>
#include <Stream.h>
#include <stdlib.h>
#include <algorithm>
#include <climits>

#include "Building.h"
#include "BuildingType.h"
#include "FixedPoint.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"
#include "Order.h"
#include "Bullet.h"
#include "Integrity.h"

void Building::step(void)
{
	computeWishedRessources(wishedResources);

	updateCallLists();
	if(underAttackTimer>0)
		underAttackTimer--;
	if(canNotConvertUnitTimer>0)
		canNotConvertUnitTimer--;
	// NOTE : Unit needs to update itself when it is in a building
}


bool Building::subscribeToBringRessourcesStep()
{
	for(int i=0; i<UnitCantWorkReasonSize; ++i)
	{
		unitsFailingRequirements[i]=0;
	}
	if (buildingState==DEAD)
		return false;
	if (verbose)
		printf("bgid=%d, subscribeToBringRessourcesStep()...\n", gid);

	bool hired=false;
	if (((Sint32)unitsWorking.size()<desiredMaxUnitWorking) /* && !unitsWorkingSubscribe.empty() */ )
	{
		Unit *choosen=NULL;
		Map *map=owner->map;
		for(int i=0; i<UnitCantWorkReasonSize; ++i)
		{
			unitsFailingRequirements[i]=0;
		}
		/* To choose a good unit, we get a composition of things:
		1-the closest the unit is, the better it is.
		2-the less the unit is hungry, the better it is.
		3-if the unit has a needed ressource, this is better.
		4-if the unit as a not needed ressource, this is worse.
		5-if the unit is close of a needed ressource, this is better

		score_to_max=(rightRes*100/d+noRes*80/(d+dr)+wrongRes*25/(d+dr))/walk+sign(timeleft>>2 - (d+dr))*500+100/harvest
		*/
		/*
		int maxValue=-INT_MAX;
		for(int n=0; n<Building::MAX_COUNT; ++n)
		{
			Unit* unit=owner->myUnits[n];
			if(unit==NULL
			|| unit->activity != Unit::ACT_RANDOM
			|| unit->medical != Unit::MED_FREE
			|| !unit->performance[HARVEST])
				continue;
			if(!canUnitWorkHere(unit))
				continue;

			int r=unit->carriedRessource;
			int dist;
			if(!map->buildingAvailable(this, unit->performance[SWIM], unit->posX, unit->posY, &dist))
			{
				//std::cout << ":" << std::flush;
				continue; //also to fill dist
			}
			int distUnitRessource;
			int nr;
			for (nr=0; nr<MAX_RESSOURCES; nr++)
			{
				if (neededRessource(nr)>0)
				{
					if(map->ressourceAvailable(owner->teamNumber, nr, unit->performance[SWIM], unit->posX, unit->posY, &distUnitRessource)) //to fill distUnitRessource
						break;
					else
						continue;
				}
			}
			if (neededRessource(nr)<=0)
			{
				//std::cout << "," << std::flush;
				continue;
			}
			int rightRes=(((r>=0) && neededRessource(r))?1:0);
			if(rightRes==1 && (unit->hungry-unit->trigHungry)/unit->race->hungryness/2<dist)
				continue;
			else if(rightRes!=1 && (unit->hungry-unit->trigHungry)/unit->race->hungryness/2<(dist+distUnitRessource))
				continue;
			int noRes=(r<0?1:0);
			int wrongRes=(((r>=0) && !neededRessource(r))?1:0);
			int value = (
				rightRes*10*(512-dist)+
				noRes*8*(512-dist-distUnitRessource)+
				wrongRes*2*(512-dist-distUnitRessource)
			)*(unit->level[WALK]+1)+
			//enoughTimeLeft*5000+
			50*(unit->level[HARVEST]+1)+
			(unit->level[SWIM]>0?-200:0);//swimmer's penalty to keep them free for swimmer tasks
			//std::cout << "d" << dist << " dr" << distUnitRessource << " rr" << rightRes << " nr" << noRes << " wr" << wrongRes << " wa" << unit->level[WALK] << " ha" << unit->level[HARVEST] << " va" << value << std::endl << std::flush;
			unit->destinationPurpose=(rightRes>0?r:nr);
			if (value>maxValue)
			{
				maxValue=value;
				choosen=unit;
			}
		}
*/
		// Compute the list of candidate units
		Unit* possibleUnits[Unit::MAX_COUNT];
		int distances[Unit::MAX_COUNT];
		int resource[Unit::MAX_COUNT];
		int teamNumber=owner->teamNumber;
		for(int n=0; n<Unit::MAX_COUNT; ++n)
		{
			possibleUnits[n]=NULL;
			distances[n] = 0;
			resource[n] = -1;
			Unit* unit=owner->myUnits[n];
			if(unit)
			{
				if(!unit->performance[HARVEST])
				{
					continue;
				}
				else if(unit->attachedBuilding == this && unit->activity == Unit::ACT_FILLING)
				{
					continue;
				}
				else if(unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
				{
					unitsFailingRequirements[UnitNotAvailable] += 1;
				}
				else if(!canUnitWorkHere(unit))
				{
					unitsFailingRequirements[UnitTooLowLevel] += 1;
				}
				else
				{
					int distBuilding=0;
					int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
					bool canSwim=unit->performance[SWIM];
					if(!map->buildingAvailable(this, canSwim, unit->posX, unit->posY, &distBuilding))
					{
						unitsFailingRequirements[UnitCantAccessBuilding] += 1;
					}
					else if(distBuilding >= timeLeft)
					{
						unitsFailingRequirements[UnitTooFarFromBuilding] += 1;
					}
					else
					{
						int unitr = unit->carriedRessource;
						if((unitr>=0) && neededRessource(unitr))
						{
							possibleUnits[n] = unit;
							distances[n] = distBuilding;
							resource[n] = unitr;
						}
						else
						{
							int bestDist = 100000;
							int bestResource = RESSOURCE_TYPE_NONE;
							bool regularFound=false;
							bool fruitFound=false;
							bool regularFoundTooFar=false;
							bool fruitFoundTooFar=false;
							int x=unit->posX;
							int y=unit->posY;
							for(int r=0; r<MAX_NB_RESSOURCES; ++r)
							{
								int need = neededRessource(r);
								if(need>0)
								{
									if(r<BASIC_COUNT)
										regularFound=true;
									else
										fruitFound=true;
									int distResource = 0;
									if (map->ressourceAvailable(teamNumber, r, canSwim, x, y, &distResource))
									{
										if(distResource<timeLeft)
										{
											int dist = (distBuilding + distResource)<<Q8_FIXED_POINT_SHIFT;
											int value = dist / need;
											if(value < bestDist)
											{
												bestDist = value;
												bestResource=r;
											}
										}
										else
										{
											if(r<BASIC_COUNT)
												regularFoundTooFar=true;
											else
												fruitFoundTooFar=true;
										}
									}
								}
							}
							if(bestResource == RESSOURCE_TYPE_NONE)
							{
								if(regularFound)
								{
									if(regularFoundTooFar)
										unitsFailingRequirements[UnitTooFarFromResource] += 1;
									else
										unitsFailingRequirements[UnitCantAccessResource] += 1;
								}
								else if(fruitFound)
								{
									if(fruitFoundTooFar)
										unitsFailingRequirements[UnitCantAccessFruit] += 1;
									else
										unitsFailingRequirements[UnitTooFarFromFruit] += 1;
								}
							}
							else
							{
								resource[n] = bestResource;
								distances[n] = bestDist;
								possibleUnits[n]=unit;
							}
						}
					}
				}
			}
		}

		int maxLevel = -1;
		int minValue = INT_MAX;
		//First: we look only for units with a needed resource:
		for(int n=0; n<Unit::MAX_COUNT; ++n)
		{
			Unit* unit=possibleUnits[n];
			if(unit==NULL)
				continue;

			int r=unit->carriedRessource;
			int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
			if ((r>=0) && neededRessource(r))
			{
				int dist = distances[n];
				int value=dist-(timeLeft>>1);
				int level = unit->level[HARVEST]*10 + unit->level[WALK];
				unit->destinationPurpose=r;
				if ((level>maxLevel) || (level==maxLevel && value<minValue))
				{
					minValue=value;
					maxLevel=level;
					choosen=unit;
				}
			}
		}

		//Second: we look for an unit who is not carying a ressource:
		if (choosen==NULL)
		{
			for(int n=0; n<Unit::MAX_COUNT; ++n)
			{
				Unit* unit=possibleUnits[n];
				if(unit==NULL)
					continue;

				if (unit->carriedRessource<0)
				{
					int r = resource[n];
					int value=distances[n];
					int level = unit->level[HARVEST]*10 + unit->level[WALK];
					if ((level>maxLevel) || (level==maxLevel && value<minValue))
					{
						minValue=value;
						maxLevel=level;
						choosen=unit;
						unit->destinationPurpose=r;
					}
				}
			}
		}

		//Third: we look for an unit who is carrying an unwanted resource:
		if (choosen==NULL)
		{
			for(int n=0; n<Unit::MAX_COUNT; ++n)
			{
				Unit* unit=possibleUnits[n];
				if(unit==NULL)
					continue;

				int r2=unit->carriedRessource;
				if ((r2>=0) && !neededRessource(r2))
				{
					int r = resource[n];
					int value=distances[n];
					int level = unit->level[HARVEST]*10 + unit->level[WALK];
					if ((level>maxLevel) || (level==maxLevel && value<minValue))
					{
						minValue=value;
						maxLevel=level;
						choosen=unit;
						unit->destinationPurpose=r;
					}
				}
			}
		}
		if (choosen)
		{
			unitsWorking.push_back(choosen);
			choosen->subscriptionSuccess(this, false);
			hired=true;
		}
	}

	updateCallLists();

	if (verbose)
		printf(" ...done\n");
	return hired;
}

bool Building::considerUnitForExplorerFlag(Unit* unit, int* dist)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		unitsFailingRequirements[UnitNotAvailable] += 1;
		return false;
	}
	if (!canUnitWorkHere(unit))
	{
		unitsFailingRequirements[UnitTooLowLevel] += 1;
		return false;
	}
	int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungryness;
	// warpDistSquare returns squared Euclidean distance, so timeLeft is
	// squared here to keep the comparison in the same units. Worker/warrior
	// flags compare against Map::buildingAvailable (linear gradient
	// distance) and must NOT square — see considerUnitForWorkerFlag.
	int timeLeftSquared = timeLeft * timeLeft;
	int directdist = owner->map->warpDistSquare(unit->posX, unit->posY, posX, posY);
	if (timeLeftSquared < directdist)
	{
		unitsFailingRequirements[UnitTooFarFromBuilding] += 1;
		return false;
	}
	*dist = directdist;
	return true;
}

bool Building::considerUnitForWorkerFlag(Unit* unit, int* dist)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		unitsFailingRequirements[UnitNotAvailable] += 1;
		return false;
	}
	if (!canUnitWorkHere(unit))
	{
		unitsFailingRequirements[UnitTooLowLevel] += 1;
		return false;
	}
	int distBuilding = 0;
	// timeLeft and distBuilding are both linear (in ticks-remaining and
	// linear gradient steps respectively); compare as-is. The corresponding
	// check in subscribeToBringRessourcesStep uses the same pairing.
	int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungryness;
	bool canSwim = unit->performance[SWIM];
	if (!owner->map->buildingAvailable(this, canSwim, unit->posX, unit->posY, &distBuilding))
	{
		unitsFailingRequirements[UnitCantAccessBuilding] += 1;
		return false;
	}
	if (distBuilding >= timeLeft)
	{
		unitsFailingRequirements[UnitTooFarFromBuilding] += 1;
		return false;
	}
	if (anyRessourceToClear[canSwim] == 2)
	{
		unitsFailingRequirements[UnitCantAccessResource] += 1;
		return false;
	}
	*dist = distBuilding;
	return true;
}

bool Building::considerUnitForWarriorFlag(Unit* unit, int* dist)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		unitsFailingRequirements[UnitNotAvailable] += 1;
		return false;
	}
	if (!canUnitWorkHere(unit))
	{
		unitsFailingRequirements[UnitTooLowLevel] += 1;
		return false;
	}
	if (unit->movement == Unit::MOV_ATTACKING_TARGET)
	{
		unitsFailingRequirements[UnitNotAvailable] += 1;
		return false;
	}
	int distBuilding = 0;
	// timeLeft and distBuilding are both linear (in ticks-remaining and
	// linear gradient steps respectively); compare as-is. The corresponding
	// check in subscribeToBringRessourcesStep uses the same pairing.
	int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungryness;
	bool canSwim = unit->performance[SWIM];
	if (!owner->map->buildingAvailable(this, canSwim, unit->posX, unit->posY, &distBuilding))
	{
		unitsFailingRequirements[UnitCantAccessBuilding] += 1;
		return false;
	}
	if (distBuilding >= timeLeft)
	{
		unitsFailingRequirements[UnitTooFarFromBuilding] += 1;
		return false;
	}
	*dist = distBuilding;
	return true;
}

bool Building::subscribeForFlagingStep()
{
	if (buildingState==DEAD)
	{
		for(int i=0; i<UnitCantWorkReasonSize; ++i)
		{
			unitsFailingRequirements[i]=0;
		}
		return false;
	}

	bool hired=false;
	subscriptionWorkingTimer++;
	if (subscriptionWorkingTimer>32)
	{
		// Reset stale failure counts for the case where the while loop below
		// doesn't run (building already fully staffed). When the loop does run,
		// this is overwritten by the per-iteration reset on iteration 1.
		for(int i=0; i<UnitCantWorkReasonSize; ++i)
		{
			unitsFailingRequirements[i]=0;
		}
		while (((Sint32)unitsWorking.size()<desiredMaxUnitWorking))
		{
			// Per-iteration reset: the same Unit::MAX_COUNT array is rescanned
			// each iteration (already-hired units are filtered via
			// attachedBuilding==this); without this, the same failing units
			// would be counted N times across N iterations.
			for(int i=0; i<UnitCantWorkReasonSize; ++i)
			{
				unitsFailingRequirements[i]=0;
			}

			//Generate the list of possible units
			Unit* possibleUnits[Unit::MAX_COUNT];
			int distances[Unit::MAX_COUNT];
			for(int n=0; n<Unit::MAX_COUNT; ++n)
			{
				possibleUnits[n]=NULL;
				distances[n] = 0;
				Unit* unit=owner->myUnits[n];
				if(!unit)
					continue;
				if(unit->attachedBuilding == this)
					continue;
				if(type->zonable[EXPLORER])
				{
					if(unit->typeNum != EXPLORER)
						continue;
					if(considerUnitForExplorerFlag(unit, &distances[n]))
						possibleUnits[n]=unit;
				}
				else if(type->zonable[WORKER])
				{
					if(unit->typeNum != WORKER)
						continue;
					if(considerUnitForWorkerFlag(unit, &distances[n]))
						possibleUnits[n]=unit;
				}
				else if(type->zonable[WARRIOR])
				{
					if(unit->typeNum != WARRIOR)
						continue;
					if(considerUnitForWarriorFlag(unit, &distances[n]))
						possibleUnits[n]=unit;
				}
			}

			int minValue=INT_MAX;
			int minLevel=INT_MAX;
			int maxLevel=-INT_MAX;
			Unit *choosen=NULL;

			/* To choose a good unit, we get a composition of things:
			1-the closer the unit is, the better it is.
			2-the less the unit is hungry, the better it is.
			3-the more hp the unit has, the better it is.
			*/
			if (type->zonable[EXPLORER])
			{
				for(int n=0; n<Unit::MAX_COUNT; ++n)
				{
					Unit* unit=possibleUnits[n];
					if(unit==NULL)
						continue;

					int timeLeft=unit->hungry/unit->race->hungryness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					timeLeft*=timeLeft;
					hp*=hp;
					int dist=distances[n];
					//Use explorers without ground attack first before ones with, so that ground attacking explorers
					//are available for more important jobs
					int value=dist-2*timeLeft-2*hp;
					int level = unit->level[MAGIC_ATTACK_GROUND];
					if ((level < minLevel) || (level==minLevel && value<minValue))
					{
						minValue=value;
						minLevel=level;
						choosen=unit;
					}
				}
			}
			else if (type->zonable[WARRIOR])
			{
				for(int n=0; n<Unit::MAX_COUNT; ++n)
				{
					Unit* unit=possibleUnits[n];
					if(unit==NULL)
						continue;

					int timeLeft=unit->hungry/unit->race->hungryness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					int dist = distances[n];
					int value=dist-2*timeLeft-2*hp;
					//We want to maximize the attack level, use higher level soldeirs first
					int level=unit->performance[ATTACK_SPEED]*unit->getRealAttackStrength();
					if ((level > maxLevel) || (level==maxLevel && value<minValue))
					{
						minValue=value;
						maxLevel=level;
						choosen=unit;
					}
				}
			}
			else if (type->zonable[WORKER])
			{
				for(int n=0; n<Unit::MAX_COUNT; ++n)
				{
					Unit* unit=possibleUnits[n];
					if(unit==NULL)
						continue;

					int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					int dist = distances[n];
					int value=dist-timeLeft-hp;
					int level = unit->level[HARVEST];
					//We want to minimize the level of harvesting units, so that the higher level
					//units are available for more important work.
					if ((level < minLevel) || (level==minLevel && value<minValue))
					{
						minValue=value;
						minLevel=level;
						choosen=unit;
					}
				}
			}
			else
				assert(false);

			if (choosen)
			{
				unitsWorking.push_back(choosen);
				choosen->subscriptionSuccess(this, false);
				hired=true;
			}
			else
				break;
		}

		updateCallLists();

		subscriptionWorkingTimer=0;
	}
	return hired;
}


void Building::subscribeUnitForInside(Unit* unit)
{
	unitsInside.push_back(unit);
	unit->subscriptionSuccess(this, true);
	updateCallLists();
}


