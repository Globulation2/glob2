/*
 * Globulation 2 team support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "Team.h"
#include "BuildingType.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "Utilities.h"



BaseTeam::BaseTeam()
{
	teamNumber=0;
	numberOfPlayer=0;
	color=0;
	playersMask=0;
	type=T_HUMAM;
}

void BaseTeam::load(SDL_RWops *stream)
{
	// loading baseteam
	teamNumber=SDL_ReadBE32(stream);
	numberOfPlayer=SDL_ReadBE32(stream);
	color=SDL_ReadBE32(stream);
	playersMask=SDL_ReadBE32(stream);
	race.load(stream);
}

void BaseTeam::save(SDL_RWops *stream)
{
	// saving baseteam
	SDL_WriteBE32(stream, teamNumber);
	SDL_WriteBE32(stream, numberOfPlayer);
	SDL_WriteBE32(stream, color);
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
	addSint32(data, color, 8);
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
	color=getSint32(data, 8);
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
	cs^=color;
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
	load(stream, &(globalContainer->buildingsTypes));
}

Team::~Team()
{
	{
		for (int i=0; i<1024; ++i)
		{
			if (myUnits[i])
				delete myUnits[i];
		}
	}

	{
		for (int i=0; i<512; ++i)
		{
			if (myBuildings[i])
				delete myBuildings[i];
		}
	}
	{
		for (int i=0; i<256; ++i)
		{
			if (myBullets[i])
				delete myBuildings[i];
		}
	}
}

void Team::init(void)
{
	{
		for (int i=0;i<1024;++i)
		{
			myUnits[i]=NULL;
		}
	}
	{
		for (int i=0;i<512;++i)
		{
			myBuildings[i]=NULL;
		}
	}
	{
		for (int i=0;i<256;i++)
		{
			myBullets[i]=NULL;
		}
	}
	palette=globalContainer->macPal;
	freeUnits=0;
	startPosX=startPosY=0;
	
	subscribeForInsideStep.clear();
	subscribeForWorkingStep.clear();
}

void Team::setBaseTeam(const BaseTeam *initial)
{
	teamNumber=initial->teamNumber;
	numberOfPlayer=initial->numberOfPlayer;
	playersMask=initial->playersMask;
	race=initial->race;

	setCorrectColor(initial->color);
	setCorrectMasks();
}

void Team::setCorrectMasks(void)
{
	allies=1<<teamNumber;
	enemies=~allies;
	sharedVision=1<<teamNumber;
	me=1<<teamNumber;
}

void Team::setCorrectColor(Sint32 color)
{
	this->color=color;
	this->palette=globalContainer->macPal;
	this->palette.decHue((float)(this->color-120));
}

void Team::computeStat(TeamStat *stats)
{
	memset(stats, 0, sizeof(TeamStat));
	{
		for (int i=0; i<1024; i++)
		{
			if (myUnits[i])
			{
				stats->totalUnit++;
				stats->numberPerType[(int)myUnits[i]->typeNum]++;
				if (myUnits[i]->activity==Unit::ACT_RANDOM)
					stats->isFree++;
				if (myUnits[i]->medical==Unit::MED_HUNGRY)
					stats->needFood++;
				else if (myUnits[i]->medical==Unit::MED_DAMAGED)
					stats->needHeal++;
				else
					stats->needNothing++;
				for (int j=0; j<NB_ABILITY; j++)
				{
					if (myUnits[i]->performance[j])
						stats->upgradeState[j][myUnits[i]->level[j]]++;
				}
			}
		}
	}
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
	Sint32 dist=MAX_SINT32;
	Sint32 newDist;
	{
		for (std::list<Building *>::iterator it=job[(int)ability].begin(); it!=job[(int)ability].end(); it++)
		{
			if ((*it)->type->level<=actLevel)
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

Building *Team::findNearestAttract(int x, int y, Abilities ability)
{
	Building *b=NULL;
	Sint32 dist=MAX_SINT32;
	Sint32 newDist;
	{
		for (std::list<Building *>::iterator it=attract[(int)ability].begin(); it!=attract[(int)ability].end(); it++)
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

Building *Team::findNearestFillableFood(int x, int y)
{
	Building *b=NULL;
	Sint32 dist=MAX_SINT32;
	Sint32 newDist;
	{
		for (std::list<Building *>::iterator it=job[HARVEST].begin(); it!=job[HARVEST].end(); it++)
		{
			if ( ((*it)->type->canFeedUnit)  || ((*it)->type->unitProductionTime))
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

void Team::load(SDL_RWops *stream, BuildingsTypes *buildingstypes)
{
	assert(buildingsToBeDestroyed.size()==0);
	buildingsToBeUpgraded.clear();
	
	// loading baseteam
	BaseTeam::load(stream);

	// loading team
	palette=globalContainer->macPal;
	palette.decHue((float)(color-120));
	
	// normal load
	{
		for (int i=0; i< 1024; i++)
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
	}

	swarms.clear();
	turrets.clear();
	virtualBuildings.clear();
	{
		for (int i=0; i<512; i++)
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
	}

	/*for (int i=0; i<256; i++)
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
	{
		for (int i=0; i< 1024; i++)
		{
			if (myUnits[i])
				myUnits[i]->loadCrossRef(stream, this);
		}
	}
	{
		for (int i=0; i<512; i++)
		{
			if (myBuildings[i])
			{
				myBuildings[i]->loadCrossRef(stream, buildingstypes, this);
				myBuildings[i]->update();
			}
		}
	}

	allies=SDL_ReadBE32(stream);
	enemies=SDL_ReadBE32(stream);
	assert(allies==~enemies);
	sharedVision=SDL_ReadBE32(stream);
	me=SDL_ReadBE32(stream);
	startPosX=SDL_ReadBE32(stream);
	startPosY=SDL_ReadBE32(stream);
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

void Team::step(void)
{
	freeUnits=0;
	{
		for (int i=0; i<1024; i++)
		{
			if (myUnits[i])
			{
				myUnits[i]->step();
				if (myUnits[i]->isDead)
				{
					//printf("Team:: Unit(uid%d)(id%d) deleted. dis=%d, mov=%d, ab=%x, ito=%d \n",myUnits[i]->UID, Unit::UIDtoID(myUnits[i]->UID), myUnits[i]->displacement, myUnits[i]->movement, (int)myUnits[i]->attachedBuilding, myUnits[i]->insideTimeout);
					delete myUnits[i];
					myUnits[i]=NULL;
				}
				else if ((myUnits[i]->activity==Unit::ACT_RANDOM)&&(myUnits[i]->medical==Unit::MED_FREE)&&(myUnits[i]->performance[HARVEST]))
				{
					freeUnits++;
				}
			}
		}
	}

	/*for (int i=0; i<512; i++)
		if (myBuildings[i])
			myBuildings[i]->step();*/
	{
		for (int i=0; i<256; i++)
			if (myBullets[i])
				myBullets[i]->step();
	}

	// this is roughly equivalent to building.step()
	{
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
	}
	buildingsToBeDestroyed.clear();

	{
		for (std::list<Building *>::iterator it=buildingsToBeUpgraded.begin(); it!=buildingsToBeUpgraded.end(); ++it)
		{
			if ( (*it)->tryToUpgradeRoom() )
			{
				std::list<Building *>::iterator ittemp=it;
				it=buildingsToBeUpgraded.erase(ittemp);
			}
		}
	}

	{
		for (std::list<Building *>::iterator it=subscribeForInsideStep.begin(); it!=subscribeForInsideStep.end(); ++it)
			if ((*it)->unitsInsideSubscribe.size()>0)
				(*it)->subscribeForInsideStep();
	}
	{
		for (std::list<Building *>::iterator it=subscribeForInsideStep.begin(); it!=subscribeForInsideStep.end(); ++it)
		{
			if ( ((*it)->fullInside()) || ((*it)->unitsInsideSubscribe.size()==0) )
			{
				std::list<Building *>::iterator ittemp=it;
				it=subscribeForInsideStep.erase(ittemp);
			}
		}
	}
	{
		for (std::list<Building *>::iterator it=subscribeForWorkingStep.begin(); it!=subscribeForWorkingStep.end(); ++it)
			if ((*it)->unitsWorkingSubscribe.size()>0)
				(*it)->subscribeForWorkingStep();
	}
	{
		for (std::list<Building *>::iterator it=subscribeForWorkingStep.begin(); it!=subscribeForWorkingStep.end(); ++it)
		{
			if ( ((*it)->fullWorking()) || ((*it)->unitsWorkingSubscribe.size()==0) )
			{
				std::list<Building *>::iterator ittemp=it;
				it=subscribeForWorkingStep.erase(ittemp);
			}
		}
	}
	{
		for (std::list<Building *>::iterator it=swarms.begin(); it!=swarms.end(); ++it)
		{
			(*it)->swarmStep();
		}
	}

	{
		for (std::list<Building *>::iterator it=turrets.begin(); it!=turrets.end(); ++it)
		{
			(*it)->turretStep();
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
	cs^=color;
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
