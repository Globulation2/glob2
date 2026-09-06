// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
#include "Race.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"

#include "Building.h"

#include "EngineTiming.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include <Stream.h>

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
	direction=UNIT_DIRECTION_NONE;
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
		trigHungry = (hungry*UNIT_HUNGRY_TRIG_NUM_WARRIOR)/UNIT_HUNGRY_TRIG_DEN;
	else
		trigHungry = hungry/UNIT_HUNGRY_TRIG_DIVISOR_DEFAULT;
	trigHungryCarying = hungry/UNIT_HUNGRY_TRIG_DIVISOR_CARRYING;
	fruitMask = 0;
	fruitCount = 0;

	// NOTE : rewrite hp from level
	hp = this->performance[HP];
	trigHP = (hp*UNIT_HP_TRIG_NUM)/UNIT_HP_TRIG_DEN;

	attachedBuilding=NULL;
	targetBuilding=NULL;
	ownExchangeBuilding=NULL;
	destinationPurpose=UNIT_DEST_PURPOSE_NONE;
	carriedRessource=UNIT_CARRIED_RESSOURCE_NONE;
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
		destinationPurpose=UNIT_DEST_PURPOSE_NONE;
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
					validTarget=true;
				}
				break;
				case ACT_UPGRADING:
				{
					displacement=DIS_GOING_TO_BUILDING;
					assert(targetBuilding==attachedBuilding);
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
	if ((action==ATTACK_SPEED) && (delta>=UNIT_ATTACK_HIT_DELTA) && (delta<(UNIT_ATTACK_HIT_DELTA+speed)))
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

			enemy->underAttackTimer = UNDER_ATTACK_TIMER_TICKS;

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

				enemy->underAttackTimer = UNDER_ATTACK_TIMER_TICKS;

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
	if (delta<=UNIT_DELTA_MAX-speed)
	{
		delta+=speed;
	}
	else
#endif
	{
		delta+=(speed-UNIT_DELTA_QUANTUM);

		endOfAction();

		if (performance[FLY])
		{
			constexpr int r = UNIT_VISION_RADIUS_FLY;
			constexpr int d = 2*UNIT_VISION_RADIUS_FLY + 1;
			owner->map->setMapDiscovered(posX-r, posY-r, d, d, owner->sharedVisionOther);
			owner->map->setMapBuildingsDiscovered(posX-r, posY-r, d, d, owner->sharedVisionOther, owner->game->teams);
			owner->map->setMapExploredByUnit(posX-r, posY-r, d, d, owner->teamNumber);
		}
		else
		{
			constexpr int r = UNIT_VISION_RADIUS_GROUND;
			constexpr int d = 2*UNIT_VISION_RADIUS_GROUND + 1;
			owner->map->setMapDiscovered(posX-r, posY-r, d, d, owner->sharedVisionOther);
			owner->map->setMapBuildingsDiscovered(posX-r, posY-r, d, d, owner->sharedVisionOther, owner->game->teams);
			owner->map->setMapExploredByUnit(posX-r, posY-r, d, d, owner->teamNumber);
		}
	}

	// gui
	if (levelUpAnimation > 0)
		levelUpAnimation--;
	if (magicActionAnimation > 0)
		magicActionAnimation--;
}
