/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "Team.h"
#include "BuildingType.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "Utilities.h"
#include <math.h>


BaseTeam::BaseTeam()
{
	teamNumber=0;
	numberOfPlayer=0;
	colorR=0;
	colorG=0;
	colorB=0;
	colorPAD=0;
	playersMask=0;
	type=T_HUMAN;
	
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

char *BaseTeam::getData()
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

bool BaseTeam::setData(const char *data, int dataLength)
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
	cs^=numberOfPlayer;
	cs^=playersMask;

	return cs;
}

Team::Team(Game *game)
:BaseTeam()
{
	this->game=game;
	init();
}

Team::Team(SDL_RWops *stream, Game *game)
:BaseTeam()
{
	this->game=game;
	init();
	bool success=load(stream, &(globalContainer->buildingsTypes));
	assert(success);
}

Team::~Team()
{
	if (!disableRecursiveDestruction)
	{
		int i;
		for (i=0; i<1024; ++i)
		{
			if (myUnits[i])
				delete myUnits[i];
		}
		for (i=0; i<512; ++i)
		{
			if (myBuildings[i])
				delete myBuildings[i];
		}
		for (i=0; i<256; ++i)
		{
			if (myBullets[i])
				delete myBuildings[i];
		}
	}
}

void Team::init(void)
{
	int i;

	for (i=0; i<1024; i++)
	{
		myUnits[i]=NULL;
	}

	for (i=0; i<512; i++)
	{
		myBuildings[i]=NULL;
	}

	for (i=0; i<256; i++)
	{
		myBullets[i]=NULL;
	}

	startPosX=startPosY=0;
	
	subscribeForInsideStep.clear();
	subscribeForWorkingStep.clear();

	for (i=0; i<EVENT_TYPE_SIZE; i++)
	{
		isEvent[i]=false;
		eventCooldown[i]=0;
	}
	eventPosX=startPosX;
	eventPosY=startPosY;
	isAlive=true;
	hasWon=false;
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
	allies=1<<teamNumber;
	enemies=~allies;
	sharedVision=1<<teamNumber;
	me=1<<teamNumber;
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


Building *Team::findNearestUpgrade(int x, int y, Abilities ability, int actLevel)
{
	Building *b=NULL;
	Sint32 dist=MAX_SINT32;
	Sint32 newDist;
	{
		for (std::list<Building *>::iterator it=upgrade[(int)ability].begin(); it!=upgrade[(int)ability].end(); it++)
		{
			if ((*it)->type->level>=actLevel)
			{
				newDist=distSquare((*it)->getMidX(), (*it)->getMidY(), x, y);
				if ( newDist<dist )
				{
					b=*it;
					dist=newDist;
				}
			}
		}
	}
	return b;
}

Building *Team::findNearestJob(int x, int y, Abilities ability, int actLevel)
{
	Building *b=NULL;
	float score=1000.0f;
	float newScore;
	float newScoreA;
	float newScoreB;
	Sint32 maxUnitWorking;
	//Sint32 dist=MAX_SINT32;
	//Sint32 newDist;
	{
		for (std::list<Building *>::iterator it=job[(int)ability].begin(); it!=job[(int)ability].end(); it++)
		{
			if ((*it)->type->level<=actLevel)
			{
				// this is balance code, take user's wishes in account
				newScoreA=0.03f*(float)distSquare((*it)->getMidX(), (*it)->getMidY(), x, y);
				maxUnitWorking=(*it)->maxUnitWorking;
				if (maxUnitWorking)
				{
					newScoreB=(float)((*it)->unitsWorking.size())/(float)maxUnitWorking;
				}
				newScore=newScoreA+newScoreB;
				if ( newScore<score )
				{
					b=*it;
					score=newScore;
				}
				/*newDist=distSquare((*it)->getMidX(), (*it)->getMidY(), x, y);
				if ( newDist<dist )
				{
					b=*it;
					dist=newDist;
				}*/
			}
		}
	}
	return b;
}

Building *Team::findBestConstruction(Unit *unit)
{
	Building *choosen=NULL;
	Sint32 dist=MAX_SINT32;
	Sint32 newDist;
	
	int x=unit->posX;
	int y=unit->posY;
	int r=unit->caryedRessource;
	int actLevel=unit->level[HARVEST];
	Map &map=game->map;
	
	if (r==-1)
	{
		int dx, dy;
		if (map.nearestRessource(x, y, &(RessourceType)r, &dx, &dy))
		{
			// Ressouce "r" is aviable around here.
			// I'll try to find a building who need this ressouce.
			//int addedDist=sqr(x-dx)+sqr(y-dy);
			
			for (std::list<Building *>::iterator it=job[HARVEST].begin(); it!=job[HARVEST].end(); it++)
			{
				Building *building=(*it);
				if ((building->type->level<=actLevel)&&(building->neededRessource(r)))
				{
					newDist=distSquare(building->getMidX(), building->getMidY(), dx, dy);
					int maxUnitWorking=building->maxUnitWorking;
					newDist-=maxUnitWorking*maxUnitWorking;
					if (newDist<dist)
					{
						unit->destinationPurprose=r;
						choosen=building;
						dist=newDist;
					}
				}
			}
			
			if (choosen)
				return choosen;
		}
	}
	else
	{
		// I'm already carying a ressource:
		// I'll only go to a building who need this ressource, if possible.
		for (std::list<Building *>::iterator it=job[HARVEST].begin(); it!=job[HARVEST].end(); it++)
		{
			Building *building=(*it);
			if ((building->type->level<=actLevel)&&(building->neededRessource(r)))
			{
				newDist=distSquare(building->getMidX(), building->getMidY(), x, y);
				int maxUnitWorking=building->maxUnitWorking;
				newDist-=maxUnitWorking*maxUnitWorking;
				if (newDist<dist)
				{
					choosen=building;
					dist=newDist;
				}
			}
		}
		
		if (choosen)
		{
			unit->destinationPurprose=r;
			return choosen;
		}
	}
	
	// Ehanced ways to find a building has failed, choose any building aviable
	for (std::list<Building *>::iterator it=job[HARVEST].begin(); it!=job[HARVEST].end(); it++)
	{
		Building *building=(*it);
		if (building->type->level<=actLevel)
		{
			newDist=distSquare(building->getMidX(), building->getMidY(), x, y);
			int maxUnitWorking=building->maxUnitWorking;
			newDist-=maxUnitWorking*maxUnitWorking;
			if (newDist<dist)
			{
				choosen=building;
				dist=newDist;
			}
		}
	}
	
	if (choosen)
		unit->destinationPurprose=choosen->neededRessource();
	return choosen;
}

Building *Team::findNearestAttract(int x, int y, Abilities ability)
{
	Building *b=NULL;
	float score=1000.0f;
	float newScore;
	float newScoreA;
	float newScoreB;
	Sint32 maxUnitWorking;
	//Sint32 dist=MAX_SINT32;
	//Sint32 newDist;
	{
		for (std::list<Building *>::iterator it=attract[(int)ability].begin(); it!=attract[(int)ability].end(); it++)
		{
			// this is balance code, take user's wishes in account
			newScoreA=0.03f*(float)distSquare((*it)->getMidX(), (*it)->getMidY(), x, y);
			maxUnitWorking=(*it)->maxUnitWorking;
			if (maxUnitWorking)
			{
				newScoreB=(float)((*it)->unitsWorking.size())/(float)maxUnitWorking;
			}
			newScore=newScoreA+newScoreB;
			if ( newScore<score )
			{
				b=*it;
				score=newScore;
			}
			/*newDist=distSquare((*it)->getMidX(), (*it)->getMidY(), x, y);
			if ( newDist<dist )
			{
				b=*it;
				dist=newDist;
			}*/
		}
	}
	return b;
}

Building *Team::findNearestFillableFood(int x, int y)
{
	Building *b=NULL;
	//Sint32 dist=MAX_SINT32;
	//Sint32 newDist;
	float score=1000.0f;
	float newScore;
	float newScoreA;
	float newScoreB;
	Sint32 maxUnitWorking;
	{
		for (std::list<Building *>::iterator it=job[HARVEST].begin(); it!=job[HARVEST].end(); it++)
		{
			if ( ((*it)->type->canFeedUnit)  || ((*it)->type->unitProductionTime))
			{
				// this is balance code, take user's wishes in account
				newScoreA=0.03f*(float)distSquare((*it)->getMidX(), (*it)->getMidY(), x, y);
				maxUnitWorking=(*it)->maxUnitWorking;
				if (maxUnitWorking)
				{
					newScoreB=(float)((*it)->unitsWorking.size())/(float)maxUnitWorking;
				}
				newScore=newScoreA+newScoreB;
				if ( newScore<score )
				{
					b=*it;
					score=newScore;
				}
				/*newDist=distSquare((*it)->getMidX(), (*it)->getMidY(), x, y);
				if ( newDist<dist )
				{
					b=*it;
					dist=newDist;
				}*/
			}
		}
	}
	return b;
}

Building *Team::findNearestHeal(int x, int y)
{
	Building *b=NULL;
	Sint32 dist=MAX_SINT32;
	Sint32 newDist;
	{
		for (std::list<Building *>::iterator it=canHealUnit.begin(); it!=canHealUnit.end(); it++)
		{
			newDist=distSquare((*it)->getMidX(), (*it)->getMidY(), x, y);
			if ( newDist<dist )
			{
				b=*it;
				dist=newDist;
			}
		}
	}
	return b;
}

Building *Team::findNearestFood(int x, int y)
{
	Building *b=NULL;
	Sint32 dist=MAX_SINT32;
	Sint32 newDist;
	{
		for (std::list<Building *>::iterator it=canFeedUnit.begin(); it!=canFeedUnit.end(); it++)
		{
			newDist=distSquare((*it)->getMidX(), (*it)->getMidY(), x, y);
			if ( newDist<dist )
			{
				b=*it;
				dist=newDist;
			}
		}
	}
	return b;
}

int Team::maxBuildLevel(void)
{
	int index;
	Unit *u;
	int maxLevel=0;
	int unitLevel;

	for (index=0; index<1024; index++)
	{
		u=myUnits[index];
		if (u)
		{
			unitLevel=u->level[BUILD];
			if (unitLevel>maxLevel)
				maxLevel=unitLevel;
		}
	}
	return maxLevel;
}

bool Team::load(SDL_RWops *stream, BuildingsTypes *buildingstypes)
{
	int i;
	assert(stream);
	assert(buildingsToBeDestroyed.size()==0);
	buildingsToBeUpgraded.clear();
	
	// loading baseteam
	if(!BaseTeam::load(stream))
		return false;

	// normal load
	for (i=0; i< 1024; i++)
	{
		if (myUnits[i])
			delete myUnits[i];

		Uint32 isUsed=SDL_ReadBE32(stream);
		if (isUsed)
		{
			myUnits[i]=new Unit(stream, this);
		}
		else
			myUnits[i]=NULL;
	}

	swarms.clear();
	turrets.clear();
	virtualBuildings.clear();
	for (i=0; i<512; i++)
	{
		if (myBuildings[i])
			delete myBuildings[i];

		Uint32 isUsed=SDL_ReadBE32(stream);
		if (isUsed)
		{
			myBuildings[i]=new Building(stream, buildingstypes, this);
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

	/*for (i=0; i<256; i++)
	{
		if (myBullets[i])
			delete myBullets[i];

		Uint32 isUsed=SDL_ReadBE32(stream);
		if (isUsed)
			myBullets[i]=new Bullet(stream);
		else
			myBullets[i]=NULL;
	}*/

	// resolve cross reference
	for (i=0; i< 1024; i++)
	{
		if (myUnits[i])
			myUnits[i]->loadCrossRef(stream, this);
	}
	for (i=0; i<512; i++)
	{
		if (myBuildings[i])
		{
			myBuildings[i]->loadCrossRef(stream, buildingstypes, this);
			myBuildings[i]->update();
		}
	}

	allies=SDL_ReadBE32(stream);
	enemies=SDL_ReadBE32(stream);
	assert(allies==~enemies);
	sharedVision=SDL_ReadBE32(stream);
	me=SDL_ReadBE32(stream);
	startPosX=SDL_ReadBE32(stream);
	startPosY=SDL_ReadBE32(stream);

	for (i=0; i<EVENT_TYPE_SIZE; i++)
	{
		isEvent[i]=false;
		eventCooldown[i]=0;
	}
	eventPosX=startPosX;
	eventPosY=startPosY;
	isAlive=true;
	
	return true;
}

void Team::save(SDL_RWops *stream)
{
	// saving baseteam
	BaseTeam::save(stream);

	// saving team
	{
		for (int i=0; i< 1024; i++)
		{
			if (myUnits[i])
			{
				SDL_WriteBE32(stream, true);
				myUnits[i]->save(stream);
			}
			else
			{
				SDL_WriteBE32(stream, false);
			}
		}
	}

	{
		for (int i=0; i<512; i++)
		{
			if (myBuildings[i])
			{
				SDL_WriteBE32(stream, true);
				myBuildings[i]->save(stream);
			}
			else
			{
				SDL_WriteBE32(stream, false);
			}
		}
	}
	/*for (int i=0; i<256; i++)
	{
		if (myBullets[i])
		{
			SDL_WriteBE32(stream, true);
			myBullets[i]->save(stream);
		}
		else
		{
			SDL_WriteBE32(stream, false);
		}
	} */
	
	// save cross reference
	{
		for (int i=0; i< 1024; i++)
		{
			if (myUnits[i])
				myUnits[i]->saveCrossRef(stream);
		}
	}
	{
		for (int i=0; i<512; i++)
		{
			if (myBuildings[i])
				myBuildings[i]->saveCrossRef(stream);
		}
	}

	SDL_WriteBE32(stream, allies);
	SDL_WriteBE32(stream, enemies);
	SDL_WriteBE32(stream, sharedVision);
	SDL_WriteBE32(stream, me);
	SDL_WriteBE32(stream, startPosX);
	SDL_WriteBE32(stream, startPosY);
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
	
	for (int i=0; i<512; i++)
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

void Team::step(void)
{
	int nbUnits=0;
	int i;
	
	for (i=0; i<1024; i++)
	{
		if (myUnits[i])
		{
			nbUnits++;
			myUnits[i]->step();
			if (myUnits[i]->isDead)
			{
				//printf("Team:: Unit(uid%d)(id%d) deleted. dis=%d, mov=%d, ab=%x, ito=%d \n",myUnits[i]->UID, Unit::UIDtoID(myUnits[i]->UID), myUnits[i]->displacement, myUnits[i]->movement, (int)myUnits[i]->attachedBuilding, myUnits[i]->insideTimeout);
				delete myUnits[i];
				myUnits[i]=NULL;
			}
		}
	
	}
	
	//for (int i=0; i<512; i++)
	//	if (myBuildings[i])
	//		myBuildings[i]->step();
	for (i=0; i<256; i++)
		if (myBullets[i])
			myBullets[i]->step();

	// this is roughly equivalent to building.step()
	
	for (std::list<int>::iterator it=buildingsToBeDestroyed.begin(); it!=buildingsToBeDestroyed.end(); ++it)
	{
		if ( myBuildings[*it]->type->unitProductionTime )
			swarms.remove(myBuildings[*it]);
		if ( myBuildings[*it]->type->shootingRange )
			turrets.remove(myBuildings[*it]);
		if ( myBuildings[*it]->type->isVirtual )
			virtualBuildings.remove(myBuildings[*it]);
		subscribeForInsideStep.remove(myBuildings[*it]);
		subscribeForWorkingStep.remove(myBuildings[*it]);
		delete myBuildings[*it];
		myBuildings[*it]=NULL;
	}
	
	buildingsToBeDestroyed.clear();
	
	for (std::list<Building *>::iterator it=buildingsToBeUpgraded.begin(); it!=buildingsToBeUpgraded.end(); ++it)
	{
		if ( (*it)->tryToUpgradeRoom() )
		{
			std::list<Building *>::iterator ittemp=it;
			it=buildingsToBeUpgraded.erase(ittemp);
		}
	}

	for (std::list<Building *>::iterator it=subscribeForInsideStep.begin(); it!=subscribeForInsideStep.end(); ++it)
		if ((*it)->unitsInsideSubscribe.size()>0)
			(*it)->subscribeForInsideStep();

	for (std::list<Building *>::iterator it=subscribeForInsideStep.begin(); it!=subscribeForInsideStep.end(); ++it)
	{
		if ( /*((*it)->fullInside()) ||*/ ((*it)->unitsInsideSubscribe.size()==0) )
		{
			std::list<Building *>::iterator ittemp=it;
			it=subscribeForInsideStep.erase(ittemp);
		}
	}

	for (std::list<Building *>::iterator it=subscribeForWorkingStep.begin(); it!=subscribeForWorkingStep.end(); ++it)
		if ((*it)->unitsWorkingSubscribe.size()>0)
			(*it)->subscribeForWorkingStep();

	for (std::list<Building *>::iterator it=subscribeForWorkingStep.begin(); it!=subscribeForWorkingStep.end(); ++it)
	{
		if ( /*((*it)->fullWorking()) ||*/ ((*it)->unitsWorkingSubscribe.size()==0) )
		{
			std::list<Building *>::iterator ittemp=it;
			it=subscribeForWorkingStep.erase(ittemp);
		}
	}
	
	bool isEnoughFoodInSwarm=false;
	
	for (std::list<Building *>::iterator it=swarms.begin(); it!=swarms.end(); ++it)
	{
		if ((*it)->ressources[CORN]>(*it)->type->ressourceForOneUnit)
			isEnoughFoodInSwarm=true;
		(*it)->swarmStep();
	}
	
	for (std::list<Building *>::iterator it=turrets.begin(); it!=turrets.end(); ++it)
	{
		(*it)->turretStep();
	}

	isAlive=isAlive && (isEnoughFoodInSwarm || (nbUnits!=0));
	// decount event cooldown counter
	for (i=0; i<EVENT_TYPE_SIZE; i++)
	{
		if (eventCooldown[i]>0)
			eventCooldown[i]--;
	}
	
	stats.step(this);
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
	{
		for (int i=0; i<1024; i++)
		{
			if (myUnits[i])
			{
				cs^=myUnits[i]->checkSum();
				cs=(cs<<31)|(cs>>1);
			}
		}
	}
	cs=(cs<<31)|(cs>>1);
	//printf("t(%d)2cs=%x\n", teamNumber, cs);
	{
		for (int i=0; i<512; i++)
		{
			if (myBuildings[i])
			{
				cs^=myBuildings[i]->checkSum();
				cs=(cs<<31)|(cs>>1);
			}
		}
	}
	cs=(cs<<31)|(cs>>1);
	//printf("t(%d)3cs=%x\n", teamNumber, cs);
	{
		for (int i=0; i<NB_ABILITY; i++)
		{
			cs^=upgrade[i].size();
			cs=(cs<<31)|(cs>>1);
			cs^=job[i].size();
			cs^=attract[i].size();

		}
	}
	cs=(cs<<31)|(cs>>1);
	//printf("t(%d)4cs=%x\n", teamNumber, cs);
	cs^=canFeedUnit.size();
	cs^=canHealUnit.size();
	
	cs^=buildingsToBeDestroyed.size();
	cs^=buildingsToBeUpgraded.size();
	
	cs^=swarms.size();
	cs^=turrets.size();

	cs^=allies;
	cs^=enemies;
	cs^=sharedVision;
	cs^=me;
	
	return cs;
}
