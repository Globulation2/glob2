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

#include <Stream.h>

#include "AINumbi.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include "Unit.h"

using namespace boost;

AINumbi::AINumbi(Player *player)
{
	init(player);
}

AINumbi::AINumbi(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	bool goodLoad=load(stream, player, versionMinor);
	assert(goodLoad);
}

void AINumbi::init(Player *player)
{
	timer=0;
	phase=0;
	phaseTime=1024;
	attackPhase=0;
	critticalWarriors=20;
	critticalTime=1024;
	attackTimer=0;
	for (int i=0; i<IntBuildingType::NB_BUILDING; i++)
		mainBuilding[i]=0;
	
	assert(player);
	
	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;
	
	assert(this->team);
	assert(this->game);
	assert(this->map);
}

AINumbi::~AINumbi()
{
}

bool AINumbi::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	init(player);
	
	stream->readEnterSection("AINumbi");
	
	phase            = stream->readSint32("phase");
	attackPhase      = stream->readSint32("attackPhase");
	phaseTime        = stream->readSint32("phaseTime");
	critticalWarriors= stream->readSint32("critticalWarriors");
	critticalTime    = stream->readSint32("critticalTime");
	attackTimer      = stream->readSint32("attackTimer");

	for (int bi=0; bi<IntBuildingType::NB_BUILDING; bi++)
	{
		std::ostringstream oss;
		oss << "mainBuilding[" << bi << "]";
		mainBuilding[bi] = stream->readSint32(oss.str().c_str());
	}
	
	stream->readLeaveSection();
	
	return true;
}

void AINumbi::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AINumbi");
	
	stream->writeSint32(phase, "phase");
	stream->writeSint32(attackPhase, "attackPhase");
	stream->writeSint32(phaseTime, "phaseTime");
	stream->writeSint32(critticalWarriors, "critticalWarriors");
	stream->writeSint32(critticalTime, "critticalTime");
	stream->writeSint32(attackTimer, "attackTimer");

	for (int bi=0; bi<IntBuildingType::NB_BUILDING; bi++)
	{
		std::ostringstream oss;
		oss << "mainBuilding[" << bi << "]";
		stream->writeSint32(mainBuilding[bi], oss.str().c_str());
	}
	
	stream->writeLeaveSection();
}


boost::shared_ptr<Order>AINumbi::getOrder(void)
{
	timer++;

	if (timer>phaseTime)
	{
		timer-=timer;
		phase++;
		//printf("AI: new phase %d.\n", phase);
	}
	if (phase==0)
	{
		// rush for food building, explore for room.
		switch (timer&0x1F)
		{
			case 0:
				return swarmsForWorkers(1, 4, 7, 1, 0);
			case 1:
				return adjustBuildings(4, 1, 3, IntBuildingType::FOOD_BUILDING);
		}
	}
	else if (phase==1)
	{
		// rush for food building
		switch (timer&0x1F)
		{
			case 0:
				return swarmsForWorkers(1, 5, 14, 0, 0);
			case 1:
				return adjustBuildings(4, 1, 3, IntBuildingType::FOOD_BUILDING);
		}
	}
	else if (phase<4)
	{
		// mainly produce units, improve health and science if possible
		switch (timer&0x1F)
		{
			case 0:
				return swarmsForWorkers(1, 9, 14, 0, 0);
			case 1:
				return adjustBuildings(4, 1, 1, IntBuildingType::FOOD_BUILDING);
			case 2:
				return adjustBuildings(44, 1, 1, IntBuildingType::HEAL_BUILDING);
			case 3:
				return adjustBuildings(40, 1, 2, IntBuildingType::SCIENCE_BUILDING);
			case 4:
				return adjustBuildings(70, 1, 0, IntBuildingType::WALKSPEED_BUILDING);
			case 5:
				return adjustBuildings(70, 1, 0, IntBuildingType::ATTACK_BUILDING);
			case 6:
				return adjustBuildings(25, 1, 1, IntBuildingType::DEFENSE_BUILDING);
		}
	}
	else if (phase<6)
	{
		// mainly produce units, improve health and science if possible
		switch (timer&0x1F)
		{
			case 0:
				return swarmsForWorkers(1, 9, 14, 1, 0);
			case 1:
				return adjustBuildings(5, 1, 1, IntBuildingType::FOOD_BUILDING);
			case 2:
				return adjustBuildings(37, 1, 1, IntBuildingType::HEAL_BUILDING);
			case 3:
				return adjustBuildings(32, 1, 2, IntBuildingType::SCIENCE_BUILDING);
			case 4:
				return adjustBuildings(25, 1, 1, IntBuildingType::DEFENSE_BUILDING);
			case 5:
				return mayUpgrade(16, 8);
		}
	}
	else if (phase<8)
	{
		// improve science now
		switch (timer&0x1F)
		{
			case 0:
				return swarmsForWorkers(1, 4, 14, 0, 0);
			case 1:
				return adjustBuildings(5, 1, 1, IntBuildingType::FOOD_BUILDING);
			case 2:
				return adjustBuildings(34, 2, 1, IntBuildingType::HEAL_BUILDING);
			case 3:
				return adjustBuildings(32, 2, 4, IntBuildingType::SCIENCE_BUILDING);
			case 4:
				return mayUpgrade(16, 4);
		}
	}
	else if (phase<10)
	{
		// produce good units, defend too.
		switch (timer&0x1F)
		{
			case 0:
				return swarmsForWorkers(1, 9, 14, 1, 1);
			case 1:
				return adjustBuildings(5, 1, 1, IntBuildingType::FOOD_BUILDING);
			case 2:
				return adjustBuildings(32, 2, 1, IntBuildingType::HEAL_BUILDING);
			case 3:
				return adjustBuildings(40, 2, 3, IntBuildingType::SCIENCE_BUILDING);
			case 4:
				return adjustBuildings(70, 1, 5, IntBuildingType::WALKSPEED_BUILDING);
			case 5:
				return adjustBuildings(20, 1, 1, IntBuildingType::DEFENSE_BUILDING);
			case 6:
				return adjustBuildings(70, 1, 3, IntBuildingType::ATTACK_BUILDING);
			case 7:
				return checkoutExpands(80, 5);
			case 8:
				return mayUpgrade(16, 4);
		}
	}
	else
	{
		// produce warriors
		switch (timer&0x1F)
		{
			case 0:
				return swarmsForWorkers(1, 10, 3, 1, 14);
			case 1:
				return adjustBuildings(6, 2, 1, IntBuildingType::FOOD_BUILDING);
			case 2:
				return adjustBuildings(37, 2, 1, IntBuildingType::HEAL_BUILDING);
			case 3:
				return adjustBuildings(38, 2, 2, IntBuildingType::SCIENCE_BUILDING);
			case 4:
				return adjustBuildings(70, 2, 5, IntBuildingType::WALKSPEED_BUILDING);
			case 5:
				return adjustBuildings(20, 2, 2, IntBuildingType::DEFENSE_BUILDING);
			case 6:
				return adjustBuildings(70, 2, 3, IntBuildingType::ATTACK_BUILDING);
			case 7:
				return mayAttack(critticalWarriors, critticalTime, 10);
			case 8:
				return checkoutExpands(40, 5);
			case 9:
				return mayUpgrade(16, 4);
		}
	}

	return shared_ptr<Order>(new NullOrder);
}

int AINumbi::estimateFood(Building *building)
{
	int rx, ry, dist;
	bool found;
	if (map->ressourceAvailable(team->teamNumber, CORN, 0, building->posX-1, building->posY-1, &rx, &ry, &dist))
		found=true;
	else if (map->ressourceAvailable(team->teamNumber, CORN, 0, building->posX+building->type->width+1, building->posY-1, &rx, &ry, &dist))
		found=true;
	else if (map->ressourceAvailable(team->teamNumber, CORN, 0, building->posX+building->type->width+1, building->posY+building->type->height+1, &rx, &ry, &dist))
		found=true;
	else if (map->ressourceAvailable(team->teamNumber, CORN, 0, building->posX-1, building->posY+building->type->height+1, &rx, &ry, &dist))
		found=true;
	else
		found=false;

	if (found)
	{
		rx+=map->getW();
		ry+=map->getH();

		int w=0;
		int h=0;
		int i;
		int rxl, rxr, ryt, ryb;
		int hole;

		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessourceTakeable(rx+i, ry, CORN)||map->isRessourceTakeable(rx+i, ry-1, CORN))
				w++;
			else if (hole--<0)
				break;
		rxr=rx+i;
		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessourceTakeable(rx-i, ry, CORN)||map->isRessourceTakeable(rx-i, ry-1, CORN))
				w++;
			else if (hole--<0)
				break;
		rxl=rx-i;

		rx=((rxr+rxl)>>1);

		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessourceTakeable(rx, ry+i, CORN)||map->isRessourceTakeable(rx-1, ry+i, CORN))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessourceTakeable(rx, ry-i, CORN)||map->isRessourceTakeable(rx-1, ry-i, CORN))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;

		ry=((ryb+ryt)>>1);


		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessourceTakeable(rx, ry+i, CORN)||map->isRessourceTakeable(rx+1, ry+i, CORN))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessourceTakeable(rx, ry-i, CORN)||map->isRessourceTakeable(rx+1, ry-i, CORN))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;

		ry=((ryt+ryb)>>1);
		w=0;
		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessourceTakeable(rx+i, ry, CORN)||map->isRessourceTakeable(rx+i, ry+1, CORN))
				w++;
			else if (hole--<0)
				break;
		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessourceTakeable(rx-i, ry, CORN)||map->isRessourceTakeable(rx-i, ry+1, CORN))
				w++;
			else if (hole--<0)
				break;
		
		//printf("r=(%d, %d), w=%d, h=%d, s=%d.\n", rx, ry, w, h, w*h);

		return (w*h);
	}
	else
		return 0;
}

int AINumbi::countUnits(void)
{
	return team->stats.getLatestStat()->totalUnit;
}

int AINumbi::countUnits(const int medicalState)
{
	if (medicalState == Unit::MED_FREE)
	{
		return team->stats.getLatestStat()->totalUnit
			- team->stats.getLatestStat()->needFoodCritical
			- team->stats.getLatestStat()->needFood
			- team->stats.getLatestStat()->needHeal;
	}
	else if (medicalState == Unit::MED_HUNGRY)
	{
		return team->stats.getLatestStat()->needFoodCritical
			+ team->stats.getLatestStat()->needFood;
	}
	else if (medicalState == Unit::MED_DAMAGED)
	{
		return team->stats.getLatestStat()->needHeal;
	}
	else
		assert(false);
	return 0;
}

boost::shared_ptr<Order>AINumbi::swarmsForWorkers(const int minSwarmNumbers, const int nbWorkersFator, const int workers, const int explorers, const int warriors)
{
	std::list<Building *> swarms=team->swarms;
	int ss=swarms.size();
	Sint32 numberRequested=1+(nbWorkersFator/(ss+1));
	int nbu=countUnits();

	for (std::list<Building *>::iterator it=swarms.begin(); it!=swarms.end(); ++it)
	{
		Building *b=*it;
		if ((b->ratio[WORKER]!=workers)||(b->ratio[EXPLORER]!=explorers)||(b->ratio[WARRIOR]!=warriors))
		{
			b->ratioLocal[WORKER]=workers;
			b->ratioLocal[EXPLORER]=explorers;
			b->ratioLocal[WARRIOR]=warriors;

			//printf("AI: (%d) ratioLocal changed.\n", b->gid);

			return shared_ptr<Order>(new OrderModifySwarm(b->gid, b->ratioLocal));
		}

		int f=estimateFood(b);
		int numberRequestedTemp=numberRequested;
		int numberRequestedLoca=b->maxUnitWorking;
		if (f<(nbu*3-1))
			numberRequestedTemp=0;
		else if (numberRequestedLoca==0)
			if (f<(nbu*5+1))
				numberRequestedTemp=0;
		
		if (numberRequestedLoca!=numberRequestedTemp)
		{
			//printf("AI: (%d) numberRequested changed to (nrt=%d) (nrl=%d)(f=%d) (nbu=%d).\n", b->UID, numberRequestedTemp, numberRequestedLoca, f, nbu);
			b->maxUnitWorkingLocal=numberRequestedTemp;
			return shared_ptr<Order>(new OrderModifyBuilding(b->gid, numberRequestedTemp));
		}
	}
	if (ss<minSwarmNumbers)
	{
		//printf("AI: not enough swarms (%d<%d).\n", ss, minSwarmNumbers);
		// TODO !
		// assert(false);
		/*int x, y;
		if (findNewEmplacement(IntBuildingType::SWARM_BUILDING, &x, &y))
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, true);
			int teamNumber=player->team->teamNumber;
			return shared_ptr<Order>(new OrderCreate(teamNumber, x, y, typeNum));
		}*/
	}
	return shared_ptr<Order>(new NullOrder);
}

void AINumbi::nextMainBuilding(const int buildingType)
{
	//printf("AI: nextMainBuilding(%d)\n", buildingType);
	Building **myBuildings=team->myBuildings;
	Building *b=myBuildings[mainBuilding[buildingType]];
	if (b==NULL)
	{
		for (int i=1; i<Building::MAX_COUNT; i++)
			if ((myBuildings[i])/*&&((myBuildings[i]->type->shortTypeNum==buildingType)||(myBuildings[i]->type->shortTypeNum==0))*/)
			{
				b=myBuildings[i];
				break;
			}
		if (b==NULL)
		{
			mainBuilding[buildingType]=0;
			//printf("AI: no more building !.\n");
		}
		else
			mainBuilding[buildingType]=Building::GIDtoID(b->gid);
	}
	else
	{
		//printf("AI: nextMainBuilding uid=%d\n", b->UID);
		int id=Building::GIDtoID(b->gid);
		for (int i=1; i<Building::MAX_COUNT; i++)
			if ((myBuildings[(i+id)&0xFF])/*&&((myBuildings[(i+id)&0xFF]->type->shortTypeNum==buildingType)||(myBuildings[(i+id)&0xFF]->type->shortTypeNum==0))*/)
			{
				b=myBuildings[(i+id)&0xFF];
				break;
			}
		mainBuilding[buildingType]=Building::GIDtoID(b->gid);
		//printf("AI: nextMainBuilding newuid=%d\n", b->UID);
	}
}

int AINumbi::nbFreeAround(const int buildingType, int posX, int posY, int width, int height)
{
	int px=posX+map->getW();
	int py=posY+map->getH();
	int x, y;
	
	int valid=256+96;
	int r;
	for (r=2; r<=3; r++)
	{
		y=py-r;
		int ew=1;
		for (x=px-ew; x<px+width+ew; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=4+(r-2)*4;
				break;
			}
		y=py+height-1+r;
		for (x=px-ew; x<px+width+ew; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=4+(r-2)*4;
				break;
			}

		x=px-r;
		for (y=py-ew; y<py+height+ew; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=4+(r-2)*4;
				break;
			}
		x=px+width-1+r;
		for (y=py-ew; y<py+height+ew; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=4+(r-2)*4;
				break;
			}
	}
	for (r=1; r<=1; r++)
	{
		y=py-r;
		for (x=px; x<px+width; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=12;
				break;
			}
		y=py+height-1+r;
		for (x=px; x<px+width; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=12;
				break;
			}

		x=px-r;
		for (y=py; y<py+height; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=12;
				break;
			}
		x=px+width-1+r;
		for (y=py; y<py+height; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				valid-=12;
				break;
			}
	}
	
	for (r=1; r<=8; r++)
	{
		y=py-r;
		bool anyBuild=false;
		for (x=px; x<px+width; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				anyBuild=true;
				break;
			}
		if (!anyBuild)
			break;
	}
	int wu=r;
	for (r=1; r<=8; r++)
	{
		y=py+height-1+r;
		bool anyBuild=false;
		for (x=px; x<px+width; x++)
			if (!map->isFreeForBuilding(x, y))
			{
				anyBuild=true;
				break;
			}
		if (!anyBuild)
			break;
	}
	wu+=r;
	for (r=1; r<=8; r++)
	{
		bool anyBuild=false;
		x=px-r;
		for (y=py; y<py+height; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				anyBuild=true;
				break;
			}
		if (!anyBuild)
			break;
	}
	int hu=r;
	for (r=1; r<=8; r++)
	{
		bool anyBuild=false;
		x=px+width-1+r;
		for (y=py; y<py+height; y++)
			if (!map->isFreeForBuilding(x, y))
			{
				anyBuild=true;
				break;
			}
		if (!anyBuild)
			break;
	}
	hu+=r;
	
	valid-=(wu)*(hu);
	
	return valid;
}

bool AINumbi::parseBuildingType(const int buildingType)
{
	return (buildingType==IntBuildingType::DEFENSE_BUILDING);
}

void AINumbi::squareCircleScann(int &dx, int &dy, int &sx, int &sy, int &x, int &y, int &mx, int &my)
{
	if (x>=mx)
	{
		dx=0;
		dy=1;
		mx++;
	}
	else if (y>=my)
	{
		dx=-1;
		dy=0;
		my++;
	}
	else if (x<=sx)
	{
		dx=0;
		dy=-1;
		sx--;
	}
	else if (y<=sy)
	{
		dx=1;
		dy=0;
		sy--;
	}
	x+=dx;
	y+=dy;
}

bool AINumbi::findNewEmplacement(const int buildingType, int *posX, int *posY)
{
	Building **myBuildings=team->myBuildings;
	Building *b=myBuildings[mainBuilding[buildingType]];
	if (b==NULL)
	{
		nextMainBuilding(buildingType);
		b=myBuildings[mainBuilding[buildingType]];
	}
	if (b==NULL)
	{
		for (int i=0; i<IntBuildingType::NB_BUILDING; i++)
		{
			if (myBuildings[mainBuilding[i]])
			{
				b=myBuildings[mainBuilding[i]];
				break;
			}
		}
	}
	if (b==NULL)
	{
		// TODO : scan the units and find a ressoucefull place.
		return false;
	}
	int typeNum=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::typeFromShortNumber(buildingType), 0, true);
	BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
	int width=bt->width;
	int height=bt->height;
	
	int valid=nbFreeAround(buildingType, b->posX, b->posY, width, height);
	//printf("AI: findNewEmplacement(%d) valid=(%d), uid=(%d), s=(%d, %d).\n", buildingType, valid, b->UID, width, height);
	if (valid>299)
	{
		int maxr;
		if (b->type->shortTypeNum==0)
			maxr=64;
		else
			maxr=16;
		//for (int r=0; r<=maxr; r++)
		//	for (int d=0; d<8; d++)

		int dx, dy, sx, sy, px, py, mx, my;
		int margin;
		if (b->type->shortTypeNum)
			margin=0;
		else
			margin=2;

		int bposX=b->posX+map->getW();
		int bposY=b->posY+map->getH();
		
		sx=bposX-width-margin;
		sy=bposY-height-margin;

		px=sx;
		py=sy;
		
		mx=bposX+b->type->width+margin;
		my=bposY+b->type->height+margin;
		
		sy--;
		px++;
		dx=1;
		dy=0;
		
		int bestValid=-1;
		for (int i=0; i<4096; i++)
		{
			squareCircleScann(dx, dy, sx, sy, px, py, mx, my);
			//printf("AI:i=%d, d=(%d, %d), s=(%d, %d), p=(%d, %d), m=(%d, %d).\n", i, dx, dy, sx, sy, px, py, mx, my);

			//int dx, dy;
			//Unit::dxdyfromDirection(d, &dx, &dy);

			//int px=b->posX+dx*(width+r);
			//int py=b->posY+dy*(height+r);
			if (map->isFreeForBuilding(px, py, width, height))
			{
				int valid=nbFreeAround(buildingType, px, py, width, height);
				if ((valid>299)&&(game->checkRoomForBuilding(px, py, bt, player->team->teamNumber)))
				{
					int rx, ry, dist;
					bool nr=map->ressourceAvailable(team->teamNumber, CORN, 0, px, py, &rx, &ry, &dist);
					if (nr)
					{
						//int dist=map->warpDistSquare(px+1, py+1, rx, ry);
						if (((dist<=(64+width*height))&&(buildingType<=1))||((dist>=(64+width*height))&&(buildingType>1)))
						{
							//printf("AI: findNewEmplacement d=%d valid=%d.\n", d, valid);
							if (valid>bestValid)
							{
								*posX=px;
								*posY=py;
								bestValid=valid;
								if ((b->type->shortTypeNum==0)||(parseBuildingType(buildingType)))
									nextMainBuilding(buildingType);
							}
						}
					}
					else if (buildingType!=1)
					{
						//printf("AI: findNewEmplacement d=%d valid=%d.\n", d, valid);
						if (valid>bestValid)
						{
							*posX=px;
							*posY=py;
							bestValid=valid;
							if ((b->type->shortTypeNum==0)||(parseBuildingType(buildingType)))
								nextMainBuilding(buildingType);
						}
					}
				}
			}
		}
		if (bestValid>-1)
			return true;
		nextMainBuilding(buildingType);
		return false;
	}
	nextMainBuilding(buildingType);
	return false;
}

boost::shared_ptr<Order>AINumbi::mayAttack(int critticalMass, int critticalTimeout, Sint32 numberRequested)
{
	Unit **myUnits=team->myUnits;
	int ft=0;
	for (int i=0; i<Unit::MAX_COUNT; i++)
		if ((myUnits[i])&&(myUnits[i]->performance[ATTACK_SPEED])&&(myUnits[i]->medical==0))
			ft++;

	if (attackPhase==0)
	{
		if (ft>=critticalMass)
		{
			//printf("AI:(crittical mass)new attack with %d units.\n", ft);
			attackPhase=1;
		}
		attackTimer++;
		if ((attackTimer>=critticalTimeout)&&(ft>numberRequested))
		{
			attackTimer=0;
			//printf("AI:(timeout)new attack with %d units.\n", ft);
			attackPhase=1;
		}
		return shared_ptr<Order>(new NullOrder);
	}
	else if (attackPhase==1)
	{
		if (ft<=(critticalMass/2))
		{
			attackPhase=3;
			//printf("AI:stop attack.\n");
			return shared_ptr<Order>(new NullOrder);
		}

		int teamNumber=player->team->teamNumber;

		for (std::list<Building *>::iterator bit=team->virtualBuildings.begin(); bit!=team->virtualBuildings.end(); ++bit)
			if ((*bit)->type->shortTypeNum==IntBuildingType::WAR_FLAG)
			{
				Building *b=*bit;
				int gbid=map->getBuilding(b->posX, b->posY);
				if (gbid==NOGBID || Building::GIDtoTeam(gbid)==teamNumber)
					return shared_ptr<Order>(new OrderDelete(b->gid)); // The target has beed successfully killed.

				if (b->maxUnitWorking!=numberRequested)
				{
					//printf("AI: OrderModifyBuilding(%d, %d)\n", b->gid, numberRequested);
					return shared_ptr<Order>(new OrderModifyBuilding(b->gid, numberRequested));
				}
			}

		// We look for a specific enemy:
		Uint32 enemies=player->team->enemies;
		int e=-1;
		for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
			if (game->teams[i]->me & enemies)
				e=i;
		if (e==-1)
			return shared_ptr<Order>(new NullOrder);

		int ex=-1, ey=-1;
		int count=0;
		bool found=false;
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			Building *b=game->teams[e]->myBuildings[i];
			if (b)
			{
				ex=b->posX;
				ey=b->posY;

				if ((syncRand()&0x1F)==0)
				{
					bool already=false;
					count=0;
					for (std::list<Building *>::iterator bit=team->virtualBuildings.begin(); bit!=team->virtualBuildings.end(); ++bit)
						if ((*bit)->type->shortTypeNum==IntBuildingType::WAR_FLAG)
						{
							count++;
							if ((*bit)->posX==ex &&(*bit)->posY==ey)
							{
								already=true;
								break;
							}
						}
					if (!already)
					{
						found=true;
						break;
					}
				}
			}
		}

		if (ex!=-1 && ey!=-1 && found && count<5)
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
			//printf("AI: OrderCreateWarFlag(%d, %d)\n", ex, ey);
			return shared_ptr<Order>(new OrderCreate(teamNumber, ex, ey, typeNum, 1, 1));
		}
		else
			return shared_ptr<Order>(new NullOrder);
	}
	else if (attackPhase==2)
	{
		assert(false);
		return shared_ptr<Order>(new NullOrder);
	}
	else if (attackPhase==3)
	{
		for (std::list<Building *>::iterator bit=team->virtualBuildings.begin(); bit!=team->virtualBuildings.end(); ++bit)
			if ((*bit)->type->shortTypeNum==IntBuildingType::WAR_FLAG)
				return shared_ptr<Order>(new OrderDelete((*bit)->gid));
		attackPhase=0;
		critticalWarriors*=2;
		critticalTime*=2;
		return shared_ptr<Order>(new NullOrder);
	}
	else
	{
		assert(false);
		return shared_ptr<Order>(new NullOrder);
	}
	
}

boost::shared_ptr<Order>AINumbi::adjustBuildings(const int numbers, const int numbersInc, const int workers, const int buildingType)
{
	Building **myBuildings=team->myBuildings;
	//Unit **myUnits=player->team->myUnits;
	int fb=0;
	
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b=myBuildings[i];
		if ((b)&&(b->type->shortTypeNum==buildingType))
		{
			fb++;
			int w=workers;
			if ((b->maxUnitWorking!=w)&&(b->type->maxUnitWorking))
				return shared_ptr<Order>(new OrderModifyBuilding(b->gid, w));
		}
	}
	
	int wr=countUnits();
	
	if (buildingType==IntBuildingType::FOOD_BUILDING)
		wr+=2*countUnits(Unit::MED_HUNGRY);
	else if (buildingType==IntBuildingType::HEAL_BUILDING)
		wr+=4*countUnits(Unit::MED_DAMAGED);
	
	if (fb<((wr/numbers)+numbersInc))
	{
		//printf("AI: findNewEmplacement(%d), fb=%d, wr=%d, numbers=%d, numbersInc=%d, nn=%d.\n", buildingType, fb, wr, numbers, numbersInc, ((wr/numbers)+numbersInc));
		int x, y;
		if (findNewEmplacement(buildingType, &x, &y))
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::typeFromShortNumber(buildingType), 0, true);
			int teamNumber=team->teamNumber;
			return shared_ptr<Order>(new OrderCreate(teamNumber, x, y, typeNum, 1, 1));
		}
		//printf("AI: findNewEmplacement(%d) failed.\n", buildingType);
		return shared_ptr<Order>(new NullOrder);
	}
	else
		return shared_ptr<Order>(new NullOrder);
}

boost::shared_ptr<Order>AINumbi::checkoutExpands(const int numbers, const int workers)
{
	//std::list<Building *> swarms=team->swarms;
	//int ss=swarms.size();
	
	Building **myBuildings=team->myBuildings;
	int ss=0;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b=myBuildings[i];
		if ((b)&&(b->type->shortTypeNum==0))
			ss++;
	}
	
	int wr=countUnits();

	if (ss<=(wr/numbers))
	{
		//printf("AI: checkoutExpands(%d<%d=(%d/%d)).\n", ss, (wr/numbers), wr, numbers);
		int x, y;
		if (findNewEmplacement(IntBuildingType::SWARM_BUILDING, &x, &y))
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, true);
			int teamNumber=team->teamNumber;
			return shared_ptr<Order>(new OrderCreate(teamNumber, x, y, typeNum, 1, 1));
		}
		return shared_ptr<Order>(new NullOrder);
	}
	else
		return shared_ptr<Order>(new NullOrder);
}

boost::shared_ptr<Order>AINumbi::mayUpgrade(const int ptrigger, const int ntrigger)
{
	Building **myBuildings=team->myBuildings;
	int numberFood[4]={0, 0, 0, 0}; // number of food buildings
	int numberUpgradingFood[4]={0, 0, 0, 0}; // number of upgrading food buildings
	Building *foodBuilding[4]={0, 0, 0, 0};
	
	int numberHealth[4]={0, 0, 0, 0}; // number of food buildings
	int numberUpgradingHealth[4]={0, 0, 0, 0}; // number of upgrading food buildings
	Building *healthBuilding[4]={0, 0, 0, 0};
	
	int numberAttack[4]={0, 0, 0, 0}; // number of food buildings
	int numberUpgradingAttack[4]={0, 0, 0, 0}; // number of upgrading food buildings
	Building *attackBuilding[4]={0, 0, 0, 0};
	
	int numberScience[4]={0, 0, 0, 0}; // number of Science buildings
	int numberUpgradingScience[4]={0, 0, 0, 0}; // number of upgrading Science buildings
	Building *scienceBuilding[4]={0, 0, 0, 0};
	
	int numberDefense[4]={0, 0, 0, 0}; // number of Defense buildings
	int numberUpgradingDefense[4]={0, 0, 0, 0}; // number of upgrading Science buildings
	Building *defenseBuilding[4]={0, 0, 0, 0};
	
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b=myBuildings[i];
		if (b)
		{
			BuildingType *bt=b->type;
			int l=bt->level;
			if (bt->shortTypeNum==IntBuildingType::FOOD_BUILDING)
			{
				if (bt->isBuildingSite)
					numberUpgradingFood[l]++;
				else
				{
					numberFood[l]++;
					if (syncRand()&1)
						foodBuilding[l]=b;
				}
			}
			else if (bt->shortTypeNum==IntBuildingType::HEAL_BUILDING)
			{
				if (bt->isBuildingSite)
					numberUpgradingHealth[l]++;
				else
				{
					numberHealth[l]++;
					if (syncRand()&1)
						healthBuilding[l]=b;
				}
			}
			else if (bt->shortTypeNum==IntBuildingType::ATTACK_BUILDING)
			{
				if (bt->isBuildingSite)
					numberUpgradingAttack[l]++;
				else
				{
					numberAttack[l]++;
					if (syncRand()&1)
						attackBuilding[l]=b;
				}
			}
			else if (bt->shortTypeNum==IntBuildingType::SCIENCE_BUILDING)
			{
				if (bt->isBuildingSite)
					numberUpgradingScience[l]++;
				else
				{
					numberScience[l]++;
					if (syncRand()&1)
						scienceBuilding[l]=b;
				}
			}
			else if (bt->shortTypeNum==IntBuildingType::DEFENSE_BUILDING)
			{
				if (bt->isBuildingSite)
					numberUpgradingDefense[l]++;
				else
				{
					numberDefense[l]++;
					if (syncRand()&1)
						defenseBuilding[l]=b;
				}
			}
		}
	}
	
	Unit **myUnits=team->myUnits;
	int wun[4]={0, 0, 0, 0};//working units
	int fun[4]={0, 0, 0, 0};//free units
	{
		for (int i=0; i<Unit::MAX_COUNT; i++)
		{
			Unit *u=myUnits[i];
			if (u)
			{
				int l=u->level[BUILD];
				if (u->activity==Unit::ACT_RANDOM)
					fun[l]++;
				wun[l]++;
			}
		}
	}
	
	//printf("sbu=(%d, %d, %d, %d) wun=(%d, %d, %d, %d)\n", sbu[0], sbu[1], sbu[2], sbu[3], wun[0], wun[1], wun[2], wun[3]);
	
	// We calculate if we may upgrade to level 1:
	int potential=wun[1]+wun[2]+wun[3]+4*(numberScience[0]+numberScience[1]+numberScience[2]+numberScience[3]);
	int now=fun[1]+fun[2]+fun[3];
	//printf("potential=(%d/%d), now=(%d/%d).\n", potential, ptrigger, now, ntrigger);
	if ((potential>ptrigger)&&(now>ntrigger))
	{
		if (numberFood[0]>numberUpgradingFood[1])
		{
			Building *b=foodBuilding[0];
			if (b)
				return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
		}
		if (numberHealth[0]>numberUpgradingHealth[1])
		{
			Building *b=healthBuilding[0];
			if (b)
				return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
		}
		if (numberAttack[0]>numberUpgradingAttack[1])
		{
			Building *b=attackBuilding[0];
			if (b)
				return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
		}
		if (numberScience[0]>numberUpgradingScience[1]+1)
		{
			Building *b=scienceBuilding[0];
			if (b)
				return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
		}
		if (numberDefense[0]>numberUpgradingDefense[1])
		{
			Building *b=defenseBuilding[0];
			if (b)
				return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
		}
	}
	
	// We calculate if we may upgrade to leverl 2:
	potential=wun[2]+wun[3]+4*(numberScience[1]+numberScience[2]+numberScience[3]);
	now=fun[2]+fun[3];
	if ((potential>ptrigger)&&(now>ntrigger))
	{
		if (numberFood[1]>numberUpgradingFood[2])
		{
			Building *b=foodBuilding[0];
			if (b)
				return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
		}
		if (numberHealth[1]>numberUpgradingHealth[2])
		{
			Building *b=healthBuilding[0];
			if (b)
				return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
		}
		if (numberAttack[1]>numberUpgradingAttack[2])
		{
			Building *b=attackBuilding[0];
			if (b)
				return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
		}
		if (numberScience[1]>numberUpgradingScience[2]+1)
		{
			Building *b=scienceBuilding[0];
			if (b)
				return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
		}
		if (numberDefense[1]>numberUpgradingDefense[2])
		{
			Building *b=defenseBuilding[0];
			if (b)
				return shared_ptr<Order>(new OrderConstruction(b->gid, 1, 1));
		}
	}
	
	return shared_ptr<Order>(new NullOrder);
}


