/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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
#include <Stream.h>

Unit::Unit(GAGCore::InputStream *stream, Team *owner)
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
	assert(race);

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

	// quality parameters
	for (int i=0; i<NB_ABILITY; i++)
	{
		this->performance[i]=race->getUnitType(typeNum, level)->performance[i];
		this->level[i]=level;
		this->canLearn[i]=(bool)race->getUnitType(typeNum, 1)->performance[i]; //TODO: is is a better way to hack this?
	}

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
	targetBuilding=NULL;
	ownExchangeBuilding=NULL;
	foreingExchangeBuilding=NULL;
	destinationPurprose=-1;
	subscribed=false;
	caryedRessource=-1;
	
	// debug vars:
	verbose=false;
}

void Unit::load(GAGCore::InputStream *stream, Team *owner)
{
	stream->readEnterSection("Unit");
	
	// unit specification
	typeNum = stream->readSint32("typeNum");
	race = &(owner->race);
	assert(race);

	// identity
	gid = stream->readUint16("gid");
	this->owner = owner;
	isDead = stream->readSint32("isDead");

	// position
	posX = stream->readSint32("posX");
	posY = stream->readSint32("posY");
	delta = stream->readSint32("delta");
	dx = stream->readSint32("dx");
	dy = stream->readSint32("dy");
	direction = stream->readSint32("direction");
	insideTimeout = stream->readSint32("insideTimeout");
	speed = stream->readSint32("speed");

	// states
	needToRecheckMedical = (bool)stream->readUint32("needToRecheckMedical");
	medical = (Medical)stream->readUint32("medical");
	activity = (Activity)stream->readUint32("activity");
	displacement = (Displacement)stream->readUint32("displacement");
	movement = (Movement)stream->readUint32("movement");
	action = (Abilities)stream->readUint32("action");
	targetX = (Sint32)stream->readSint32("targetX");
	targetY = (Sint32)stream->readSint32("targetY");

	// trigger parameters
	hp = stream->readSint32("hp");
	trigHP = stream->readSint32("trigHP");

	// hungry
	hungry = stream->readSint32("hungry");
	trigHungry = stream->readSint32("trigHungry");
	trigHungryCarying = (trigHungry*4)/10;
	fruitMask = stream->readUint32("fruitMask");
	fruitCount = stream->readUint32("fruitCount");

	// quality parameters
	stream->readEnterSection("abilities");
	for (int i=0; i<NB_ABILITY; i++)
	{
		stream->readEnterSection(i);
		performance[i] = stream->readSint32("performance");
		level[i] = stream->readSint32("level");
		canLearn[i] = (bool)stream->readUint32("canLearn");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	destinationPurprose = stream->readSint32("destinationPurprose");
	subscribed = (bool)stream->readUint32("subscribed");

	caryedRessource = stream->readSint32("caryedRessource");
	verbose = false;
	
	stream->readLeaveSection();
}

void Unit::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Unit");
	
	// unit specification
	// we drop the unittype pointer, we save only the number
	stream->writeSint32(typeNum, "typeNum");

	// identity
	stream->writeUint16(gid, "gid");
	stream->writeSint32(isDead, "isDead");

	// position
	stream->writeSint32(posX, "posX");
	stream->writeSint32(posY, "posY");
	stream->writeSint32(delta, "delta");
	stream->writeSint32(dx, "dx");
	stream->writeSint32(dy, "dy");
	stream->writeSint32(direction, "direction");
	stream->writeSint32(insideTimeout, "insideTimeout");
	stream->writeSint32(speed, "speed");

	// states
	stream->writeUint32((Uint32)needToRecheckMedical, "needToRecheckMedical");
	stream->writeUint32((Uint32)medical, "medical");
	stream->writeUint32((Uint32)activity, "activity");
	stream->writeUint32((Uint32)displacement, "displacement");
	stream->writeUint32((Uint32)movement, "movement");
	stream->writeUint32((Uint32)action, "action");
	stream->writeSint32(targetX, "targetX");
	stream->writeSint32(targetY, "targetY");

	// trigger parameters
	stream->writeSint32(hp, "hp");
	stream->writeSint32(trigHP, "trigHP");

	// hungry
	stream->writeSint32(hungry, "hungry");
	stream->writeSint32(trigHungry, "trigHungry");
	stream->writeUint32(fruitMask, "fruitMask");
	stream->writeUint32(fruitCount, "fruitCount");

	// quality parameters
	stream->writeEnterSection("abilities");
	for (int i=0; i<NB_ABILITY; i++)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(performance[i], "performance");
		stream->writeUint32(level[i], "level");
		stream->writeUint32((Uint32)canLearn[i], "canLearn");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeSint32(destinationPurprose, "destinationPurprose");
	stream->writeUint32((Uint32)subscribed, "subscribed");
	stream->writeSint32(caryedRessource, "caryedRessource");
	
	stream->writeLeaveSection();
}

void Unit::loadCrossRef(GAGCore::InputStream *stream, Team *owner)
{
	stream->readEnterSection("Unit");
	Uint16 gbid;
	
	gbid = stream->readUint16("attachedBuilding");
	if (gbid == NOGBID)
		attachedBuilding = NULL;
	else
		attachedBuilding = owner->myBuildings[Building::GIDtoID(gbid)];
	
	gbid = stream->readUint16("targetBuilding");
	if (gbid == NOGBID)
		targetBuilding = NULL;
	else
		targetBuilding = owner->myBuildings[Building::GIDtoID(gbid)];
		
	gbid = stream->readUint16("ownExchangeBuilding");
	if (gbid == NOGBID)
		ownExchangeBuilding = NULL;
	else
		ownExchangeBuilding = owner->myBuildings[Building::GIDtoID(gbid)];
	
	gbid = stream->readUint16("foreingExchangeBuilding");
	if (gbid == NOGBID)
		foreingExchangeBuilding = NULL;
	else
		foreingExchangeBuilding = owner->myBuildings[Building::GIDtoID(gbid)];
		
	stream->readLeaveSection();
}

void Unit::saveCrossRef(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Unit");
	
	if (attachedBuilding)
		stream->writeUint16(attachedBuilding->gid, "attachedBuilding");
	else
		stream->writeUint16(NOGBID, "attachedBuilding");
		
	if (targetBuilding)
		stream->writeUint16(targetBuilding->gid, "targetBuilding");
	else
		stream->writeUint16(NOGBID, "targetBuilding");
		
	if (ownExchangeBuilding)
		stream->writeUint16(ownExchangeBuilding->gid, "ownExchangeBuilding");
	else
		stream->writeUint16(NOGBID, "ownExchangeBuilding");
		
	if (foreingExchangeBuilding)
		stream->writeUint16(foreingExchangeBuilding->gid, "foreingExchangeBuilding");
	else
		stream->writeUint16(NOGBID, "foreingExchangeBuilding");
		
	stream->writeLeaveSection();
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
			assert(targetBuilding==attachedBuilding);
			targetX=targetBuilding->getMidX();
			targetY=targetBuilding->getMidY();
		}
		break;
		case MED_FREE:
		{
			switch(activity)
			{
				case ACT_FLAG:
				{
					displacement=DIS_GOING_TO_FLAG;
					assert(targetBuilding==attachedBuilding);
					targetX=attachedBuilding->getMidX();
					targetY=attachedBuilding->getMidY();
				}
				break;
				case ACT_UPGRADING:
				{
					displacement=DIS_GOING_TO_BUILDING;
					assert(targetBuilding==attachedBuilding);
					targetX=targetBuilding->getMidX();
					targetY=targetBuilding->getMidY();
				}
				break;
				case ACT_FILLING:
				{
					assert(attachedBuilding);
					if (caryedRessource==destinationPurprose)
					{
						displacement=DIS_GOING_TO_BUILDING;
						targetBuilding=attachedBuilding;
						targetX=targetBuilding->getMidX();
						targetY=targetBuilding->getMidY();
					}
					else if (ownExchangeBuilding)
					{
						if (foreingExchangeBuilding)
						{
							// Here we are exchanging fruits between ownExchangeBuilding and foreingExchangeBuilding.
							if (caryedRessource>=HAPPYNESS_BASE
								&& (foreingExchangeBuilding->receiveRessourceMask & (1<<(caryedRessource-HAPPYNESS_BASE)))
								&& (foreingExchangeBuilding->ressources[caryedRessource]<foreingExchangeBuilding->type->maxRessource[caryedRessource]))
							{
								assert((Uint32)destinationPurprose==(ownExchangeBuilding->receiveRessourceMask & foreingExchangeBuilding->sendRessourceMask));
								displacement=DIS_GOING_TO_BUILDING;
								targetBuilding=foreingExchangeBuilding;
								targetX=targetBuilding->getMidX();
								targetY=targetBuilding->getMidY();
							}
							else
							{
								displacement=DIS_GOING_TO_BUILDING;
								targetBuilding=ownExchangeBuilding;
								targetX=targetBuilding->getMidX();
								targetY=targetBuilding->getMidY();
							}
						}
						else
						{
							assert(false); // Units never get subscription success to fill a food building from an exchange building. (yet)
							// Here we are taking fruits from ownExchangeBuilding to a food building (attachedBuilding).
							
							if (caryedRessource>=HAPPYNESS_BASE
								&& (attachedBuilding->ressources[caryedRessource]<attachedBuilding->type->maxRessource[caryedRessource]))
							{
								assert(destinationPurprose==caryedRessource);
								displacement=DIS_GOING_TO_BUILDING;
								targetBuilding=attachedBuilding;
								targetX=targetBuilding->getMidX();
								targetY=targetBuilding->getMidY();
							}
							else
							{
								assert(destinationPurprose>=HAPPYNESS_BASE);
								displacement=DIS_GOING_TO_BUILDING;
								targetBuilding=ownExchangeBuilding;
								targetX=targetBuilding->getMidX();
								targetY=targetBuilding->getMidY();
							}
						}
					}
					else
					{
						displacement=DIS_GOING_TO_RESSOURCE;
						targetBuilding=NULL;
						bool rv=owner->map->ressourceAvailable(owner->teamNumber, destinationPurprose, performance[SWIM], posX, posY, &targetX, &targetY, NULL);
						fprintf(logFile, "[%d] raa targetXY=(%d, %d)=%d\n", gid, targetX, targetY, rv);
					}
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

void Unit::syncStep(void)
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

void Unit::standardRandomActivity()
{
	attachedBuilding=NULL;
	targetBuilding=NULL;
	ownExchangeBuilding=NULL;
	foreingExchangeBuilding=NULL;
	activity=Unit::ACT_RANDOM;
	displacement=Unit::DIS_RANDOM;
	needToRecheckMedical=true;
	subscribed=false;
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
	targetBuilding=NULL;
	ownExchangeBuilding=NULL;
	foreingExchangeBuilding=NULL;
	subscribed=false;
	assert(needToRecheckMedical);
}

void Unit::handleMedical(void)
{
	if ((displacement==DIS_ENTERING_BUILDING) || (displacement==DIS_INSIDE) || (displacement==DIS_EXITING_BUILDING))
		return;
	
	if (verbose)
		printf("guid=(%d) handleMedical...\n", gid);
	hungry-=race->hungryness;
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
				targetBuilding=NULL;
				ownExchangeBuilding=NULL;
				foreingExchangeBuilding=NULL;
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
	
	if (verbose)
		printf("guid=(%d) handleActivity (medical=%d) (needToRecheckMedical=%d) (attachedBuilding=%p)...\n",
			gid, medical, needToRecheckMedical, attachedBuilding);
	
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
					targetBuilding=NULL;
					if (verbose)
						printf("guid=(%d) unitsWorkingSubscribe(findBestFoodable) dp=(%d), gbid=(%d)\n", gid, destinationPurprose, b->gid);
					b->unitsWorkingSubscribe.push_front(this);
					subscribed=true;
					if (b->subscribeToBringRessources!=1)
					{
						b->subscribeToBringRessources=1;
						b->lastWorkingSubscribe=0;
						owner->subscribeToBringRessources.push_front(b);
					}
					return;
				}
			}

			// second we look for upgrade
			Building *b;
			b=owner->findBestUpgrade(this);
			if (b)
			{
				assert(destinationPurprose>=WALK);
				assert(destinationPurprose<ARMOR);
				activity=ACT_UPGRADING;
				attachedBuilding=b;
				targetBuilding=b;
				if (verbose)
					printf("guid=(%d) going to upgrade at dp=(%d), gbid=(%d)\n", gid, destinationPurprose, b->gid);
				b->unitsInsideSubscribe.push_front(this);
				subscribed=true;
				if (b->subscribeForInside!=1)
				{
					b->subscribeForInside=1;
					b->lastInsideSubscribe=0;
					owner->subscribeForInside.push_front(b);
				}
				return;
			}
			
			// third we go to flag
			b=owner->findBestZonable(this);
			if (b)
			{
				destinationPurprose=-1;
				fprintf(logFile, "[%d] sdp1 destinationPurprose=%d\n", gid, destinationPurprose);
				activity=ACT_FLAG;
				attachedBuilding=b;
				targetBuilding=b;
				if (verbose)
					printf("guid=(%d) unitsWorkingSubscribe(findBestZonable) dp=(%d), gbid=(%d)\n", gid, destinationPurprose, b->gid);
				b->unitsWorkingSubscribe.push_front(this);
				subscribed=true;
				if (b->subscribeForFlaging!=1)
				{
					b->subscribeForFlaging=1;
					b->lastWorkingSubscribe=0;
					owner->subscribeForFlaging.push_back(b);
				}
				return;
			}
			
			// fourth we harvest for construction, or other lower priority.
			if (performance[HARVEST])
			{
				// if we have a ressource
				Building *b=owner->findBestFillable(this);
				if (b)
				{
					assert(b->type->canExchange || (destinationPurprose>=0 && b->neededRessource(destinationPurprose)));
					activity=ACT_FILLING;
					attachedBuilding=b;
					targetBuilding=NULL;
					if (verbose)
						printf("guid=(%d) unitsWorkingSubscribe(findBestFillable) dp=(%d), gbid=(%d)\n", gid, destinationPurprose, b->gid);
					b->unitsWorkingSubscribe.push_front(this);
					subscribed=true;
					if (b->subscribeToBringRessources!=1)
					{
						b->subscribeToBringRessources=1;
						b->lastWorkingSubscribe=0;
						owner->subscribeToBringRessources.push_front(b);
					}
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
				if (b)
				{
					destinationPurprose=HEAL;
					fprintf(logFile, "[%d] sdp2 destinationPurprose=%d\n", gid, destinationPurprose);
					activity=ACT_UPGRADING;
					attachedBuilding=b;
					targetBuilding=b;
					needToRecheckMedical=false;
					if (verbose)
						printf("guid=(%d) Going to heal building\n", gid);
					targetX=attachedBuilding->getMidX();
					targetY=attachedBuilding->getMidY();
					b->unitsInsideSubscribe.push_front(this);
					b->lastInsideSubscribe=0;
					subscribed=true;
					if (b->subscribeForInside!=1)
					{
						b->subscribeForInside=1;
						owner->subscribeForInside.push_front(b);
					}
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
			targetBuilding=NULL;
			ownExchangeBuilding=NULL;
			foreingExchangeBuilding=NULL;
			subscribed=false;
		}

		if (medical==MED_HUNGRY)
		{
			Building *b;
			b=owner->findNearestFood(this);
			if (b!=NULL)
			{
				Team *currentTeam=owner;
				Team *targetTeam=b->owner;
				if (currentTeam!=targetTeam)
				{
					currentTeam->setEvent(posX, posY, Team::UNIT_CONVERTED_LOST, typeNum);
					targetTeam->setEvent(posX, posY, Team::UNIT_CONVERTED_ACQUIERED, typeNum);
					int targetID=-1;
					for (int i=0; i<1024; i++)//we search for a free place for a unit.
						if (targetTeam->myUnits[i]==NULL)
						{
							targetID=i;
							break;
						}

					if (targetID!=-1)
					{
						Sint32 currentID=Unit::GIDtoID(gid);
						assert(currentTeam->myUnits[currentID]);
						currentTeam->myUnits[currentID]=NULL;
						targetTeam->myUnits[targetID]=this;
						Uint16 targetGID=(GIDfrom(targetID, targetTeam->teamNumber));
						printf("Unit guid=%d (%d) switched to guid=%d (%d)\n", gid, Unit::GIDtoTeam(gid), targetGID, Unit::GIDtoTeam(targetGID));
						if (performance[FLY])
						{
							assert(owner->map->getAirUnit(posX, posY)==gid);
							owner->map->setAirUnit(posX, posY, targetGID);
						}
						else
						{
							assert(owner->map->getGroundUnit(posX, posY)==gid);
							owner->map->setGroundUnit(posX, posY, targetGID);
						}
						gid=targetGID;
						owner=targetTeam;
					}
				}

				destinationPurprose=FEED;
				fprintf(logFile, "[%d] sdp3 destinationPurprose=%d\n", gid, destinationPurprose);
				activity=ACT_UPGRADING;
				attachedBuilding=b;
				targetBuilding=b;
				needToRecheckMedical=false;
				if (verbose)
					printf("guid=(%d) Subscribed to food at building gbid=(%d)\n", gid, b->gid);
				b->unitsInsideSubscribe.push_front(this);
				b->lastInsideSubscribe=0;
				subscribed=true;
				if (b->subscribeForInside!=1)
				{
					b->subscribeForInside=1;
					owner->subscribeForInside.push_front(b);
				}
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
				fprintf(logFile, "[%d] sdp4 destinationPurprose=%d\n", gid, destinationPurprose);
				activity=ACT_UPGRADING;
				attachedBuilding=b;
				targetBuilding=b;
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
				fprintf(logFile, "[%d] sdp5 destinationPurprose=%d\n", gid, destinationPurprose);
				owner->map->decRessource(posX+dx, posY+dy, caryedRessource);
				
				targetBuilding=attachedBuilding;
				if (owner->map->doesUnitTouchBuilding(this, attachedBuilding->gid, &dx, &dy))
					displacement=DIS_FILLING_BUILDING;
				else
				{
					displacement=DIS_GOING_TO_BUILDING;
					targetX=targetBuilding->getMidX();
					targetY=targetBuilding->getMidY();
				}
			}
			else if (displacement==DIS_GOING_TO_BUILDING)
			{
				assert(targetBuilding);
				if (owner->map->doesUnitTouchBuilding(this, targetBuilding->gid, &dx, &dy))
					displacement=DIS_FILLING_BUILDING;
			}
			else if (displacement==DIS_FILLING_BUILDING)
			{
				bool loopMove=false;
				bool exchangeReady=false;
				assert(targetBuilding);
				
				if (foreingExchangeBuilding && (targetBuilding==foreingExchangeBuilding))
				{
					// Here we are aside a foreing exchange building, to drop our own ressource and take a foreign ressource:
					assert(targetBuilding);
					assert(ownExchangeBuilding);
					assert(foreingExchangeBuilding);
					assert(targetBuilding->type->canExchange);
					assert(ownExchangeBuilding->type->canExchange);
					assert(foreingExchangeBuilding->type->canExchange);
					assert(owner!=targetBuilding->owner);
					assert(owner==ownExchangeBuilding->owner);
					assert(owner!=foreingExchangeBuilding->owner);
					
					assert(caryedRessource>=HAPPYNESS_BASE);
					// Let's check for possible exchange ressources:
					
					bool goodToGive=targetBuilding->ressources[caryedRessource]<targetBuilding->type->maxRessource[caryedRessource];
					int ressourceToTake=-1;
					for (int r=0; r<HAPPYNESS_COUNT; r++)
						if ((destinationPurprose & (1<<r))
							&& foreingExchangeBuilding->ressources[HAPPYNESS_BASE+r]>0)
						{
							ressourceToTake=HAPPYNESS_BASE+r;
							break;
						}
					
					if (goodToGive && (ressourceToTake>=0))
					{
						// OK, we can proceed to the exchange.
						targetBuilding->ressources[caryedRessource]+=targetBuilding->type->multiplierRessource[caryedRessource];
						targetBuilding->ressources[ressourceToTake]-=targetBuilding->type->multiplierRessource[ressourceToTake];
						
						if (targetBuilding->ressources[caryedRessource]>targetBuilding->type->maxRessource[caryedRessource])
							targetBuilding->ressources[caryedRessource]=targetBuilding->type->maxRessource[caryedRessource];
					
						if (targetBuilding->ressources[ressourceToTake]<0)
							targetBuilding->ressources[ressourceToTake]=0;
					
						caryedRessource=ressourceToTake;
						targetBuilding=ownExchangeBuilding;
						
						displacement=DIS_GOING_TO_BUILDING;
						targetX=targetBuilding->getMidX();
						targetY=targetBuilding->getMidY();
						exchangeReady=true;
						if (verbose)
							printf("guid=(%d) exchangeReady at foreingExchangeBuilding\n", gid);
					}
					else
					{
						if (verbose)
							printf("guid=(%d) loopMove at foreingExchangeBuilding, caryedRessource=%d, destinationPurprose=%d, goodToGive=%d, goodToTake=%d\n", gid, caryedRessource, destinationPurprose, goodToGive, (ressourceToTake>=0));
						loopMove=true;
					}
				}
				else if (foreingExchangeBuilding && (targetBuilding==ownExchangeBuilding))
				{
					// Here we are aside our own exchange building, to take one of our ressource and maybe to drop a (foreign) ressource.
					assert(targetBuilding);
					assert(ownExchangeBuilding);
					assert(foreingExchangeBuilding);
					assert(targetBuilding->type->canExchange);
					assert(ownExchangeBuilding->type->canExchange);
					assert(foreingExchangeBuilding->type->canExchange);
					assert(owner==targetBuilding->owner);
					assert(owner==ownExchangeBuilding->owner);
					assert(owner!=foreingExchangeBuilding->owner);
					
					if (caryedRessource>=0)
					{
						targetBuilding->ressources[caryedRessource]+=targetBuilding->type->multiplierRessource[caryedRessource];
						if (targetBuilding->ressources[caryedRessource]>targetBuilding->type->maxRessource[caryedRessource])
							targetBuilding->ressources[caryedRessource]=targetBuilding->type->maxRessource[caryedRessource];
						caryedRessource=-1;
					}
					
					// Let's grab the right ressource.
					int ressourceToTake=-1;
					for (int r=0; r<HAPPYNESS_COUNT; r++)
						if (foreingExchangeBuilding->receiveRessourceMask & (1<<r))
						{
							ressourceToTake=HAPPYNESS_BASE+r;
							break;
						}
					
					if (ressourceToTake>=0)
					{
						int foreignBuildingDist;
						int timeLeft=(hungry-trigHungry)/race->hungryness;
						if (owner->map->buildingAvailable(foreingExchangeBuilding, performance[SWIM], posX, posY, &foreignBuildingDist)
							&& (foreignBuildingDist<(timeLeft>>1)))
						{
							targetBuilding->ressources[ressourceToTake]-=targetBuilding->type->multiplierRessource[ressourceToTake];
							if (targetBuilding->ressources[ressourceToTake]<0)
								targetBuilding->ressources[ressourceToTake]=0;
							caryedRessource=ressourceToTake;

							targetBuilding=foreingExchangeBuilding;
							displacement=DIS_GOING_TO_BUILDING;
							targetX=targetBuilding->getMidX();
							targetY=targetBuilding->getMidY();
							exchangeReady=true;
							if (verbose)
								printf("guid=(%d) exchangeReady at ownExchangeBuilding\n", gid);
						}
					}
				}
				else if ((foreingExchangeBuilding==NULL) && (targetBuilding==ownExchangeBuilding))
				{
					assert(targetBuilding);
					assert(ownExchangeBuilding);
					assert(foreingExchangeBuilding==NULL);
					assert(targetBuilding->type->canExchange);
					assert(ownExchangeBuilding->type->canExchange);
					assert(owner==targetBuilding->owner);
					assert(owner==ownExchangeBuilding->owner);
					
					assert(attachedBuilding);
					assert(attachedBuilding->type->canFeedUnit);
					assert(destinationPurprose>=HAPPYNESS_BASE);
					
					// Let's grab the right ressource.
					
					if (targetBuilding->ressources[destinationPurprose]>0)
					{
						targetBuilding->ressources[destinationPurprose]-=targetBuilding->type->multiplierRessource[destinationPurprose];
						if (targetBuilding->ressources[destinationPurprose]<0)
							targetBuilding->ressources[destinationPurprose]=0;
						caryedRessource=destinationPurprose;
						fprintf(logFile, "[%d] sdp6 destinationPurprose=%d\n", gid, destinationPurprose);
						
						targetBuilding=attachedBuilding;
						displacement=DIS_GOING_TO_BUILDING;
						targetX=targetBuilding->getMidX();
						targetY=targetBuilding->getMidY();
						exchangeReady=true;
						if (verbose)
							printf("guid=(%d) took a foreign fruit in our exhange building to food\n", gid);
					}
				}
				else if ((caryedRessource>=0) && (targetBuilding->ressources[caryedRessource]<targetBuilding->type->maxRessource[caryedRessource]))
				{
					if (verbose)
						printf("guid=(%d) Giving ressource (%d) to building gbid=(%d) old-amount=(%d)\n", gid, destinationPurprose, targetBuilding->gid, targetBuilding->ressources[caryedRessource]);
					targetBuilding->ressources[caryedRessource]+=targetBuilding->type->multiplierRessource[caryedRessource];
					if (targetBuilding->ressources[caryedRessource]>targetBuilding->type->maxRessource[caryedRessource])
						targetBuilding->ressources[caryedRessource]=targetBuilding->type->maxRessource[caryedRessource];
					caryedRessource=-1;
					BuildingType *bt=targetBuilding->type;
					switch (targetBuilding->constructionResultState)
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
				
				if (!loopMove && !exchangeReady)
				{
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
						int needs[MAX_NB_RESSOURCES];
						attachedBuilding->wishedRessources(needs);
						int teamNumber=owner->teamNumber;
						bool canSwim=performance[SWIM];
						int timeLeft=(hungry-trigHungry)/race->hungryness;
						
						if (timeLeft>0)
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
													int value=(buildingDist<<1)/need; // We double the cost to get a ressource in an exchange building.
													if (value<minValue)
													{
														bestRessource=r;
														minValue=value;

														ownExchangeBuilding=*bi;
														foreingExchangeBuilding=NULL;
														targetBuilding=*bi;
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
								destinationPurprose=bestRessource;
								fprintf(logFile, "[%d] sdp7 destinationPurprose=%d\n", gid, destinationPurprose);
								assert(activity==ACT_FILLING);
								if (takeInExchangeBuilding)
								{
									displacement=DIS_GOING_TO_BUILDING;
									targetX=targetBuilding->getMidX();
									targetY=targetBuilding->getMidY();
								}
								else
								{
									int dummyDist;
									if (owner->map->doesUnitTouchRessource(this, destinationPurprose, &dx, &dy))
										displacement=DIS_HARVESTING;
									else if (map->ressourceAvailable(teamNumber, destinationPurprose, canSwim, posX, posY, &targetX, &targetY, &dummyDist))
									{
										fprintf(logFile, "[%d] rab targetXY=(%d, %d)\n", gid, targetX, targetY);
										displacement=DIS_GOING_TO_RESSOURCE;
									}
									else
									{
										assert(false);//You can remove this assert(), but *do* notice me!
										stopAttachedForBuilding(false);
									}
								}
							}
							else if (targetBuilding!=foreingExchangeBuilding && attachedBuilding->type->canExchange && owner->openMarket())
							{
								// Here we try to start a new exchange with a foreign exchange building:
								assert(owner==targetBuilding->owner);
								if (ownExchangeBuilding)
								{
									assert(owner==ownExchangeBuilding->owner);
									assert(ownExchangeBuilding==attachedBuilding);
								}
								if (foreingExchangeBuilding)
									assert(owner!=foreingExchangeBuilding->owner);

								ownExchangeBuilding=attachedBuilding;

								foreingExchangeBuilding=NULL;
								int minDist=INT_MAX;

								Uint32 sendRessourceMask=ownExchangeBuilding->sendRessourceMask;
								Uint32 receiveRessourceMask=ownExchangeBuilding->receiveRessourceMask;
								for (int ti=0; ti<owner->game->session.numberOfTeam; ti++)
									if (ti!=teamNumber && (owner->game->teams[ti]->sharedVisionExchange & owner->me))
									{
										Team *foreignTeam=owner->game->teams[ti];
										std::list<Building *> foreignFillable=foreignTeam->fillable;
										for (std::list<Building *>::iterator fbi=foreignFillable.begin(); fbi!=foreignFillable.end(); ++fbi)
											if ((*fbi)->type->canExchange)
											{
												Uint32 foreignSendRessourceMask=(*fbi)->sendRessourceMask;
												Uint32 foreignReceiveRessourceMask=(*fbi)->receiveRessourceMask;
												int foreignBuildingDist;
												if ((sendRessourceMask & foreignReceiveRessourceMask)
													&& (receiveRessourceMask & foreignSendRessourceMask)
													&& map->buildingAvailable(*fbi, canSwim, posX, posY, &foreignBuildingDist)
													&& foreignBuildingDist<(timeLeft>>1)
													&& foreignBuildingDist<minDist)
												{
													foreingExchangeBuilding=*fbi;
													minDist=foreignBuildingDist;
													destinationPurprose=receiveRessourceMask & foreignSendRessourceMask;
													fprintf(logFile, "[%d] sdp8 destinationPurprose=%d\n", gid, destinationPurprose);
												}
											}
									}

								if (foreingExchangeBuilding)
								{
									assert(activity==ACT_FILLING);
									displacement=DIS_GOING_TO_BUILDING;
									targetBuilding=ownExchangeBuilding;
									targetX=targetBuilding->getMidX();
									targetY=targetBuilding->getMidY();
								}
								else
								{
									if (verbose)
										printf("guid=(%d) can't find any suitable foreign exchange building, unsubscribing.\n", gid);
									stopAttachedForBuilding(false);
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
						fruitCount=attachedBuilding->eatOnce(&fruitMask);
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
			int usr=attachedBuilding->unitStayRange;
			int usr2=usr*usr;
			if (verbose)
				printf("guid=(%d) ACT_FLAG distance=%d, usr2=%d\n", gid, distance, usr2);
			
			displacement=DIS_GOING_TO_FLAG;
			if (distance<=usr2)
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
								&& map->isRessource(x, y, attachedBuilding->clearingRessources))
							{
								dx=tdx;
								dy=tdy;
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
				int quality=INT_MAX; // Smaller is better.
				movement=MOV_RANDOM_GROUND;
				if (verbose)
					printf("guid=(%d) selecting movement\n", gid);
				
				Building *tempTargetBuilding=NULL;
				for (int x=-8; x<=8; x++)
					for (int y=-8; y<=8; y++)
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
										quality=newQuality;
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
									if (((owner->sharedVisionExchange & tm)==0) || (u->foreingExchangeBuilding==NULL))
									{
										int attackStrength=u->performance[ATTACK_STRENGTH];
										int newQuality=((x*x+y*y)<<8)/(1+attackStrength);
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
											{
												movement=MOV_GOING_TARGET;
												tempTargetBuilding=NULL;
											}
											targetX=posX+x;
											targetY=posY+y;
											quality=newQuality;
										}
									}
								}
							}
						}
				if (movement==MOV_GOING_TARGET && tempTargetBuilding!=NULL)
				{
					if (owner->map->pathfindBuilding(tempTargetBuilding, (performance[SWIM]>0), posX, posY, &dx, &dy, verbose))
					{
						if (verbose)
							printf("guid=(%d) Warrior found path pos=(%d, %d) to building %d, d=(%d, %d)\n", gid, posX, posY, tempTargetBuilding->gid, dx, dy);
						movement=MOV_GOING_DXDY;
					}
					else
					{
						if (verbose)
							printf("guid=(%d) Warrior failed path pos=(%d, %d) to building %d, d=(%d, %d)\n", gid, posX, posY, tempTargetBuilding->gid, dx, dy);
						movement=MOV_RANDOM_GROUND;
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
					if (map->warpDistSquare(x, y, bx, by)<=usr2 && map->isRessource(x, y, attachedBuilding->clearingRessources))
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
				movement=MOV_GOING_DXDY;
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
			if ((performance[ATTACK_SPEED]) && (medical==MED_FREE) && (map->doesUnitTouchEnemy(this, &dx, &dy)))
				movement=MOV_ATTACKING_TARGET;
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
				movement=MOV_GOING_DXDY;
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
			else if (map->pathfindBuilding(targetBuilding, canSwim, posX, posY, &dx, &dy, verbose))
			{
				if (verbose)
					printf("guid=(%d) Unit found path b pos=(%d, %d) to building %d, d=(%d, %d)\n", gid, posX, posY, attachedBuilding->gid, dx, dy);
				movement=MOV_GOING_DXDY;
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
				fprintf(logFile, "guid=(%d) exiting gbid=%d\n", gid, attachedBuilding->gid);
				attachedBuilding->unitsInside.remove(this);
				attachedBuilding->unitsInsideSubscribe.remove(this);
				attachedBuilding->updateCallLists();
				attachedBuilding->updateConstructionState();
				attachedBuilding=NULL;
				targetBuilding=NULL;
				assert(ownExchangeBuilding==NULL);
				assert(foreingExchangeBuilding==NULL);
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
			if (map->pathfindRessource(teamNumber, destinationPurprose, canSwim, posX, posY, &dx, &dy, &stopWork, verbose))
			{
				if (verbose)
					printf("guid=(%d) Unit found path r pos=(%d, %d) to ressource %d, d=(%d, %d)\n", gid, posX, posY, destinationPurprose, dx, dy);
				directionFromDxDy();
				movement=MOV_GOING_DXDY;
			}
			else
			{
				if (verbose)
					printf("guid=(%d) Unit failed path r pos=(%d, %d) to ressource %d, aborting work.\n", gid, posX, posY, destinationPurprose);

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
			owner->map->pathfindRandom(this, verbose);
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
			{
				assert(owner->map->getAirUnit(posX, posY)==NOGUID);
				owner->map->setAirUnit(posX, posY, gid);
			}
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
			{
				assert(owner->map->getAirUnit(posX, posY)==NOGUID);
				owner->map->setAirUnit(posX, posY, gid);
			}
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
	bool swim=(performance[SWIM]>0);
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
		dx=0;
		dy=0;
	}
}

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
	assert(gid<32768);
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

Uint32 Unit::checkSum(std::list<Uint32> *checkSumsList)
{
	Uint32 cs=0;
	
	cs^=typeNum;
	if (checkSumsList)
		checkSumsList->push_back(typeNum);// [0]
	cs=(cs<<1)|(cs>>31);
	
	cs^=isDead;
	if (checkSumsList)
		checkSumsList->push_back(isDead);// [1]
	cs=(cs<<1)|(cs>>31);
	cs^=gid;
	if (checkSumsList)
		checkSumsList->push_back(gid);// [2]
	cs=(cs<<1)|(cs>>31);

	cs^=posX;
	if (checkSumsList)
		checkSumsList->push_back(posX);// [3]
	cs=(cs<<1)|(cs>>31);
	cs^=posY;
	if (checkSumsList)
		checkSumsList->push_back(posY);// [4]
	cs=(cs<<1)|(cs>>31);
	cs^=delta;
	if (checkSumsList)
		checkSumsList->push_back(delta);// [5]
	cs=(cs<<1)|(cs>>31);
	cs^=dx;
	if (checkSumsList)
		checkSumsList->push_back(dx);// [6]
	cs^=dy;
	if (checkSumsList)
		checkSumsList->push_back(dy);// [7]
	cs^=direction;
	if (checkSumsList)
		checkSumsList->push_back(direction);// [8]
	cs=(cs<<1)|(cs>>31);
	cs^=insideTimeout;
	if (checkSumsList)
		checkSumsList->push_back(insideTimeout);// [9]
	cs=(cs<<1)|(cs>>31);
	cs^=speed;
	if (checkSumsList)
		checkSumsList->push_back(speed);// [10]
	cs=(cs<<1)|(cs>>31);

	cs^=(int)needToRecheckMedical;
	if (checkSumsList)
		checkSumsList->push_back(needToRecheckMedical);// [11]
	cs=(cs<<1)|(cs>>31);
	cs^=medical;
	if (checkSumsList)
		checkSumsList->push_back(medical);// [12]
	cs^=activity;
	if (checkSumsList)
		checkSumsList->push_back(activity);// [13]
	cs^=displacement;
	if (checkSumsList)
		checkSumsList->push_back(displacement);// [14]
	cs^=movement;
	if (checkSumsList)
		checkSumsList->push_back(movement);// [15]
	cs^=action;
	if (checkSumsList)
		checkSumsList->push_back(action);// [16]
	cs=(cs<<1)|(cs>>31);
	cs^=targetX;
	if (checkSumsList)
		checkSumsList->push_back(targetX);// [17]
	cs^=targetY;
	if (checkSumsList)
		checkSumsList->push_back(targetY);// [18]
	cs=(cs<<1)|(cs>>31);

	cs^=hp;
	if (checkSumsList)
		checkSumsList->push_back(hp);// [19]
	cs^=trigHP;
	if (checkSumsList)
		checkSumsList->push_back(trigHP);// [20]
	cs=(cs<<1)|(cs>>31);

	cs^=hungry;
	if (checkSumsList)
		checkSumsList->push_back(hungry);// [21]
	cs^=trigHungry;
	if (checkSumsList)
		checkSumsList->push_back(trigHungry);// [22]
	cs^=trigHungryCarying;
	if (checkSumsList)
		checkSumsList->push_back(trigHungryCarying);// [23]
	cs=(cs<<1)|(cs>>31);
	
	cs^=fruitMask;
	if (checkSumsList)
		checkSumsList->push_back(fruitMask);// [24]
	cs^=fruitCount;
	if (checkSumsList)
		checkSumsList->push_back(fruitCount);// [25]
	cs=(cs<<1)|(cs>>31);

	for (int i=0; i<NB_ABILITY; i++)
	{
		cs^=performance[i];
		cs=(cs<<1)|(cs>>31);
		cs^=level[i];
		cs=(cs<<1)|(cs>>31);
		cs^=(Uint32)canLearn[i];
		cs=(cs<<1)|(cs>>31);
	}
	if (checkSumsList)
		checkSumsList->push_back(cs);// [26]
	cs=(cs<<1)|(cs>>31);
	
	cs^=(attachedBuilding!=NULL ? 1:0);
	if (checkSumsList)
		checkSumsList->push_back((attachedBuilding!=NULL ? 1:0));// [27]
	cs=(cs<<1)|(cs>>31);
	cs^=(targetBuilding!=NULL ? 1:0);
	if (checkSumsList)
		checkSumsList->push_back((targetBuilding!=NULL ? 1:0));// [28]
	cs^=(ownExchangeBuilding!=NULL ? 2:0);
	if (checkSumsList)
		checkSumsList->push_back((ownExchangeBuilding!=NULL ? 1:0));// [29]
	cs^=(foreingExchangeBuilding!=NULL ? 4:0);
	if (checkSumsList)
		checkSumsList->push_back((foreingExchangeBuilding!=NULL ? 1:0));// [30]
	cs=(cs<<1)|(cs>>31);
	
	cs^=destinationPurprose;
	if (checkSumsList)
		checkSumsList->push_back(destinationPurprose);// [31]
	cs^=(Uint32)subscribed;
	if (checkSumsList)
		checkSumsList->push_back(subscribed);// [32]
	cs^=caryedRessource;
	if (checkSumsList)
		checkSumsList->push_back(caryedRessource);// [33]
	
	if (checkSumsList)
		checkSumsList->push_back(0);// [34]
	if (checkSumsList)
		checkSumsList->push_back(0);// [35]
	if (checkSumsList)
		checkSumsList->push_back(0);// [36]
	if (checkSumsList)
		checkSumsList->push_back(0);// [37]
	if (checkSumsList)
		checkSumsList->push_back(0);// [38]
	if (checkSumsList)
		checkSumsList->push_back(0);// [39]
	
	return cs;
}


