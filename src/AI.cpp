/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

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

#include "AI.h"
#include "Player.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "Game.h"


AI::AI(Player *player)
{
	printf("AI: new1.\n");
	init(player);
	
}

AI::AI(SDL_RWops *stream, Player *player)
{
	printf("AI: new2.\n");
	init(player);
	
	load(stream);
}

void AI::init(Player *player)
{
	printf("AI: init.\n");
	timer=0;
	phase=0;
	phaseTime=0;
	attackPhase=0;
	critticalWarriors=0;
	critticalTime=0;
	attackTimer=0;
	for (int i=0; i<BuildingType::NB_BUILDING; i++)
		mainBuilding[i]=0;
	strategy=NONE;
	this->player=player;
}

int AI::estimateFood(int x, int y)
{
	int rx, ry;
	Map *map=&player->team->game->map;
	if (map->nearestRessource(x, y, CORN, &rx, &ry))
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
			if (map->isRessource(rx+i, ry, (RessourceType)CORN)||map->isRessource(rx+i, ry-1, (RessourceType)CORN))
				w++;
			else if (hole--<0)
				break;
		rxr=rx+i;
		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessource(rx-i, ry, (RessourceType)CORN)||map->isRessource(rx-i, ry-1, (RessourceType)CORN))
				w++;
			else if (hole--<0)
				break;
		rxl=rx-i;
		
		rx=((rxr+rxl)>>1);
		
		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessource(rx, ry+i, (RessourceType)CORN)||map->isRessource(rx-1, ry+i, (RessourceType)CORN))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessource(rx, ry-i, (RessourceType)CORN)||map->isRessource(rx-1, ry-i, (RessourceType)CORN))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;
		
		ry=((ryb+ryt)>>1);
		
		
		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessource(rx, ry+i, (RessourceType)CORN)||map->isRessource(rx+1, ry+i, (RessourceType)CORN))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessource(rx, ry-i, (RessourceType)CORN)||map->isRessource(rx+1, ry-i, (RessourceType)CORN))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;
		
		ry=((ryt+ryb)>>1);
		w=0;
		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessource(rx+i, ry, (RessourceType)CORN)||map->isRessource(rx+i, ry+1, (RessourceType)CORN))
				w++;
			else if (hole--<0)
				break;
		hole=2;
		for (i=0; i<32; i++)
			if (map->isRessource(rx-i, ry, (RessourceType)CORN)||map->isRessource(rx-i, ry+1, (RessourceType)CORN))
				w++;
			else if (hole--<0)
				break;
		
		//printf("r=(%d, %d), w=%d, h=%d, s=%d.\n", rx, ry, w, h, w*h);
		
		return (w*h);
	}
	else
		return 0;
}

int AI::countUnits(void)
{
	Unit **myUnits=player->team->myUnits;
	int c=0;
	for (int i=0; i<1024; i++)
		if (myUnits[i])
			c++;
	return c;
}

int AI::countUnits(const int medicalState)
{
	Unit **myUnits=player->team->myUnits;
	int c=0;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u &&(u->medical==medicalState))
			c++;
	}
	return c;
}

Order *AI::swarmsForWorkers(const int minSwarmNumbers, const int nbWorkersFator, const int workers, const int explorers, const int warriors)
{
	std::list<Building *> swarms=player->team->swarms;
	int ss=swarms.size();
	Sint32 numberRequested=1+(nbWorkersFator/(ss+1));
	int nbu=countUnits();

	for (std::list<Building *>::iterator it=swarms.begin(); it!=swarms.end(); ++it)
	{
		Building *b=*it;
		if ((b->ratioLocal[UnitType::WORKER]!=workers)||(b->ratioLocal[UnitType::EXPLORER]!=explorers)||(b->ratioLocal[UnitType::WARRIOR]!=warriors))
		{
			b->ratioLocal[UnitType::WORKER]=workers;
			b->ratioLocal[UnitType::EXPLORER]=explorers;
			b->ratioLocal[UnitType::WARRIOR]=warriors;

			printf("AI: (%d) ratioLocal changed.\n", b->UID);

			Sint32 rdyPtr[1][UnitType::NB_UNIT_TYPE];
			memcpy(rdyPtr, b->ratioLocal, UnitType::NB_UNIT_TYPE*sizeof(Sint32));
			return new OrderModifySwarms(&(b->UID), rdyPtr, 1);
		}

		int f=estimateFood(b->posX, b->posY);
		int numberRequestedTemp=numberRequested;
		int numberRequestedLoca=b->maxUnitWorkingLocal;
		if (f<(nbu*3-1))
			numberRequestedTemp=0;
		else if (numberRequestedLoca==0)
			if (f<(nbu*5+1))
				numberRequestedTemp=0;
		
		if (numberRequestedLoca!=numberRequestedTemp)
		{
			printf("AI: (%d) numberRequested changed to (nrt=%d) (nrl=%d)(f=%d) (nbu=%d).\n", b->UID, numberRequestedTemp, numberRequestedLoca, f, nbu);
			b->maxUnitWorkingLocal=numberRequestedTemp;
			return new OrderModifyBuildings(&b->UID, &numberRequestedTemp, 1);
		}
	}
	if (ss<minSwarmNumbers)
	{
		printf("AI: not enough swarms (%d<%d).\n", ss, minSwarmNumbers);
		// TODO !
		// assert(false);
		/*int x, y;
		if (findNewEmplacement(BuildingType::SWARM_BUILDING, &x, &y))
		{
			int typeNum=globalContainer->buildingsTypes.getTypeNum(0, 0, true);
			int teamNumber=player->team->teamNumber;
			return new OrderCreate(teamNumber, x, y, (BuildingType::BuildingTypeNumber)typeNum);
		}*/
	}
	return new NullOrder();
}

void AI::nextMainBuilding(const int buildingType)
{
	//printf("AI: nextMainBuilding(%d)\n", buildingType);
	Building **myBuildings=player->team->myBuildings;
	Building *b=myBuildings[mainBuilding[buildingType]];
	if (b==NULL)
	{
		for (int i=1; i<512; i++)
			if ((myBuildings[i])/*&&((myBuildings[i]->type->type==buildingType)||(myBuildings[i]->type->type==0))*/)
			{
				b=myBuildings[i];
				break;
			}
		if (b==NULL)
		{
			mainBuilding[buildingType]=0;
			printf("AI: no more building !.\n");
		}
		else
			mainBuilding[buildingType]=Building::UIDtoID(b->UID);
	}
	else
	{
		//printf("AI: nextMainBuilding uid=%d\n", b->UID);
		int id=Building::UIDtoID(b->UID);
		for (int i=1; i<512; i++)
			if ((myBuildings[(i+id)&0xFF])/*&&((myBuildings[(i+id)&0xFF]->type->type==buildingType)||(myBuildings[(i+id)&0xFF]->type->type==0))*/)
			{
				b=myBuildings[(i+id)&0xFF];
				break;
			}
		mainBuilding[buildingType]=Building::UIDtoID(b->UID);
		//printf("AI: nextMainBuilding newuid=%d\n", b->UID);
	}
}

bool AI::checkUIDRoomForBuilding(int px, int py, int width, int height)
{
	Game *game=player->team->game;
	for (int x=px; x<px+width; x++)
		for (int y=py; y<py+height; y++)
		{
			int uid=game->map.getUnit(x, y);
			if ((uid<0)&&(uid!=NOUID))
				return false;
		}
	return true;
}

int AI::nbFreeAround(const int buildingType, int posX, int posY, int width, int height)
{
	Game *game=player->team->game;

	int px=posX+game->map.getW();
	int py=posY+game->map.getH();
	int x, y;
	
	int valid=256+96;
	int r;
	for (r=2; r<=3; r++)
	{
		y=py-r;
		int ew=1;
		for (x=px-ew; x<px+width+ew; x++)
		{
			int uid=game->map.getUnit(x, y);
			if ((uid<0)&&(uid!=NOUID))
			{
				valid-=4+(r-2)*4;
				break;
			}
		}
		y=py+height-1+r;
		for (x=px-ew; x<px+width+ew; x++)
		{
			int uid=game->map.getUnit(x, y);
			if ((uid<0)&&(uid!=NOUID))
			{
				valid-=4+(r-2)*4;
				break;
			}
		}

		x=px-r;
		for (y=py-ew; y<py+height+ew; y++)
		{
			int uid=game->map.getUnit(x, y);
			if ((uid<0)&&(uid!=NOUID))
			{
				valid-=4+(r-2)*4;
				break;
			}
		}
		x=px+width-1+r;
		for (y=py-ew; y<py+height+ew; y++)
		{
			int uid=game->map.getUnit(x, y);
			if ((uid<0)&&(uid!=NOUID))
			{
				valid-=4+(r-2)*4;
				break;
			}
		}
	}
	for (r=1; r<=1; r++)
	{
		y=py-r;
		for (x=px; x<px+width; x++)
		{
			int uid=game->map.getUnit(x, y);
			if ((uid>0)||(uid==NOUID))
			{
				valid-=12;
				break;
			}
		}
		y=py+height-1+r;
		for (x=px; x<px+width; x++)
		{
			int uid=game->map.getUnit(x, y);
			if ((uid>0)||(uid==NOUID))
			{
				valid-=12;
				break;
			}
		}

		x=px-r;
		for (y=py; y<py+height; y++)
		{
			int uid=game->map.getUnit(x, y);
			if ((uid>0)||(uid==NOUID))
			{
				valid-=12;
				break;
			}
		}
		x=px+width-1+r;
		for (y=py; y<py+height; y++)
		{
			int uid=game->map.getUnit(x, y);
			if ((uid>0)||(uid==NOUID))
			{
				valid-=12;
				break;
			}
		}
	}
	
	for (r=1; r<=8; r++)
	{
		y=py-r;
		bool anyBuild=false;
		for (x=px; x<px+width; x++)
		{
			int uid=game->map.getUnit(x, y);
			if ((uid<0)&&(uid!=NOUID))
			{
				anyBuild=true;
				break;
			}
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
		{
			int uid=game->map.getUnit(x, y);
			if ((uid<0)&&(uid!=NOUID))
			{
				anyBuild=true;
				break;
			}
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
		{
			int uid=game->map.getUnit(x, y);
			if ((uid<0)&&(uid!=NOUID))
			{
				anyBuild=true;
				break;
			}
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
		{
			int uid=game->map.getUnit(x, y);
			if ((uid<0)&&(uid!=NOUID))
			{
				anyBuild=true;
				break;
			}
		}
		if (!anyBuild)
			break;
	}
	hu+=r;
	
	valid-=(wu)*(hu);
	
	return valid;
}

bool AI::parseBuildingType(const int buildingType)
{
	return (buildingType==BuildingType::DEFENSE_BUILDING);
}

void AI::squareCircleScann(int &dx, int &dy, int &sx, int &sy, int &x, int &y, int &mx, int &my)
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

bool AI::findNewEmplacement(const int buildingType, int *posX, int *posY)
{
	Building **myBuildings=player->team->myBuildings;
	Building *b=myBuildings[mainBuilding[buildingType]];
	if (b==NULL)
	{
		nextMainBuilding(buildingType);
		b=myBuildings[mainBuilding[buildingType]];
	}
	if (b==NULL)
	{
		for (int i=0; i<BuildingType::NB_BUILDING; i++)
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
	int typeNum=globalContainer->buildingsTypes.getTypeNum(buildingType, 0, true);
	BuildingType *bt=globalContainer->buildingsTypes.getBuildingType(typeNum);
	int width=bt->width;
	int height=bt->height;
	
	int valid=nbFreeAround(buildingType, b->posX, b->posY, width, height);
	//printf("AI: findNewEmplacement(%d) valid=(%d), uid=(%d), s=(%d, %d).\n", buildingType, valid, b->UID, width, height);
	if (valid>299)
	{
		int maxr;
		if (b->type->type==0)
			maxr=64;
		else
			maxr=16;
		//for (int r=0; r<=maxr; r++)
		//	for (int d=0; d<8; d++)

		int dx, dy, sx, sy, px, py, mx, my;
		int margin;
		if (b->type->type)
			margin=0;
		else
			margin=2;

		int bposX=b->posX+player->team->game->map.getW();
		int bposY=b->posY+player->team->game->map.getH();
		
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
			if (checkUIDRoomForBuilding(px, py, width, height))
			{
				int valid=nbFreeAround(buildingType, px, py, width, height);
				if ((valid>299)&&(player->team->game->checkRoomForBuilding(px, py, typeNum, player->team->teamNumber)))
				{
					Game *game=player->team->game;
					int rx, ry;
					bool nr=game->map.nearestRessource(px, py, CORN, &rx, &ry);
					if (nr)
					{
						int dist=game->map.warpDistSquare(px+1, py+1, rx, ry);
						if (((dist<=(64+width*height))&&(buildingType<=1))||((dist>=(64+width*height))&&(buildingType>1)))
						{
							//printf("AI: findNewEmplacement d=%d valid=%d.\n", d, valid);
							if (valid>bestValid)
							{
								*posX=px;
								*posY=py;
								bestValid=valid;
								if ((b->type->type==0)||(parseBuildingType(buildingType)))
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
							if ((b->type->type==0)||(parseBuildingType(buildingType)))
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

Order *AI::mayAttack(int critticalMass, int critticalTimeout, Sint32 numberRequested)
{
	Unit **myUnits=player->team->myUnits;
	int ft=0;
	{
		for (int i=0; i<1024; i++)
		{
			if ((myUnits[i])&&(myUnits[i]->performance[ATTACK_SPEED])&&(myUnits[i]->medical==0))
				ft++;
		}
	}
	if (attackPhase==0)
	{
		if (ft>=critticalMass)
		{
			printf("AI:(crittical mass)new attack with %d units.\n", ft);
			attackPhase=1;
		}
		attackTimer++;
		if ((attackTimer>=critticalTimeout)&&(ft>numberRequested))
		{
			attackTimer=0;
			printf("AI:(timeout)new attack with %d units.\n", ft);
			attackPhase=1;
		}
		return new NullOrder();
	}
	else if (attackPhase==1)
	{
		if (ft<=(critticalMass/2))
		{
			attackPhase=3;
			printf("AI:stop attack.\n");
			return new NullOrder();
		}
		
		Game *game=player->team->game;
		int teamNumber=player->team->teamNumber;
		
		{
			for (int i=0; i<512; i++)
			{
				Building *b=player->team->myBuildings[i];
				if ((b)&&(b->type->type==BuildingType::WAR_FLAG))
				{
					int uid=game->map.getUnit(b->posX, b->posY);
					if ((uid==NOUID)||(uid>=0)||(Building::UIDtoTeam(uid)==teamNumber))
						return new OrderDelete(b->UID);
						
					if (b->maxUnitWorkingLocal!=numberRequested)
						return new OrderModifyBuildings(&b->UID, &numberRequested, 1);
				}
			}
		}
		
		
		Uint32 enemies=player->team->enemies;
		int e=-1;
		{
			for (int i=0; i<game->session.numberOfTeam; i++)
				if (game->teams[i]->me & enemies)
					e=i;
		}
		if (e==-1)
			return new NullOrder();
		
		int ex, ey=-1;
		{
			for (int i=0; i<512; i++)
			{
				Building *b=game->teams[e]->myBuildings[i];
				if (b)
				{
					ex=b->posX;
					ey=b->posY;
					if (syncRand()&0x1F==0)
						break;
				}
			}
		}
		
		if (ey!=-1)
		{
			
			int typeNum=globalContainer->buildingsTypes.getTypeNum(BuildingType::WAR_FLAG, 0, false);
			return new OrderCreate(teamNumber, ex, ey, (BuildingType::BuildingTypeNumber)typeNum);
		}
		else
			return new NullOrder();
	}
	else if (attackPhase==2)
	{
		assert(false);
		return new NullOrder();
	}
	else if (attackPhase==3)
	{
		Building **myBuildings=player->team->myBuildings;
		for (int i=0; i<512; i++)
		{
			Building *b=myBuildings[i];
			if ((b)&&(b->type->type==BuildingType::WAR_FLAG))
			{
				return new OrderDelete(b->UID);
			}
		}
		attackPhase=0;
		critticalWarriors*=2;
		critticalTime*=2;
		return new NullOrder();
	}
	else
	{
		assert(false);
		return new NullOrder();
	}
	
}

Order *AI::adjustBuildings(const int numbers, const int numbersInc, const int workers, const int buildingType)
{
	Building **myBuildings=player->team->myBuildings;
	//Unit **myUnits=player->team->myUnits;
	int fb=0;
	
	for (int i=0; i<512; i++)
	{
		Building *b=myBuildings[i];
		if ((b)&&(b->type->type==buildingType))
		{
			fb++;
			int w=workers;
			if ((b->maxUnitWorkingLocal!=w)&&(b->type->maxUnitWorking))
			{

				//printf("AI: (%d) (%d) numberRequested changed.\n", buildingType, b->UID);
				return new OrderModifyBuildings(&b->UID, &w, 1);
			}
		}
	}
	
	
	int wr=countUnits();
	
	if (buildingType==BuildingType::FOOD_BUILDING)
		wr+=2*countUnits(Unit::MED_HUNGRY);
	else if (buildingType==BuildingType::HEALTH_BUILDING)
		wr+=4*countUnits(Unit::MED_DAMAGED);
	
	if (fb<((wr/numbers)+numbersInc))
	{
		//printf("AI: findNewEmplacement(%d), fb=%d, wr=%d, numbers=%d, numbersInc=%d, nn=%d.\n", buildingType, fb, wr, numbers, numbersInc, ((wr/numbers)+numbersInc));
		int x, y;
		if (findNewEmplacement(buildingType, &x, &y))
		{
			int typeNum=globalContainer->buildingsTypes.getTypeNum(buildingType, 0, true);
			int teamNumber=player->team->teamNumber;
			return new OrderCreate(teamNumber, x, y, (BuildingType::BuildingTypeNumber)typeNum);
		}
		//printf("AI: findNewEmplacement(%d) failed.\n", buildingType);
		return new NullOrder();
	}
	else
		return new NullOrder();
}

Order *AI::checkoutExpands(const int numbers, const int workers)
{
	//Building **myBuildings=player->team->myBuildings;
	std::list<Building *> swarms=player->team->swarms;
	int ss=swarms.size();
	
	int wr=countUnits();

	if (ss<=(wr/numbers))
	{
		printf("AI: checkoutExpands(%d<%d=(%d/%d)).\n", ss, (wr/numbers), wr, numbers);
		int x, y;
		if (findNewEmplacement(BuildingType::SWARM_BUILDING, &x, &y))
		{
			int typeNum=globalContainer->buildingsTypes.getTypeNum(0, 0, true);
			int teamNumber=player->team->teamNumber;
			return new OrderCreate(teamNumber, x, y, (BuildingType::BuildingTypeNumber)typeNum);
		}
		return new NullOrder();
	}
	else
		return new NullOrder();
}

Order *AI::mayUpgrade(const int ptrigger, const int ntrigger)
{
	Building **myBuildings=player->team->myBuildings;
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
	
	for (int i=0; i<512; i++)
	{
		Building *b=myBuildings[i];
		if (b)
		{
			BuildingType *bt=b->type;
			int l=bt->level;
			if (bt->type==BuildingType::FOOD_BUILDING)
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
			else if (bt->type==BuildingType::HEALTH_BUILDING)
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
			else if (bt->type==BuildingType::ATTACK_BUILDING)
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
			else if (bt->type==BuildingType::SCIENCE_BUILDING)
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
			else if (bt->type==BuildingType::DEFENSE_BUILDING)
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
	
	Unit **myUnits=player->team->myUnits;
	int wun[4]={0, 0, 0, 0};//working units
	int fun[4]={0, 0, 0, 0};//free units
	{
		for (int i=0; i<1024; i++)
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
	
	// We calculate if we may upgrade to leverl 1:
	int potential=wun[1]+wun[2]+wun[3]+4*(numberScience[0]+numberScience[1]+numberScience[2]+numberScience[3]);
	int now=fun[1]+fun[2]+fun[3];
	//printf("potential=(%d/%d), now=(%d/%d).\n", potential, ptrigger, now, ntrigger);
	if ((potential>ptrigger)&&(now>ntrigger))
	{
		if (numberFood[0]>numberUpgradingFood[1])
		{
			Building *b=foodBuilding[0];
			if (b)
				return new OrderUpgrade(b->UID);
		}
		if (numberHealth[0]>numberUpgradingHealth[1])
		{
			Building *b=healthBuilding[0];
			if (b)
				return new OrderUpgrade(b->UID);
		}
		if (numberAttack[0]>numberUpgradingAttack[1])
		{
			Building *b=attackBuilding[0];
			if (b)
				return new OrderUpgrade(b->UID);
		}
		if (numberScience[0]>numberUpgradingScience[1]+1)
		{
			Building *b=scienceBuilding[0];
			if (b)
				return new OrderUpgrade(b->UID);
		}
		if (numberDefense[0]>numberUpgradingDefense[1])
		{
			Building *b=defenseBuilding[0];
			if (b)
				return new OrderUpgrade(b->UID);
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
				return new OrderUpgrade(b->UID);
		}
		if (numberHealth[1]>numberUpgradingHealth[2])
		{
			Building *b=healthBuilding[0];
			if (b)
				return new OrderUpgrade(b->UID);
		}
		if (numberAttack[1]>numberUpgradingAttack[2])
		{
			Building *b=attackBuilding[0];
			if (b)
				return new OrderUpgrade(b->UID);
		}
		if (numberScience[1]>numberUpgradingScience[2]+1)
		{
			Building *b=scienceBuilding[0];
			if (b)
				return new OrderUpgrade(b->UID);
		}
		if (numberDefense[1]>numberUpgradingDefense[2])
		{
			Building *b=defenseBuilding[0];
			if (b)
				return new OrderUpgrade(b->UID);
		}
	}
	
	return new NullOrder();
}

Order *AI::getOrder(void)
{
	timer++;
	if (phaseTime==0)
	{
		phaseTime=1024;
		critticalWarriors=20;
		critticalTime=1024;
		attackTimer=0;
		if (phaseTime)
			printf("AI: new phaseTime %d.\n", phaseTime);
		strategy=(Strategy)(1+(syncRand()%(NB_STRATEGY-1)));
		if (strategy)
			printf("AI: new strategy %d.\n", strategy);
		for (int i=0; i<BuildingType::NB_BUILDING; i++)
			nextMainBuilding(i);
		if (strategy)
			printf("AI: main Emplacement selected.\n");
	}
	else if (timer>phaseTime)
	{
		timer-=timer;
		phase++;
		printf("AI: new phase %d.\n", phase);
	}
	if (strategy==SEVEN_PHASES)
	{
		if (phase==0)
		{
			// rush for food building, explore for room.
			switch (timer&0x1F)
			{
				case 0:
					return swarmsForWorkers(1, 4, 7, 1, 0);
				case 1:
					return adjustBuildings(4, 1, 3, BuildingType::FOOD_BUILDING);
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
					return adjustBuildings(4, 1, 3, BuildingType::FOOD_BUILDING);
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
					return adjustBuildings(4, 1, 1, BuildingType::FOOD_BUILDING);
				case 2:
					return adjustBuildings(44, 1, 1, BuildingType::HEALTH_BUILDING);
				case 3:
					return adjustBuildings(40, 1, 2, BuildingType::SCIENCE_BUILDING);
				case 4:
					return adjustBuildings(70, 1, 0, BuildingType::WALKSPEED_BUILDING);
				case 5:
					return adjustBuildings(70, 1, 0, BuildingType::ATTACK_BUILDING);
				case 6:
					return adjustBuildings(25, 1, 1, BuildingType::DEFENSE_BUILDING);
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
					return adjustBuildings(5, 1, 1, BuildingType::FOOD_BUILDING);
				case 2:
					return adjustBuildings(37, 1, 1, BuildingType::HEALTH_BUILDING);
				case 3:
					return adjustBuildings(32, 1, 2, BuildingType::SCIENCE_BUILDING);
				case 4:
					return adjustBuildings(25, 1, 1, BuildingType::DEFENSE_BUILDING);
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
					return adjustBuildings(5, 1, 1, BuildingType::FOOD_BUILDING);
				case 2:
					return adjustBuildings(34, 2, 1, BuildingType::HEALTH_BUILDING);
				case 3:
					return adjustBuildings(32, 2, 4, BuildingType::SCIENCE_BUILDING);
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
					return adjustBuildings(5, 1, 1, BuildingType::FOOD_BUILDING);
				case 2:
					return adjustBuildings(32, 2, 1, BuildingType::HEALTH_BUILDING);
				case 3:
					return adjustBuildings(40, 2, 3, BuildingType::SCIENCE_BUILDING);
				case 4:
					return adjustBuildings(70, 1, 5, BuildingType::WALKSPEED_BUILDING);
				case 5:
					return adjustBuildings(20, 1, 1, BuildingType::DEFENSE_BUILDING);
				case 6:
					return adjustBuildings(70, 1, 3, BuildingType::ATTACK_BUILDING);
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
					return adjustBuildings(6, 2, 1, BuildingType::FOOD_BUILDING);
				case 2:
					return adjustBuildings(37, 2, 1, BuildingType::HEALTH_BUILDING);
				case 3:
					return adjustBuildings(38, 2, 2, BuildingType::SCIENCE_BUILDING);
				case 4:
					return adjustBuildings(70, 2, 5, BuildingType::WALKSPEED_BUILDING);
				case 5:
					return adjustBuildings(20, 2, 2, BuildingType::DEFENSE_BUILDING);
				case 6:
					return adjustBuildings(70, 2, 3, BuildingType::ATTACK_BUILDING);
				case 7:
					return mayAttack(critticalWarriors, critticalTime, 10);
				case 8:
					return checkoutExpands(40, 5);
				case 9:
					return mayUpgrade(16, 4);
			}
		}
	}
	
	return new NullOrder();
}

void AI::save(SDL_RWops *stream)
{
	//Game *game=player->team->game;
	//assert((game->session.versionMajor>=0)&&(game->session.versionMinor>=3));
	
	SDL_RWwrite(stream, "GLO2", 4, 1);
	SDL_WriteBE32(stream, phase);
	SDL_WriteBE32(stream, attackPhase);
	SDL_WriteBE32(stream, phaseTime);
	SDL_WriteBE32(stream, critticalWarriors);
	SDL_WriteBE32(stream, critticalTime);
	SDL_WriteBE32(stream, attackTimer);
	SDL_WriteBE32(stream, (Uint32)strategy);
	
	SDL_RWwrite(stream, mainBuilding, BuildingType::NB_BUILDING, 4);
	
	SDL_RWwrite(stream, "GLO2", 4, 1);
}

bool AI::load(SDL_RWops *stream)
{
	Game *game=player->team->game;
	if ((game->session.versionMajor>=0)&&(game->session.versionMinor>=3))
	{
		char signature[4];
		SDL_RWread(stream, signature, 4, 1);
		if (memcmp(signature,"GLO2",4)!=0)
			return false;
		phase            =SDL_ReadBE32(stream);
		attackPhase      =SDL_ReadBE32(stream);
		phaseTime        =SDL_ReadBE32(stream);
		critticalWarriors=SDL_ReadBE32(stream);
		critticalTime    =SDL_ReadBE32(stream);
		attackTimer      =SDL_ReadBE32(stream);
		strategy         =(Strategy)SDL_ReadBE32(stream);
		
		SDL_RWread(stream, mainBuilding, BuildingType::NB_BUILDING, 4);
		
		SDL_RWread(stream, signature, 4, 1);
		if (memcmp(signature,"GLO2",4)!=0)
			return false;
	}
	return true;
}
