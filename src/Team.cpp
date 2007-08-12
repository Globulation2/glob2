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

#include <climits>
//#include <float.h>
#include <math.h>

#include <Toolkit.h>
#include <StringTable.h>

#include "BuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Marshaling.h"
#include "NetConsts.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"

BaseTeam::BaseTeam()
{
	teamNumber=0;
	numberOfPlayer=0;
	colorR=0;
	colorG=0;
	colorB=0;
	colorPAD=0;
	playersMask=0;
	type=T_AI;

	disableRecursiveDestruction=false;
}




bool BaseTeam::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	// loading baseteam
	stream->readEnterSection("BaseTeam");
	type = (TeamType)stream->readUint32("type");
	teamNumber = stream->readSint32("teamNumber");
	numberOfPlayer = stream->readSint32("numberOfPlayer");
	stream->read(&colorR, 1, "colorR");
	stream->read(&colorG, 1, "colorG");
	stream->read(&colorB, 1, "colorB");
	stream->read(&colorPAD, 1, "colorPAD");
	playersMask = stream->readUint32("playersMask");
	stream->readLeaveSection();
	if (!race.load(stream, versionMinor))
		return false;
	// TODO : overwrite only when it is a certain type, but require carefull thinking. For now, overwrite
	// if (type == T_HUMAN)
	race.load();
	return true;
}




void BaseTeam::save(GAGCore::OutputStream *stream)
{
	// saving baseteam
	stream->writeEnterSection("BaseTeam");
	stream->writeUint32((Uint32)type, "type");
	stream->writeSint32(teamNumber, "teamNumber");
	stream->writeSint32(numberOfPlayer, "numberOfPlayer");
	stream->write(&colorR, 1, "colorR");
	stream->write(&colorG, 1, "colorG");
	stream->write(&colorB, 1, "colorB");
	stream->write(&colorPAD, 1, "colorPAD");
	stream->writeUint32(playersMask, "playersMask");
	stream->writeLeaveSection();
	race.save(stream);
}




Uint8 *BaseTeam::getData()
{
	addSint32(data, teamNumber, 0);
	addSint32(data, numberOfPlayer, 4);
	addUint8(data, colorR, 8);
	addUint8(data, colorG, 9);
	addUint8(data, colorB, 10);
	addUint8(data, colorPAD, 11);
	addSint32(data, playersMask, 12);
	// TODO : give race to the network here.

	return data;
}




bool BaseTeam::setData(const Uint8 *data, int dataLength)
{
	if (dataLength!=getDataLength())
		return false;

	teamNumber=getSint32(data, 0);
	numberOfPlayer=getSint32(data, 4);
	colorR=getUint8(data, 8);
	colorG=getUint8(data, 9);
	colorB=getUint8(data, 10);
	colorPAD=getUint8(data, 11);
	playersMask=getSint32(data, 12);
	// TODO : create the race from the network here.

	return true;
}




int BaseTeam::getDataLength()
{
	return 16;
}




Uint32 BaseTeam::checkSum()
{
	Uint32 cs=0;

	cs^=teamNumber;
	cs=(cs<<31)|(cs>>1);
	cs^=numberOfPlayer;
	cs=(cs<<31)|(cs>>1);
	cs^=playersMask;
	cs=(cs<<31)|(cs>>1);

	return cs;
}




Team::Team(Game *game)
:BaseTeam()
{
	logFile = globalContainer->logFileManager->getFile("Team.log");
	assert(game);
	this->game=game;
	this->map=&game->map;
	init();
}




Team::Team(GAGCore::InputStream *stream, Game *game, Sint32 versionMinor)
:BaseTeam()
{
	logFile = globalContainer->logFileManager->getFile("Team.log");
	assert(game);
	this->game=game;
	this->map=&game->map;
	init();
	bool success = load(stream, &(globalContainer->buildingsTypes), versionMinor);
	assert(success);
}




Team::~Team()
{
	if (!disableRecursiveDestruction)
	{
		clearMem();
	}
}




void Team::init(void)
{
	for (int i=0; i<1024; i++)
		myUnits[i]=NULL;

	for (int i=0; i<1024; i++)
		myBuildings[i]=NULL;

	startPosX=startPosY=0;
	startPosSet=0;

	isAlive=true;
	hasWon=false;
	prestige=0;
	unitConversionLost = 0;
	unitConversionGained = 0;
	for(int i=0; i<MAX_NB_RESSOURCES; ++i)
		teamRessources[i]=0;

	for(int i=0; i<GESize; ++i)
		eventCooldownTimers[i]=0;

	noMoreBuildingSitesCountdown=0;
}




void Team::setBaseTeam(const BaseTeam *initial)
{
	teamNumber=initial->teamNumber;
	numberOfPlayer=initial->numberOfPlayer;
	playersMask=initial->playersMask;
	race=initial->race;
	fprintf(logFile, "Team::setBaseTeam(), teamNumber=%d, playersMask=%d\n", teamNumber, playersMask);

	setCorrectColor(initial->colorR, initial->colorG, initial->colorB);
	setCorrectMasks();
}




bool Team::load(GAGCore::InputStream *stream, BuildingsTypes *buildingstypes, Sint32 versionMinor)
{
	assert(stream);
	assert(buildingsToBeDestroyed.size()==0);
	buildingsTryToBuildingSiteRoom.clear();

	// loading baseteam
	if(!BaseTeam::load(stream, versionMinor))
		return false;

	stream->readEnterSection("Team");

	// normal load
	stream->readEnterSection("myUnits");
	for (int i=0; i<1024; i++)
	{
		if (myUnits[i])
			delete myUnits[i];

		stream->readEnterSection(i);
		Uint32 isUsed = stream->readUint32("isUsed");
		if (isUsed)
			myUnits[i] = new Unit(stream, this, versionMinor);
		else
			myUnits[i] = NULL;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	swarms.clear();
	turrets.clear();
	canExchange.clear();
	virtualBuildings.clear();
	clearingFlags.clear();

	prestige = 0;
	stream->readEnterSection("myBuildings");
	for (int i=0; i<1024; i++)
	{
		if (myBuildings[i])
			delete myBuildings[i];

		stream->readEnterSection(i);
		Uint32 isUsed = stream->readUint32("isUsed");
		if (isUsed)
		{
			myBuildings[i] = new Building(stream, buildingstypes, this, versionMinor);
			if (myBuildings[i]->type->unitProductionTime)
				swarms.push_back(myBuildings[i]);
			if (myBuildings[i]->type->shootingRange)
				turrets.push_back(myBuildings[i]);
			if (myBuildings[i]->type->canExchange)
				canExchange.push_back(myBuildings[i]);
			if (myBuildings[i]->type->isVirtual)
				virtualBuildings.push_back(myBuildings[i]);
			if (myBuildings[i]->type->zonable[WORKER])
				clearingFlags.push_back(myBuildings[i]);
		}
		else
			myBuildings[i] = NULL;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	// resolve cross reference
	stream->readEnterSection("myUnits");
	for (int i=0; i<1024; i++)
	{
		if (myUnits[i])
		{
			stream->readEnterSection(i);
			myUnits[i]->loadCrossRef(stream, this, versionMinor);
			stream->readLeaveSection();
		}
	}
	stream->readLeaveSection();

	stream->readEnterSection("myBuildings");
	for (int i=0; i<1024; i++)
	{
		if (myBuildings[i])
		{
			stream->readEnterSection(i);
			myBuildings[i]->loadCrossRef(stream, buildingstypes, this, versionMinor);
			if (myBuildings[i]->type->canExchange)
				canExchange.push_back(myBuildings[i]);
			stream->readLeaveSection();
		}
	}
	stream->readLeaveSection();

	allies = stream->readUint32("allies");
	enemies = stream->readUint32("enemies");
	sharedVisionExchange = stream->readUint32("sharedVisionExchange");
	sharedVisionFood = stream->readUint32("sharedVisionFood");
	sharedVisionOther = stream->readUint32("sharedVisionOther");
	me = stream->readUint32("me");
	startPosX = stream->readSint32("startPosX");
	startPosY = stream->readSint32("startPosY");
	startPosSet = stream->readSint32("startPosSet");
	unitConversionLost = stream->readSint32("unitConversionLost");
	unitConversionGained = stream->readSint32("unitConversionGained");

	stream->readEnterSection("teamRessources");
	for (unsigned int i=0; i<MAX_NB_RESSOURCES; ++i)
	{
		stream->readEnterSection(i);
		teamRessources[i] = stream->readUint32("teamRessources");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	for(int i=0; i<GESize; ++i)
		eventCooldownTimers[i]=0;

	if (!stats.load(stream, versionMinor))
	{
		stream->readLeaveSection();
		return false;
	}
	stats.step(this, true);

	isAlive = true;

	stream->readLeaveSection();
	return true;
}




void Team::save(GAGCore::OutputStream *stream)
{
	// saving baseteam
	BaseTeam::save(stream);

	stream->writeEnterSection("Team");

	// saving team
	stream->writeEnterSection("myUnits");
	for (int i=0; i<1024; i++)
	{
		stream->writeEnterSection(i);
		if (myUnits[i])
		{
			stream->writeUint32(true, "isUsed");
			myUnits[i]->save(stream);
		}
		else
		{
			stream->writeUint32(false, "isUsed");
		}
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("myBuildings");
	for (int i=0; i<1024; i++)
	{
		stream->writeEnterSection(i);
		if (myBuildings[i])
		{
			stream->writeUint32(true, "isUsed");
			myBuildings[i]->save(stream);
		}
		else
		{
			stream->writeUint32(false, "isUsed");
		}
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	// save cross reference
	stream->writeEnterSection("myUnits");
	for (int i=0; i<1024; i++)
	{
		if (myUnits[i])
		{
			stream->writeEnterSection(i);
			myUnits[i]->saveCrossRef(stream);
			stream->writeLeaveSection();
		}
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("myBuildings");
	for (int i=0; i<1024; i++)
	{
		if (myBuildings[i])
		{
			stream->writeEnterSection(i);
			myBuildings[i]->saveCrossRef(stream);
			stream->writeLeaveSection();
		}
	}
	stream->writeLeaveSection();

	stream->writeUint32(allies, "allies");
	stream->writeUint32(enemies, "enemies");
	stream->writeUint32(sharedVisionOther, "sharedVisionExchange");
	stream->writeUint32(sharedVisionFood, "sharedVisionFood");
	stream->writeUint32(sharedVisionOther, "sharedVisionOther");
	stream->writeUint32(me, "me");
	stream->writeSint32(startPosX, "startPosX");
	stream->writeSint32(startPosY, "startPosY");
	stream->writeSint32(startPosSet, "startPosSet");
	stream->writeSint32(unitConversionLost, "unitConversionLost");
	stream->writeSint32(unitConversionGained, "unitConversionGained");

	stream->writeEnterSection("teamRessources");
	for (unsigned int i=0; i<MAX_NB_RESSOURCES; ++i)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(teamRessources[i], "teamRessources");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stats.save(stream);

	stream->writeLeaveSection();
}




void Team::createLists(void)
{
	assert(swarms.size()==0);
	assert(turrets.size()==0);
	assert(virtualBuildings.size()==0);

	swarms.clear();
	turrets.clear();
	virtualBuildings.clear();

	for (int i=0; i<1024; i++)
		if (myBuildings[i])
	{
		if (myBuildings[i]->type->unitProductionTime)
			swarms.push_back(myBuildings[i]);
		if (myBuildings[i]->type->shootingRange)
			turrets.push_back(myBuildings[i]);
		if (myBuildings[i]->type->isVirtual)
			virtualBuildings.push_back(myBuildings[i]);
		if (myBuildings[i]->type->zonable[WORKER])
			clearingFlags.push_back(myBuildings[i]);
		myBuildings[i]->update();
	}
}




void Team::clearLists(void)
{
	for (int i=0; i<NB_ABILITY; i++)
		upgrade[i].clear();
	canFeedUnit.clear();
	canHealUnit.clear();
	canExchange.clear();
	buildingsWaitingForDestruction.clear();
	buildingsToBeDestroyed.clear();
	buildingsTryToBuildingSiteRoom.clear();
	buildingsNeedingUnits.clear();
	swarms.clear();
	turrets.clear();
	virtualBuildings.clear();
}




void Team::clearMap(void)
{
	assert(map);

	for (int i=0; i<1024; ++i)
		if (myUnits[i])
			if (myUnits[i]->performance[FLY])
				map->setAirUnit(myUnits[i]->posX, myUnits[i]->posY, NOGUID);
	else
		map->setGroundUnit(myUnits[i]->posX, myUnits[i]->posY, NOGUID);

	for (int i=0; i<1024; ++i)
		if (myBuildings[i])
			if (!myBuildings[i]->type->isVirtual)
				map->setBuilding(myBuildings[i]->posX, myBuildings[i]->posY, myBuildings[i]->type->width, myBuildings[i]->type->height, NOGBID);

}




void Team::clearMem(void)
{
	for (int i=0; i<1024; ++i)
		if (myUnits[i])
	{
		delete myUnits[i];
		myUnits[i] = NULL;
	}
	for (int i=0; i<1024; ++i)
		if (myBuildings[i])
	{
		delete myBuildings[i];
		myBuildings[i] = NULL;
	}
}




void Team::integrity(void)
{
	assert(noMoreBuildingSitesCountdown<=noMoreBuildingSitesCountdownMax);
	for (int id=0; id<1024; id++)
	{
		Building *b=myBuildings[id];
		if (b)
			b->integrity();
	}
	for (std::list<Building *>::iterator it=virtualBuildings.begin(); it!=virtualBuildings.end(); ++it)
	{
		assert(*it);
		assert((*it)->type);
		assert((*it)->type->isVirtual);
		assert(myBuildings[Building::GIDtoID((*it)->gid)]);
	}
	for (std::list<Building *>::iterator it=clearingFlags.begin(); it!=clearingFlags.end(); ++it)
	{
		assert(*it);
		assert((*it)->type);
		assert((*it)->type->isVirtual);
		assert(myBuildings[Building::GIDtoID((*it)->gid)]);
	}

	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u)
			u->integrity();
	}
}




void Team::setCorrectMasks(void)
{
	me=teamNumberToMask(teamNumber);
	allies=me;
	enemies=~allies;
	sharedVisionExchange=me;
	sharedVisionFood=me;
	sharedVisionOther=me;
}




void Team::setCorrectColor(Uint8 r, Uint8 g, Uint8 b)
{
	this->colorR=r;
	this->colorG=g;
	this->colorB=b;
}




void Team::setCorrectColor(float value)
{
	float r, g, b;
	Utilities::HSVtoRGB(&r, &g, &b, value, 0.8f, 0.9f);
	this->colorR=(Uint8)(255.0f*r);
	this->colorG=(Uint8)(255.0f*g);
	this->colorB=(Uint8)(255.0f*b);
}




void Team::update()
{
	for (int i=0; i<1024; i++)
		if (myBuildings[i])
			myBuildings[i]->update();
}




bool Team::openMarket()
{
	int numberOfTeam=game->mapHeader.getNumberOfTeams();
	for (int ti=0; ti<numberOfTeam; ti++)
		if (ti!=teamNumber && (game->teams[ti]->sharedVisionExchange & me))
			return true;
	return false;
}




Building *Team::findNearestHeal(Unit *unit)
{
	if (unit->hungry < 0)
		return NULL;
	if (unit->performance[FLY])
	{
		Sint32 x = unit->posX;
		Sint32 y = unit->posY;
		Sint32 maxDist = unit->hungry / unit->race->hungryness + unit->hp;
		Building *choosen = NULL;
		Sint32 bestDist2 = maxDist * maxDist;
		for (std::list<Building *>::iterator bi=canHealUnit.begin(); bi!=canHealUnit.end(); ++bi)
		{
			Building *b=(*bi);
			Sint32 dist2 = map->warpDistSquare(x, y, b->posX, b->posY);
			if (dist2 < bestDist2)
			{
				choosen = b;
				bestDist2 = dist2;
			}
		}
		return choosen;
	}
	else
	{
		Sint32 x = unit->posX;
		Sint32 y = unit->posY;
		Sint32 maxDist = unit->hungry / race.hungryness + unit->hp;
		bool canSwim = unit->performance[SWIM];
		Building *choosen=  NULL;
		Sint32 bestDist = maxDist;
		for (std::list<Building *>::iterator bi=canHealUnit.begin(); bi!=canHealUnit.end(); ++bi)
		{
			int buildingDist;//initialized in buildingAvailable next line
			if (map->buildingAvailable((*bi), canSwim, x, y, &buildingDist) && (buildingDist < bestDist))
			{
				choosen = (*bi);
				bestDist = buildingDist;
			}
		}
		return choosen;
	}
}




Building *Team::findNearestFood(Unit *unit)
{
	MapHeader& header=game->mapHeader;

	bool concurency = false;//Becomes true if there is a team whose inn-view is on for us but who is not allied to us.
	for (int ti= 0; ti < header.getNumberOfTeams(); ti++)
		if (ti != teamNumber && (game->teams[ti]->sharedVisionFood & me) && !(game->teams[ti]->allies & me))
		{
			concurency = true;
			break;
		}
	
	// first, we check for the best food an enemy can offer:
	Sint32 bestEnemyHappyness = 0;
	Sint32 maxDist = unit->hungry / unit->race->hungryness + unit->hp;
	Building *bestEnemyFood = NULL;
	if (concurency)
	{
		if (unit->verbose)
			printf("guid=(%d), Team::findNearestFood(), concurency\n", unit->gid);
		if (unit->performance[FLY])
		{
			Sint32 bestDist = maxDist;
			for (int ti = 0; ti < header.getNumberOfTeams(); ti++)
			{
				if (ti == teamNumber)
					continue;
				Team *team = game->teams[ti];
				if ((!team->sharedVisionFood & me) || (team->allies & me))
					continue;
				for (std::list<Building *>::iterator bi = team->canFeedUnit.begin(); bi != team->canFeedUnit.end(); ++bi)
				{
					Sint32 dist = 1 + (Sint32)sqrt(map->warpDistSquare(unit->posX, unit->posY, (*bi)->posX, (*bi)->posY));
					if (dist >= maxDist)
						continue;
					int happyness = (*bi)->availableHappynessLevel();
					if (happyness > bestEnemyHappyness)
					{
						bestEnemyHappyness = happyness;
						bestDist = dist;
						bestEnemyFood = *bi;
					}
					else if (happyness == bestEnemyHappyness && dist < bestDist)
					{
						bestDist = dist;
						bestEnemyFood = *bi;
					}
				}
			}
		}
		else
		{
			Sint32 bestDist = maxDist;
			bool canSwim = (unit->performance[SWIM] > 0);
			for (int ti = 0; ti < header.getNumberOfTeams(); ti++)
			{
				if (ti == teamNumber)
					continue;
				Team *team = game->teams[ti];
				if ((!team->sharedVisionFood & me) || (team->allies & me))
					continue;
				for (std::list<Building *>::iterator bi = team->canFeedUnit.begin(); bi != team->canFeedUnit.end(); ++bi)
				{
					int dist = 1 + (Sint32)sqrt(map->warpDistSquare(unit->posX, unit->posY, (*bi)->posX, (*bi)->posY));
					if (dist >= maxDist)
						continue;
					if (!map->buildingAvailable(*bi, canSwim, unit->posX, unit->posY, &dist))
						continue;
					if (dist >= maxDist)
						continue;
					int happyness = (*bi)->availableHappynessLevel();
					if (happyness > bestEnemyHappyness)
					{
						bestEnemyHappyness = happyness;
						bestDist = dist;
						bestEnemyFood = *bi;
					}
					else if (happyness == bestEnemyHappyness && dist < bestDist)
					{
						bestDist = dist;
						bestEnemyFood = *bi;
					}
				}
			}
		}
		if (unit->verbose && bestEnemyFood)
			printf("guid=(%d), Team::findNearestFood(), bestEnemyHappyness=%d, bestEnemyFood->gid=%d\n", unit->gid, bestEnemyHappyness, bestEnemyFood->gid);
	}

	//Second, we check if we have any satisfactory inns on our team.
	// That mean it has to be better or equal than the ennemy food.
	if (unit->performance[FLY])
	{
		Sint32 bestDist = maxDist;
		Building *choosenFood = NULL;
		for (std::list<Building *>::iterator bi=canFeedUnit.begin(); bi!=canFeedUnit.end(); ++bi)
		{
			if ((*bi)->availableHappynessLevel() < bestEnemyHappyness)
				continue;
			Sint32 dist = 1 + (Sint32)sqrt(map->warpDistSquare(unit->posX, unit->posY, (*bi)->posX, (*bi)->posY));
			if (dist >= bestDist)
				continue;
			bestDist = dist;
			choosenFood = *bi;
		}
		if (choosenFood)
			return choosenFood;
	}
	else
	{
		bool canSwim = (unit->performance[SWIM] > 0);
		Sint32 bestDist = maxDist;
		Building *choosenFood = NULL;
		for (std::list<Building *>::iterator bi=canFeedUnit.begin(); bi!=canFeedUnit.end(); ++bi)
		{
			if ((*bi)->availableHappynessLevel() < bestEnemyHappyness)
				continue;
			int dist = 1 + (Sint32)sqrt(map->warpDistSquare(unit->posX, unit->posY, (*bi)->posX, (*bi)->posY));
			if (dist >= bestDist)
				continue;
			if (!map->buildingAvailable(*bi, canSwim, unit->posX, unit->posY, &dist))
				continue;
			if (dist >= bestDist)
				continue;
			bestDist = dist;
			choosenFood = *bi;
		}
		if (choosenFood)
			return choosenFood;
	}
	
	return bestEnemyFood;
}




Building *Team::findBestUpgrade(Unit *unit)
{
	Building *choosen=NULL;
	Sint32 score=0x7FFFFFFF;
	int x=unit->posX;
	int y=unit->posY;
	for (int ability=(int)WALK; ability<(int)ARMOR; ability++)
	{
		//This is bad code. If WALK ever ceases to be the first ability or ARMOR ever ceases
		// to be the last, this code willl fail.
		if (unit->canLearn[ability])
		{
			if (unit->verbose)
				printf("guid=(%d) unit->canLearn[ability=%d]\n", unit->gid, ability);
			int actLevel=unit->level[ability];
			for (std::list<Building *>::iterator bi=upgrade[ability].begin(); bi!=upgrade[ability].end(); ++bi)
			{
				Building *b=(*bi);
				if (unit->verbose)
					printf("guid=(%d)  b->gid=%d, b->type->level=%d, actLevel=%d\n", unit->gid, b->gid, b->type->level, actLevel);
				if (b->type->level >= actLevel)
				{
					Sint32 newScore=(map->warpDistSquare(b->posX, b->posY, x, y)<<8)/(b->maxUnitInside-b->unitsInside.size());
					if (newScore<score)
					{
						unit->destinationPurpose=(Sint32)ability;
						//fprintf(logFile, "[%d] score=%d, newScore=%d\n", unit->gid, score, newScore);
						fprintf(logFile, "[%d] tdp6 destinationPurpose=%d\n", unit->gid, unit->destinationPurpose);
						choosen=b;
						score=newScore;
					}
				}
			}
		}
	}
	return choosen;
}


bool Team::prioritize_building(Building* lhs, Building* rhs)
{
	int priority_lhs=0;
	if(lhs->type->shortTypeNum==IntBuildingType::FOOD_BUILDING && !lhs->type->isBuildingSite)
		priority_lhs=2+lhs->type->level*10;
	else
		priority_lhs=1+lhs->type->level*10;
	
	int priority_rhs=0;
	if(rhs->type->shortTypeNum==IntBuildingType::FOOD_BUILDING && !rhs->type->isBuildingSite)
		priority_rhs=2+rhs->type->level*10;
	else
		priority_rhs=1+rhs->type->level*10;

	if(priority_lhs != priority_rhs)	
	{
		return priority_lhs > priority_rhs;
	}
	else
	{
		//This uses some fraction math in order to be able to compare the relative percent of units needed
		//for each building. The fractions are (needed_units / wanted_units) for both lhs and rhs.
		//The trick is to put them into a common denominator, which is done by cross multiplying.
		//The denominators don't actually need to be computed, only the numerators.
		int ratio_lhs_unit = (lhs->maxUnitWorking  - lhs->unitsWorking.size()) * rhs->unitsWorking.size();
		int ratio_rhs_unit = (rhs->maxUnitWorking  - rhs->unitsWorking.size()) * lhs->unitsWorking.size();
		if(ratio_lhs_unit == ratio_rhs_unit)
		{
			lhs->computeWishedRessources();
			rhs->computeWishedRessources();
			int ratio_lhs_ressource = lhs->totalWishedRessource();
			int ratio_rhs_ressource = rhs->totalWishedRessource();
			return ratio_lhs_ressource > ratio_rhs_ressource;
		}
		else
		{
			return ratio_lhs_unit > ratio_rhs_unit;
		}
	}
	return false;
}


void Team::add_building_needing_work(Building* b)
{
	bool did_find_position=false;
	for(unsigned i=0; i<buildingsNeedingUnits.size(); ++i)
	{
		if(prioritize_building(b, buildingsNeedingUnits[i]))
		{
			buildingsNeedingUnits.insert(buildingsNeedingUnits.begin() + i, b);
			did_find_position=true;
			break;
		}
	}
	if(!did_find_position)
		buildingsNeedingUnits.push_back(b);
}


void Team::remove_building_needing_work(Building* b)
{
	buildingsNeedingUnits.erase(std::find(buildingsNeedingUnits.begin(), buildingsNeedingUnits.end(), b));
}



void Team::updateAllBuildingTasks()
{
	std::sort(buildingsNeedingUnits.begin(), buildingsNeedingUnits.end(), Team::prioritize_building);
	for(unsigned i=0; i<buildingsNeedingUnits.size(); ++i)
	{
		if(buildingsNeedingUnits[i]->type->isVirtual)
			buildingsNeedingUnits[i]->subscribeForFlagingStep();
		else
			buildingsNeedingUnits[i]->subscribeToBringRessourcesStep();
	}
}



int Team::maxBuildLevel(void)
{
	int maxLevel=0;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u && u->performance[BUILD])
		{
			int unitLevel=u->level[BUILD];
			if (unitLevel>maxLevel)
				maxLevel=unitLevel;
		}
	}
	return maxLevel;
}




void Team::removeFromAbilitiesLists(Building *building)
{
	for (int ui=0; ui<NB_ABILITY; ui++)
		if (building->type->upgrade[ui])
			upgrade[ui].remove(building);

	if (building->type->canFeedUnit)
		canFeedUnit.remove(building);
	if (building->type->canHealUnit)
		canHealUnit.remove(building);
	if (building->type->canExchange)
		canExchange.remove(building);

	if (building->type->unitProductionTime)
		swarms.remove(building);
	if (building->type->shootingRange)
		turrets.remove(building);

	if (building->type->zonable[WORKER])
		clearingFlags.remove(building);

	if (building->type->isVirtual)
		virtualBuildings.remove(building);
}




void Team::addToStaticAbilitiesLists(Building *building)
{
	if (building->type->canExchange)
		canExchange.push_back(building);

	if (building->type->unitProductionTime)
		swarms.push_back(building);

	if (building->type->shootingRange)
		turrets.push_back(building);

	if (building->type->zonable[WORKER])
		clearingFlags.push_back(building);
;
	if (building->type->isVirtual)
		virtualBuildings.push_back(building);
}




void Team::syncStep(void)
{
	integrity();

	if (noMoreBuildingSitesCountdown>0)
		noMoreBuildingSitesCountdown--;

	int nbUsefullUnits = 0;
	int nbUsefullUnitsAlone = 0;
	for (int i = 0; i < 1024; i++)
	{
		Unit *u = myUnits[i];
		if (u)
		{
			if (u->typeNum != EXPLORER)
			{
				nbUsefullUnits++;
				if (u->medical == Unit::MED_FREE || (u->insideTimeout < 0 && u->attachedBuilding && u->attachedBuilding->type->canFeedUnit))
					nbUsefullUnitsAlone++;
			}
			u->syncStep();
			if (u->isDead)
			{
				fprintf(logFile, "unit guid=%d deleted\n", u->gid);
				if (u->attachedBuilding)
					fprintf(logFile, " attachedBuilding->bgid=%d\n", u->attachedBuilding->gid);
				if(game->selectedUnit == u)
					game->selectedUnit = NULL;
				delete u;
				myUnits[i] = NULL;
			}
		}
	}

	bool isDirtyGlobalGradient=false;
	for (std::list<Building *>::iterator it=buildingsWaitingForDestruction.begin(); it!=buildingsWaitingForDestruction.end(); ++it)
	{
		Building *building=*it;
		if (building->unitsInside.size()==0)
		{
			if (building->buildingState==Building::WAITING_FOR_DESTRUCTION)
			{
				if (!building->type->isVirtual)
				{
					map->setBuilding(building->posX, building->posY, building->type->width, building->type->height, NOGBID);
					map->dirtyLocalGradient(building->posX-16, building->posY-16, 31+building->type->width, 31+building->type->height, teamNumber);
					isDirtyGlobalGradient=true;
				}
				building->buildingState=Building::DEAD;
				prestige-=(*it)->type->prestige;
				buildingsToBeDestroyed.push_front(building);
			}

			std::list<Building *>::iterator ittemp=it;
			it=buildingsWaitingForDestruction.erase(ittemp);
		}
	}
	if (isDirtyGlobalGradient)
	{
		dirtyGlobalGradient();
		map->updateForbiddenGradient(teamNumber);
		map->updateGuardAreasGradient(teamNumber);
	}

	for (std::list<Building *>::iterator it=buildingsToBeDestroyed.begin(); it!=buildingsToBeDestroyed.end(); ++it)
	{
		Building *building=*it;
		fprintf(logFile, "building guid=%d deleted\n", building->gid);
		fflush(logFile);

		removeFromAbilitiesLists(building);

		assert(building->unitsWorking.size()==0);
		assert(building->unitsInside.size()==0);

		//TODO: optimisation: we can avoid some of thoses remove(Building *) by keeping a building state to detect which remove() are needed.
		buildingsTryToBuildingSiteRoom.remove(building);

		if (game->selectedBuilding==building)
			game->selectedBuilding=NULL;

		myBuildings[Building::GIDtoID(building->gid)]=NULL;
		delete building;
	}

	if (buildingsToBeDestroyed.size())
		buildingsToBeDestroyed.clear();

	for (std::list<Building *>::iterator it=buildingsTryToBuildingSiteRoom.begin(); it!=buildingsTryToBuildingSiteRoom.end(); ++it)
	{
		if ((*it)->tryToBuildingSiteRoom())
		{
			std::list<Building *>::iterator ittemp=it;
			it=buildingsTryToBuildingSiteRoom.erase(ittemp);
		}
	}

	updateAllBuildingTasks();

	bool isEnoughFoodInSwarm=false;

	for (int i=0; i<1024; ++i)
	{
		if(myBuildings[i])
		{
			//Step in myBuildings does virtually nothing
			myBuildings[i]->step();
		}
	}
	
	for (std::list<Building *>::iterator it=swarms.begin(); it!=swarms.end(); ++it)
		{
			if (!(*it)->locked[1] && (*it)->ressources[CORN]>(*it)->type->ressourceForOneUnit)
				isEnoughFoodInSwarm=true;
			(*it)->swarmStep();
		}

	for (std::list<Building *>::iterator it=turrets.begin(); it!=turrets.end(); ++it)
		(*it)->turretStep();

	for (std::list<Building *>::iterator it=clearingFlags.begin(); it!=clearingFlags.end(); ++it)
		(*it)->clearingFlagStep();

	bool isDying= (playersMask==0)
		|| (!isEnoughFoodInSwarm && nbUsefullUnitsAlone==0 && (nbUsefullUnits==0 || (canFeedUnit.size()==0 && canHealUnit.size()==0)));
	if (isAlive && isDying)
	{
		isAlive=false;
		fprintf(logFile, "Team %d is dead:\n", teamNumber);
		fprintf(logFile, " isEnoughFoodInSwarm=%d\n", isEnoughFoodInSwarm);
		fprintf(logFile, " nbUsefullUnitsAlone=%d\n", nbUsefullUnitsAlone);
		fprintf(logFile, " nbUsefullUnits=%d\n", nbUsefullUnits);
		fprintf(logFile, "  canFeedUnit.size()=%zd\n", canFeedUnit.size());
		fprintf(logFile, "  canHealUnit.size()=%zd\n", canHealUnit.size());
	}

	stats.step(this);
	updateEvents();
}




void Team::checkControllingPlayers(void)
{
	if (!hasWon)
	{
		bool stillInControl = false;
		for (int i=0; i<game->gameHeader.getNumberOfPlayers(); i++)
		{
			if ((game->players[i]->teamNumber == teamNumber) &&
				game->players[i]->type != Player::P_LOST_DROPPING &&
				game->players[i]->type != Player::P_LOST_FINAL)
				stillInControl = true;
		}
		isAlive = isAlive && stillInControl;
	}
}



void Team::pushGameEvent(boost::shared_ptr<GameEvent> event)
{
	///Ignore events when the cooldown is above 0
	if(eventCooldownTimers[event->getEventType()] == 0)
	{
		events.push(event);
		eventCooldownTimers[event->getEventType()]=50;
	}
}
	


boost::shared_ptr<GameEvent> Team::getEvent()
{
	if(events.empty())
		return boost::shared_ptr<GameEvent>();

	boost::shared_ptr<GameEvent> event = events.front();
	events.pop();
	return event;
}
	


void Team::updateEvents()
{
	for(int i=0; i<GESize; ++i)
	{
		if(eventCooldownTimers[i]>0)
			eventCooldownTimers[i]-=1;
	}


	bool testAnother=true;
	while(testAnother && !events.empty())
	{
		boost::shared_ptr<GameEvent> event = events.front();
		if((game->stepCounter - event->getStep()) > 100)
		{
			events.pop();
		}
		else
		{
			testAnother=false;
		}
	}
}

	
bool Team::wasRecentEvent(GameEventType type)
{
	return eventCooldownTimers[type]==50;
}


void Team::dirtyGlobalGradient()
{
	game->dirtyWarFlagGradient();
	for (int id=0; id<1024; id++)
	{
		Building *b=myBuildings[id];
		if (b)
			for (int canSwim=0; canSwim<2; canSwim++)
				if (b->globalGradient[canSwim])
				{
					//printf("freeing globalGradient for gbid=%d (%p)\n", b->gid, b->globalGradient[canSwim]);
					delete[] b->globalGradient[canSwim];
					b->globalGradient[canSwim]=NULL;
					b->locked[canSwim]=false;
				}
	}
}

void Team::dirtyWarFlagGradient()
{
	for (std::list<Building *>::const_iterator it = virtualBuildings.begin(); it != virtualBuildings.end(); ++it)
	{
		Building *b = *it;
		if (b->type->zonable[WARRIOR])
			for (int canSwim=0; canSwim<2; canSwim++)
				if (b->globalGradient[canSwim])
				{
					delete[] b->globalGradient[canSwim];
					b->globalGradient[canSwim]=NULL;
					b->locked[canSwim]=false;
				}
	}
}

Uint32 Team::checkSum(std::vector<Uint32> *checkSumsVector, std::vector<Uint32> *checkSumsVectorForBuildings, std::vector<Uint32> *checkSumsVectorForUnits)
{
	Uint32 cs=0;

	cs^=BaseTeam::checkSum();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [1+t*20]

	for (int i=0; i<1024; i++)
		if (myUnits[i])
	{
		cs^=myUnits[i]->checkSum(checkSumsVectorForUnits);
		cs=(cs<<31)|(cs>>1);
	}
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [2+t*20]

	for (int i=0; i<1024; i++)
		if (myBuildings[i])
	{
		cs^=myBuildings[i]->checkSum(checkSumsVectorForBuildings);
		cs=(cs<<31)|(cs>>1);
	}
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [3+t*20]

	for (int i=0; i<NB_ABILITY; i++)
	{
		cs^=upgrade[i].size();
		cs=(cs<<31)|(cs>>1);
	}
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [4+t*20]

	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [7+t*20]

	cs^=canExchange.size();
	cs^=canFeedUnit.size();
	cs^=canHealUnit.size();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [8+t*20]

	cs^=buildingsToBeDestroyed.size();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [9+t*20]
	cs^=buildingsTryToBuildingSiteRoom.size();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [10+t*20]

	cs^=swarms.size();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [11+t*20]
	cs^=turrets.size();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [12+t*20]

	cs^=allies;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [13+t*20]
	cs^=enemies;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [14+t*20]
	cs^=sharedVisionExchange;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [15+t*20]
	cs^=sharedVisionFood;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [16+t*20]
	cs^=sharedVisionOther;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [17+t*20]
	cs^=me;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [18+t*20]

	cs^=noMoreBuildingSitesCountdown;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [19+t*20]

	cs^=prestige;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [20+t*20]

	return cs;
}




std::string Team::getFirstPlayerName(void) const
{
	for (int i=0; i<game->gameHeader.getNumberOfPlayers(); i++)
	{
		if (game->players[i]->team == this)
			return game->players[i]->name;
	}
	return Toolkit::getStringTable()->getString("[Uncontrolled]");
}
