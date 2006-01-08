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
#include <set>

Unit::Unit(GAGCore::InputStream *stream, Team *owner, Sint32 versionMinor)
{
	logFile = globalContainer->logFileManager->getFile("Unit.log");
	load(stream, owner, versionMinor);
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
		this->canLearn[i]=(bool)race->getUnitType(typeNum, 3)->performance[i]; //TODO: is is a better way to hack this?
		// This hack prevent units from unlearning. Units level 3 must have all the abilities of all preceedings levels
	}
	
	experience = 0;
	experienceLevel = 0;

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
	validTarget=false;
	magicActionTimeout=0;

	// trigger parameters
	hp=0;

	// warriors fight to death TODO: this is overided !?!?
	if (performance[ATTACK_SPEED])
		trigHP=0;
	else
		trigHP=20;

	// warriors wait more tiem before going to eat
	hungry=HUNGRY_MAX;
	if (performance[ATTACK_SPEED])
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
	
	// gui
	levelUpAnimation = 0;
	magicActionAnimation = 0;
	
	// debug vars:
	verbose=false;
}

void Unit::load(GAGCore::InputStream *stream, Team *owner, Sint32 versionMinor)
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
	if (versionMinor >= 46)
		validTarget = (bool)stream->readSint32("validTarget");
	else
		validTarget = false;
	
	if (versionMinor >= 41)
		magicActionTimeout = stream->readSint32("magicActionTimeout");
	else
		magicActionTimeout = 0;

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
		if ((versionMinor < 41) && (i >= 10) && (i < 15))
		{
			performance[i] = race->getUnitType(typeNum, 0)->performance[i];
			level[i] = 0;
			canLearn[i] = (bool)race->getUnitType(typeNum, 1)->performance[i];
		}
		else
		{
			stream->readEnterSection(i);
			performance[i] = stream->readSint32("performance");
			level[i] = stream->readSint32("level");
			canLearn[i] = (bool)stream->readUint32("canLearn");
			stream->readLeaveSection();
		}
	}
	stream->readLeaveSection();
	
	if (versionMinor >= 40)
	{
		experience = stream->readSint32("experience");
		experienceLevel = stream->readSint32("experienceLevel");
	}
	else
	{
		experience = 0;
		experienceLevel = 0;
	}

	destinationPurprose = stream->readSint32("destinationPurprose");
	if (versionMinor < 41 && destinationPurprose >= 10)
		destinationPurprose += 5;
	
	subscribed = (bool)stream->readUint32("subscribed");

	caryedRessource = stream->readSint32("caryedRessource");
	
	// gui
	levelUpAnimation = 0;
	magicActionAnimation = 0;
	
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
	stream->writeSint32(validTarget, "validTarget");
	stream->writeSint32(magicActionTimeout, "magicActionTimeout");

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
	
	stream->writeSint32(experience, "experience");
	stream->writeSint32(experienceLevel, "experienceLevel");

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
			validTarget=true;
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
					validTarget=true;
				}
				break;
				case ACT_UPGRADING:
				{
					displacement=DIS_GOING_TO_BUILDING;
					assert(targetBuilding==attachedBuilding);
					targetX=targetBuilding->getMidX();
					targetY=targetBuilding->getMidY();
					validTarget=true;
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
						validTarget=true;
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
								validTarget=true;
							}
							else
							{
								displacement=DIS_GOING_TO_BUILDING;
								targetBuilding=ownExchangeBuilding;
								targetX=targetBuilding->getMidX();
								targetY=targetBuilding->getMidY();
								validTarget=true;
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
								validTarget=true;
							}
							else
							{
								assert(destinationPurprose>=HAPPYNESS_BASE);
								displacement=DIS_GOING_TO_BUILDING;
								targetBuilding=ownExchangeBuilding;
								targetX=targetBuilding->getMidX();
								targetY=targetBuilding->getMidY();
								validTarget=true;
							}
						}
					}
					else
					{
						displacement=DIS_GOING_TO_RESSOURCE;
						targetBuilding=NULL;
						owner->map->ressourceAvailable(owner->teamNumber, destinationPurprose, performance[SWIM], posX, posY, &targetX, &targetY, NULL);
						validTarget=true;
						//fprintf(logFile, "[%d] raa targetXY=(%d, %d)=%d\n", gid, targetX, targetY, rv);
					}
				}
				break;
				case ACT_RANDOM :
				{
					displacement=DIS_RANDOM;
					validTarget=false;
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
			
			int degats=getRealAttackStrength()-enemy->getRealArmor();
			if (degats<=0)
				degats=1;
			enemy->hp-=degats;
			enemy->owner->setEvent(posX+dx, posY+dy, Team::UNIT_UNDER_ATTACK_EVENT, enemyGUID, enemyTeam);
			incrementExperience(degats);
		}
		else
		{
			Uint16 enemyGBID=owner->map->getBuilding(posX+dx, posY+dy);
			if (enemyGBID!=NOGBID)
			{
				int enemyID=Building::GIDtoID(enemyGBID);
				int enemyTeam=Building::GIDtoTeam(enemyGBID);
				Building *enemy=owner->game->teams[enemyTeam]->myBuildings[enemyID];
				int degats=getRealAttackStrength()-enemy->type->armor;
				if (degats<=0)
					degats=1;
				enemy->hp-=degats;
				enemy->owner->setEvent(posX+dx, posY+dy, Team::BUILDING_UNDER_ATTACK_EVENT, enemyGBID, enemyTeam);
				if (enemy->hp<0)
					enemy->kill();
				incrementExperience(degats);
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
			owner->map->setMapExploredByUnit(posX-3, posY-3, 7, 7, owner->teamNumber);
		}
		else
		{
			owner->map->setMapDiscovered(posX-1, posY-1, 3, 3, owner->sharedVisionOther);
			owner->map->setMapBuildingsDiscovered(posX-1, posY-1, 3, 3, owner->sharedVisionOther, owner->game->teams);
			owner->map->setMapExploredByUnit(posX-1, posY-1, 3, 3, owner->teamNumber);
		}
	}
	
	// gui
	if (levelUpAnimation > 0)
		levelUpAnimation--;
	if (magicActionAnimation > 0)
		magicActionAnimation--;
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
	validTarget=false;
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
	validTarget=false;
	
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

void Unit::handleMagic(void)
{
	assert(medical==MED_FREE);
	assert((displacement!=DIS_ENTERING_BUILDING) && (displacement!=DIS_INSIDE) && (displacement!=DIS_EXITING_BUILDING));
	
	magicActionTimeout--;
	if (magicActionTimeout > 0)
		return;
	
	Map *map = &owner->game->map;
	Team **teams = owner->game->teams;
	
	bool hasUsedMagicAction = false;
	if (performance[MAGIC_ATTACK_AIR] || performance[MAGIC_ATTACK_GROUND])
	{
		std::set<Uint16> damagedBuildings;
		damagedBuildings.insert(NOGBID);
		for (int yi=posY-3; yi<=posY+3; yi++)
			for (int xi=posX-3; xi<=posX+3; xi++)
			{
				// damaging enemy units:
				for (int altitude=0; altitude<2; altitude++)
				{
					Uint16 targetGUID;
					Sint32 attackForce;
					if ((altitude == 1) && performance[MAGIC_ATTACK_AIR])
					{
						targetGUID = map->getAirUnit(xi, yi);
						attackForce = performance[MAGIC_ATTACK_AIR];
					}
					else if ((altitude == 0) && performance[MAGIC_ATTACK_GROUND])
					{
						targetGUID = map->getGroundUnit(xi, yi);
						attackForce = performance[MAGIC_ATTACK_GROUND];
					}
					else
						continue;
					if (targetGUID != NOGUID)
					{
						Sint32 targetTeam = Unit::GIDtoTeam(targetGUID);
						Uint16 targetID = Unit::GIDtoID(targetGUID);
						Uint32 targetTeamMask = 1<<targetTeam;
						if (owner->enemies & targetTeamMask)
						{
							Unit *enemyUnit = teams[targetTeam]->myUnits[targetID];
							Sint32 damage = attackForce + experienceLevel - enemyUnit->getRealArmor();
							if (damage > 0)
							{
								enemyUnit->hp -= damage;
								enemyUnit->owner->setEvent(xi, yi, Team::UNIT_UNDER_ATTACK_EVENT, targetGUID, targetTeam);
								incrementExperience(damage);
								magicActionAnimation = MAGIC_ACTION_ANIMATION_FRAME_COUNT;
								hasUsedMagicAction = true;
							}
						}
					}
				}
				
				// damaging enemy buildings:
				if (performance[MAGIC_ATTACK_GROUND])
				{
					Uint16 targetGBID = map->getBuilding(xi, yi);
					if (damagedBuildings.insert(targetGBID).second)
					{
						Sint32 targetTeam = Building::GIDtoTeam(targetGBID);
						Uint16 targetID = Building::GIDtoID(targetGBID);
						Uint32 targetTeamMask = 1<<targetTeam;
						if (owner->enemies & targetTeamMask)
						{
							Building *enemyBuilding = teams[targetTeam]->myBuildings[targetID];
							Sint32 damage = performance[MAGIC_ATTACK_GROUND] + experienceLevel - enemyBuilding->type->armor;
							if (damage > 0)
							{
								enemyBuilding->hp -= damage;
								enemyBuilding->owner->setEvent(xi, yi, Team::BUILDING_UNDER_ATTACK_EVENT, targetGBID, targetTeam);
								if (enemyBuilding->hp <= 0)
									enemyBuilding->kill();
								incrementExperience(damage);
								magicActionAnimation = MAGIC_ACTION_ANIMATION_FRAME_COUNT;
								hasUsedMagicAction = true;
							}
						}
					}
				}
			}
		
		Sint32 magicLevel = std::max(level[MAGIC_ATTACK_AIR], level[MAGIC_ATTACK_GROUND]);
		if (hasUsedMagicAction)
			magicActionTimeout = race->getUnitType(typeNum, level[magicLevel])->magicActionCooldown;
	}
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
			// disconnect from building
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
			validTarget=false;
			
			// remove from map
			if (performance[FLY])
				owner->map->setAirUnit(posX, posY, NOGUID);
			else
				owner->map->setGroundUnit(posX, posY, NOGUID);
				
			// generate death animation
			owner->map->getSector(posX, posY)->deathAnimations.push_back(new UnitDeathAnimation(posX, posY, owner));
		}
		isDead = true;
	}
}

void Unit::handleActivity(void)
{
	// freeze unit health when inside a building
	if ((displacement==DIS_ENTERING_BUILDING) || (displacement==DIS_INSIDE) || (displacement==DIS_EXITING_BUILDING))
		return;
	
	if (verbose)
		printf("guid=(%d) handleActivity (medical=%d, activity=%d) (needToRecheckMedical=%d) (attachedBuilding=%p)...\n",
			gid, medical, activity, needToRecheckMedical, attachedBuilding);
	
	if (medical==MED_FREE)
	{
		handleMagic();
		
		if (activity==ACT_RANDOM)
		{
			// look for a "job"
			// else keep walking around
			if (verbose)
				printf("guid=(%d) looking for a job...\n", gid);

			// first we look for a food building to fill, because it is the first priority.
			// FIXME: Ugly hacks added by Andrew to make sure that globs only subscribe to an inn they have a chance of being accepted at
			if (performance[HARVEST])
			{
				Building *b=owner->findBestFoodable(this);

				int dist;
				// Is there a building we can reach in time?
				const int timeLeft=(hungry-trigHungry)/race->hungryness;
				if (b!=NULL && owner->map->buildingAvailable(b, performance[SWIM], posX, posY, &dist) && dist<timeLeft)
				{
					bool canSubscribe=(caryedRessource>=0) && b->neededRessource(caryedRessource);

					if (!canSubscribe)
					{
						int needs[MAX_NB_RESSOURCES];
						b->wishedRessources(needs);
						for (int r=0; r<MAX_RESSOURCES; r++)
							if (needs[r]>0 && owner->map->ressourceAvailable(owner->teamNumber, r, performance[SWIM], posX, posY, &dist) && (dist<timeLeft))
							{
								canSubscribe=true;
								break;
							};
					};

					if (canSubscribe)
					{
						assert(destinationPurprose>=0);
						assert(b->neededRessource(destinationPurprose));
						activity=ACT_FILLING;
						attachedBuilding=b;
						targetBuilding=NULL;
						if (verbose)
							printf("guid=(%d) unitsWorkingSubscribe(findBestZonable) dp=(%d), gbid=(%d)\n", gid, destinationPurprose, b->gid);
						b->unitsWorkingSubscribe.push_front(this);
						subscribed=true;
						if (b->subscribeToBringRessources!=1)
							{
								b->subscribeToBringRessources=1;
								owner->subscribeToBringRessources.push_front(b);
							}
						if (b->subscriptionWorkingTimer<=0)
							b->subscriptionWorkingTimer=1;
						return;
					}
				}
			}
			
			// second we go to flag
			Building *b;
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
					owner->subscribeForFlaging.push_back(b);
				}
				if (b->subscriptionWorkingTimer<=0)
					b->subscriptionWorkingTimer=1;
				return;
			}

			// third we look for upgrade
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
					owner->subscribeForInside.push_front(b);
				}
				if (b->subscriptionInsideTimer<=0)
					b->subscriptionInsideTimer=1;
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
						owner->subscribeToBringRessources.push_front(b);
					}
					if (b->subscriptionWorkingTimer<=0)
						b->subscriptionWorkingTimer=1;
					return;
				}
			}
			
			if (verbose)
				printf("guid=(%d) no job found.\n", gid);
			
			// nothing to do:
			// we go to a heal building if we'r not fully healed: (1/8 trigger)
			if (hp+(performance[HP]/10) < performance[HP])
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
					validTarget=true;
					b->unitsInsideSubscribe.push_front(this);
					subscribed=true;
					if (b->subscribeForInside!=1)
					{
						b->subscribeForInside=1;
						owner->subscribeForInside.push_front(b);
					}
					if (b->subscriptionInsideTimer<=0)
						b->subscriptionInsideTimer=1;
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
				if (currentTeam != targetTeam)
				{
					// Unit conversion code
					
					// Send events and keep track of number of unit converted
					currentTeam->setEvent(posX, posY, Team::UNIT_CONVERTED_LOST, typeNum, targetTeam->teamNumber);
					currentTeam->unitConversionLost++;
					targetTeam->setEvent(posX, posY, Team::UNIT_CONVERTED_ACQUIERED, typeNum, currentTeam->teamNumber);
					targetTeam->unitConversionGained++;
					
					// Find free slot in other team
					int targetID=-1;
					for (int i=0; i<1024; i++)//we search for a free place for a unit.
						if (targetTeam->myUnits[i]==NULL)
						{
							targetID=i;
							break;
						}

					// If free slot, do the conversion, change owner and ID
					if (targetID!=-1)
					{
						Sint32 currentID=Unit::GIDtoID(gid);
						assert(currentTeam->myUnits[currentID]);
						currentTeam->myUnits[currentID]=NULL;
						targetTeam->myUnits[targetID]=this;
						Uint16 targetGID=(GIDfrom(targetID, targetTeam->teamNumber));
						if (verbose)
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
				subscribed=true;
				if (b->subscribeForInside!=1)
				{
					b->subscribeForInside=1;
					owner->subscribeForInside.push_front(b);
				}
				if (b->subscriptionInsideTimer<=0)
					b->subscriptionInsideTimer=1;
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
				subscribed=true;
				if (b->subscribeForInside!=1)
				{
					b->subscribeForInside=1;
					owner->subscribeForInside.push_front(b);
				}
				if (b->subscriptionInsideTimer<=0)
					b->subscriptionInsideTimer=1;
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
		if (verbose)
			printf("guid=(%d) handleDisplacement() subscribed, then displacement=DIS_RANDOM\n", gid);
		displacement=DIS_RANDOM;
		validTarget=false;
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
				if (owner->map->doesUnitTouchRessource(this, destinationPurprose, &dx, &dy))
				{
					displacement=DIS_HARVESTING;
					validTarget=false;
				}
			}
			else if (displacement==DIS_HARVESTING)
			{
				// we got the ressource.
				caryedRessource=destinationPurprose;
				fprintf(logFile, "[%d] sdp5 destinationPurprose=%d\n", gid, destinationPurprose);
				owner->map->decRessource(posX+dx, posY+dy, caryedRessource);
				assert(movement == MOV_HARVESTING);
				movement = MOV_RANDOM_GROUND; // we do this to avoid the handleMovement() to aditionaly decRessource() the same ressource.
				
				targetBuilding=attachedBuilding;
				if (owner->map->doesUnitTouchBuilding(this, attachedBuilding->gid, &dx, &dy))
				{
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
				if (owner->map->doesUnitTouchBuilding(this, targetBuilding->gid, &dx, &dy))
				{
					displacement=DIS_FILLING_BUILDING;
					validTarget=false;
				}
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
						validTarget=true;
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
							validTarget=true;
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
						validTarget=true;
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
							if (attachedBuilding->hp > bt->hpMax)
								attachedBuilding->hp = bt->hpMax;
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
						validTarget=false;
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
									validTarget=true;
								}
								else
								{
									int dummyDist;
									if (owner->map->doesUnitTouchRessource(this, destinationPurprose, &dx, &dy))
									{
										displacement=DIS_HARVESTING;
										validTarget=false;
									}
									else if (map->ressourceAvailable(teamNumber, destinationPurprose, canSwim, posX, posY, &targetX, &targetY, &dummyDist))
									{
										fprintf(logFile, "[%d] rab targetXY=(%d, %d)\n", gid, targetX, targetY);
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
									validTarget=true;
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
				if (owner->map->doesUnitTouchBuilding(this, attachedBuilding->gid, &dx, &dy))
				{
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
				
				if (destinationPurprose==FEED)
				{
					insideTimeout=-attachedBuilding->type->timeToFeedUnit;
					speed=attachedBuilding->type->insideSpeed;
				}
				else if (destinationPurprose==HEAL)
				{
					//insideTimeout=-(attachedBuilding->type->timeToHealUnit*(performance[HP]-hp))/performance[HP];
					insideTimeout=-attachedBuilding->type->timeToHealUnit;
					speed=(attachedBuilding->type->insideSpeed*performance[HP])/(performance[HP]-hp);
				}
				else
				{
					insideTimeout=-attachedBuilding->type->upgradeTime[destinationPurprose];
					speed=attachedBuilding->type->insideSpeed;
				}
			}
			else if (displacement==DIS_INSIDE)
			{
				// we stay inside while the unit upgrades.
				if (insideTimeout>=0)
				{
					//printf("Exiting building\n");
					displacement=DIS_EXITING_BUILDING;
					validTarget=false;

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
							//printf("Ability %d got level %d\n", destinationPurprose, attachedBuilding->type->level+1);
							assert(canLearn[destinationPurprose]);
							level[destinationPurprose] = attachedBuilding->type->level + 1;
							UnitType *ut = race->getUnitType(typeNum, level[destinationPurprose]);
							performance[destinationPurprose] = ut->performance[destinationPurprose];
							//printf("New performance[%d]=%d\n", destinationPurprose, performance[destinationPurprose]);
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

void Unit::handleMovement(void)
{
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
					&& ((mapCase.ressource.type == WOOD)
						|| (mapCase.ressource.type == CORN)
						|| (mapCase.ressource.type == PAPYRUS)
						|| (mapCase.ressource.type == ALGA)))
				{
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
					tab[i] = owner->map->getExplored(posX + dxTab[i], posY + dyTab[i], owner->teamNumber);
				//printf("tab ");
				//for (int i = 0; i < 8; i++)
				//	printf("%3d; ", tab[i]);
				//printf("d=%d\n", direction);
				for (int di = 0; di < 8; di++)
				{
					int d = (di + direction + 4) & 7;
					if ((tab[d] > 0) && (tab[(d + 1) & 7] == 0) && (tab[(d + 2) & 7] == 0))
					{
						direction = (d + 1) & 7;
						dxdyfromDirection();
						movement = MOV_GOING_DXDY;
						found = true;
						break;
					}
				}
				if (!found)
				{
					int scoreX = 0;
					int scoreY = 0;
					for (int delta = -3; delta <= 3; delta++)
					{
						scoreX += owner->map->getExplored(posX - 4, posY + delta, owner->teamNumber);
						scoreX -= owner->map->getExplored(posX + 4, posY + delta, owner->teamNumber);
						scoreY += owner->map->getExplored(posX + delta, posY - 4, owner->teamNumber);
						scoreY -= owner->map->getExplored(posX + delta, posY + 4, owner->teamNumber);
					}
					int cdx, cdy;
					simplifyDirection(scoreX, scoreY, &cdx, &cdy);
					//printf("score = (%2d, %2d), cd = (%d, %d)\n", scoreX, scoreY, cdx, cdy);
					if (cdx == 0 && cdy == 0)
						movement = MOV_RANDOM_FLY;
					else
					{
						dx = cdx;
						dy = cdy;
						directionFromDxDy();
						movement = MOV_GOING_DXDY;
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
			int quality=INT_MAX; // Smaller is better.
			movement=MOV_RANDOM_GROUND;
			if (verbose)
				printf("guid=(%d) selecting movement\n", gid);
			
			// we look for the best target to attack around us
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
									validTarget=true;
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
									int attackStrength=u->getRealAttackStrength();
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
										validTarget=true;
										quality=newQuality;
									}
								}
							}
						}
					}
			// we try to go to enemy building
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
			// if we haven't find anything satisfactory, follow guard area gradients
			if (movement == MOV_RANDOM_GROUND)
			{
				if (owner->map->pathfindGuardArea(owner->teamNumber, (performance[SWIM]>0), posX, posY, &dx, &dy))
				{
					directionFromDxDy();
					movement = MOV_GOING_DXDY;
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
						dxdyfromDirection(d, &cdx, &cdy);
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
						dxdyfromDirection();
						movement = MOV_GOING_DXDY;
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
					if (map->warpDistSquare(x, y, bx, by)<=usr2 && map->isRessourceTakeable(x, y, attachedBuilding->clearingRessources))
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

int Unit::getRealArmor(void) const
{
	int armorReductionPerHappyness = race->getUnitType(typeNum, level[ARMOR])->armorReductionPerHappyness;
	return performance[ARMOR] - fruitCount * armorReductionPerHappyness;
}

int Unit::getRealAttackStrength(void) const
{
	return performance[ATTACK_STRENGTH] + experienceLevel;
}

int Unit::getNextLevelThreshold(void) const
{
	return (experienceLevel + 1) * (experienceLevel + 1) * race->getUnitType(typeNum, level[ATTACK_STRENGTH])->experiencePerLevel;
}

void Unit::incrementExperience(int increment)
{
	int nextLevelThreshold = getNextLevelThreshold();
	experience += increment;
	if (experience > nextLevelThreshold)
	{
		experienceLevel++;
		experience -= nextLevelThreshold;
		levelUpAnimation = LEVEL_UP_ANIMATION_FRAME_COUNT;
	}
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

Uint32 Unit::checkSum(std::vector<Uint32> *checkSumsVector)
{
	Uint32 cs=0;
	
	cs^=typeNum;
	if (checkSumsVector)
		checkSumsVector->push_back(typeNum);// [0]
	cs=(cs<<1)|(cs>>31);
	
	cs^=isDead;
	if (checkSumsVector)
		checkSumsVector->push_back(isDead);// [1]
	cs=(cs<<1)|(cs>>31);
	cs^=gid;
	if (checkSumsVector)
		checkSumsVector->push_back(gid);// [2]
	cs=(cs<<1)|(cs>>31);

	cs^=posX;
	if (checkSumsVector)
		checkSumsVector->push_back(posX);// [3]
	cs=(cs<<1)|(cs>>31);
	cs^=posY;
	if (checkSumsVector)
		checkSumsVector->push_back(posY);// [4]
	cs=(cs<<1)|(cs>>31);
	cs^=delta;
	if (checkSumsVector)
		checkSumsVector->push_back(delta);// [5]
	cs=(cs<<1)|(cs>>31);
	cs^=dx;
	if (checkSumsVector)
		checkSumsVector->push_back(dx);// [6]
	cs^=dy;
	if (checkSumsVector)
		checkSumsVector->push_back(dy);// [7]
	cs^=direction;
	if (checkSumsVector)
		checkSumsVector->push_back(direction);// [8]
	cs=(cs<<1)|(cs>>31);
	cs^=insideTimeout;
	if (checkSumsVector)
		checkSumsVector->push_back(insideTimeout);// [9]
	cs=(cs<<1)|(cs>>31);
	cs^=speed;
	if (checkSumsVector)
		checkSumsVector->push_back(speed);// [10]
	cs=(cs<<1)|(cs>>31);

	cs^=(int)needToRecheckMedical;
	if (checkSumsVector)
		checkSumsVector->push_back(needToRecheckMedical);// [11]
	cs=(cs<<1)|(cs>>31);
	cs^=medical;
	if (checkSumsVector)
		checkSumsVector->push_back(medical);// [12]
	cs^=activity;
	if (checkSumsVector)
		checkSumsVector->push_back(activity);// [13]
	cs^=displacement;
	if (checkSumsVector)
		checkSumsVector->push_back(displacement);// [14]
	cs^=movement;
	if (checkSumsVector)
		checkSumsVector->push_back(movement);// [15]
	cs^=action;
	if (checkSumsVector)
		checkSumsVector->push_back(action);// [16]
	cs=(cs<<1)|(cs>>31);
	cs^=targetX;
	if (checkSumsVector)
		checkSumsVector->push_back(targetX);// [17]
	cs^=targetY;
	if (checkSumsVector)
		checkSumsVector->push_back(targetY);// [18]
	cs=(cs<<1)|(cs>>31);

	cs^=hp;
	if (checkSumsVector)
		checkSumsVector->push_back(hp);// [19]
	cs^=trigHP;
	if (checkSumsVector)
		checkSumsVector->push_back(trigHP);// [20]
	cs=(cs<<1)|(cs>>31);

	cs^=hungry;
	if (checkSumsVector)
		checkSumsVector->push_back(hungry);// [21]
	cs^=trigHungry;
	if (checkSumsVector)
		checkSumsVector->push_back(trigHungry);// [22]
	cs^=trigHungryCarying;
	if (checkSumsVector)
		checkSumsVector->push_back(trigHungryCarying);// [23]
	cs=(cs<<1)|(cs>>31);
	
	cs^=fruitMask;
	if (checkSumsVector)
		checkSumsVector->push_back(fruitMask);// [24]
	cs^=fruitCount;
	if (checkSumsVector)
		checkSumsVector->push_back(fruitCount);// [25]
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
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [26]
	cs=(cs<<1)|(cs>>31);
	
	cs^=(attachedBuilding!=NULL ? 1:0);
	if (checkSumsVector)
		checkSumsVector->push_back((attachedBuilding!=NULL ? 1:0));// [27]
	cs=(cs<<1)|(cs>>31);
	cs^=(targetBuilding!=NULL ? 1:0);
	if (checkSumsVector)
		checkSumsVector->push_back((targetBuilding!=NULL ? 1:0));// [28]
	cs^=(ownExchangeBuilding!=NULL ? 2:0);
	if (checkSumsVector)
		checkSumsVector->push_back((ownExchangeBuilding!=NULL ? 1:0));// [29]
	cs^=(foreingExchangeBuilding!=NULL ? 4:0);
	if (checkSumsVector)
		checkSumsVector->push_back((foreingExchangeBuilding!=NULL ? 1:0));// [30]
	cs=(cs<<1)|(cs>>31);
	
	cs^=destinationPurprose;
	if (checkSumsVector)
		checkSumsVector->push_back(destinationPurprose);// [31]
	cs^=(Uint32)subscribed;
	if (checkSumsVector)
		checkSumsVector->push_back(subscribed);// [32]
	cs^=caryedRessource;
	if (checkSumsVector)
		checkSumsVector->push_back(caryedRessource);// [33]
	
	if (checkSumsVector)
		checkSumsVector->push_back(0);// [34]
	if (checkSumsVector)
		checkSumsVector->push_back(0);// [35]
	if (checkSumsVector)
		checkSumsVector->push_back(0);// [36]
	if (checkSumsVector)
		checkSumsVector->push_back(0);// [37]
	if (checkSumsVector)
		checkSumsVector->push_back(0);// [38]
	if (checkSumsVector)
		checkSumsVector->push_back(0);// [39]
	
	return cs;
}


