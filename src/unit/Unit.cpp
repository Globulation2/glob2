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
	// unit specification
	this->typeNum = typeNum;

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
	destinationPurpose=-1;
	carriedRessource=-1;
	jobTimer = 0;

	previousClearingArea=std::nullopt;
	previousClearingAreaDistance=0;

	// gui
	levelUpAnimation = 0;
	magicActionAnimation = 0;

	// debug vars:
	verbose=false;
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
		destinationPurpose=-1;
		activity=ACT_FLAG;
		attachedBuilding=b;
	    setTargetBuilding(b);
		if (verbose)
			printf("guid=(%d) unitsWorkingSubscribe(findBestZonable) dp=(%d), gbid=(%d)\n", gid, destinationPurpose, b->gid);
	}
	else if(inside == false)
	{
		assert(destinationPurpose>=0);
		assert(b->neededRessource(destinationPurpose));
		activity=ACT_FILLING;
		attachedBuilding=b;
		setTargetBuilding(NULL);
		if (verbose)
			printf("guid=(%d) unitsWorkingSubscribe(findBestZonable) dp=(%d), gbid=(%d)\n", gid, destinationPurpose, b->gid);
	}
	else
	{
		activity=ACT_UPGRADING;
		attachedBuilding=b;
		setTargetBuilding(b);
		if (verbose)
			printf("guid=(%d) unitsWorkingSubscribe(findBestZonable) dp=(%d), gbid=(%d)\n", gid, destinationPurpose, b->gid);
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
					if (carriedRessource==destinationPurpose)
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
						owner->map->ressourceAvailableUpdate(owner->teamNumber, destinationPurpose, performance[SWIM], posX, posY, &targetX, &targetY, NULL);
						validTarget=true;
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

			enemy->owner->pushGameEvent(GameEvent::unitUnderAttack(owner->game->stepCounter, enemy->posX, enemy->posY, enemy->typeNum));

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

				enemy->owner->pushGameEvent(GameEvent::buildingUnderAttack(owner->game->stepCounter, enemy->posX, enemy->posY, enemy->shortTypeNum));

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
