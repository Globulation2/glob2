/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "Unit.h"
#include "Race.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"

Unit::Unit(SDL_RWops *stream, Team *owner)
{
	logFile = globalContainer->logFileManager->getFile("Unit.log");
	load(stream, owner);
}

Unit::Unit(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, int level)
{
	logFile = globalContainer->logFileManager->getFile("Unit.log");
	
	// unit specification
	this->typeNum=typeNum;

	assert(team);
	race=&(team->race);

	// identity
	this->gid=gid;
	owner=team;
	isDead=false;

	// position
	posX=x;
	posY=y;
	delta=0;
	dx=0;
	dy=0;
	direction=8;
	insideTimeout=0;
	speed=32;

	// states
	needToRecheckMedical=true;
	medical=MED_FREE;
	activity=ACT_RANDOM;
	displacement=DIS_RANDOM;
	if (performance[FLY])
		movement=MOV_RANDOM_FLY;
	else
		movement=MOV_RANDOM_GROUND;
		
	targetX=0;
	targetY=0;

	// quality parameters
	for (int i=0; i<NB_ABILITY; i++)
	{
		this->performance[i]=race->getUnitType(typeNum, level)->performance[i];
		this->level[i]=level;
		this->canLearn[i]=race->getUnitType(typeNum, 1)->performance[i]; //TODO: is is a better way to hack this?
	}

	// trigger parameters
	hp=0;

	// warriors fight to death TODO: this is overided !?!?
	if (performance[ATTACK_SPEED])
		trigHP=0;
	else
		trigHP=20;

	// warriors wait more tiem before going to eat
	hungry=HUNGRY_MAX;
	if (this->performance[ATTACK_SPEED])
		trigHungry=(hungry*2)/10;
	else
		trigHungry=hungry/4;
	trigHungryCarying=hungry/10;
	fruitMask=0;
	fruitCount=0;


	// NOTE : rewrite hp from level
	hp=this->performance[HP];
	trigHP=(hp*3)/10;

	attachedBuilding=NULL;
	destinationPurprose=-1;
	subscribed=false;
	caryedRessource=-1;
	
	// debug vars:
	verbose=false;
}

void Unit::load(SDL_RWops *stream, Team *owner)
{
	// unit specification
	typeNum=SDL_ReadBE32(stream);
	race=&(owner->race);

	// identity
	gid=SDL_ReadBE16(stream);
	this->owner=owner;
	isDead=SDL_ReadBE32(stream);

	// position
	posX=SDL_ReadBE32(stream);
	posY=SDL_ReadBE32(stream);
	delta=SDL_ReadBE32(stream);
	dx=SDL_ReadBE32(stream);
	dy=SDL_ReadBE32(stream);
	direction=SDL_ReadBE32(stream);
	insideTimeout=SDL_ReadBE32(stream);
	speed=SDL_ReadBE32(stream);

	// states
	needToRecheckMedical=(bool)SDL_ReadBE32(stream);
	medical=(Medical)SDL_ReadBE32(stream);
	activity=(Activity)SDL_ReadBE32(stream);
	displacement=(Displacement)SDL_ReadBE32(stream);
	movement=(Movement)SDL_ReadBE32(stream);
	action=(Abilities)SDL_ReadBE32(stream);
	targetX=(Sint32)SDL_ReadBE32(stream);
	targetY=(Sint32)SDL_ReadBE32(stream);

	// trigger parameters
	hp=SDL_ReadBE32(stream);
	trigHP=SDL_ReadBE32(stream);

	// hungry
	hungry=SDL_ReadBE32(stream);
	trigHungry=SDL_ReadBE32(stream);
	trigHungryCarying=(trigHungry*4)/10;
	fruitMask=SDL_ReadBE32(stream);
	fruitCount=SDL_ReadBE32(stream);

	// quality parameters
	for (int i=0; i<NB_ABILITY; i++)
	{
		performance[i]=SDL_ReadBE32(stream);
		level[i]=SDL_ReadBE32(stream);
		canLearn[i]=(bool)SDL_ReadBE32(stream);
	}

	destinationPurprose=(Abilities)SDL_ReadBE32(stream);
	subscribed=(bool)SDL_ReadBE32(stream);

	caryedRessource=(Sint32)SDL_ReadBE32(stream);
	verbose=false;
}

void Unit::save(SDL_RWops *stream)
{
	// unit specification
	// we drop the unittype pointer, we save only the number
	SDL_WriteBE32(stream, (Uint32)typeNum);

	// identity
	SDL_WriteBE16(stream, gid);
	SDL_WriteBE32(stream, isDead);

	// position
	SDL_WriteBE32(stream, posX);
	SDL_WriteBE32(stream, posY);
	SDL_WriteBE32(stream, delta);
	SDL_WriteBE32(stream, dx);
	SDL_WriteBE32(stream, dy);
	SDL_WriteBE32(stream, direction);
	SDL_WriteBE32(stream, insideTimeout);
	SDL_WriteBE32(stream, speed);

	// states
	SDL_WriteBE32(stream, (Uint32)needToRecheckMedical);
	SDL_WriteBE32(stream, (Uint32)medical);
	SDL_WriteBE32(stream, (Uint32)activity);
	SDL_WriteBE32(stream, (Uint32)displacement);
	SDL_WriteBE32(stream, (Uint32)movement);
	SDL_WriteBE32(stream, (Uint32)action);
	SDL_WriteBE32(stream, (Uint32)targetX);
	SDL_WriteBE32(stream, (Uint32)targetY);

	// trigger parameters
	SDL_WriteBE32(stream, hp);
	SDL_WriteBE32(stream, trigHP);

	// hungry
	SDL_WriteBE32(stream, hungry);
	SDL_WriteBE32(stream, trigHungry);
	SDL_WriteBE32(stream, fruitMask);
	SDL_WriteBE32(stream, fruitCount);

	// quality parameters
	for (int i=0; i<NB_ABILITY; i++)
	{
		SDL_WriteBE32(stream, performance[i]);
		SDL_WriteBE32(stream, level[i]);
		SDL_WriteBE32(stream, (Uint32)canLearn[i]);
	}

	SDL_WriteBE32(stream, (Uint32)destinationPurprose);
	SDL_WriteBE32(stream, (Uint32)subscribed);
	SDL_WriteBE32(stream, (Uint32)caryedRessource);
}

void Unit::loadCrossRef(SDL_RWops *stream, Team *owner)
{
	Uint16 gbid=SDL_ReadBE16(stream);
	if (gbid==NOGBID)
		attachedBuilding=NULL;
	else
		attachedBuilding=owner->myBuildings[Building::GIDtoID(gbid)];
}

void Unit::saveCrossRef(SDL_RWops *stream)
{
	if (attachedBuilding)
		SDL_WriteBE16(stream, attachedBuilding->gid);
	else
		SDL_WriteBE16(stream, NOGBID);
}

void Unit::subscriptionSuccess(void)
{
	if (verbose)
		printf("guid=(%d), subscriptionSuccess()\n", gid);
	subscribed=false;
	switch(medical)
	{
		case MED_HUNGRY :
		case MED_DAMAGED :
		{
			activity=ACT_UPGRADING;
			displacement=DIS_GOING_TO_BUILDING;
			targetX=attachedBuilding->getMidX();
			targetY=attachedBuilding->getMidY();
		}
		break;
		case MED_FREE:
		{
			switch(activity)
			{
				case ACT_FLAG:
				{
					displacement=DIS_GOING_TO_FLAG;
					targetX=attachedBuilding->getMidX();
					targetY=attachedBuilding->getMidY();
				}
				break;
				case ACT_UPGRADING:
				{
					displacement=DIS_GOING_TO_BUILDING;
					targetX=attachedBuilding->getMidX();
					targetY=attachedBuilding->getMidY();
				}
				break;
				case ACT_FILLING:
				{
					if (caryedRessource==destinationPurprose)
					{
						displacement=DIS_GOING_TO_BUILDING;
						targetX=attachedBuilding->getMidX();
						targetY=attachedBuilding->getMidY();
					}
					else
						displacement=DIS_GOING_TO_RESSOURCE;
				}
				break;
				case ACT_RANDOM :
				{
					displacement=DIS_RANDOM;
				}
				break;
				default:
					assert(false);
			}
		}
		break;
	}
}

void Unit::step(void)
{
	assert(speed>0);
	if ((action==ATTACK_SPEED) && (delta>=128) && (delta<(128+speed)))
	{
		Uint16 enemyGUID=owner->map->getGroundUnit(posX+dx, posY+dy);
		if (enemyGUID!=NOGUID)
		{
			int enemyID=GIDtoID(enemyGUID);
			int enemyTeam=GIDtoTeam(enemyGUID);
			Unit *enemy=owner->game->teams[enemyTeam]->myUnits[enemyID];
			int degats=performance[ATTACK_STRENGTH]-enemy->performance[ARMOR];
			if (degats<=0)
				degats=1;
			enemy->hp-=degats;
			enemy->owner->setEvent(posX+dx, posY+dy, Team::UNIT_UNDER_ATTACK_EVENT, enemyGUID);
		}
		else
		{
			Uint16 enemyGBID=owner->map->getBuilding(posX+dx, posY+dy);
			if (enemyGBID!=NOGBID)
			{
				int enemyID=Building::GIDtoID(enemyGBID);
				int enemyTeam=Building::GIDtoTeam(enemyGBID);
				Building *enemy=owner->game->teams[enemyTeam]->myBuildings[enemyID];
				int degats=performance[ATTACK_STRENGTH]-enemy->type->armor;
				if (degats<=0)
					degats=1;
				enemy->hp-=degats;
				enemy->owner->setEvent(posX+dx, posY+dy, Team::BUILDING_UNDER_ATTACK_EVENT, enemyGBID);
				if (enemy->hp<0)
					enemy->kill();
			}
		}
	}
	
//#define BURST_UNIT_MODE
#ifdef BURST_UNIT_MODE
	delta=0;
#else
	if (delta<=255-speed)
	{
		delta+=speed;
	}
	else
#endif
	{
		//printf("action=%d, speed=%d, perf[a]=%d, t->perf[a]=%d\n", action, speed, performance[action], race->getUnitType(typeNum, 0)->performance[action]);
		delta+=(speed-256);
		
		endOfAction();
		
		if (performance[FLY])
		{
			owner->map->setMapDiscovered(posX-3, posY-3, 7, 7, owner->sharedVisionOther);
			owner->map->setMapBuildingsDiscovered(posX-3, posY-3, 7, 7, owner->sharedVisionOther, owner->game->teams);
		}
		else
		{
			owner->map->setMapDiscovered(posX-1, posY-1, 3, 3, owner->sharedVisionOther);
			owner->map->setMapBuildingsDiscovered(posX-1, posY-1, 3, 3, owner->sharedVisionOther, owner->game->teams);
		}
	}
}

void Unit::selectPreferedMovement(void)
{
	if (performance[FLY])
		action=FLY;
	else if ((performance[SWIM]) && (owner->map->isWater(posX, posY)) )
		action=SWIM;
	else if ((performance[WALK]) && (!owner->map->isWater(posX, posY)) )
		action=WALK;
	else
		assert(false);
}

void Unit::selectPreferedGroundMovement(void)
{
	assert(!performance[FLY]);
	if ((performance[SWIM]) && (owner->map->isWater(posX, posY)) )
		action=SWIM;
	else if ((performance[WALK]) && (!owner->map->isWater(posX, posY)) )
		action=WALK;
	else
		assert(false);
}

bool Unit::isUnitHungry(void)
{
	int realTrigHungry;
	if (caryedRessource==-1)
		realTrigHungry=trigHungry;
	else
		realTrigHungry=trigHungryCarying;

	return (hungry<=realTrigHungry);
}

void Unit::stopAttachedForBuilding(bool goingInside)
{
	if (verbose)
		printf("guid=(%d) stopAttachedForBuilding()\n", gid);
	assert(attachedBuilding);
	
	if (goingInside)
	{
		attachedBuilding->unitsInside.remove(this);
		attachedBuilding->unitsInsideSubscribe.remove(this);
		
		if (activity==ACT_UPGRADING)
		{
			assert(displacement==DIS_GOING_TO_BUILDING);
			if (destinationPurprose==HEAL || destinationPurprose==FEED)
				needToRecheckMedical=true;
		}
	}
	else
	{
		for (std::list<Unit *>::iterator  it=attachedBuilding->unitsInside.begin(); it!=attachedBuilding->unitsInside.end(); ++it)
			assert(*it!=this);
		for (std::list<Unit *>::iterator  it=attachedBuilding->unitsInsideSubscribe.begin(); it!=attachedBuilding->unitsInsideSubscribe.end(); ++it)
			assert(*it!=this);
	}
	
	activity=ACT_RANDOM;
	displacement=DIS_RANDOM;
	
	attachedBuilding->unitsWorking.remove(this);
	attachedBuilding->unitsWorkingSubscribe.remove(this);
	attachedBuilding->updateCallLists();
	attachedBuilding=NULL;
	subscribed=false;
	assert(needToRecheckMedical);
}

void Unit::handleMedical(void)
{
	if ((displacement==DIS_ENTERING_BUILDING) || (displacement==DIS_INSIDE) || (displacement==DIS_EXITING_BUILDING))
		return;
	
	hungry-=race->unitTypes[0][0].hungryness;
	if (hungry<=0)
		hp--;
	
	medical=MED_FREE;
	if (isUnitHungry())
		medical=MED_HUNGRY;
	else if (hp<=trigHP)
		medical=MED_DAMAGED;

	if (hp<0)
	{
		fprintf(logFile, "guid=%d, set isDead(%d), beacause hungry.\n", gid, isDead);
		if (attachedBuilding)
			fprintf(logFile, " attachedBuilding->gid=%d.\n", attachedBuilding->gid);
		
		if (!isDead)
		{
			if (attachedBuilding)
			{
				assert((displacement!=DIS_ENTERING_BUILDING) && (displacement!=DIS_INSIDE) && (displacement!=DIS_EXITING_BUILDING));
				attachedBuilding->unitsWorking.remove(this);
				attachedBuilding->unitsInside.remove(this);
				attachedBuilding->unitsWorkingSubscribe.remove(this);
				attachedBuilding->unitsInsideSubscribe.remove(this);
				attachedBuilding->updateCallLists();
				attachedBuilding=NULL;
				subscribed=false;
			}
			
			activity=ACT_RANDOM;
			
			if (performance[FLY])
				owner->map->setAirUnit(posX, posY, NOGUID);
			else
				owner->map->setGroundUnit(posX, posY, NOGUID);
		}
		isDead=true;
	}
}

void Unit::handleActivity(void)
{
	// freeze unit health when inside a building
	if ((displacement==DIS_ENTERING_BUILDING) || (displacement==DIS_INSIDE) || (displacement==DIS_EXITING_BUILDING))
		return;
	
	if (medical==MED_FREE)
	{
		if (activity==ACT_RANDOM)
		{
			// look for a "job"
			// else keep walking around
			if (verbose)
				printf("guid=(%d) looking for a job...\n", gid);

			// first we look for a food building to fill, because it is the first priority.
			if (performance[HARVEST])
			{
				Building *b=owner->findBestFoodable(this);
				if (b!=NULL)
				{
					assert(destinationPurprose>=0);
					assert(b->neededRessource(destinationPurprose));
					activity=ACT_FILLING;
					attachedBuilding=b;
					if (verbose)
						printf("guid=(%d) unitsWorkingSubscribe(findBestFoodable) dp=(%d), gbid=(%d)\n", gid, destinationPurprose, b->gid);
					b->unitsWorkingSubscribe.push_front(this);
					//b->lastWorkingSubscribe=0;
					subscribed=true;
					owner->subscribeToBringRessources.push_front(b);
					return;
				}
			}

			// second we look for upgrade
			Building *b;
			b=owner->findBestUpgrade(this);
			if (b!=NULL)
			{
				assert(destinationPurprose>=WALK);
				assert(destinationPurprose<ARMOR);
				activity=ACT_UPGRADING;
				attachedBuilding=b;
				if (verbose)
					printf("guid=(%d) going to upgrade at dp=(%d), gbid=(%d)\n", gid, destinationPurprose, b->gid);
				b->unitsInsideSubscribe.push_front(this);
				b->lastInsideSubscribe=0;
				subscribed=true;
				owner->subscribeForInside.push_front(b);
				return;
			}
			
			// third we go to flag
			b=owner->findBestZonable(this);
			if (b!=NULL)
			{
				destinationPurprose=-1;
				activity=ACT_FLAG;
				attachedBuilding=b;
				if (verbose)
					printf("guid=(%d) unitsWorkingSubscribe(findBestZonable) dp=(%d), gbid=(%d)\n", gid, destinationPurprose, b->gid);
				b->unitsWorkingSubscribe.push_front(this);
				//b->lastWorkingSubscribe=0;
				subscribed=true;
				owner->subscribeForFlaging.push_front(b);
				return;
			}
			
			// fourth we harvest for construction, or other lower priority.
			if (performance[HARVEST])
			{
				// if we have a ressource
				Building *b=owner->findBestFillable(this);
				if (b!=NULL)
				{
					assert(destinationPurprose>=0);
					assert(b->neededRessource(destinationPurprose));
					activity=ACT_FILLING;
					attachedBuilding=b;
					if (verbose)
						printf("guid=(%d) unitsWorkingSubscribe(findBestFillable) dp=(%d), gbid=(%d)\n", gid, destinationPurprose, b->gid);
					b->unitsWorkingSubscribe.push_front(this);
					//b->lastWorkingSubscribe=0;
					subscribed=true;
					owner->subscribeToBringRessources.push_front(b);
					return;
				}
			}
			
			if (verbose)
				printf("guid=(%d) no job found.\n", gid);
			
			// nothing to do:
			// we go to a heal building if we'r not fully healed:
			if (hp<performance[HP])
			{
				Building *b;
				b=owner->findNearestHeal(this);
				if (b!=NULL)
				{
					destinationPurprose=HEAL;
					activity=ACT_UPGRADING;
					attachedBuilding=b;
					needToRecheckMedical=false;
					if (verbose)
						printf("guid=(%d) Going to heal building\n", gid);
					targetX=attachedBuilding->getMidX();
					targetY=attachedBuilding->getMidY();
					b->unitsInsideSubscribe.push_front(this);
					b->lastInsideSubscribe=0;
					subscribed=true;
					owner->subscribeForInside.push_front(b);
				}
				else
					activity=ACT_RANDOM;
			}
		}
		else
		{
			// we keep the job
		}
	}
	else if (needToRecheckMedical)
	{
		if (attachedBuilding)
		{
			if (verbose)
				printf("guid=(%d) Need medical while working, abort work\n", gid);
			attachedBuilding->unitsWorking.remove(this);
			attachedBuilding->unitsInside.remove(this);
			attachedBuilding->unitsWorkingSubscribe.remove(this);
			attachedBuilding->unitsInsideSubscribe.remove(this);
			attachedBuilding->updateCallLists();
			attachedBuilding=NULL;
			subscribed=false;
		}
		
		if (medical==MED_HUNGRY)
		{
			Building *b;
			b=owner->findNearestFood(this);
			if (b!=NULL)
			{
				destinationPurprose=FEED;
				activity=ACT_UPGRADING;
				attachedBuilding=b;
				needToRecheckMedical=false;
				if (verbose)
					printf("guid=(%d) Subscribed to food at building gbid=(%d)\n", gid, b->gid);
				b->unitsInsideSubscribe.push_front(this);
				b->lastInsideSubscribe=0;
				subscribed=true;
				owner->subscribeForInside.push_front(b);
			}
			else
				activity=ACT_RANDOM;
		}
		else if (medical==MED_DAMAGED)
		{
			Building *b;
			b=owner->findNearestHeal(this);
			if (b!=NULL)
			{
				destinationPurprose=HEAL;
				activity=ACT_UPGRADING;
				attachedBuilding=b;
				needToRecheckMedical=false;
				if (verbose)
					printf("guid=(%d) Subscribed to heal at building gbid=(%d)\n", gid, b->gid);
				b->unitsInsideSubscribe.push_front(this);
				b->lastInsideSubscribe=0;
				subscribed=true;
				owner->subscribeForInside.push_front(b);
			}
			else
				activity=ACT_RANDOM;
		}
		else
			assert(false);
	}
}

void Unit::handleDisplacement(void)
{
	if (subscribed)
	{
		displacement=DIS_RANDOM;
	}
	else switch (activity)
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
		}
		break;
		
		case ACT_FILLING:
		{
			assert(attachedBuilding);
			assert(displacement!=DIS_RANDOM);
			
			if (displacement==DIS_GOING_TO_RESSOURCE)
			{
				if (owner->map->doesUnitTouchRessource(this, destinationPurprose, &dx, &dy))
					displacement=DIS_HARVESTING;
			}
			else if (displacement==DIS_HARVESTING)
			{
				// we got the ressource.
				caryedRessource=destinationPurprose;
				owner->map->decRessource(posX+dx, posY+dy, caryedRessource);
				
				if (owner->map->doesUnitTouchBuilding(this, attachedBuilding->gid, &dx, &dy))
					displacement=DIS_FILLING_BUILDING;
				else
				{
					displacement=DIS_GOING_TO_BUILDING;
					targetX=attachedBuilding->getMidX();
					targetY=attachedBuilding->getMidY();
				}
			}
			else if (displacement==DIS_GOING_TO_BUILDING)
			{
				if (owner->map->doesUnitTouchBuilding(this, attachedBuilding->gid, &dx, &dy))
					displacement=DIS_FILLING_BUILDING;
			}
			else if (displacement==DIS_FILLING_BUILDING)
			{
				if (attachedBuilding->ressources[caryedRessource]<attachedBuilding->type->maxRessource[caryedRessource])
				{
					if (verbose)
						printf("guid=(%d) Giving ressource (%d) to building gbid=(%d) old-amount=(%d)\n", gid, destinationPurprose, attachedBuilding->gid, attachedBuilding->ressources[(int)destinationPurprose]);
					attachedBuilding->ressources[caryedRessource]+=attachedBuilding->type->multiplierRessource[caryedRessource];
					if (attachedBuilding->ressources[caryedRessource] > attachedBuilding->type->maxRessource[caryedRessource])
						attachedBuilding->ressources[caryedRessource]=attachedBuilding->type->maxRessource[caryedRessource];
					caryedRessource=-1;
					BuildingType *bt=attachedBuilding->type;
					switch (attachedBuilding->constructionResultState)
					{
						case Building::NO_CONSTRUCTION:
						break;
						
						case Building::NEW_BUILDING:
						case Building::UPGRADE: 
						{
							attachedBuilding->hp+=bt->hpInc;
						}
						break;
						
						case Building::REPAIR:
						{
							int totRessources=0;
							for (unsigned i=0; i<MAX_NB_RESSOURCES; i++)
								totRessources+=bt->maxRessource[i];
							attachedBuilding->hp+=bt->hpMax/totRessources;
						}
						break;
						
						default:
							assert(false);
					}
				}

				attachedBuilding->update();
				//NOTE: if attachedBuilding has become NULL; it's beacause the building doesn't need me anymore.
				if (!attachedBuilding)
				{
					if (verbose)
						printf("guid=(%d) The building doesn't need me any more.\n", gid);
					activity=ACT_RANDOM;
					displacement=DIS_RANDOM;
					subscribed=false;
					assert(needToRecheckMedical);
				}
				else
				{
					Uint8 needs[MAX_NB_RESSOURCES];
					attachedBuilding->neededRessources(needs);
					int teamNumber=owner->teamNumber;
					bool canSwim=performance[SWIM];
					int timeLeft=(hungry-trigHungry)/race->unitTypes[0][0].hungryness;
					destinationPurprose=-1;
					int minValue=owner->map->getW()+owner->map->getW();
					Map* map=owner->map;
					for (int r=0; r<MAX_NB_RESSOURCES; r++)
					{
						int need=needs[r];
						if (need)
						{
							int distToRessource;
							if (map->ressourceAviable(teamNumber, r, canSwim, posX, posY, &distToRessource))
							{
								if ((distToRessource<<1)>=timeLeft)
									continue; //We don't choose this ressource, because it won't have time to reach the ressource and bring it back.
								int value=distToRessource/need;
								if (value<minValue)
								{
									destinationPurprose=r;
									minValue=value;
								}
							}
						}
					}

					if (verbose)
						printf("guid=(%d) destinationPurprose=%d, minValue=%d\n", gid, destinationPurprose, minValue);

					if (destinationPurprose>=0)
					{
						assert(activity==ACT_FILLING);
						int dummyDist;
						if (owner->map->doesUnitTouchRessource(this, destinationPurprose, &dx, &dy))
							displacement=DIS_HARVESTING;
						else if (map->ressourceAviable(teamNumber, destinationPurprose, canSwim, posX, posY, &targetX, &targetY, &dummyDist, 255))
							displacement=DIS_GOING_TO_RESSOURCE;
						else
						{
							assert(false);//You can remove this assert(), but *do* notice me!
							activity=ACT_RANDOM;
							displacement=DIS_RANDOM;
							subscribed=false;
							assert(needToRecheckMedical);
						}
					}
					else
					{
						if (verbose)
							printf("guid=(%d) can't find any wished ressource, unsubscribing.\n", gid);
						stopAttachedForBuilding(false);
					}
				}
			}
			else
				displacement=DIS_RANDOM;
		}
		break;
		
		case ACT_UPGRADING:
		{
			assert(attachedBuilding);
			
			if (displacement==DIS_GOING_TO_BUILDING)
			{
				if (owner->map->doesUnitTouchBuilding(this, attachedBuilding->gid, &dx, &dy))
					displacement=DIS_ENTERING_BUILDING;
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
				
				if (destinationPurprose==FEED)
				{
					insideTimeout=-attachedBuilding->type->timeToFeedUnit;
				}
				else if (destinationPurprose==HEAL)
				{
					insideTimeout=-attachedBuilding->type->timeToHealUnit;
				}
				else
				{
					insideTimeout=-attachedBuilding->type->upgradeTime[destinationPurprose];
				}
				speed=attachedBuilding->type->insideSpeed;
			}
			else if (displacement==DIS_INSIDE)
			{
				// we stay inside while the unit upgrades.
				if (insideTimeout>=0)
				{
					//printf("Exiting building\n");
					displacement=DIS_EXITING_BUILDING;

					if (destinationPurprose==FEED)
					{
						hungry=HUNGRY_MAX;
						attachedBuilding->ressources[CORN]--;
						assert(attachedBuilding->ressources[CORN]>=0);
						fruitCount=attachedBuilding->getFruits(&fruitMask);
						//printf("I'm not hungry any more :-)\n");
						needToRecheckMedical=true;
					}
					else if (destinationPurprose==HEAL)
					{
						hp=performance[HP];
						//printf("I'm healed : healt h %d/%d\n", hp, performance[HP]);
						needToRecheckMedical=true;
					}
					else
					{
						//printf("Ability %d got level %d\n", destinationPurprose, attachedBuilding->type->level+1);
						assert(canLearn[destinationPurprose]);
						level[destinationPurprose]=attachedBuilding->type->level+1;
						UnitType *ut=race->getUnitType(typeNum, level[destinationPurprose]);
						performance[destinationPurprose]=ut->performance[destinationPurprose];

						//printf("New performance[%d]=%d\n", destinationPurprose, performance[destinationPurprose]);
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
				displacement=DIS_RANDOM;
		}
		break;

		case ACT_FLAG:
		{
			assert(attachedBuilding);
			targetX=attachedBuilding->posX;
			targetY=attachedBuilding->posY;
			int distance=owner->map->warpDistSquare(targetX, targetY, posX, posY);
			int range=(Sint32)((attachedBuilding->unitStayRange)*(attachedBuilding->unitStayRange));
			if (verbose)
				printf("guid=(%d) ACT_FLAG distance=%d, range=%d\n", gid, distance, range);
			if (distance<=range)
			{
				if (typeNum==WORKER)
					displacement=DIS_CLEARING_RESSOURCES;
				else if (typeNum==EXPLORER)
					displacement=DIS_REMOVING_BLACK_AROUND;
				else if (typeNum==WARRIOR)
					displacement=DIS_ATTACKING_AROUND;
				else
					assert(false);
			}
			else
				displacement=DIS_GOING_TO_FLAG;
		}
		break;

		default:
		{
			assert(false);
			break;
		}
	}
}

void Unit::handleMovement(void)
{
	switch (displacement)
	{
		case DIS_REMOVING_BLACK_AROUND:
		{
			assert(performance[FLY]);
			if (verbose)
				printf("guid=(%d) DIS_REMOVING_BLACK_AROUND\n", gid);
			if (attachedBuilding)
			{
				movement=MOV_GOING_DXDY;
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
			else if ((movement!=MOV_GOING_DXDY)||((syncRand()&0xFF)<0xEF))
			{
				int dist[8];
				int minDist=32;
				int minDistj=8;
				for (int j=0; j<8; j++)
				{
					dist[j]=32;
					// WARNING : the i=4 is linked to the sight range of the explorer.
					for (int i=4; i<32; i+=4)
					{
						int dx, dy;
						dxdyfromDirection(j, &dx, &dy);
						if (!owner->map->isMapDiscovered(posX+i*dx, posY+j*dy, owner->sharedVisionOther))
						{
							dist[j]=i;
							break;
						}
					}
					if (dist[j]<minDist)
					{
						minDist=dist[j];
						minDistj=j;
					}
					if (verbose)
						printf("dist[%d]=%d.\n", j, dist[j]);
				}
				
				if (minDist==32)
				{
					if (verbose)
						printf("guid=(%d) I can''t see black.\n", gid);
					if ((syncRand()&0xFF)<0xEF)
					{
						movement=MOV_GOING_DXDY;
					}
					else if ((syncRand()&0xFF)<0xEF)
					{
						directionFromDxDy();
						direction=(direction+((syncRand()&1)<<1)+7)&7;
						dxdyfromDirection();
						movement=MOV_GOING_DXDY;
					}
					else 
					{
						movement=MOV_RANDOM_FLY;
					}
				}
				else
				{
					int decj=syncRand()&7;
					if (verbose)
						printf("guid=(%d) minDist=%d, minDistj=%d.\n", gid, minDist, minDistj);
					for (int j=0; j<8; j++)
					{
						int d=(decj+j)&7;

						if (dist[d]<=minDist)
						{
							movement=MOV_GOING_DXDY;
							dxdyfromDirection(d, &dx, &dy);
							break;
						}
					}
				}
			}

			if (movement!=MOV_GOING_DXDY || owner->map->getAirUnit(posX+dx, posY+dy)!=NOGUID)
				movement=MOV_RANDOM_FLY;
		}
		break;

		case DIS_ATTACKING_AROUND:
		{
			assert(performance[ATTACK_SPEED]);
			//if ((performance[ATTACK_SPEED]) && (medical==MED_FREE) && (owner->map->doesUnitTouchEnemy(this, &dx, &dy)))
			//{
			//	movement=MOV_ATTACKING_TARGET;
			//}
			//else
			//{
				int quality=256; // Smaller is better.
				movement=MOV_RANDOM_GROUND;
				if (verbose)
					printf("guid=(%d) selecting movement\n", gid);
				
				for (int x=-8; x<=8; x++)
					for (int y=-8; y<=8; y++)
						if (owner->map->isFOWDiscovered(posX+x, posY+y, owner->sharedVisionOther))
						{
							if (attachedBuilding&&
								owner->map->warpDistSquare(posX+x, posY+y, attachedBuilding->posX, attachedBuilding->posY)
									>((int)attachedBuilding->unitStayRange*(int)attachedBuilding->unitStayRange))
								continue;
							Uint16 gid;
							gid=owner->map->getBuilding(posX+x, posY+y);
							if (gid!=NOGBID)
							{
								int team=Building::GIDtoTeam(gid);
								Uint32 tm=1<<team;
								if (owner->enemies & tm)
								{
									int id=Building::GIDtoID(gid);
									int newQuality=(x*x+y*y);
									BuildingType *bt=owner->game->teams[team]->myBuildings[id]->type;
									int shootDamage=bt->shootDamage;
									newQuality/=(1+shootDamage);
									if (verbose)
										printf("guid=(%d) warrior found building with newQuality=%d\n", this->gid, newQuality);
									if (newQuality<quality)
									{
										if (abs(x)<=1 && abs(y)<=1)
										{
											movement=MOV_ATTACKING_TARGET;
											dx=x;
											dy=y;
										}
										else
											movement=MOV_GOING_TARGET;
										targetX=posX+x;
										targetY=posY+y;
										quality=newQuality;
									}
								}
							}
							gid=owner->map->getGroundUnit(posX+x, posY+y);
							if (gid!=NOGUID)
							{
								int team=Unit::GIDtoTeam(gid);
								Uint32 tm=1<<team;
								if (owner->enemies & tm)
								{
									int id=Building::GIDtoID(gid);
									Unit *u=owner->game->teams[team]->myUnits[id];
									int strength=u->performance[ATTACK_STRENGTH];
									int newQuality=(x*x+y*y)/(1+strength);
									if (verbose)
										printf("guid=(%d) warrior found unit with newQuality=%d\n", this->gid, newQuality);
									if (newQuality<quality)
									{
										if (abs(x)<=1 && abs(y)<=1)
										{
											movement=MOV_ATTACKING_TARGET;
											dx=x;
											dy=y;
										}
										else
											movement=MOV_GOING_TARGET;
										targetX=posX+x;
										targetY=posY+y;
										quality=newQuality;
									}
								}
							}
						}
			//}
		}
		break;
		
		case DIS_CLEARING_RESSOURCES:
		{
			Map *map=owner->map;
			if (movement==MOV_HARVESTING)
				map->decRessource(posX+dx, posY+dy);
			
			int bx=attachedBuilding->posX;
			int by=attachedBuilding->posY;
			int usr=attachedBuilding->unitStayRange;
			int usr2=usr*usr;
			for (int tdx=-1; tdx<=1; tdx++)
				for (int tdy=-1; tdy<=1; tdy++)
				{
					int x=posX+tdx;
					int y=posY+tdy;
					if (map->warpDistSquare(x, y, bx, by)<=usr2 && map->isRemovableRessource(x, y))
					{
						dx=tdx;
						dy=tdy;
						movement=MOV_HARVESTING;
						return;
					}
				}
			bool canSwim=performance[SWIM];
			assert(attachedBuilding);
			if (attachedBuilding->localRessources[canSwim]==NULL)
				map->updateLocalRessources(attachedBuilding, canSwim);
			if (map->pathfindLocalRessource(attachedBuilding, canSwim, posX, posY, &dx, &dy))
			{
				directionFromDxDy();
				movement=MOV_GOING_DXDY;
			}
			else
				movement=MOV_RANDOM_GROUND;
		}
		break;

		case DIS_RANDOM:
		{
			Map *map=owner->map;
			if ((performance[ATTACK_SPEED]) && (medical==MED_FREE) && (map->doesUnitTouchEnemy(this, &dx, &dy)))
				movement=MOV_ATTACKING_TARGET;
			else if (performance[FLY])
				movement=MOV_RANDOM_FLY;
			else if (map->getForbidden(posX, posY)&owner->me)
			{
				int shortestDist=INT_MAX;
				bool found=false;
				for (std::list<Building *>::iterator it=owner->virtualBuildings.begin(); it!=owner->virtualBuildings.end(); ++it)
					if ((*it)->type->zonableForbidden)
					{
						int dist=map->warpDistSquare(posX, posY, (*it)->posX, (*it)->posY);
						if (dist<shortestDist)
						{
							targetX=(*it)->posX;
							targetY=(*it)->posY;
							found=true;
						}
					}
				assert(found);
				movement=MOV_ESCAPING_FORBIDDEN;
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
			
			if ((performance[ATTACK_SPEED]) && (medical==MED_FREE) && (owner->map->doesUnitTouchEnemy(this, &dx, &dy)))
				movement=MOV_ATTACKING_TARGET;
			else if (performance[FLY])
			{
				movement=MOV_FLYING_TARGET;
			}
			else if (map->pathfindBuilding(attachedBuilding, canSwim, posX, posY, &dx, &dy))
			{
				if (verbose)
					printf("guid=(%d) Unit found path pos=(%d, %d) to building %d, d=(%d, %d)\n", gid, posX, posY, attachedBuilding->gid, dx, dy);
				movement=MOV_GOING_DXDY;
			}
			else
			{
				if (verbose)
					printf("guid=(%d) Unit failed path pos=(%d, %d) to building %d, d=(%d, %d)\n", gid, posX, posY, attachedBuilding->gid, dx, dy);
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
				fprintf(logFile, "guid=(%d) exiting gbid=%d\n", gid, attachedBuilding->gid);
				attachedBuilding->unitsInside.remove(this);
				attachedBuilding->unitsInsideSubscribe.remove(this);
				attachedBuilding->updateCallLists();
				attachedBuilding->updateConstructionState();
				attachedBuilding=NULL;
				subscribed=false;
				assert(needToRecheckMedical);
			}
			else
			{
				fprintf(logFile, "guid=(%d) can't find exit, gbid=%d\n", gid, attachedBuilding->gid);
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
			if (map->pathfindRessource(teamNumber, destinationPurprose, canSwim, posX, posY, &dx, &dy, &stopWork))
			{
				if (verbose)
					printf("guid=(%d) Unit found path pos=(%d, %d) to ressource %d, d=(%d, %d)\n", gid, posX, posY, destinationPurprose, dx, dy);
				directionFromDxDy();
				movement=MOV_GOING_DXDY;
			}
			else
			{
				if (verbose)
					printf("guid=(%d) Unit failed path pos=(%d, %d) to ressource %d, aborting work.\n", gid, posX, posY, destinationPurprose);

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

void Unit::handleAction(void)
{
	switch (movement)
	{
		case MOV_RANDOM_GROUND:
		{
			assert(!performance[FLY]);
			owner->map->setGroundUnit(posX, posY, NOGUID);
			dx=-1+syncRand()%3;
			dy=-1+syncRand()%3;
			directionFromDxDy();
			setNewValidDirectionGround();
			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());
			selectPreferedGroundMovement();
			speed=performance[action];
			assert(owner->map->getGroundUnit(posX, posY)==NOGUID);
			owner->map->setGroundUnit(posX, posY, gid);
			break;
		}
		
		case MOV_RANDOM_FLY:
		{
			assert(performance[FLY]);
			owner->map->setAirUnit(posX, posY, NOGUID);
			dx=-1+syncRand()%3;
			dy=-1+syncRand()%3;
			directionFromDxDy();
			setNewValidDirectionAir();
			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());
			action=FLY;
			speed=performance[FLY];
			assert(owner->map->getAirUnit(posX, posY)==NOGUID);
			owner->map->setAirUnit(posX, posY, gid);
			break;
		}
		
		case MOV_GOING_TARGET:
		{
			assert(!performance[FLY]);
			owner->map->setGroundUnit(posX, posY, NOGUID);
			gotoGroundTarget();
			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());
			selectPreferedGroundMovement();
			speed=performance[action];
			assert(owner->map->getGroundUnit(posX, posY)==NOGUID);
			owner->map->setGroundUnit(posX, posY, gid);
			break;
		}
		
		case MOV_FLYING_TARGET:
		{
			owner->map->setAirUnit(posX, posY, NOGUID);
			
			flytoTarget();
			
			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());

			action=FLY;
			speed=performance[FLY];

			owner->map->setAirUnit(posX, posY, gid);
			break;
		}
		
		case MOV_ESCAPING_FORBIDDEN:
		{
			assert(!performance[FLY]);
			owner->map->setGroundUnit(posX, posY, NOGUID);
			escapeGroundTarget();
			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());
			selectPreferedGroundMovement();
			speed=performance[action];
			assert(owner->map->getGroundUnit(posX, posY)==NOGUID);
			owner->map->setGroundUnit(posX, posY, gid);
			break;
		}

		case MOV_GOING_DXDY:
		{
			bool fly=performance[FLY];
			if (fly)
				owner->map->setAirUnit(posX, posY, NOGUID);
			else
				owner->map->setGroundUnit(posX, posY, NOGUID);
			
			directionFromDxDy();
			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());
			
			selectPreferedMovement();
			speed=performance[action];
			
			if (fly)
				owner->map->setAirUnit(posX, posY, gid);
			else
			{
				assert(owner->map->getGroundUnit(posX, posY)==NOGUID);
				owner->map->setGroundUnit(posX, posY, gid);
			}
			
			if (verbose)
				printf("guid=(%d) MOV_GOING_DXDY d=(%d, %d; %d).\n", gid, direction, dx, dy);
			break;
		}
		
		case MOV_ENTERING_BUILDING:
		{
			// NOTE : this is a hack : We don't delete the unit on the map
			// because we have to draw it while it is entering.
			// owner->map->setUnit(posX, posY, NOUID);
			posX+=dx;
			posY+=dy;
			directionFromDxDy();
			selectPreferedMovement();
			speed=performance[action];
			break;
		}
		
		case MOV_EXITING_BUILDING:
		{
			directionFromDxDy();
			selectPreferedMovement();
			speed=performance[action];

			if (performance[FLY])
				owner->map->setAirUnit(posX, posY, gid);
			else
			{
				assert(owner->map->getGroundUnit(posX, posY)==NOGUID);
				owner->map->setGroundUnit(posX, posY, gid);
			}
			break;
		}
		
		case MOV_INSIDE:
		{
			break;
		}
		
		case MOV_FILLING:
		{
			directionFromDxDy();
			action=BUILD;
			speed=performance[action];
			break;
		}

		case MOV_ATTACKING_TARGET:
		{
			directionFromDxDy();
			action=ATTACK_SPEED;
			speed=performance[action];
			break;
		}
		
		case MOV_HARVESTING:
		{
			directionFromDxDy();
			action=HARVEST;
			speed=performance[action];
			break;
		}
		
		default:
		{
			assert (false);
			break;
		}
	}
}

void Unit::setNewValidDirectionGround(void)
{
	assert(!performance[FLY]);
	int i=0;
	bool swim=performance[SWIM];
	Uint32 me=owner->me;
	while ( i<8 && !owner->map->isFreeForGroundUnit(posX+dx, posY+dy, swim, me))
	{
		direction=(direction+1)&7;
		dxdyfromDirection();
		i++;
	}
	if (i==8)
	{
		direction=8;
		dxdyfromDirection();
	}
}

void Unit::setNewValidDirectionAir(void)
{
	assert(performance[FLY]);
	int i=0;
	while ( i<8 && !owner->map->isFreeForAirUnit(posX+dx, posY+dy))
	{
		direction=(direction+1)&7;
		dxdyfromDirection();
		i++;
	}
	if (i==8)
	{
		direction=8;
		dxdyfromDirection();
	}
}

/*bool Unit::valid(int x, int y)
{
	// Is there anythig that could block an unit?
	if (performance[FLY])
		return owner->map->isFreeForAirUnit(x, y);
	else
		return owner->map->isFreeForGroundUnit(x, y, performance[SWIM], owner->me);
}

bool Unit::validHard(int x, int y)
{
	// Is there anythig that could block an unit? (except other units)
	assert(!performance[FLY]);
	return owner->map->isHardSpaceForGroundUnit(x, y, performance[SWIM], owner->me);
}

bool Unit::areOnlyUnitsInFront(int dx, int dy)
{
	if (!validHard(posX+dx, posY+dy))
		return false;

	int dir=directionFromDxDy(dx, dy);
	int ldx, ldy;

	dxdyfromDirection((dir+1)&7, &ldx, &ldy);

	if (!validHard(posX+ldx, posY+ldy))
		return false;

	dxdyfromDirection((dir+7)&7, &ldx, &ldy);

	if (!validHard(posX+ldx, posY+ldy))
		return false;

	return true;
}*/

void Unit::flytoTarget()
{
	assert(performance[FLY]);
	int ldx=targetX-posX;
	int ldy=targetY-posY;
	simplifyDirection(ldx, ldy, &dx, &dy);
	directionFromDxDy();
	Map *map=owner->map;
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	int cDirection=direction;
	direction=(cDirection+1)&7;
	dxdyfromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	direction=(cDirection+7)&7;
	dxdyfromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	direction=(cDirection+2)&7;
	dxdyfromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	direction=(cDirection+6)&7;
	dxdyfromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	direction=(cDirection+3)&7;
	dxdyfromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	direction=(cDirection+5)&7;
	dxdyfromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	direction=(cDirection+4)&7;
	dxdyfromDirection();
	if (map->isFreeForAirUnit(posX+dx, posY+dy))
		return;
	dx=0;
	dy=0;
	direction=8;
	if (verbose)
		printf("guid=(%d) flyto failed pos=(%d, %d) \n", gid, posX, posY);
}

void Unit::gotoGroundTarget()
{
	int ldx=targetX-posX;
	int ldy=targetY-posY;
	simplifyDirection(ldx, ldy, &dx, &dy);
	directionFromDxDy();
	bool canSwim=performance[SWIM];
	Uint32 teamMask=owner->me;
	Map *map=owner->map;
	if (map->isFreeForGroundUnit(posX+dx, posY+dy, canSwim, teamMask))
		return;
	int cDirection=direction;
	direction=(cDirection+1)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnit(posX+dx, posY+dy, canSwim, teamMask))
		return;
	direction=(cDirection+7)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnit(posX+dx, posY+dy, canSwim, teamMask))
		return;
	direction=(cDirection+2)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnit(posX+dx, posY+dy, canSwim, teamMask))
		return;
	direction=(cDirection+6)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnit(posX+dx, posY+dy, canSwim, teamMask))
		return;
	direction=(cDirection+3)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnit(posX+dx, posY+dy, canSwim, teamMask))
		return;
	direction=(cDirection+5)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnit(posX+dx, posY+dy, canSwim, teamMask))
		return;
	direction=(cDirection+4)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnit(posX+dx, posY+dy, canSwim, teamMask))
		return;
	dx=0;
	dy=0;
	direction=8;
	if (verbose)
		printf("guid=(%d) gotoGroundTarget failed pos=(%d, %d) \n", gid, posX, posY);
}

void Unit::escapeGroundTarget()
{
	int ldx=posX-targetX;
	int ldy=posY-targetY;
	simplifyDirection(ldx, ldy, &dx, &dy);
	directionFromDxDy();
	bool canSwim=performance[SWIM];
	Map *map=owner->map;
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	int cDirection=direction;
	direction=(cDirection+1)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	direction=(cDirection+7)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	direction=(cDirection+2)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	direction=(cDirection+6)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	direction=(cDirection+3)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	direction=(cDirection+5)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	direction=(cDirection+4)&7;
	dxdyfromDirection();
	if (map->isFreeForGroundUnitNoForbidden(posX+dx, posY+dy, canSwim))
		return;
	dx=0;
	dy=0;
	direction=8;
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

void Unit::dxdyfromDirection(void)
{
	const int tab[9][2]={	{ -1, -1},
							{ 0, -1},
							{ 1, -1},
							{ 1, 0},
							{ 1, 1},
							{ 0, 1},
							{ -1, 1},
							{ -1, 0},
							{ 0, 0} };
	assert(direction>=0);
	assert(direction<=8);
	dx=tab[direction][0];
	dy=tab[direction][1];
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

/*void Unit::dxdyfromDirection(int direction, int *dx, int *dy)
{
	const int tab[9][2]={	{ -1, -1},
							{ 0, -1},
							{ 1, -1},
							{ 1, 0},
							{ 1, 1},
							{ 0, 1},
							{ -1, 1},
							{ -1, 0},
							{ 0, 0} };
	assert(direction>=0);
	assert(direction<=8);
	*dx=tab[direction][0];
	*dy=tab[direction][1];
}*/

void Unit::simplifyDirection(int ldx, int ldy, int *cdx, int *cdy)
{
	int mapw=owner->map->getW();
	int maph=owner->map->getH();
	if (ldx>(mapw>>1))
		ldx-=mapw;
	else if (ldx<-(mapw>>1))
		ldx+=mapw;
	if (ldy>(maph>>1))
		ldy-=maph;
	else if (ldy<-(maph>>1))
		ldy+=maph;

	if (abs(ldx)>(2*abs(ldy)))
	{
		*cdx=SIGN(ldx);
		*cdy=0;
	}
	else if (abs(ldy)>(2*abs(ldx)))
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

Sint32 Unit::GIDtoID(Uint16 gid)
{
	return (gid%1024);
}

Sint32 Unit::GIDtoTeam(Uint16 gid)
{
	assert(gid<32768);
	return (gid/1024);
}

Uint16 Unit::GIDfrom(Sint32 id, Sint32 team)
{
	assert(id>=0);
	assert(id<1024);
	assert(team>=0);
	assert(team<32);
	return id+team*1024;
}


void Unit::integrity()
{
	assert(gid<32768);
	if (isDead)
		return;
	
	if (!needToRecheckMedical)
	{
		assert(activity==ACT_UPGRADING);
		if (!subscribed) 
			assert(displacement==DIS_GOING_TO_BUILDING || displacement==DIS_ENTERING_BUILDING || displacement==DIS_INSIDE);
		assert(destinationPurprose==HEAL || destinationPurprose==FEED);
	}
}

Sint32 Unit::checkSum()
{
	Sint32 cs=0;
	
	cs^=typeNum;
	
	cs^=gid;
	cs^=isDead;

	cs^=posX;
	cs^=posY;
	cs^=delta;
	cs^=dx;
	cs^=dy;
	cs^=direction;
	cs^=insideTimeout;
	cs^=speed;
	cs=(cs<<1)|(cs>>31);
	//printf("%d,1,%x\n", gid, cs);

	cs^=(int)needToRecheckMedical;
	//printf("%d,1a,%x\n", gid, cs);
	cs^=medical;
	cs^=activity;
	cs^=displacement;
	cs^=movement;
	//printf("%d,1b,%x\n", gid, cs);
	cs^=action;
	//printf("%d,1c,%x\n", gid, cs);
	cs^=targetX;
	cs^=targetY;
	//printf("%d,1d,%x\n", gid, cs);
	cs=(cs<<1)|(cs>>31);
	//printf("%d,2,%x\n", gid, cs);

	cs^=hp;
	cs^=trigHP;

	cs^=hungry;
	cs^=trigHungry;

	for (int i=0; i<NB_ABILITY; i++)
	{
		cs^=performance[i];
		cs=(cs<<1)|(cs>>31);
		cs^=level[i];
		cs=(cs<<1)|(cs>>31);
		cs^=canLearn[i];
		cs=(cs<<1)|(cs>>31);
	}
	
	cs^=(attachedBuilding!=NULL ? 1:0);
	cs^=destinationPurprose;
	//printf("%d,3,%x***\n", gid, cs);
	
	return cs;
}


