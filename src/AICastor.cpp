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

#include "AICastor.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include "Utilities.h"


#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 2

inline static void dxdyfromDirection(int direction, int *dx, int *dy)
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
}

void AICastor::firstInit()
{
	obstacleUnitMap=NULL;
	obstacleBuildingMap=NULL;
	spaceForBuildingMap=NULL;
	buildingNeighbourMap=NULL;
	twoSpaceNeighbourMap=NULL;
	
	workPowerMap=NULL;
	workRangeMap=NULL;
	workAbilityMap=NULL;
	hydratationMap=NULL;
	wheatGrowthMap=NULL;
	
	goodFoodBuildingMap=NULL;
	
	ressourcesCluster=NULL;
}

AICastor::AICastor(Player *player)
{
	logFile=globalContainer->logFileManager->getFile("AICastor.log");
	
	firstInit();
	init(player);
}

AICastor::AICastor(SDL_RWops *stream, Player *player, Sint32 versionMinor)
{
	logFile=globalContainer->logFileManager->getFile("AICastor.log");
	
	firstInit();
	bool goodLoad=load(stream, player, versionMinor);
	assert(goodLoad);
}

void AICastor::init(Player *player)
{
	assert(player);
	
	// Logical :
	timer=0;

	// Structural:
	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;

	assert(this->team);
	assert(this->game);
	assert(this->map);
	
	size_t size=map->w*map->h;
	assert(size>0);
	
	if (obstacleUnitMap!=NULL)
		delete[] obstacleUnitMap;
	obstacleUnitMap=new Uint8[size];
	
	if (obstacleBuildingMap!=NULL)
		delete[] obstacleBuildingMap;
	obstacleBuildingMap=new Uint8[size];
	
	if (spaceForBuildingMap!=NULL)
		delete[] spaceForBuildingMap;
	spaceForBuildingMap=new Uint8[size];
	
	if (buildingNeighbourMap!=NULL)
		delete[] buildingNeighbourMap;
	buildingNeighbourMap=new Uint8[size];
	
	if (twoSpaceNeighbourMap!=NULL)
		delete[] twoSpaceNeighbourMap;
	twoSpaceNeighbourMap=new Uint8[size];
	
	
	if (workPowerMap!=NULL)
		delete[] workPowerMap;
	workPowerMap=new Uint8[size];
	
	if (workRangeMap!=NULL)
		delete[] workRangeMap;
	workRangeMap=new Uint8[size];
	
	if (workAbilityMap!=NULL)
		delete[] workAbilityMap;
	workAbilityMap=new Uint8[size];
	
	if (hydratationMap!=NULL)
		delete[] hydratationMap;
	hydratationMap=new Uint8[size];
	
	if (wheatGrowthMap!=NULL)
		delete[] wheatGrowthMap;
	wheatGrowthMap=new Uint8[size];
	
	if (goodFoodBuildingMap!=NULL)
		delete[] goodFoodBuildingMap;
	goodFoodBuildingMap=new Uint8[size];
	
	
	if (ressourcesCluster!=NULL)
		delete[] ressourcesCluster;
	ressourcesCluster=new Uint16[size];
	
	hydratationMapComputed=false;
}

AICastor::~AICastor()
{
	if (obstacleUnitMap!=NULL)
		delete[] obstacleUnitMap;
	
	if (obstacleBuildingMap!=NULL)
		delete[] obstacleBuildingMap;
	
	if (spaceForBuildingMap!=NULL)
		delete[] spaceForBuildingMap;
	
	if (buildingNeighbourMap!=NULL)
		delete[] buildingNeighbourMap;
	
	if (twoSpaceNeighbourMap!=NULL)
		delete[] twoSpaceNeighbourMap;
	
	
	if (workPowerMap!=NULL)
		delete[] workPowerMap;
	
	if (workRangeMap!=NULL)
		delete[] workRangeMap;
	
	if (workAbilityMap!=NULL)
		delete[] workAbilityMap;
	
	if (hydratationMap!=NULL)
		delete[] hydratationMap;
	
	if (wheatGrowthMap!=NULL)
		delete[] wheatGrowthMap;
	
	if (goodFoodBuildingMap!=NULL)
		delete[] goodFoodBuildingMap;
	
	
	if (ressourcesCluster!=NULL)
		delete[] ressourcesCluster;
}

bool AICastor::load(SDL_RWops *stream, Player *player, Sint32 versionMinor)
{
	printf("AICastor::load\n");
	init(player);
	assert(game);
	
	if (versionMinor<29)
	{
		//TODO:init
		return true;
	}
	
	Sint32 aiFileVersion=SDL_ReadBE32(stream);
	if (aiFileVersion<AI_FILE_MIN_VERSION)
		return false;
	if (aiFileVersion>=1)
		timer=SDL_ReadBE32(stream);
	else
		timer=0;
	
	return true;
}

void AICastor::save(SDL_RWops *stream)
{
	SDL_WriteBE32(stream, AI_FILE_VERSION);
	SDL_WriteBE32(stream, timer);
}


Order *AICastor::getOrder(void)
{
	timer++;
	
	computeObstacleUnitMap();
	computeObstacleBuildingMap();
	computeSpaceForBuildingMap();
	computeBuildingNeighbourMap();
	computeTwoSpaceNeighbourMap();
	
	computeWorkPowerMap();
	computeWorkRangeMap();
	computeWorkAbilityMap();
	
	if (!hydratationMapComputed)
		computeHydratationMap();
	computeWheatGrowthMap();
	
	Order *gfbm=computeGoodFoodBuildingMap(50, 5);
	if (gfbm)
		return gfbm;
	
	return new NullOrder();
}


void AICastor::computeCanSwim()
{
	// If our population has more healthy-working-units able to swimm than healthy-working-units
	// unable to swimm then we choose to be able to go trough water:
	Unit **myUnits=team->myUnits;
	int sumCanSwim=0;
	int sumCantSwim=0;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u && u->typeNum==WORKER && u->medical==0)
		{
			if (u->performance[SWIM]>0)
				sumCanSwim++;
			else
				sumCantSwim++;
		}
	}
	
	canSwim=(sumCanSwim>sumCantSwim);
}

void AICastor::computeObstacleUnitMap()
{
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//size_t size=w*h;
	Case *cases=map->cases;
	Uint32 teamMask=team->me;
		
	for (int y=0; y<h; y++)
	{
		int wy=w*y;
		for (int x=0; x<w; x++)
		{
			int wyx=wy+x;
			Case c=cases[wyx];
			if (c.building==NOGBID)
			{
				if (c.ressource.type!=NO_RES_TYPE)
					obstacleUnitMap[wyx]=0;
				else if (c.forbidden&teamMask)
					obstacleUnitMap[wyx]=0;
				else if (!canSwim && (c.terrain>=256) && (c.terrain<256+16)) // !canSwim && isWatter ?
					obstacleUnitMap[wyx]=0;
				else
					obstacleUnitMap[wyx]=1;
			}
			else
				obstacleUnitMap[wyx]=0;
		}
	}
}


void AICastor::computeObstacleBuildingMap()
{
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	//size_t size=w*h;
	Case *cases=map->cases;
	
	for (int y=0; y<h; y++)
	{
		int wy=w*y;
		for (int x=0; x<w; x++)
		{
			int wyx=wy+x;
			Case c=cases[wyx];
			if (c.building==NOGBID)
			{
				if (c.terrain>=16) // if (!isGrass)
					obstacleBuildingMap[wyx]=0;
				else if (c.ressource.type!=NO_RES_TYPE)
					obstacleBuildingMap[wyx]=0;
				else
					obstacleBuildingMap[wyx]=1;
			}
			else
				obstacleBuildingMap[wyx]=0;
		}
	}
}

void AICastor::computeSpaceForBuildingMap()
{
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	
	memcpy(spaceForBuildingMap, obstacleBuildingMap, size);
	
	for (int y=0; y<h; y++)
	{
		int wy0=w*y;
		int wy1=w*((y+1)&wMask);
		
		for (int x=0; x<w; x++)
		{
			int wyx[4];
			wyx[0]=wy0+x+0;
			wyx[1]=wy0+x+1;
			wyx[2]=wy1+x+0;
			wyx[3]=wy1+x+1;
			Uint8 obs[4];
			for (int i=0; i<4; i++)
				obs[i]=spaceForBuildingMap[wyx[i]];
			Uint8 min=255;
			for (int i=0; i<4; i++)
				if (min>obs[i])
					min=obs[i];
			if (min!=0)
				spaceForBuildingMap[wyx[0]]=min+1;
		}
	}
}

void AICastor::computeBuildingNeighbourMap()
{
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *gradient=buildingNeighbourMap;
	Case *cases=map->cases;
	
	Uint8 *wheatGradient=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
	memset(gradient, 0, size);
	
	Building **myBuildings=team->myBuildings;
	for (int i=0; i<1024; i++)
	{
		Building *b=myBuildings[i];
		if (b)
		{
			int bx=b->posX;
			int by=b->posY;
			int bw=b->type->width;
			int bh=b->type->height;
			
			// we skip building with already a neighbour:
			bool neighbour=false;
			bool wheat=false;
			for (int xi=bx-1; xi<=bx+bw; xi++)
			{
				int index;
				index=(xi&wMask)+(((by-1 )&hMask)<<wDec);
				if (cases[index].building!=NOGBID)
					neighbour=true;
				if (wheatGradient[index]==255)
					wheat=true;
				index=(xi&wMask)+(((by+bh)&hMask)<<wDec);
				if (cases[index].building!=NOGBID)
					neighbour=true;
				if (wheatGradient[index]==255)
					wheat=true;
			}
			if (!neighbour)
				for (int yi=by-1; yi<=by+bh; yi++)
				{
					int index;
					index=((bx-1 )&wMask)+((yi&hMask)<<wDec);
					if (cases[index].building!=NOGBID)
						neighbour=true;
					if (wheatGradient[index]==255)
						wheat=true;
					index=((bx+bw)&wMask)+((yi&hMask)<<wDec);
					if (cases[index].building!=NOGBID)
						neighbour=true;
					if (wheatGradient[index]==255)
						wheat=true;
				}
			
			Uint8 dirty;
			if (neighbour || !wheat)
				dirty=1;
			else
				dirty=0;
			
			// dirty two case range, without corners;
			for (int xi=bx-2; xi<=bx+bw; xi++)
			{
				Uint8 *p;
				p=&gradient[(xi&wMask)+(((by   -3)&hMask)<<wDec)];
				(*p)|=1;
				p=&gradient[(xi&wMask)+(((by+bh+1)&hMask)<<wDec)];
				(*p)|=1;
			}
			for (int yi=by-2; yi<=by+bh; yi++)
			{
				Uint8 *p;
				p=&gradient[((bx   -3)&wMask)+((yi&hMask)<<wDec)];
				(*p)|=1;
				p=&gradient[((bx+bw+1)&wMask)+((yi&hMask)<<wDec)];
				(*p)|=1;
			}
			
			// sum from bit 1 for range 3, which are good places, without corners:
			for (int xi=bx-1; xi<bx+bw; xi++)
			{
				Uint8 *p;
				p=&gradient[(xi&wMask)+(((by-2 )&hMask)<<wDec)];
				*p=(*p+2)|dirty;
				p=&gradient[(xi&wMask)+(((by+bh)&hMask)<<wDec)];
				*p=(*p+2)|dirty;
			}
			
			for (int yi=by-1; yi<by+bh; yi++)
			{
				Uint8 *p;
				p=&gradient[((bx-2 )&wMask)+((yi&hMask)<<wDec)];
				*p=(*p+2)|neighbour;
				p=&gradient[((bx+bw)&wMask)+((yi&hMask)<<wDec)];
				*p=(*p+2)|dirty;
			}
		}
	}
}

void AICastor::computeTwoSpaceNeighbourMap()
{
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *gradient=twoSpaceNeighbourMap;
	
	memset(gradient, 0, size);
	
	Building **myBuildings=team->myBuildings;
	for (int i=0; i<1024; i++)
	{
		Building *b=myBuildings[i];
		if (b)
		{
			int bx=b->posX;
			int by=b->posY;
			int bw=b->type->width;
			int bh=b->type->height;
			
			// dirty one case range:
			for (int xi=bx-2; xi<=bx+bw; xi++)
			{
				Uint8 *p;
				p=&gradient[(xi&wMask)+(((by -2)&hMask)<<wDec)];
				(*p)|=1;
				p=&gradient[(xi&wMask)+(((by+bh)&hMask)<<wDec)];
				(*p)|=1;
			}
			for (int yi=by-1; yi<by+bh; yi++)
			{
				Uint8 *p;
				p=&gradient[((bx -2)&wMask)+((yi&hMask)<<wDec)];
				(*p)|=1;
				p=&gradient[((bx+bw)&wMask)+((yi&hMask)<<wDec)];
				(*p)|=1;
			}
			
			// dirty two case range, without corners:
			for (int xi=bx-2; xi<=bx+bw; xi++)
			{
				Uint8 *p;
				p=&gradient[(xi&wMask)+(((by   -3)&hMask)<<wDec)];
				(*p)|=1;
				p=&gradient[(xi&wMask)+(((by+bh+1)&hMask)<<wDec)];
				(*p)|=1;
			}
			for (int yi=by-2; yi<=by+bh; yi++)
			{
				Uint8 *p;
				p=&gradient[((bx   -3)&wMask)+((yi&hMask)<<wDec)];
				(*p)|=1;
				p=&gradient[((bx+bw+1)&wMask)+((yi&hMask)<<wDec)];
				(*p)|=1;
			}
			
			// sum from bit 1 fro range 3, which are good places:
			for (int xi=bx-4; xi<=bx+bw+2; xi++)
			{
				Uint8 *p;
				p=&gradient[(xi&wMask)+(((by   -4)&hMask)<<wDec)];
				(*p)+=2;
				p=&gradient[(xi&wMask)+(((by+bh+2)&hMask)<<wDec)];
				(*p)+=2;
			}
			for (int yi=by-3; yi<=by+bh+1; yi++)
			{
				Uint8 *p;
				p=&gradient[((bx   -4)&wMask)+((yi&hMask)<<wDec)];
				(*p)+=2;
				p=&gradient[((bx+bw+2)&wMask)+((yi&hMask)<<wDec)];
				(*p)+=2;
			}
		}
	}
}


void AICastor::computeWorkPowerMap()
{
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *gradient=workPowerMap;
	Uint8 maxRange=64;
	if (maxRange>w/2)
		maxRange=w/2;
	if (maxRange>h/2)
		maxRange=h/2;
	
	memset(gradient, 0, size);
	
	Unit **myUnits=team->myUnits;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u && u->typeNum==WORKER && u->medical==0 && u->activity!=Unit::ACT_UPGRADING)
		{
			int range=((u->hungry-u->trigHungry)>>1)/u->race->unitTypes[0][0].hungryness;
			if (range<0)
				continue;
			//printf(" range=%d\n", range);
			if (range>maxRange)
				range=maxRange;
			int ux=u->posX;
			int uy=u->posY;
			static const int reducer=3;
			{
				Uint8 *gp=&gradient[(ux&wMask)+((uy&hMask)<<wDec)];
				Uint16 sum=*gp+(range>>reducer);
				if (sum>255)
					sum=255;
				*gp=sum;
			}
			for (int r=1; r<range; r++)
			{
				for (int dx=-r; dx<=r; dx++)
				{
					Uint8 *gp=&gradient[((ux+dx)&wMask)+(((uy -r)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>255)
						sum=255;
					*gp=sum;
				}
				for (int dx=-r; dx<=r; dx++)
				{
					Uint8 *gp=&gradient[((ux+dx)&wMask)+(((uy +r)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>255)
						sum=255;
					*gp=sum;
				}
				for (int dy=(1-r); dy<r; dy++)
				{
					Uint8 *gp=&gradient[((ux -r)&wMask)+(((uy+dy)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>255)
						sum=255;
					*gp=sum;
				}
				for (int dy=(1-r); dy<r; dy++)
				{
					Uint8 *gp=&gradient[((ux +r)&wMask)+(((uy+dy)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>255)
						sum=255;
					*gp=sum;
				}
			}
		}
	}
}


void AICastor::computeWorkRangeMap()
{
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *gradient=workRangeMap;
	
	memcpy(gradient, obstacleUnitMap, size);
	
	Unit **myUnits=team->myUnits;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if (u && u->typeNum==WORKER && u->medical==0 && u->activity!=Unit::ACT_UPGRADING)
		{
			int range=((u->hungry-u->trigHungry)>>1)/u->race->unitTypes[0][0].hungryness;
			if (range<0)
				continue;
			//printf(" range=%d\n", range);
			if (range>255)
				range=255;
			int index=(u->posX&wMask)+((u->posY&hMask)<<wDec);
			gradient[index]=(Uint8)range;
		}
	}
	
	map->updateGlobalGradient(gradient);
}


void AICastor::computeWorkAbilityMap()
{
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	
	for (size_t i=0; i<size; i++)
	{
		Uint8 workPower=workPowerMap[i];
		Uint8 workRange=workRangeMap[i];
		
		Uint32 workAbility=((workPower*workRange)>>5);
		if (workAbility>255)
			workAbility=255;
		
		workAbilityMap[i]=(Uint8)workAbility;
	}
}

void AICastor::computeHydratationMap()
{
	printf("computeHydratationMap()...\n");
	int w=map->w;
	int h=map->w;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	size_t size=w*h;
	
	memset(hydratationMap, 0, size);
	
	Case *cases=map->cases;
	for (size_t i=0; i<size; i++)
	{
		Uint16 t=cases[i].terrain;
		if ((t>=256) && (t<256+16)) // if WATER
			hydratationMap[i]=16;
	}
	
	updateGlobalGradientNoObstacle(hydratationMap);
	hydratationMapComputed=true;
	printf("...computeHydratationMap() done\n");
}

void AICastor::computeWheatGrowthMap()
{
	int w=map->w;
	int h=map->w;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	size_t size=w*h;
	
	memcpy(wheatGrowthMap, obstacleBuildingMap, size);
	Uint8 *wheatGradient=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
	
	for (size_t i=0; i<size; i++)
	{
		Uint8 wheat=wheatGradient[i]; // [0..255]
		if (wheat>=254)
			wheatGrowthMap[i]=1+hydratationMap[i];
	}
	
	map->updateGlobalGradient(wheatGrowthMap);
	
	for (size_t i=0; i<size; i++)
	{
		Uint8 *p=&wheatGrowthMap[i];
		if ((*p)!=0)
			(*p)--;
	}
}

Order *AICastor::computeGoodFoodBuildingMap(Uint8 minWork, Uint8 minWheat)
{
	// minWork: 50 is a safe value.
	// minWheat: 5 is a safe value
	
	int w=map->w;
	int h=map->w;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	size_t size=w*h;
	
	memset(goodFoodBuildingMap, 0, size);
	
	Uint8 bestWheatScore=0;
	Uint8 bestWorkScore=0;
	size_t bestIndex;
	
	// first, we look for any neighbour:
	for (size_t i=0; i<size; i++)
	{
		Uint8 neighbour=buildingNeighbourMap[i];
		if (neighbour!=2)
			continue;
		
		Uint8 space=spaceForBuildingMap[i];
		if (space<2)
			continue;
		
		Uint8 work=workAbilityMap[i];
		if (work<minWork)
			continue;
		
		Uint8 wheat=wheatGrowthMap[i];
		if (wheat<minWheat)
			continue;
		
		goodFoodBuildingMap[i]=wheat;
		if (wheat>=bestWheatScore && (wheat>bestWheatScore || work>bestWorkScore))
		{
			bestWheatScore=wheat;
			bestWorkScore=work;
			bestIndex=i;
		}
	}
	
	if (bestWheatScore>0 && bestWorkScore>0)
	{
		Sint32 x=(bestIndex&map->wMask);
		Sint32 y=((bestIndex>>map->wDec)&map->hMask);
		int typeNum=globalContainer->buildingsTypes.getTypeNum(BuildingType::FOOD_BUILDING, 0, true);
		return new OrderCreate(team->teamNumber, x, y, (BuildingType::BuildingTypeNumber)typeNum);
	}
	
	
	Uint8 bestNeighbourScore=0;
	// second, we look for a new distance-2 based emplacement
	for (size_t i=0; i<size; i++)
	{
		Uint8 neighbour=twoSpaceNeighbourMap[i];
		if (neighbour&1 || neighbour<2)
			continue;
		
		Uint8 space=spaceForBuildingMap[i];
		if (space<2)
			continue;
		
		Uint8 work=workAbilityMap[i];
		if (work<minWork)
			continue;
		
		Uint8 wheat=wheatGrowthMap[i];
		if (wheat<minWheat)
			continue;
		
		goodFoodBuildingMap[i]=wheat;
		if (/*neighbour>bestNeighbourScore &&*/ wheat>=bestWheatScore && (wheat>bestWheatScore || work>bestWorkScore))
		{
			bestNeighbourScore=neighbour;
			bestWheatScore=wheat;
			bestWorkScore=work;
			bestIndex=i;
		}
	}
	
	if (bestNeighbourScore>0 && bestWheatScore>0 && bestWorkScore>0)
	{
		Sint32 x=(bestIndex&map->wMask);
		Sint32 y=((bestIndex>>map->wDec)&map->hMask);
		int typeNum=globalContainer->buildingsTypes.getTypeNum(BuildingType::FOOD_BUILDING, 0, true);
		return new OrderCreate(team->teamNumber, x, y, (BuildingType::BuildingTypeNumber)typeNum);
	}
	
	return NULL;
}

void AICastor::computeRessourcesCluster()
{
	printf("computeRessourcesCluster()\n");
	int w=map->w;
	int h=map->w;
	int wMask=map->wMask;
	int hMask=map->hMask;
	size_t size=w*h;
	
	memset(ressourcesCluster, 0, size*2);
	
	//int i=0;
	Uint8 old=0xFF;
	Uint16 id=0;
	bool usedid[65536];
	memset(usedid, 0, 65536*sizeof(bool));
	for (int y=0; y<h; y++)
	{
		for (int x=0; x<w; x++)
		{
			Case *c=map->cases+w*(y&hMask)+(x&wMask); // case
			Ressource r=c->ressource; // ressource
			Uint8 rt=r.type; // ressources type
			
			int rci=x+y*w; // ressource cluster index
			Uint16 *rcp=&ressourcesCluster[rci]; // ressource cluster pointer
			Uint16 rc=*rcp; // ressource cluster
			
			if (rt==0xFF)
			{
				*rcp=0;
				old=0xFF;
			}
			else
			{
				printf("ressource rt=%d, at (%d, %d)\n", rt, x, y);
				if (rt!=old)
				{
					printf(" rt!=old\n");
					id=1;
					while (usedid[id])
						id++;
					if (id)
						usedid[id]=true;
					old=rt;
					printf("  id=%d\n", id);
				}
				if (rc!=id)
				{
					if (rc==0)
					{
						*rcp=id;
						printf(" wrote.\n");
					}
					else
					{
						Uint16 oldid=id;
						usedid[oldid]=false;
						id=rc; // newid
						printf(" cleaning oldid=%d to id=%d.\n", oldid, id);
						// We have to correct last ressourcesCluster values:
						*rcp=id;
						while (*rcp==oldid)
						{
							*rcp=id;
							rcp--;
						}
					}
				}
			}
		}
		memcpy(ressourcesCluster+((y+1)&hMask)*w, ressourcesCluster+y*w, w*2);
	}
	
	int used=0;
	for (int id=1; id<65536; id++)
		if (usedid[id])
			used++;
	printf("computeRessourcesCluster(), used=%d\n", used);
}

void AICastor::updateGlobalGradientNoObstacle(Uint8 *gradient)
{
	//In this algotithm, "l" stands for one case at Left, "r" for one case at Right, "u" for Up, and "d" for Down.
	// Warning, this is *nearly* a copy-past, 4 times, once for each direction.
	int w=map->w;
	int h=map->h;
	int hMask=map->hMask;
	int wMask=map->wMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	
	for (int yi=0; yi<h; yi++)
	{
		int wy=((yi&hMask)<<wDec);
		int wyu=(((yi-1)&hMask)<<wDec);
		for (int xi=yi; xi<(yi+w); xi++)
		{
			int x=xi&wMask;
			Uint8 max=gradient[wy+x];
			if (max!=16)
			{
				int xl=(x-1)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyu+x ];
				side[2]=gradient[wyu+xr];
				side[3]=gradient[wy +xl];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==0)
					gradient[wy+x]=0;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	for (int y=hMask; y>=0; y--)
	{
		int wy=(y<<wDec);
		int wyd=(((y+1)&hMask)<<wDec);
		for (int xi=y; xi<(y+w); xi++)
		{
			int x=xi&wMask;
			Uint8 max=gradient[wy+x];
			if (max!=16)
			{
				int xl=(x-1)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyd+xr];
				side[1]=gradient[wyd+x ];
				side[2]=gradient[wyd+xl];
				side[3]=gradient[wy +xl];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==0)
					gradient[wy+x]=0;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	for (int x=0; x<w; x++)
	{
		int xl=(x-1)&wMask;
		for (int yi=x; yi<(x+h); yi++)
		{
			int wy=((yi&hMask)<<wDec);
			int wyu=(((yi-1)&hMask)<<wDec);
			int wyd=(((yi+1)&hMask)<<wDec);
			Uint8 max=gradient[wy+x];
			if (max!=16)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyd+xl];
				side[2]=gradient[wy +xl];
				side[3]=gradient[wyu+x ];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==0)
					gradient[wy+x]=0;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	for (int x=wMask; x>=0; x--)
	{
		int xr=(x+1)&wMask;
		for (int yi=x; yi<(x+h); yi++)
		{
			int wy=((yi&hMask)<<wDec);
			int wyu=(((yi-1)&hMask)<<wDec);
			int wyd=(((yi+1)&hMask)<<wDec);
			Uint8 max=gradient[wy+x];
			if (max!=16)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xr];
				side[1]=gradient[wy +xr];
				side[2]=gradient[wyd+xr];
				side[3]=gradient[wyu+x ];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==0)
					gradient[wy+x]=0;
				else
					gradient[wy+x]=max-1;
			}
		}
	}
}
