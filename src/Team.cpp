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

#include "Team.h"
#include "BuildingType.h"
#include "Game.h"
#include "Utilities.h"
#include <math.h>
#include "Marshaling.h"
#include "GlobalContainer.h"
#include <float.h>
#include "LogFileManager.h"
#include <Toolkit.h>
#include <StringTable.h>

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

bool BaseTeam::load(SDL_RWops *stream)
{
	// loading baseteam
	teamNumber=SDL_ReadBE32(stream);
	numberOfPlayer=SDL_ReadBE32(stream);
	SDL_RWread(stream, &colorR, 1, 1);
	SDL_RWread(stream, &colorG, 1, 1);
	SDL_RWread(stream, &colorB, 1, 1);
	SDL_RWread(stream, &colorPAD, 1, 1);
	playersMask=SDL_ReadBE32(stream);
	if(!race.load(stream))
		return false;
	return true;
}

void BaseTeam::save(SDL_RWops *stream)
{
	// saving baseteam
	SDL_WriteBE32(stream, teamNumber);
	SDL_WriteBE32(stream, numberOfPlayer);
	SDL_RWwrite(stream, &colorR, 1, 1);
	SDL_RWwrite(stream, &colorG, 1, 1);
	SDL_RWwrite(stream, &colorB, 1, 1);
	SDL_RWwrite(stream, &colorPAD, 1, 1);
	SDL_WriteBE32(stream, playersMask);
	race.save(stream);
}

Uint8 BaseTeam::getOrderType()
{
	return DATA_BASE_TEAM;
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

	teamNumber=getSint32safe(data, 0);
	numberOfPlayer=getSint32safe(data, 4);
	colorR=getUint8(data, 8);
	colorG=getUint8(data, 9);
	colorB=getUint8(data, 10);
	colorPAD=getUint8(data, 11);
	playersMask=getSint32safe(data, 12);
	// TODO : create the race from the network here.

	return true;
}

int BaseTeam::getDataLength()
{
	return 16;
}

Sint32 BaseTeam::checkSum()
{
	Sint32 cs=0;

	cs^=teamNumber;
	cs^=numberOfPlayer;
	cs^=playersMask;

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

Team::Team(SDL_RWops *stream, Game *game, Sint32 versionMinor)
:BaseTeam()
{
	logFile = globalContainer->logFileManager->getFile("Team.log");
	assert(game);
	this->game=game;
	this->map=&game->map;
	init();
	bool success=load(stream, &(globalContainer->buildingsTypes), versionMinor);
	assert(success);
}

Team::~Team()
{
	if (!disableRecursiveDestruction)
	{
		for (int i=0; i<1024; ++i)
			if (myUnits[i])
				delete myUnits[i];
		for (int i=0; i<1024; ++i)
			if (myBuildings[i])
				delete myBuildings[i];
	}
}

void Team::init(void)
{
	for (int i=0; i<1024; i++)
		myUnits[i]=NULL;

	for (int i=0; i<1024; i++)
		myBuildings[i]=NULL;

	startPosX=startPosY=0;

	subscribeToBringRessources.clear();
	subscribeForFlaging.clear();

	for (int i=0; i<EVENT_TYPE_SIZE; i++)
	{
		isEvent[i]=false;
		eventCooldown[i]=0;
	}
	eventPosX=startPosX;
	eventPosY=startPosY;
	isAlive=true;
	hasWon=false;
	prestige=0;
}

void Team::setBaseTeam(const BaseTeam *initial, bool overwriteAfterbase)
{
	teamNumber=initial->teamNumber;
	numberOfPlayer=initial->numberOfPlayer;
	playersMask=initial->playersMask;
	race=initial->race;
	
	// This case is a bit hard to understand.
	// When you load a teamed saved game, you don't want to change your aliances.
	// But players may join the network game in adifferent order than when the game was saved.
	if (overwriteAfterbase)
	{
		setCorrectColor(initial->colorR, initial->colorG, initial->colorB);
		setCorrectMasks();
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

Building *Team::findNearestHeal(Unit *unit)
{
	if (unit->performance[FLY])
	{
		int x=unit->posX;
		int y=unit->posY;
		int timeLeft=unit->hungry/race.unitTypes[0][0].hungryness;
		Building *choosen=NULL;
		int minDist=INT_MAX;
		for (std::list<Building *>::iterator bi=canHealUnit.begin(); bi!=canHealUnit.end(); ++bi)
		{
			Building *b=(*bi);
			int buildingDist=map->warpDistSquare(x, y, b->posX, b->posY);
			if (buildingDist<timeLeft && buildingDist<minDist)
			{
				choosen=b;
				minDist=buildingDist;
			}
		}
		return choosen;
	}
	else
	{
		int x=unit->posX;
		int y=unit->posY;
		int timeLeft=unit->hungry/race.unitTypes[0][0].hungryness;
		bool canSwim=unit->performance[SWIM];
		Building *choosen=NULL;
		int minDist=INT_MAX;
		for (std::list<Building *>::iterator bi=canHealUnit.begin(); bi!=canHealUnit.end(); ++bi)
		{
			Building *b=(*bi);
			int buildingDist;
			if (map->buildingAviable(b, canSwim, x, y, &buildingDist) && buildingDist<timeLeft && buildingDist<minDist)
			{
				choosen=b;
				minDist=buildingDist;
			}
		}
		return choosen;
	}
}

Building *Team::findNearestFood(Unit *unit)
{
	if (unit->performance[FLY])
	{
		int x=unit->posX;
		int y=unit->posY;
		Building *choosen=NULL;
		int minDist=INT_MAX;
		for (std::list<Building *>::iterator bi=canFeedUnit.begin(); bi!=canFeedUnit.end(); ++bi)
		{
			Building *b=(*bi);
			int buildingDist=map->warpDistSquare(x, y, b->posX, b->posY);
			if (buildingDist<minDist)
			{
				choosen=b;
				minDist=buildingDist;
			}
		}
		return choosen;
	}
	else
	{
		int x=unit->posX;
		int y=unit->posY;
		bool canSwim=unit->performance[SWIM];
		Building *choosen=NULL;
		int minDist=INT_MAX;
		for (std::list<Building *>::iterator bi=canFeedUnit.begin(); bi!=canFeedUnit.end(); ++bi)
		{
			Building *b=(*bi);
			int buildingDist;
			if (map->buildingAviable(b, canSwim, x, y, &buildingDist) && buildingDist<minDist)
			{
				choosen=b;
				minDist=buildingDist;
			}
		}
		return choosen;
	}
}

Building *Team::findBestFoodable(Unit *unit)
{
	int x=unit->posX;
	int y=unit->posY;
	int r=unit->caryedRessource;
	int timeLeft=unit->hungry/race.unitTypes[0][0].hungryness;
	bool canSwim=unit->performance[SWIM];
	
	if (r!=-1)
	{
		// I'm already carying a ressource:
		// I'll only go to a building who need this ressource, if possible.
		Building *choosen=NULL;
		double score=DBL_MAX;
		for (std::list<Building *>::iterator bi=foodable.begin(); bi!=foodable.end(); ++bi)
		{
			Building *b=(*bi);
			if (b->neededRessource(r))
			{
				int buildingDist;
				if (map->buildingAviable(b, canSwim, x, y, &buildingDist) && (buildingDist<timeLeft))
				{
					double newScore=(double)buildingDist/(double)(b->maxUnitWorking-b->unitsWorking.size());
					if (newScore<score)
					{
						choosen=b;
						score=newScore;
					}
				}
			}
		}
		if (choosen)
		{
			unit->destinationPurprose=r;
			return choosen;
		}
	}

	Building *choosen=NULL;
	double score=DBL_MAX;
	for (unsigned ri=0; ri<MAX_RESSOURCES; ri++)
		if (ri==CORN || ri>=HAPPYNESS_BASE)
		{
			int ressourceDist;
			if (map->ressourceAviable(teamNumber, ri, canSwim, x, y, &ressourceDist) && (ressourceDist<timeLeft))
				for (std::list<Building *>::iterator bi=foodable.begin(); bi!=foodable.end(); ++bi)
				{
					Building *b=(*bi);
					if (b->neededRessource(ri))
					{
						int buildingDist;
						if (map->buildingAviable(b, canSwim, x, y, &buildingDist) && (buildingDist<timeLeft))
						{
							double newScore=(double)(ressourceDist+buildingDist)/(double)(b->maxUnitWorking-b->unitsWorking.size());
							if (newScore<score)
							{
								choosen=b;
								score=newScore;
								unit->destinationPurprose=ri;
							}
						}
					}
				}
		}
	
	return choosen;
}

Building *Team::findBestFillable(Unit *unit)
{
	int x=unit->posX;
	int y=unit->posY;
	int r=unit->caryedRessource;
	int actLevel=unit->level[HARVEST];
	assert(!unit->performance[FLY]);
	bool canSwim=unit->performance[SWIM];
	int timeLeft=unit->hungry/race.unitTypes[0][0].hungryness;
	
	if (r!=-1)
	{
		// I'm already carying a ressource:
		// I'll only go to a building who need this ressource, if possible.
		Building *choosen=NULL;
		double score=DBL_MAX;
		for (std::list<Building *>::iterator bi=fillable.begin(); bi!=fillable.end(); ++bi)
		{
			Building *b=(*bi);
			if ((b->type->level<=actLevel)&&(b->neededRessource(r)))
			{
				int buildingDist;
				if (map->buildingAviable(b, canSwim, x, y, &buildingDist) && (buildingDist<timeLeft))
				{
					double newScore=(double)(buildingDist)/(double)(b->maxUnitWorking-b->unitsWorking.size());
					if (newScore<score)
					{
						choosen=b;
						score=newScore;
					}
				}
			}
		}
		if (choosen)
		{
			unit->destinationPurprose=r;
			return choosen;
		}
	}

	Building *choosen=NULL;
	double score=DBL_MAX;
	for (unsigned ri=0; ri<MAX_RESSOURCES; ri++)
	{
		int ressourceDist;
		if (map->ressourceAviable(teamNumber, ri, canSwim, x, y, &ressourceDist) && (ressourceDist<timeLeft))
			for (std::list<Building *>::iterator bi=fillable.begin(); bi!=fillable.end(); ++bi)
			{
				Building *b=(*bi);
				if (b->type->level<=actLevel && b->neededRessource(ri))
				{
					int buildingDist;
					if (map->buildingAviable(b, canSwim, x, y, &buildingDist) && (buildingDist<timeLeft))
					{
						double newScore=(double)(buildingDist+ressourceDist)/(double)(b->maxUnitWorking-b->unitsWorking.size());
						if (newScore<score)
						{
							choosen=b;
							score=newScore;
							unit->destinationPurprose=ri;
						}
					}
				}
			}
	}
		
	return choosen;
}


Building *Team::findBestZonable(Unit *unit)
{
	Building *choosen=NULL;
	double score=DBL_MAX;
	int x=unit->posX;
	int y=unit->posY;
	std::list<Building *> bl=zonable[unit->typeNum];
	bool canSwim=unit->performance[SWIM];
	int timeLeft=unit->hungry/race.unitTypes[0][0].hungryness;
	
	if (unit->performance[FLY])
	{
		for (std::list<Building *>::iterator bi=bl.begin(); bi!=bl.end(); ++bi)
		{
			Building *b=(*bi);
			double buildingDist=sqrt((double)map->warpDistSquare(b->getMidX(), b->getMidY(), x, y));
			if ((int)buildingDist<timeLeft)
			{
				double newScore=(double)buildingDist/(double)(b->maxUnitWorking-b->unitsWorking.size());
				if (newScore<score)
				{
					choosen=b;
					score=newScore;
				}
			}
		}
	}
	else
		for (std::list<Building *>::iterator bi=bl.begin(); bi!=bl.end(); ++bi)
		{
			Building *b=(*bi);
			int buildingDist;
			if (map->buildingAviable(b, canSwim, x, y, &buildingDist) && (buildingDist<timeLeft))
			{
				double newScore=(double)buildingDist/(double)(b->maxUnitWorking-b->unitsWorking.size());
				if (newScore<score)
				{
					choosen=b;
					score=newScore;
				}
			}
		}
	return choosen;
}

Building *Team::findBestUpgrade(Unit *unit)
{
	Building *choosen=NULL;
	float score=FLT_MAX;
	int x=unit->posX;
	int y=unit->posY;
	for (int ability=(int)WALK; ability<(int)ARMOR; ability++)
		if (unit->canLearn[ability])
		{
			int actLevel=unit->level[ability];
			for (std::list<Building *>::iterator bi=upgrade[ability].begin(); bi!=upgrade[ability].end(); ++bi)
			{
				Building *b=(*bi);
				if (b->type->level>=actLevel)
				{
					float newScore=(float)map->warpDistSquare(b->posX, b->posY, x, y)/(float)(b->maxUnitInside-b->unitsInside.size());
					if (newScore<score)
					{
						unit->destinationPurprose=(Sint32)ability;
						choosen=b;
						score=newScore;
					}
				}
			}
		}
	return choosen;
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

bool Team::load(SDL_RWops *stream, BuildingsTypes *buildingstypes, Sint32 versionMinor)
{
	assert(stream);
	assert(buildingsToBeDestroyed.size()==0);
	buildingsTryToBuildingSiteRoom.clear();
	
	// loading baseteam
	if(!BaseTeam::load(stream))
		return false;

	// normal load
	for (int i=0; i<1024; i++)
	{
		if (myUnits[i])
			delete myUnits[i];

		Uint32 isUsed=SDL_ReadBE32(stream);
		if (isUsed)
			myUnits[i]=new Unit(stream, this);
		else
			myUnits[i]=NULL;
	}

	swarms.clear();
	turrets.clear();
	virtualBuildings.clear();
	prestige=0;
	for (int i=0; i<1024; i++)
	{
		if (myBuildings[i])
			delete myBuildings[i];

		Uint32 isUsed=SDL_ReadBE32(stream);
		if (isUsed)
		{
			myBuildings[i]=new Building(stream, buildingstypes, this, versionMinor);
			if (myBuildings[i]->type->unitProductionTime)
				swarms.push_front(myBuildings[i]);
			if (myBuildings[i]->type->shootingRange)
				turrets.push_front(myBuildings[i]);
			if (myBuildings[i]->type->isVirtual)
				virtualBuildings.push_front(myBuildings[i]);
		}
		else
			myBuildings[i]=NULL;
	}

	// resolve cross reference
	for (int i=0; i<1024; i++)
		if (myUnits[i])
			myUnits[i]->loadCrossRef(stream, this);
	for (int i=0; i<1024; i++)
		if (myBuildings[i])
			myBuildings[i]->loadCrossRef(stream, buildingstypes, this);

	allies=SDL_ReadBE32(stream);
	enemies=SDL_ReadBE32(stream);
	sharedVisionExchange=SDL_ReadBE32(stream);
	sharedVisionFood=SDL_ReadBE32(stream);
	sharedVisionOther=SDL_ReadBE32(stream);
	me=SDL_ReadBE32(stream);
	startPosX=SDL_ReadBE32(stream);
	startPosY=SDL_ReadBE32(stream);
	
	// load stats
	stats.endOfGameStatIndex=SDL_ReadBE32(stream);
	for (int i=0; i<TeamStats::END_OF_GAME_STATS_SIZE; i++)
	{
		stats.endOfGameStats[i].value[EndOfGameStat::TYPE_UNITS]=SDL_ReadBE32(stream);
		stats.endOfGameStats[i].value[EndOfGameStat::TYPE_BUILDINGS]=SDL_ReadBE32(stream);
		stats.endOfGameStats[i].value[EndOfGameStat::TYPE_PRESTIGE]=SDL_ReadBE32(stream);
	}

	for (int i=0; i<EVENT_TYPE_SIZE; i++)
	{
		isEvent[i]=false;
		eventCooldown[i]=0;
	}
	eventPosX=startPosX;
	eventPosY=startPosY;
	isAlive=true;
	
	return true;
}

void Team::update()
{
	for (int i=0; i<1024; i++)
		if (myBuildings[i])
			myBuildings[i]->update();
}

void Team::save(SDL_RWops *stream)
{
	// saving baseteam
	BaseTeam::save(stream);

	// saving team
	for (int i=0; i<1024; i++)
		if (myUnits[i])
		{
			SDL_WriteBE32(stream, true);
			myUnits[i]->save(stream);
		}
		else
		{
			SDL_WriteBE32(stream, false);
		}

	for (int i=0; i<1024; i++)
		if (myBuildings[i])
		{
			SDL_WriteBE32(stream, true);
			myBuildings[i]->save(stream);
		}
		else
		{
			SDL_WriteBE32(stream, false);
		}
	
	// save cross reference
	for (int i=0; i<1024; i++)
		if (myUnits[i])
			myUnits[i]->saveCrossRef(stream);
	
	for (int i=0; i<1024; i++)
		if (myBuildings[i])
			myBuildings[i]->saveCrossRef(stream);

	SDL_WriteBE32(stream, allies);
	SDL_WriteBE32(stream, enemies);
	SDL_WriteBE32(stream, sharedVisionExchange);
	SDL_WriteBE32(stream, sharedVisionFood);
	SDL_WriteBE32(stream, sharedVisionOther);
	SDL_WriteBE32(stream, me);
	SDL_WriteBE32(stream, startPosX);
	SDL_WriteBE32(stream, startPosY);
	
	// save stats
	SDL_WriteBE32(stream, stats.endOfGameStatIndex);
	for (int i=0; i<TeamStats::END_OF_GAME_STATS_SIZE; i++)
	{
		SDL_WriteBE32(stream, stats.endOfGameStats[i].value[EndOfGameStat::TYPE_UNITS]);
		SDL_WriteBE32(stream, stats.endOfGameStats[i].value[EndOfGameStat::TYPE_BUILDINGS]);
		SDL_WriteBE32(stream, stats.endOfGameStats[i].value[EndOfGameStat::TYPE_PRESTIGE]);
	}
}

void Team::createLists(void)
{
	// TODO : are thoses assert() right ?
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
				swarms.push_front(myBuildings[i]);
			if (myBuildings[i]->type->shootingRange)
				turrets.push_front(myBuildings[i]);
			if (myBuildings[i]->type->isVirtual)
				virtualBuildings.push_front(myBuildings[i]);
			myBuildings[i]->update();
		}
}

void Team::integrity(void)
{
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
	}
	
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u)
			u->integrity();
	}
}

void Team::step(void)
{
	integrity();
	int nbUnits=0;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u)
		{
			nbUnits++;
			u->step();
			if (u->isDead)
			{
				fprintf(logFile, "unit guid=%d deleted\n", u->gid);
				if (u->attachedBuilding)
					fprintf(logFile, " attachedBuilding->bgid=%d\n", u->attachedBuilding->gid);

				if(game->selectedUnit==u)
					game->selectedUnit=NULL;

				delete u;
				myUnits[i]=NULL;
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
			else
				assert(building->buildingState==Building::DEAD);
			
			std::list<Building *>::iterator ittemp=it;
			it=buildingsWaitingForDestruction.erase(ittemp);
		}
	}
	if (isDirtyGlobalGradient)
		dirtyGlobalGradient();

	for (std::list<Building *>::iterator it=buildingsToBeDestroyed.begin(); it!=buildingsToBeDestroyed.end(); ++it)
	{
		Building *building=*it;
		fprintf(logFile, "building guid=%d deleted\n", building->gid);

		if (building->type->unitProductionTime)
			swarms.remove(building);
		if (building->type->shootingRange)
			turrets.remove(building);
		if (building->type->isVirtual)
			virtualBuildings.remove(building);

		assert(building->unitsWorking.size()==0);
		assert(building->unitsInside.size()==0);
		assert(building->unitsWorkingSubscribe.size()==0);
		assert(building->unitsInsideSubscribe.size()==0);

		//TODO: optimisation: we can avoid some of thoses remove(Building *) by keeping a building state to detect which remove() are needed.
		buildingsTryToBuildingSiteRoom.remove(building);
		subscribeForInside.remove(building);
		subscribeToBringRessources.remove(building);
		subscribeForFlaging.remove(building);

		if(game->selectedBuilding==building)
			game->selectedBuilding=NULL;

		myBuildings[Building::GIDtoID(building->gid)]=NULL;
		delete building;
	}
	
	if (buildingsToBeDestroyed.size())
		buildingsToBeDestroyed.clear();
	
	for (std::list<Building *>::iterator it=buildingsTryToBuildingSiteRoom.begin(); it!=buildingsTryToBuildingSiteRoom.end(); ++it)
		if ((*it)->tryToBuildingSiteRoom())
		{
			std::list<Building *>::iterator ittemp=it;
			it=buildingsTryToBuildingSiteRoom.erase(ittemp);
		}
	
	//printf("subscribeForInside.size()=%d\n", subscribeForInside.size());
	for (std::list<Building *>::iterator it=subscribeForInside.begin(); it!=subscribeForInside.end(); ++it)
		if ((*it)->unitsInsideSubscribe.size()>0)
			(*it)->subscribeForInsideStep();

	for (std::list<Building *>::iterator it=subscribeForInside.begin(); it!=subscribeForInside.end(); ++it)
		if ((*it)->unitsInsideSubscribe.size()==0)
		{
			std::list<Building *>::iterator ittemp=it;
			it=subscribeForInside.erase(ittemp);
		}
	
	//subscribeToBringRessourcesStep
	for (std::list<Building *>::iterator it=subscribeToBringRessources.begin(); it!=subscribeToBringRessources.end(); ++it)
		if ((*it)->unitsWorkingSubscribe.size()>0)
			(*it)->subscribeToBringRessourcesStep();

	for (std::list<Building *>::iterator it=subscribeToBringRessources.begin(); it!=subscribeToBringRessources.end(); ++it)
		if ((*it)->unitsWorkingSubscribe.size()==0)
		{
			std::list<Building *>::iterator ittemp=it;
			it=subscribeToBringRessources.erase(ittemp);
		}
	
	//subscribeForFlagingStep
	for (std::list<Building *>::iterator it=subscribeForFlaging.begin(); it!=subscribeForFlaging.end(); ++it)
		if ((*it)->unitsWorkingSubscribe.size()>0)
			(*it)->subscribeForFlagingStep();

	for (std::list<Building *>::iterator it=subscribeForFlaging.begin(); it!=subscribeForFlaging.end(); ++it)
		if ((*it)->unitsWorkingSubscribe.size()==0)
		{
			std::list<Building *>::iterator ittemp=it;
			it=subscribeForFlaging.erase(ittemp);
		}
	
	bool isEnoughFoodInSwarm=false;
	
	for (std::list<Building *>::iterator it=swarms.begin(); it!=swarms.end(); ++it)
	{
		if ((*it)->ressources[CORN]>(*it)->type->ressourceForOneUnit)
			isEnoughFoodInSwarm=true;
		(*it)->swarmStep();
	}
	
	for (std::list<Building *>::iterator it=turrets.begin(); it!=turrets.end(); ++it)
		(*it)->turretStep();

	isAlive=isAlive && (isEnoughFoodInSwarm || (nbUnits!=0));
	// decount event cooldown counter
	for (int i=0; i<EVENT_TYPE_SIZE; i++)
		if (eventCooldown[i]>0)
			eventCooldown[i]--;
	
	stats.step(this);
}

void Team::computeForbiddenArea()
{
	map->clearForbiddenArea(me);
	for (int id=0; id<1024; id++)
	{
		Building *b=myBuildings[id];
		if (b && b->buildingState==Building::ALIVE && b->type->zonableForbidden)
			map->setForbiddenArea(b->posX, b->posY, b->unitStayRange, me);
	}
}

void Team::dirtyGlobalGradient()
{
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

Sint32 Team::checkSum()
{
	Sint32 cs=0;

	cs^=BaseTeam::checkSum();

	cs=(cs<<31)|(cs>>1);

	cs^=teamNumber;
	cs^=numberOfPlayer;
	cs^=playersMask;
	
	// Let's avoid to have too much calculation
	cs=(cs<<31)|(cs>>1);
	//printf("t(%d)1cs=%x\n", teamNumber, cs);
	for (int i=0; i<1024; i++)
	{
		if (myUnits[i])
		{
			cs^=myUnits[i]->checkSum();
			cs=(cs<<31)|(cs>>1);
		}
	}
	
	cs=(cs<<31)|(cs>>1);
	//printf("t(%d)2cs=%x\n", teamNumber, cs);
	for (int i=0; i<1024; i++)
	{
		if (myBuildings[i])
		{
			cs^=myBuildings[i]->checkSum();
			cs=(cs<<31)|(cs>>1);
		}
	}
	
	cs=(cs<<31)|(cs>>1);
	//printf("t(%d)3cs=%x\n", teamNumber, cs);
	for (int i=0; i<NB_ABILITY; i++)
	{
		cs^=upgrade[i].size();
		cs=(cs<<31)|(cs>>1);
	}
	cs^=foodable.size();
	cs=(cs<<31)|(cs>>1);
	cs^=fillable.size();
	cs=(cs<<31)|(cs>>1);
	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		cs^=zonable[i].size();
		cs=(cs<<31)|(cs>>1);
	}
	
	cs=(cs<<31)|(cs>>1);
	//printf("t(%d)4cs=%x\n", teamNumber, cs);
	cs^=canFeedUnit.size();
	cs=(cs<<31)|(cs>>1);
	cs^=canHealUnit.size();
	cs=(cs<<31)|(cs>>1);
	
	cs^=buildingsToBeDestroyed.size();
	cs=(cs<<31)|(cs>>1);
	cs^=buildingsTryToBuildingSiteRoom.size();
	cs=(cs<<31)|(cs>>1);
	
	cs^=swarms.size();
	cs=(cs<<31)|(cs>>1);
	cs^=turrets.size();
	cs=(cs<<31)|(cs>>1);

	cs^=allies;
	cs^=enemies;
	cs^=sharedVisionExchange;
	cs^=sharedVisionFood;
	cs^=sharedVisionOther;
	cs^=me;

	return cs;
}

const char *Team::getFirstPlayerName(void)
{
	for (int i=0; i<game->session.numberOfPlayer; i++)
	{
		if (game->players[i]->team == this)
			return game->players[i]->name;
	}
	return  Toolkit::getStringTable()->getString("[Uncontrolled]");
}
