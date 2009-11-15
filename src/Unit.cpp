/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
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
#include "UnitSkin.h"
#include "UnitsSkins.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"

#include "Building.h"

#include "Utilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include <Stream.h>
#include <set>
#include <climits>

Unit::Unit(GAGCore::InputStream *stream, Team *owner, Sint32 versionMinor)
{
	init(0,0,0,0,owner,0);
	load(stream, owner, versionMinor);
}

Unit::Unit(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, int level)
{
	init(x, y, gid, typeNum, team, level);
}

void Unit::init(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, int level)
{
	logFile = globalContainer->logFileManager->getFile("Unit.log");
	
	// unit specification
	this->typeNum = typeNum;
	defaultSkinNameFromType();
	skinPointerFromName();

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

	targetX = 0;
	targetY = 0;
	validTarget = false;
	magicActionTimeout = 0;

	underAttackTimer = 0;

	// trigger parameters
	hp=0;

	// warriors fight to death TODO: this is overridden !?!?
	if (performance[ATTACK_SPEED])
		trigHP = 0;
	else
		trigHP = 20;

	// warriors wait more tiem before going to eat
	hungry = HUNGRY_MAX;
	hungryness = race->hungryness;
	if (performance[ATTACK_SPEED])
		trigHungry = (hungry*2)/10;
	else
		trigHungry = hungry/4;
	trigHungryCarying = hungry/10;
	fruitMask = 0;
	fruitCount = 0;

	// NOTE : rewrite hp from level
	hp = this->performance[HP];
	trigHP = (hp*3)/10;

	attachedBuilding=NULL;
	targetBuilding=NULL;
	ownExchangeBuilding=NULL;
	destinationPurprose=-1;
	caryedRessource=-1;
	jobTimer = 0;
	
	previousClearingAreaX=static_cast<unsigned int>(-1);
	previousClearingAreaY=static_cast<unsigned int>(-1);
	previousClearingAreaDistance=0;
	
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
	skinName = stream->readText("skinName");
	skinPointerFromName();
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
	validTarget = (bool)stream->readSint32("validTarget");
	magicActionTimeout = stream->readSint32("magicActionTimeout");

	// under attack timer
	if(versionMinor >= 61)
		underAttackTimer = stream->readUint8("underAttackTimer");
	else
		underAttackTimer = 0;


	// trigger parameters
	hp = stream->readSint32("hp");
	trigHP = stream->readSint32("trigHP");

	// hungry
	hungry = stream->readSint32("hungry");
	hungryness = stream->readSint32("hungryness");
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
	

	experience = stream->readSint32("experience");
	experienceLevel = stream->readSint32("experienceLevel");

	destinationPurprose = stream->readSint32("destinationPurprose");
	caryedRessource = stream->readSint32("caryedRessource");

	jobTimer = stream->readSint32("jobTimer");
	
	previousClearingAreaX=static_cast<unsigned int>(-1);
	previousClearingAreaY=static_cast<unsigned int>(-1);
	previousClearingAreaDistance=0;

	// gui
	levelUpAnimation = 0;
	magicActionAnimation = 0;
	jobTimer = 0;

	verbose = false;
	
	stream->readLeaveSection();
}

void Unit::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Unit");
	
	// unit specification
	// we drop the unittype pointer, we save only the number
	stream->writeSint32(typeNum, "typeNum");
	stream->writeText(skinName, "skinName");

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

	// attack timer
	stream->writeUint8(underAttackTimer, "underAttackTimer");

	// trigger parameters
	stream->writeSint32(hp, "hp");
	stream->writeSint32(trigHP, "trigHP");

	// hungry
	stream->writeSint32(hungry, "hungry");
	stream->writeSint32(hungryness, "hungryness");
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
	stream->writeSint32(caryedRessource, "caryedRessource");	
	stream->writeSint32(jobTimer, "jobTimer");

	
	stream->writeLeaveSection();
}

void Unit::loadCrossRef(GAGCore::InputStream *stream, Team *owner, Sint32 versionMinor)
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
		
	stream->writeLeaveSection();
}

void Unit::setTargetBuilding(Building * b)
{
	if(targetBuilding!=NULL) {
		targetBuilding->removeUnitFromHarvesting(this);
	}
	if(b!=NULL)
	{
		targetX=b->getMidX();
		targetY=b->getMidY();
	}
//TODO: Deal with "validTarget=true;"
    targetBuilding = b;
}

void Unit::subscriptionSuccess(Building* building, bool inside)
{
	Building* b=building;
	
	if (building->type->isVirtual)
	{
		destinationPurprose=-1;
		fprintf(logFile, "[%d] sdp1 destinationPurprose=%d\n", gid, destinationPurprose);
		activity=ACT_FLAG;
		attachedBuilding=b;
	    setTargetBuilding(b);
		if (verbose)
			printf("guid=(%d) unitsWorkingSubscribe(findBestZonable) dp=(%d), gbid=(%d)\n", gid, destinationPurprose, b->gid);
	}
	else if(inside == false)
	{
		assert(destinationPurprose>=0);
		assert(b->neededRessource(destinationPurprose));
		activity=ACT_FILLING;
		attachedBuilding=b;
		setTargetBuilding(NULL);
		if (verbose)
			printf("guid=(%d) unitsWorkingSubscribe(findBestZonable) dp=(%d), gbid=(%d)\n", gid, destinationPurprose, b->gid);
	}
	else
	{
		activity=ACT_UPGRADING;
		attachedBuilding=b;
		setTargetBuilding(b);
		if (verbose)
			printf("guid=(%d) unitsWorkingSubscribe(findBestZonable) dp=(%d), gbid=(%d)\n", gid, destinationPurprose, b->gid);
	}

	if (verbose)
		printf("guid=(%d), subscriptionSuccess()\n", gid);

	switch(medical)
	{
		case MED_HUNGRY :
		case MED_DAMAGED :
		case MED_FREE:
		{
			switch(activity)
			{
				case ACT_FLAG:
				{
					displacement=DIS_GOING_TO_FLAG;
					assert(targetBuilding==attachedBuilding);
					//targetX=attachedBuilding->getMidX();
					//targetY=attachedBuilding->getMidY();
					validTarget=true;
				}
				break;
				case ACT_UPGRADING:
				{
					displacement=DIS_GOING_TO_BUILDING;
					assert(targetBuilding==attachedBuilding);
					//targetX=targetBuilding->getMidX();
					//targetY=targetBuilding->getMidY();
					validTarget=true;
				}
				break;
				case ACT_FILLING:
				{
					assert(attachedBuilding);
					if (caryedRessource==destinationPurprose)
					{
						displacement=DIS_GOING_TO_BUILDING;
						setTargetBuilding(attachedBuilding);
						//targetX=targetBuilding->getMidX();
						//targetY=targetBuilding->getMidY();
						validTarget=true;
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
	//warrior attacks?
	assert(speed>0);
	if ((action==ATTACK_SPEED) && (delta>=128) && (delta<(128+speed)))
	{
		Uint16 enemyGUID=owner->map->getGroundUnit(posX+dx, posY+dy);
		if (enemyGUID!=NOGUID)
		{
			int enemyID=GIDtoID(enemyGUID);
			int enemyTeam=GIDtoTeam(enemyGUID);
			Unit *enemy=owner->game->teams[enemyTeam]->myUnits[enemyID];
			
			int degats=getRealAttackStrength()-enemy->getRealArmor(false);
			if (degats<=0)
				degats=1;
			enemy->hp-=degats;
			
			enemy->underAttackTimer = 240;

			boost::shared_ptr<GameEvent> event(new UnitUnderAttackEvent(owner->game->stepCounter, enemy->posX, enemy->posY, enemy->typeNum));
			enemy->owner->pushGameEvent(event);

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
			
				enemy->underAttackTimer = 240;

				boost::shared_ptr<GameEvent> event(new BuildingUnderAttackEvent(owner->game->stepCounter, enemy->posX, enemy->posY, enemy->shortTypeNum));
				enemy->owner->pushGameEvent(event);

				if (enemy->hp<0)
					enemy->kill();
				incrementExperience(degats);
			}
		}
	}
	
	//We give globs 32 ticks to wait for a job before moving onto
	//another activity like upgrading
	if (medical==MED_FREE && activity==ACT_RANDOM)
	{
		jobTimer++;
	}

	if(underAttackTimer > 0)
		underAttackTimer -= 1;
	
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
	setTargetBuilding(NULL);
	ownExchangeBuilding=NULL;
	activity=Unit::ACT_RANDOM;
	displacement=Unit::DIS_RANDOM;
	validTarget=false;
	needToRecheckMedical=true;
}

void Unit::stopAttachedForBuilding(bool goingInside)
{
	if (verbose)
		printf("guid=(%d) stopAttachedForBuilding()\n", gid);
	assert(attachedBuilding);
	
	if (goingInside)
	{
		attachedBuilding->removeUnitFromInside(this);
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
	}
	
	activity=ACT_RANDOM;
	displacement=DIS_RANDOM;
	validTarget=false;
	
	attachedBuilding->removeUnitFromWorking(this);
	attachedBuilding=NULL;
	setTargetBuilding(NULL);
	ownExchangeBuilding=NULL;
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
		int ATTACK_RANGE=3;
		for (int yi=posY-ATTACK_RANGE; yi<=posY+ATTACK_RANGE; yi++)
			for (int xi=posX-ATTACK_RANGE; xi<=posX+ATTACK_RANGE; xi++)
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
							Sint32 damage = attackForce + experienceLevel - enemyUnit->getRealArmor(true);
							if (damage > 0)
							{
								enemyUnit->hp -= damage;
								
								boost::shared_ptr<GameEvent> event(new UnitUnderAttackEvent(owner->game->stepCounter, xi, yi, enemyUnit->typeNum));
								enemyUnit->owner->pushGameEvent(event);
								
								incrementExperience(damage);
								magicActionAnimation = MAGIC_ACTION_ANIMATION_FRAME_COUNT;
								hasUsedMagicAction = true;
							}
						}
					}
				}
				
				// damaging enemy buildings: this has been removed for balance purposes
			}
		
		Sint32 magicLevel = std::max(level[MAGIC_ATTACK_AIR], level[MAGIC_ATTACK_GROUND]);
		if (hasUsedMagicAction)
			magicActionTimeout = race->getUnitType(typeNum, level[magicLevel])->magicActionCooldown;
	}
}

void Unit::handleMedical(void)
{
	/* Make sure explorers try to immediately feed after healing to increase their range. */
	if ((typeNum == EXPLORER) && (displacement == DIS_EXITING_BUILDING))
	{
		medical=MED_FREE;
		if ((destinationPurprose == HEAL) && (hungry < ((HUNGRY_MAX * 9) / 10)))
		{
			// fprintf (stderr, "forcing explorer hunger: gid: %d, hungry: %d\n", gid, hungry);
			needToRecheckMedical = 1;
			medical = MED_HUNGRY;
			return;
		}
		else if ((destinationPurprose == FEED) && (hp < (((performance[HP]) * 9) / 10)))
		{
			// fprintf (stderr, "forcing explorer healing: gid: %d, hp: %d\n", gid, hp);
			needToRecheckMedical = 1;
			medical = MED_DAMAGED;
			return;
		}
	}
	
	if ((displacement==DIS_ENTERING_BUILDING) || (displacement==DIS_INSIDE) || (displacement==DIS_EXITING_BUILDING))
		return;
	
	if (verbose)
		printf("guid=(%d) handleMedical...\n", gid);
	hungry -= hungryness;
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
				attachedBuilding->removeUnitFromWorking(this);
				attachedBuilding->removeUnitFromInside(this);
				attachedBuilding=NULL;
				ownExchangeBuilding=NULL;
			}
			setTargetBuilding(NULL);
            // //TODO: in beta4 this line was ommitted. delete?
			// ownExchangeBuilding=NULL;
			
			activity=ACT_RANDOM;
			validTarget=false;
			
			// remove from map
			if (performance[FLY])
				owner->map->setAirUnit(posX, posY, NOGUID);
			else
				owner->map->setGroundUnit(posX, posY, NOGUID);
			
			if(previousClearingAreaX!=static_cast<unsigned int>(-1))
			{
				owner->map->setClearingAreaUnclaimed(previousClearingAreaX, previousClearingAreaY, owner->teamNumber);
			}
			owner->map->clearImmobileUnit(posX, posY);
			
			// generate death animation
			if (!globalContainer->runNoX)
				owner->map->getSector(posX, posY)->deathAnimations.push_back(new UnitDeathAnimation(posX, posY, owner));
		}
		isDead = true;
	}
}

void Unit::handleActivity(void)
{
        if ((displacement==DIS_EXITING_BUILDING)
            && (typeNum == EXPLORER)) {
          // fprintf (stderr, "exiting explorer: gid: %d, medical: %d, destinationPurprose: %d\n", gid, medical, destinationPurprose);
        }

	// freeze unit health when inside a building
	if ((displacement==DIS_ENTERING_BUILDING) || (displacement==DIS_INSIDE)
            || ((displacement==DIS_EXITING_BUILDING)
                && ! ((typeNum == EXPLORER) && (medical != MED_FREE))))
		return;
	
	if (verbose)
		printf("guid=(%d) handleActivity (medical=%d, activity=%d) (needToRecheckMedical=%d) (attachedBuilding=%p)...\n",
			gid, medical, activity, needToRecheckMedical, attachedBuilding);
			
	if(activity!=ACT_RANDOM)
		jobTimer=0;

	if (medical==MED_FREE)
	{
		handleMagic();

		if (activity==ACT_RANDOM)
		{
			// nothing to do:
			//Wait for 32 ticks before doing something else, to allow buildings time to hire units
			if(jobTimer>32)
			{
				// We look for an upgrade
				Building* b=owner->findBestUpgrade(this);
				if (b)
				{
					assert(destinationPurprose>=WALK);
					assert(destinationPurprose<ARMOR);
					activity=ACT_UPGRADING;
					attachedBuilding=b;
					setTargetBuilding(b);
					if (verbose)
						printf("guid=(%d) going to upgrade at dp=(%d), gbid=(%d)\n", gid, destinationPurprose, b->gid);
					b->subscribeUnitForInside(this);
					return;
				}

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
						setTargetBuilding(b);
						needToRecheckMedical=false;
						if (verbose)
							printf("guid=(%d) Going to heal building\n", gid);
						targetX=attachedBuilding->getMidX();
						targetY=attachedBuilding->getMidY();
						validTarget=true;
						b->subscribeUnitForInside(this);
					}
					else
						activity=ACT_RANDOM;
				}
			}
		}
	}
	else if (needToRecheckMedical)
	{
		// disconnect from building
		if (attachedBuilding)
		{
			if (verbose)
				printf("guid=(%d) Need medical while working, abort work\n", gid);
			attachedBuilding->removeUnitFromWorking(this);
			attachedBuilding->removeUnitFromInside(this);
			attachedBuilding=NULL;
			ownExchangeBuilding=NULL;
		}
		setTargetBuilding(NULL);

		if (medical==MED_HUNGRY)
		{
			Building *b;
			b=owner->findNearestFood(this);
                        /*if (typeNum == EXPLORER) {
                           fprintf (stderr, "gid: %d, b: %x\n", gid, b);
                        }*/

			if (b!=NULL)
			{
				Team *currentTeam=owner;
				Team *targetTeam=b->owner;
				if (currentTeam != targetTeam)
				{
					// Unit conversion code
					
					// Send events and keep track of number of unit converted
					boost::shared_ptr<GameEvent> event(new UnitLostConversionEvent(owner->game->stepCounter, posX, posY, targetTeam->getFirstPlayerName()));
					currentTeam->pushGameEvent(event);
					currentTeam->unitConversionLost++;
					
					boost::shared_ptr<GameEvent> event2(new UnitGainedConversionEvent(owner->game->stepCounter, posX, posY, currentTeam->getFirstPlayerName()));
					targetTeam->pushGameEvent(event2);
					targetTeam->unitConversionGained++;
					
					// Find free slot in other team
					int targetID=-1;
					for (int i=0; i<Unit::MAX_COUNT; i++)//we search for a free place for a unit.
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
				setTargetBuilding(b);
				needToRecheckMedical=false;
				if (verbose)
					printf("guid=(%d) Subscribed to food at building gbid=(%d)\n", gid, b->gid);
				b->subscribeUnitForInside(this);
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
				setTargetBuilding(b);
				needToRecheckMedical=false;
				if (verbose)
					printf("guid=(%d) Subscribed to heal at building gbid=(%d)\n", gid, b->gid);
				b->subscribeUnitForInside(this);
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
				
				setTargetBuilding(attachedBuilding);
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
					assert(destinationPurprose>=HAPPYNESS_BASE);
					
					// Let's grab the right ressource.
					
					if (targetBuilding->ressources[destinationPurprose]>0)
					{
						targetBuilding->removeRessourceFromBuilding(destinationPurprose);
						caryedRessource=destinationPurprose;
						fprintf(logFile, "[%d] sdp6 destinationPurprose=%d\n", gid, destinationPurprose);
						
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
				else if ((caryedRessource>=0) && (targetBuilding->ressources[caryedRessource]<targetBuilding->type->maxRessource[caryedRessource]))
				{
					if (verbose)
						printf("guid=(%d) Giving ressource (%d) to building gbid=(%d) old-amount=(%d)\n", gid, destinationPurprose, targetBuilding->gid, targetBuilding->ressources[caryedRessource]);
					targetBuilding->addRessourceIntoBuilding(caryedRessource);
					caryedRessource=-1;
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
						attachedBuilding->wishedRessources(needs);
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
								destinationPurprose=bestRessource;
								fprintf(logFile, "[%d] sdp7 destinationPurprose=%d\n", gid, destinationPurprose);
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
					int levelsToBeUpgraded=attachedBuilding->type->level+1-level[destinationPurprose];
					insideTimeout=-attachedBuilding->type->upgradeTime[destinationPurprose];
					speed=attachedBuilding->type->insideSpeed/levelsToBeUpgraded;
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
					&& ((mapCase.ressource.type == WOOD)
						|| (mapCase.ressource.type == CORN)
						|| (mapCase.ressource.type == PAPYRUS)
						|| (mapCase.ressource.type == ALGA))
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
						dxdyfromDirection();
						movement = MOV_GOING_DXDY;
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
			
			///Don't change targets if we still have a valid target
			if(owner->map->doesUnitTouchEnemy(this, &dx, &dy))
			{
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
						movement = MOV_GOING_DXDY;
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
				attachedBuilding->removeUnitFromInside(this);
				attachedBuilding->updateConstructionState();
				attachedBuilding=NULL;
				setTargetBuilding(NULL);
				assert(ownExchangeBuilding==NULL);
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
	owner->map->clearImmobileUnit(posX, posY);
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
			for(int q = 0; q < 5; ++q) //hack - look for a direction safe from guard towers
			{
				dx=-1+syncRand()%3;
				dy=-1+syncRand()%3;
				if(locationIsInEnemyGuardTowerRange(posX + dx, posY + dy))continue;
				else break;
			}
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
			owner->map->pathfindPointToPoint(posX, posY, targetX, targetY, &dx, &dy, (performance[SWIM] > 0 ? true : false), owner->me, 12);
			directionFromDxDy();
			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());

			if(dx == 0 && dy == 0)
				owner->map->markImmobileUnit(posX, posY, owner->teamNumber);
			
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
			
			if(dx == 0 && dy == 0)
				owner->map->markImmobileUnit(posX, posY, owner->teamNumber);
			
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
			posX=(posX+dx)&(owner->map->getMaskW());
			posY=(posY+dy)&(owner->map->getMaskH());
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
			owner->map->markImmobileUnit(posX, posY, owner->teamNumber);
			directionFromDxDy();
			action=BUILD;
			speed=performance[action];
			break;
		}

		case MOV_ATTACKING_TARGET:
		{
			owner->map->markImmobileUnit(posX, posY, owner->teamNumber);
			directionFromDxDy();
			action=ATTACK_SPEED;
			speed=performance[action];
			break;
		}
		
		case MOV_HARVESTING:
		{	
			owner->map->markImmobileUnit(posX, posY, owner->teamNumber);
			directionFromDxDy();
			action=HARVEST;
			speed=performance[action];
			//TODO: WTH???
			if(speed==0)
				speed/=speed;
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
	//TODO remove code duplication by calling dxdyfromDirection(int direction, int *dx, int *dy)
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

        /* We consider a cell to be vertical or horizontal in
           direction (rather than diagonal) if it is 2.41 times more
           vertical than horizontal, or vice versa.  This is because
           the halfway point between 45 degrees and 90 degrees is 67.5
           degrees and sin(67.5 deg) / cos(67.5 deg) =
           2.41421356237. */
	if ((100 * abs(ldx)) > (241 * abs(ldy)))
	{
		*cdx=SIGN(ldx);
		*cdy=0;
	}
	else if ((100 * abs(ldy)) > (241 * abs(ldx)))
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
	assert(gid<Unit::MAX_COUNT*Team::MAX_COUNT);
	return (gid%Unit::MAX_COUNT);
}

Sint32 Unit::GIDtoTeam(Uint16 gid)
{
	assert(gid<Unit::MAX_COUNT*Team::MAX_COUNT);
	return (gid/Unit::MAX_COUNT);
}

Uint16 Unit::GIDfrom(Sint32 id, Sint32 team)
{
	assert(id>=0);
	assert(id<Unit::MAX_COUNT);
	assert(team>=0);
	assert(team<Team::MAX_COUNT);
	return id+team*Unit::MAX_COUNT;
}

//! Return the real armor, taking into account the reduction due to fruits
int Unit::getRealArmor(bool isMagic) const
{
	int armorReductionPerHappyness = race->getUnitType(typeNum, level[ARMOR])->armorReductionPerHappyness;
	if (isMagic) //magic bypasses armor yet fruit penalties still apply
		return 0 - fruitCount * armorReductionPerHappyness;
	else
		return performance[ARMOR] - fruitCount * armorReductionPerHappyness;
}

//! Return the real attack strengh, taking into account the experience level
int Unit::getRealAttackStrength(void) const
{
	return performance[ATTACK_STRENGTH] + experienceLevel;
}

//! Return the amount of experience to level-up
int Unit::getNextLevelThreshold(void) const
{
	return (experienceLevel + 1) * (experienceLevel + 1) * race->getUnitType(typeNum, level[ATTACK_STRENGTH])->experiencePerLevel;
}

//! Increment experience. If level-up occures, handle it. Multiple level-up may occur at once.
void Unit::incrementExperience(int increment)
{
	experience += increment;
	int nextLevelThreshold = getNextLevelThreshold();
	while (experience > nextLevelThreshold)
	{
		experience -= nextLevelThreshold;
		experienceLevel++;
		nextLevelThreshold = getNextLevelThreshold();
		levelUpAnimation = LEVEL_UP_ANIMATION_FRAME_COUNT;
	}
}

//! Compute the skin pointer from a skin name
void Unit::skinPointerFromName(void)
{
	if (!globalContainer->runNoX)
	{
		skin = globalContainer->unitsSkins->getSkin(skinName);
		if (skin == NULL)
		{
			// if skin is invalid, retry with default
			std::cerr << "Unit::skinPointerFromName : invalid skin name " << skinName << std::endl;
			defaultSkinNameFromType();
			skin = globalContainer->unitsSkins->getSkin(skinName);
			if (!skin)
				abort();
		}
	}
	else
		skin = NULL;
}


//! Compute the skin name from the unit type
void Unit::defaultSkinNameFromType(void)
{
	switch (typeNum)
	{
		case WORKER: skinName = "worker"; break;
		case EXPLORER: skinName = "explorer"; break;
		case WARRIOR: skinName = "warrior"; break;
		default: assert(false); break;
	}
}

//! Return how many steps we can do until we are hungry
int Unit::numberOfStepsLeftUntilHungry(void)
{
	int timeLeft;
	if (hungryness)
		timeLeft = (hungry-trigHungry) / hungryness;
	else
		timeLeft = INT_MAX;
	stepsLeftUntilHungry = timeLeft;
	return timeLeft;
}

//! Iterate on all resource types to see if it is gettable
void Unit::computeMinDistToResources(void)
{
	bool allResourcesAreTooFar = true;
	for (size_t ri = 0; ri < MAX_RESSOURCES; ri++)
		if (!owner->map->ressourceAvailable(owner->teamNumber, ri, performance[SWIM], posX, posY, &minDistToResource[ri]))
			minDistToResource[ri] = -1;
		else if (minDistToResource[ri] < stepsLeftUntilHungry)
			allResourcesAreTooFar = false;
	// the dist to an already carried resource is zero
	if (caryedRessource >= 0)
		minDistToResource[caryedRessource] = 0;
}

void Unit::integrity()
{
	assert(gid<32768);
	if (isDead)
		return;
	
	if (!needToRecheckMedical)
	{
		assert(activity==ACT_UPGRADING);
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
	cs=(cs<<1)|(cs>>31);
	
	cs^=destinationPurprose;
	if (checkSumsVector)
		checkSumsVector->push_back(destinationPurprose);// [31]
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
