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

#include <float.h>
#include <Toolkit.h>
#include <StringTable.h>
#include "Team.h"
#include "BuildingType.h"
#include "Game.h"
#include "Utilities.h"
#include <math.h>
#include "Marshaling.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"

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

bool BaseTeam::load(SDL_RWops *stream, Sint32 versionMinor)
{
	// loading baseteam
	if (versionMinor>25)
		type=(TeamType)SDL_ReadBE32(stream);
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
	SDL_WriteBE32(stream, (Uint32)type);
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

Sint32 BaseTeam::checkSum()
{
	Sint32 cs=0;

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

	subscribeForInside.clear();
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
	noMoreBuildingSitesCountdown=0;
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

bool Team::openMarket()
{
	int numberOfTeam=game->session.numberOfTeam;
	for (int ti=0; ti<numberOfTeam; ti++)
		if (ti!=teamNumber && (game->teams[ti]->sharedVisionExchange & me))
			return true;
	return false;
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
	SessionGame &session=game->session;
	
	bool concurency=false;
	for (int ti=0; ti<session.numberOfTeam; ti++)
		if (ti!=teamNumber && (game->teams[ti]->sharedVisionFood & me) && !(game->teams[ti]->allies & me))
		{
			concurency=true;
			break;
		}
	
	int enemyHappyness=0;
	int maxHappyness[32];
	memset(maxHappyness, 0, 32*sizeof(int));
	if (concurency)
	{
		if (unit->performance[FLY])
		{
			for (int ti=0; ti<session.numberOfTeam; ti++)
				if (ti!=teamNumber)
				{
					Team *t=game->teams[ti];
					if ((t->sharedVisionFood & me) && !(t->allies & me))
						for (std::list<Building *>::iterator bi=t->canFeedUnit.begin(); bi!=t->canFeedUnit.end(); ++bi)
						{
							int h=(*bi)->aviableHappynessLevel();
							if (h>maxHappyness[ti])
							{
								maxHappyness[ti]=h;
								if (h>enemyHappyness)
									enemyHappyness=h;
							}
						}
				}
		}
		else
		{
			int x=unit->posX;
			int y=unit->posY;
			bool canSwim=unit->performance[SWIM];
			for (int ti=0; ti<session.numberOfTeam; ti++)
				if (ti!=teamNumber)
				{
					Team *t=game->teams[ti];
					if ((t->sharedVisionFood & me) && !(t->allies & me))
						for (std::list<Building *>::iterator bi=t->canFeedUnit.begin(); bi!=t->canFeedUnit.end(); ++bi)
						{
							int h=(*bi)->aviableHappynessLevel();
							int dist;
							if (h>maxHappyness[ti] && map->buildingAviable(*bi, canSwim, x, y, &dist))
							{
								maxHappyness[ti]=h;
								if (h>enemyHappyness)
									enemyHappyness=h;
							}
						}
				}
		}
	}
	
	if (unit->performance[FLY])
	{
		int x=unit->posX;
		int y=unit->posY;
		Building *choosen=NULL;
		int minDist=INT_MAX;
		for (std::list<Building *>::iterator bi=canFeedUnit.begin(); bi!=canFeedUnit.end(); ++bi)
		{
			Building *b=(*bi);
			if (b->aviableHappynessLevel()>=enemyHappyness)
			{
				int buildingDist=map->warpDistSquare(x, y, b->posX, b->posY);
				if (buildingDist<minDist)
				{
					choosen=b;
					minDist=buildingDist;
				}
			}
		}
		if (choosen)
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
			if (b->aviableHappynessLevel()>=enemyHappyness)
			{
				int buildingDist;
				if (map->buildingAviable(b, canSwim, x, y, &buildingDist) && buildingDist<minDist)
				{
					choosen=b;
					minDist=buildingDist;
				}
			}
		}
		if (choosen)
			return choosen;
	}
	
	if (!concurency)
		return NULL;
	
	bool concurent[32];
	memset(concurent, 0, 32*sizeof(bool));
		
	for (int ti=0; ti<session.numberOfTeam; ti++)
		if (ti!=teamNumber)
			concurent[ti]=(maxHappyness[ti]>=enemyHappyness) && (game->teams[ti]->sharedVisionFood & me) && !(game->teams[ti]->allies & me);
	
	if (unit->performance[FLY])
	{
		int x=unit->posX;
		int y=unit->posY;
		Building *choosen=NULL;
		int minDist=INT_MAX;
		for (int ti=0; ti<session.numberOfTeam; ti++)
			if (ti!=teamNumber && concurent[ti])
			{
				Team *t=game->teams[ti];
				for (std::list<Building *>::iterator bi=t->canFeedUnit.begin(); bi!=t->canFeedUnit.end(); ++bi)
				{
					Building *b=(*bi);
					if (b->aviableHappynessLevel()>=enemyHappyness)
					{
						int buildingDist=map->warpDistSquare(x, y, b->posX, b->posY);
						if (buildingDist<minDist)
						{
							choosen=b;
							minDist=buildingDist;
						}
					}
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
		for (int ti=0; ti<session.numberOfTeam; ti++)
			if (ti!=teamNumber && concurent[ti])
			{
				Team *t=game->teams[ti];
				for (std::list<Building *>::iterator bi=t->canFeedUnit.begin(); bi!=t->canFeedUnit.end(); ++bi)
				{
					Building *b=(*bi);
					if (b->aviableHappynessLevel()>=enemyHappyness)
					{
						int buildingDist;
						if (map->buildingAviable(b, canSwim, x, y, &buildingDist) && buildingDist<minDist)
						{
							choosen=b;
							minDist=buildingDist;
						}
					}
				}
			}
		
		if (choosen)
			printf("guid=%d found gbui=%d\n", unit->gid, choosen->gid);
		else
		{
			printf("guid=%d found no ennemy building (enemyHappyness=%d)\n", unit->gid, enemyHappyness);
			
			int x=unit->posX;
			int y=unit->posY;
			bool canSwim=unit->performance[SWIM];
			Building *choosen=NULL;
			int minDist=INT_MAX;
			for (int ti=0; ti<session.numberOfTeam; ti++)
				if (ti!=teamNumber && concurent[ti])
				{
					printf(" team ti=%d suitable, nbb=%d\n", ti, canFeedUnit.size());
					Team *t=game->teams[ti];
					for (std::list<Building *>::iterator bi=t->canFeedUnit.begin(); bi!=t->canFeedUnit.end(); ++bi)
					{
						Building *b=(*bi);
						if (b->aviableHappynessLevel()>=enemyHappyness)
						{
							int buildingDist;
							if (map->buildingAviable(b, canSwim, x, y, &buildingDist))
							{
								if (/*map->buildingAviable(b, canSwim, x, y, &buildingDist) &&*/ buildingDist<minDist)
								{
									choosen=b;
									minDist=buildingDist;
								}
								else
									printf(" building bgid=%d buildingDist=%d, minDist=%d\n", b->gid, buildingDist, minDist);
							}
							else
								printf(" building bgid=%d not aviable\n", b->gid);
						}
						else
							printf(" building bgid=%d aviableHappynessLevel()=%d < %d\n", b->gid, b->aviableHappynessLevel(), enemyHappyness);
					}
				}
				else
					printf(" team ti=%d not suitable\n", ti);
		}
		return choosen;
	}
}

Building *Team::findBestFoodable(Unit *unit)
{
	int x=unit->posX;
	int y=unit->posY;
	int r=unit->caryedRessource;
	int timeLeft=(unit->hungry-unit->trigHungry)/race.unitTypes[0][0].hungryness;
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
	int timeLeft=(unit->hungry-unit->trigHungry)/race.unitTypes[0][0].hungryness;
	
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
				if (b->type->level<=actLevel)
				{
					int need=b->neededRessource(ri);
					int buildingDist;
					if (need && map->buildingAviable(b, canSwim, x, y, &buildingDist) && (buildingDist<timeLeft))
					{
						double newScore=(double)(buildingDist+ressourceDist)/((double)(b->maxUnitWorking-b->unitsWorking.size())*(double)need);
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
	if (choosen)
		return choosen;
	
	if (!openMarket())
		return NULL;
	
	SessionGame &session=game->session;
	// We compute all what's aviable from foreign ressources: (mask)
	Uint32 allForeignSendRessourceMask=0;
	Uint32 allForeignReceiveRessourceMask=0;
	for (int ti=0; ti<session.numberOfTeam; ti++)
		if (ti!=teamNumber && (game->teams[ti]->sharedVisionExchange & me))
		{
			std::list<Building *> foreignCanExchange=game->teams[ti]->canExchange;
			for (std::list<Building *>::iterator fbi=foreignCanExchange.begin(); fbi!=foreignCanExchange.end(); ++fbi)
			{
				Uint32 sendRessourceMask=(*fbi)->sendRessourceMask;
				for (int r=0; r<HAPPYNESS_COUNT; r++)
					if ((sendRessourceMask & (1<<r)) && ((*fbi)->ressources[HAPPYNESS_BASE+r]<=0))
						sendRessourceMask&=(~(1<<r));
				allForeignSendRessourceMask|=sendRessourceMask;

				Uint32 receiveRessourceMask=(*fbi)->receiveRessourceMask;
				for (int r=0; r<HAPPYNESS_COUNT; r++)
					if ((receiveRessourceMask & (1<<r)) && ((*fbi)->ressources[HAPPYNESS_BASE+r]>=(*fbi)->type->maxRessource[HAPPYNESS_BASE+r]))
						receiveRessourceMask&=(~(1<<r));
				allForeignReceiveRessourceMask|=receiveRessourceMask;
			}
		}
	
	//printf(" allForeignSendRessourceMask=%d, allForeignReceiveRessourceMask=%d\n", allForeignSendRessourceMask, allForeignReceiveRessourceMask);
	if (allForeignSendRessourceMask==0 || allForeignReceiveRessourceMask==0)
		return NULL;
	
	// We compute all what's aviable from our own ressources: (mask)
	Uint32 allOwnSendRessourceMask=0;
	Uint32 allOwnReceiveRessourceMask=0;
	for (std::list<Building *>::iterator bi=canExchange.begin(); bi!=canExchange.end(); ++bi)
	{
		Uint32 sendRessourceMask=(*bi)->sendRessourceMask;
		for (int r=0; r<HAPPYNESS_COUNT; r++)
			if ((sendRessourceMask & (1<<r)) && ((*bi)->ressources[HAPPYNESS_BASE+r]<=0))
				sendRessourceMask&=(~(1<<r));
		allOwnSendRessourceMask|=sendRessourceMask;

		Uint32 receiveRessourceMask=(*bi)->receiveRessourceMask;
		for (int r=0; r<HAPPYNESS_COUNT; r++)
			if ((receiveRessourceMask & (1<<r)) && ((*bi)->ressources[HAPPYNESS_BASE+r]>=(*bi)->type->maxRessource[HAPPYNESS_BASE+r]))
				receiveRessourceMask&=(~(1<<r));
		allOwnReceiveRessourceMask|=receiveRessourceMask;
	}
	
	//printf(" allOwnSendRessourceMask=%d, allOwnReceiveRessourceMask=%d\n", allOwnSendRessourceMask, allOwnReceiveRessourceMask);
	if ((allForeignSendRessourceMask & allOwnReceiveRessourceMask)==0 || (allForeignReceiveRessourceMask & allOwnSendRessourceMask)==0)
		return NULL;
	
	choosen=NULL;
	score=DBL_MAX;
	for (std::list<Building *>::iterator bi=canExchange.begin(); bi!=canExchange.end(); ++bi)
	{
		Uint32 sendRessourceMask=(*bi)->sendRessourceMask;
		Uint32 receiveRessourceMask=(*bi)->receiveRessourceMask;
		int buildingDist;
		if ((sendRessourceMask & allForeignReceiveRessourceMask)
			&& (receiveRessourceMask & allForeignSendRessourceMask)
			&& map->buildingAviable(*bi, canSwim, x, y, &buildingDist)
			&& (buildingDist<timeLeft))
			for (int ti=0; ti<session.numberOfTeam; ti++)
				if (ti!=teamNumber && (game->teams[ti]->sharedVisionExchange & me))
				{
					Team *foreignTeam=game->teams[ti];
					std::list<Building *> foreignCanExchange=foreignTeam->canExchange;
					for (std::list<Building *>::iterator fbi=foreignCanExchange.begin(); fbi!=foreignCanExchange.end(); ++fbi)
					{
						Uint32 foreignSendRessourceMask=(*fbi)->sendRessourceMask;
						Uint32 foreignReceiveRessourceMask=(*fbi)->receiveRessourceMask;
						int foreignBuildingDist;
						if ((sendRessourceMask & foreignReceiveRessourceMask)
							&& (receiveRessourceMask & foreignSendRessourceMask)
							&& map->buildingAviable(*fbi, canSwim, x, y, &foreignBuildingDist)
							&& (buildingDist+foreignBuildingDist)<(timeLeft>>1))
						{
							double newScore=(double)(buildingDist+foreignBuildingDist)/(double)((*bi)->maxUnitWorking-(*bi)->unitsWorking.size());
							if (newScore<score)
							{
								choosen=*bi;
								score=newScore;
								assert(choosen);
								unit->ownExchangeBuilding=*bi;
								unit->foreingExchangeBuilding=*fbi;
								unit->destinationPurprose=receiveRessourceMask & foreignSendRessourceMask;
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
	int timeLeft=(unit->hungry-unit->trigHungry)/race.unitTypes[0][0].hungryness;
	
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
	if(!BaseTeam::load(stream, versionMinor))
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

	subscribeForInside.clear();
	subscribeToBringRessources.clear();
	subscribeForFlaging.clear();

	// resolve cross reference
	for (int i=0; i<1024; i++)
		if (myUnits[i])
			myUnits[i]->loadCrossRef(stream, this);
	for (int i=0; i<1024; i++)
		if (myBuildings[i])
		{
			myBuildings[i]->loadCrossRef(stream, buildingstypes, this);
			if (myBuildings[i]->unitsInsideSubscribe.size())
			{
				subscribeForInside.push_back(myBuildings[i]);
				myBuildings[i]->subscribeForInside=1;
			}
			if (myBuildings[i]->unitsWorkingSubscribe.size())
			{
				if (myBuildings[i]->type->defaultUnitStayRange)
				{
					subscribeForFlaging.push_back(myBuildings[i]);
					myBuildings[i]->subscribeForFlaging=1;
				}
				else
				{
					subscribeToBringRessources.push_back(myBuildings[i]);
					myBuildings[i]->subscribeToBringRessources=1;
				}
			}
			if (myBuildings[i]->type->canExchange)
				canExchange.push_back(myBuildings[i]);
		}

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

void Team::clearLists(void)
{
	foodable.clear();
	fillable.clear();
	for (int i=0; i<NB_UNIT_TYPE; i++)
		zonable[i].clear();
	for (int i=0; i<NB_ABILITY; i++)
		upgrade[i].clear();
	canFeedUnit.clear();
	canHealUnit.clear();
	canExchange.clear();
	subscribeForInside.clear();
	subscribeToBringRessources.clear();
	subscribeForFlaging.clear();
	buildingsWaitingForDestruction.clear();
	buildingsToBeDestroyed.clear();
	buildingsTryToBuildingSiteRoom.clear();
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
	}

	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u)
			u->integrity();
	}
}

void Team::syncStep(void)
{
	integrity();
	
	if (noMoreBuildingSitesCountdown>0)
		noMoreBuildingSitesCountdown--;
	
	int nbUnits=0;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u)
		{
			if (u->displacement!=Unit::DIS_EXITING_BUILDING || u->movement!=Unit::MOV_INSIDE)
				nbUnits++;
			u->syncStep();
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
			(*it)->subscribeForInside=2;
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
			(*it)->subscribeToBringRessources=2;
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
			(*it)->subscribeForFlaging=2;
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

Sint32 Team::checkSum(std::list<Uint32> *checkSumsList, std::list<Uint32> *checkSumsListForBuildings)
{
	Sint32 cs=0;

	cs^=BaseTeam::checkSum();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [0+t*20]
	
	for (int i=0; i<1024; i++)
		if (myUnits[i])
		{
			cs^=myUnits[i]->checkSum();
			cs=(cs<<31)|(cs>>1);
		}
	if (checkSumsList)
		checkSumsList->push_back(cs);// [1+t*20]
		
	for (int i=0; i<1024; i++)
		if (myBuildings[i])
		{
			cs^=myBuildings[i]->checkSum(checkSumsListForBuildings);
			cs=(cs<<31)|(cs>>1);
		}
	if (checkSumsList)
		checkSumsList->push_back(cs);// [2+t*20]
	
	for (int i=0; i<NB_ABILITY; i++)
	{
		cs^=upgrade[i].size();
		cs=(cs<<31)|(cs>>1);
	}
	if (checkSumsList)
		checkSumsList->push_back(cs);// [3+t*20]
	
	cs^=foodable.size();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [4+t*20]
	cs^=fillable.size();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [5+t*20]
	
	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		cs^=zonable[i].size();
		cs=(cs<<31)|(cs>>1);
	}
	if (checkSumsList)
		checkSumsList->push_back(cs);// [6+t*20]
	
	cs^=canExchange.size();
	cs^=canFeedUnit.size();
	cs^=canHealUnit.size();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [7+t*20]
	
	cs^=buildingsToBeDestroyed.size();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [8+t*20]
	cs^=buildingsTryToBuildingSiteRoom.size();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [9+t*20]
	
	cs^=swarms.size();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [10+t*20]
	cs^=turrets.size();
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [11+t*20]

	cs^=allies;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [12+t*20]
	cs^=enemies;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [13+t*20]
	cs^=sharedVisionExchange;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [14+t*20]
	cs^=sharedVisionFood;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [15+t*20]
	cs^=sharedVisionOther;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [16+t*20]
	cs^=me;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [17+t*20]

	cs^=noMoreBuildingSitesCountdown;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [18+t*20]
	
	cs^=prestige;
	cs=(cs<<31)|(cs>>1);
	if (checkSumsList)
		checkSumsList->push_back(cs);// [19+t*20]
	
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
