// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière


#include "AICastor.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapInternal.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include "Utilities.h"

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 2

using std::shared_ptr;

void AICastor::computeObstacleUnitMap()
{
	//printf("computeObstacleUnitMap()...\n");
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	size_t size=w*h;
	const auto& cases=map->cases;
	Uint32 teamMask=team->me;
	for (size_t i=0; i<size; i++)
	{
		const auto& c=cases[i];
		if (c.building!=NOGBID)
			obstacleUnitMap[i]=0;
		else if (c.ressource.type!=NO_RES_TYPE)
			obstacleUnitMap[i]=0;
		else if (c.forbidden&teamMask)
			obstacleUnitMap[i]=0;
		else if (!canSwim && (c.terrain>=AI_CASTOR_TERRAIN_WATER_FIRST) && (c.terrain<AI_CASTOR_TERRAIN_WATER_FIRST+AI_CASTOR_TERRAIN_WATER_COUNT)) // !canSwim && isWatter ?
			obstacleUnitMap[i]=0;
		else
			obstacleUnitMap[i]=1;
	}
	//printf("...computeObstacleUnitMap() done\n");
}


void AICastor::computeObstacleBuildingMap()
{
	//printf("computeObstacleBuildingMap()...\n");
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	const auto& cases=map->cases;
	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.building!=NOGBID)
			obstacleBuildingMap[i]=0;
		else  if (c.terrain>=AI_CASTOR_TERRAIN_GRASS_COUNT) // if (!isGrass)
			obstacleBuildingMap[i]=0;
		else if (c.ressource.type!=NO_RES_TYPE)
			obstacleBuildingMap[i]=0;
		else
			obstacleBuildingMap[i]=1;
	}
	//printf("...computeObstacleBuildingMap() done\n");
}

void AICastor::computeSpaceForBuildingMap(int max)
{
	//printf("computeSpaceForBuildingMap()...\n");
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	
	memcpy(spaceForBuildingMap, obstacleBuildingMap, size);
	
	for (int i=1; i<max; i++)
	{
		for (int y=0; y<h; y++)
		{
			int wy0=w*y;
			int wy1=w*((y+1)&hMask);
			
			for (int x=0; x<w; x++)
			{
				int wyx[4];
				wyx[0]=wy0+x+0;
				wyx[1]=wy0+((x+1)&wMask);
				wyx[2]=wy1+x+0;
				wyx[3]=wy1+((x+1)&wMask);
				Uint8 obs[AI_CASTOR_CORNERS];
				for (int i=0; i<AI_CASTOR_CORNERS; i++)
					obs[i]=spaceForBuildingMap[wyx[i]];
				Uint8 min=AI_CASTOR_UINT8_MAX_VALUE;
				for (int i=0; i<AI_CASTOR_CORNERS; i++)
					if (min>obs[i])
						min=obs[i];
				if (min!=0)
					spaceForBuildingMap[wyx[0]]=min+1;
			}
		}
	}
	//printf("...computeSpaceForBuildingMap() done\n");
}

void AICastor::computeBuildingNeighbourMapOfBuilding(int bx, int by, int bw, int bh, int dw, int dh)
{
	//int w=map->w;
	//int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	
	//size_t size=w*h;
	Uint8 *gradient=buildingNeighbourMap;
	const auto& cases=map->cases;
	
	//Uint8 *wheatGradient=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
	
	/*int bx=b->posX;
	int by=b->posY;
	int bw=b->type->width;
	int bh=b->type->height;*/
	
	// we skip building with already a neighbour:
	bool neighbour=false;
	//bool wheat=false;
	for (int xi=bx-1; xi<=bx+bw; xi++)
	{
		int index;
		index=(xi&wMask)+(((by-1 )&hMask)<<wDec);
		if (cases[index].building!=NOGBID)
			neighbour=true;
		//if (wheatGradient[index]==255)
		//	wheat=true;
		index=(xi&wMask)+(((by+bh)&hMask)<<wDec);
		if (cases[index].building!=NOGBID)
			neighbour=true;
		//if (wheatGradient[index]==255)
		//	wheat=true;
	}
	if (!neighbour)
		for (int yi=by-1; yi<=by+bh; yi++)
		{
			int index;
			index=((bx-1 )&wMask)+((yi&hMask)<<wDec);
			if (cases[index].building!=NOGBID)
				neighbour=true;
			//if (wheatGradient[index]==255)
			//	wheat=true;
			index=((bx+bw)&wMask)+((yi&hMask)<<wDec);
			if (cases[index].building!=NOGBID)
				neighbour=true;
			//if (wheatGradient[index]==255)
			//	wheat=true;
		}
	
	Uint8 dirty;
	if (neighbour || /*!wheat ||*/ bw!=dw || bh!=dh)
		dirty=AI_CASTOR_NEIGHBOUR_DIRTY_BIT;
	else
		dirty=0;

	// dirty at a range of 1 space case, without corners;
	for (int xi=bx-dw+1; xi<bx+bw; xi++)
	{
		gradient[(xi&wMask)+(((by-dh-1)&hMask)<<wDec)]|=AI_CASTOR_NEIGHBOUR_DIRTY_BIT;
		gradient[(xi&wMask)+(((by+bh+1)&hMask)<<wDec)]|=AI_CASTOR_NEIGHBOUR_DIRTY_BIT;
	}
	for (int yi=by-dh+1; yi<by+bh; yi++)
	{
		gradient[((bx-dw-1)&wMask)+((yi&hMask)<<wDec)]|=AI_CASTOR_NEIGHBOUR_DIRTY_BIT;
		gradient[((bx+bw+1)&wMask)+((yi&hMask)<<wDec)]|=AI_CASTOR_NEIGHBOUR_DIRTY_BIT;
	}
	{
		// the same with inner inner corners:
		gradient[((bx-dw)&wMask)+(((by-dh)&hMask)<<wDec)]|=AI_CASTOR_NEIGHBOUR_DIRTY_BIT;
		gradient[((bx-dw)&wMask)+(((by+bh)&hMask)<<wDec)]|=AI_CASTOR_NEIGHBOUR_DIRTY_BIT;
		gradient[((bx+bw)&wMask)+(((by-dh)&hMask)<<wDec)]|=AI_CASTOR_NEIGHBOUR_DIRTY_BIT;
		gradient[((bx+bw)&wMask)+(((by+bh)&hMask)<<wDec)]|=AI_CASTOR_NEIGHBOUR_DIRTY_BIT;
	}

	// At a range of 0 space case (neighbours), without corners,
	// we increment (bit 1 to 3), and dirty bit 0 in case:
	for (int xi=bx-dw+1; xi<bx+bw; xi++)
	{
		Uint8 *p;
		p=&gradient[(xi&wMask)+(((by-dh)&hMask)<<wDec)];
		*p=((*p+AI_CASTOR_NEIGHBOUR_DIRECT_INCR)|dirty)&(~AI_CASTOR_NEIGHBOUR_CENTRE_BIT);
		p=&gradient[(xi&wMask)+(((by+bh)&hMask)<<wDec)];
		*p=((*p+AI_CASTOR_NEIGHBOUR_DIRECT_INCR)|dirty)&(~AI_CASTOR_NEIGHBOUR_CENTRE_BIT);
	}
	for (int yi=by-dh+1; yi<by+bh; yi++)
	{
		Uint8 *p;
		p=&gradient[((bx-dw)&wMask)+((yi&hMask)<<wDec)];
		*p=((*p+AI_CASTOR_NEIGHBOUR_DIRECT_INCR)|dirty)&(~AI_CASTOR_NEIGHBOUR_CENTRE_BIT);
		p=&gradient[((bx+bw)&wMask)+((yi&hMask)<<wDec)];
		*p=((*p+AI_CASTOR_NEIGHBOUR_DIRECT_INCR)|dirty)&(~AI_CASTOR_NEIGHBOUR_CENTRE_BIT);
	}

	// At a range of 2 space case, without corners,
	// we increment (bit 5 to 7):
	for (int xi=bx-dw; xi<bx+bw+1; xi++)
	{
		Uint8 *p;
		p=&gradient[(xi&wMask)+(((by-dh-2)&hMask)<<wDec)];
		(*p)+=AI_CASTOR_NEIGHBOUR_FAR_INCR;
		p=&gradient[(xi&wMask)+(((by+bh+2)&hMask)<<wDec)];
		(*p)+=AI_CASTOR_NEIGHBOUR_FAR_INCR;
	}
	for (int yi=by-dh; yi<by+bh+1; yi++)
	{
		Uint8 *p;
		p=&gradient[((bx-dw-2)&wMask)+((yi&hMask)<<wDec)];
		(*p)+=AI_CASTOR_NEIGHBOUR_FAR_INCR;
		p=&gradient[((bx+bw+2)&wMask)+((yi&hMask)<<wDec)];
		(*p)+=AI_CASTOR_NEIGHBOUR_FAR_INCR;
	}
	{
		// the same with inner inner corners:
		Uint8 *p;
		p=&gradient[((bx-dw-1)&wMask)+(((by-dh-1)&hMask)<<wDec)];
		(*p)+=AI_CASTOR_NEIGHBOUR_FAR_INCR;
		p=&gradient[((bx-dw-1)&wMask)+(((by+bh+1)&hMask)<<wDec)];
		(*p)+=AI_CASTOR_NEIGHBOUR_FAR_INCR;
		p=&gradient[((bx+bw+1)&wMask)+(((by-dh-1)&hMask)<<wDec)];
		(*p)+=AI_CASTOR_NEIGHBOUR_FAR_INCR;
		p=&gradient[((bx+bw+1)&wMask)+(((by+bh+1)&hMask)<<wDec)];
		(*p)+=AI_CASTOR_NEIGHBOUR_FAR_INCR;
	}
}

void AICastor::computeBuildingNeighbourMap(int dw, int dh)
{
	int w=map->w;
	int h=map->h;
	//size_t size=w*h;
	
	//int hDec=map->hDec;
	int wDec=map->wDec;
	
	int wMask=map->wMask;
	int hMask=map->hMask;
	
	Uint8 *gradient=buildingNeighbourMap;
	//memset(gradient, 0, size);
	Uint32 visionMask=team->me;
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			for (int dy=0; dy<dh; dy++)
				for (int dx=0; dx<dw; dx++)
				{
					size_t index=(((y+dy)&hMask)<<wDec)+((x+dw)&wMask);
					if ((map->mapDiscovered[index]&visionMask))
						goto doubleBreak;
				}
			gradient[(y<<wDec)+x]=AI_CASTOR_NEIGHBOUR_OUT_OF_VISION;
			continue;
		doubleBreak:
			gradient[(y<<wDec)+x]=0;
		}
	
	Game *game=team->game;
	for (Sint32 ti=0; ti<game->mapHeader.getNumberOfTeams(); ti++)
	{
		Team *team=game->teams[ti];
		assert(team);
		if (!team)
			continue;
		Building **myBuildings=team->myBuildings;
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			Building *b=myBuildings[i];
			if (b && !b->type->isVirtual)
			{
				int bx=b->posX;
				int by=b->posY;
				int bw=b->type->width;
				int bh=b->type->height;
				computeBuildingNeighbourMapOfBuilding(bx, by, bw, bh, dw, dh);
			}
		}
	}
	
	for (std::list<Game::BuildProject>::iterator bpi=game->buildProjects.begin(); bpi!=game->buildProjects.end(); bpi++)
	{
		int bx=bpi->posX&map->getMaskW();
		int by=bpi->posY&map->getMaskH();
		//int teamNumber=bpi->teamNumber;
		Sint32 typeNum=(bpi->typeNum);
		BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
		int bw=bt->width;
		int bh=bt->height;
		computeBuildingNeighbourMapOfBuilding(bx, by, bw, bh, dw, dh);
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
	Uint8 maxRange=AI_CASTOR_WORK_POWER_MAX_RANGE;
	if (maxRange>w/AI_CASTOR_HALFMAP_DIV)
		maxRange=w/AI_CASTOR_HALFMAP_DIV;
	if (maxRange>h/AI_CASTOR_HALFMAP_DIV)
		maxRange=h/AI_CASTOR_HALFMAP_DIV;
	
	memset(gradient, 0, size);
	
	Unit **myUnits=team->myUnits;
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=myUnits[i];
		if (u && u->typeNum==WORKER && u->medical==0 && u->activity!=Unit::ACT_UPGRADING)
		{
			int range=((u->hungry-u->trigHungry)>>AI_CASTOR_HUNGER_RANGE_SHIFT)/u->race->hungryness;
			if (range<0)
				continue;
			//printf(" range=%d\n", range);
			if (range>maxRange)
				range=maxRange;
			int ux=u->posX;
			int uy=u->posY;
			static const int reducer=AI_CASTOR_POWER_STAMP_REDUCER;
			{
				Uint8 *gp=&gradient[(ux&wMask)+((uy&hMask)<<wDec)];
				Uint16 sum=*gp+(range>>reducer);
				if (sum>AI_CASTOR_UINT8_MAX_VALUE)
					sum=AI_CASTOR_UINT8_MAX_VALUE;
				*gp=sum;
			}
			for (int r=1; r<range; r++)
			{
				for (int dx=-r; dx<=r; dx++)
				{
					Uint8 *gp=&gradient[((ux+dx)&wMask)+(((uy -r)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>AI_CASTOR_UINT8_MAX_VALUE)
						sum=AI_CASTOR_UINT8_MAX_VALUE;
					*gp=sum;
				}
				for (int dx=-r; dx<=r; dx++)
				{
					Uint8 *gp=&gradient[((ux+dx)&wMask)+(((uy +r)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>AI_CASTOR_UINT8_MAX_VALUE)
						sum=AI_CASTOR_UINT8_MAX_VALUE;
					*gp=sum;
				}
				for (int dy=(1-r); dy<r; dy++)
				{
					Uint8 *gp=&gradient[((ux -r)&wMask)+(((uy+dy)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>AI_CASTOR_UINT8_MAX_VALUE)
						sum=AI_CASTOR_UINT8_MAX_VALUE;
					*gp=sum;
				}
				for (int dy=(1-r); dy<r; dy++)
				{
					Uint8 *gp=&gradient[((ux +r)&wMask)+(((uy+dy)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>AI_CASTOR_UINT8_MAX_VALUE)
						sum=AI_CASTOR_UINT8_MAX_VALUE;
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
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=myUnits[i];
		if (u && u->typeNum==WORKER && u->medical==0 && u->activity!=Unit::ACT_UPGRADING)
		{
			int range=((u->hungry-u->trigHungry)>>AI_CASTOR_HUNGER_RANGE_SHIFT)/u->race->hungryness;
			if (range<0)
				continue;
			//printf(" range=%d\n", range);
			if (range>GRADIENT_AT_GOAL)
				range=GRADIENT_AT_GOAL;
			int index=(u->posX&wMask)+((u->posY&hMask)<<wDec);
			gradient[index]=(Uint8)range;
		}
	}
	
	updateGlobalGradient(gradient);
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
		
		Uint32 workAbility=((workPower*workRange)>>AI_CASTOR_WORK_ABILITY_NORM_SHIFT);
		if (workAbility>GRADIENT_AT_GOAL)
			workAbility=GRADIENT_AT_GOAL;

		workAbilityMap[i]=(Uint8)workAbility;
	}
}

void AICastor::computeHydratationMap()
{
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	size_t size=w*h;
	
	Uint16 *gradient=(Uint16 *)malloc(2*size);
	memset(gradient, 0, 2*size);
	const auto& cases=map->cases;
	static const int range=AI_CASTOR_HYDRATATION_RANGE;
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			Uint16 t=cases[x+(y<<wDec)].terrain;
			if ((t>=AI_CASTOR_TERRAIN_SAND_FIRST)&&(t<AI_CASTOR_TERRAIN_SAND_FIRST+AI_CASTOR_TERRAIN_SAND_COUNT)) // if SAND
				for (int r=1; r<range; r++)
				{
					for (int dx=-r; dx<=r; dx++)
					{
						Uint16 *gp=&gradient[((x+dx)&wMask)+(((y -r)&hMask)<<wDec)];
						*gp+=(range-r);
					}
					for (int dx=-r; dx<=r; dx++)
					{
						Uint16 *gp=&gradient[((x+dx)&wMask)+(((y +r)&hMask)<<wDec)];
						*gp+=(range-r);
					}
					for (int dy=(1-r); dy<r; dy++)
					{
						Uint16 *gp=&gradient[((x -r)&wMask)+(((y+dy)&hMask)<<wDec)];
						*gp+=(range-r);
					}
					for (int dy=(1-r); dy<r; dy++)
					{
						Uint16 *gp=&gradient[((x +r)&wMask)+(((y+dy)&hMask)<<wDec)];
						*gp+=(range-r);
					}
				}
		}
	for (size_t i=0; i<size; i++)
	{
		Uint16 value=gradient[i]>>AI_CASTOR_HYDRATATION_NORM_SHIFT;
		if (value<GRADIENT_AT_GOAL)
			hydratationMap[i]=value;
		else
			hydratationMap[i]=GRADIENT_AT_GOAL;
	}
	free(gradient);
}

void AICastor::computeNotGrassMap()
{
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	size_t size=w*h;
	
	memset(notGrassMap, 0, size);
	
	const auto& cases=map->cases;
	for (size_t i=0; i<size; i++)
	{
		Uint16 t=cases[i].terrain;
		// Preserve >16 (not >=16 like obstacleBuildingMap above) — see bug M6.
		if (t>AI_CASTOR_TERRAIN_GRASS_COUNT)// if !GRASS
			notGrassMap[i]=AI_CASTOR_GRADIENT_OBSTACLE_NO_OBSTACLE;
	}
	
	updateGlobalGradientNoObstacle(notGrassMap);
}

void AICastor::computeWheatCareMap()
{
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	size_t sizeMask=(size-1);
	//Uint8 *wheatGradient=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
	//Case *cases=map->cases;
	//Uint32 teamMask=team->me;
	
	Uint8 *temp=wheatCareMap[1];
	wheatCareMap[1]=wheatCareMap[0];
	wheatCareMap[0]=temp;
	
	memcpy(wheatCareMap[0], obstacleUnitMap, size);
	for (size_t i=0; i<=sizeMask; i++)
		if (wheatCareMap[0][i]!=0 && notGrassMap[i]==AI_CASTOR_NOTGRASS_NEIGHBOUR_VAL && hydratationMap[i]>0
			&& ((wheatCareMap[1][i]>AI_CASTOR_WHEATCARE_PREV_HIGH_THRESHOLD)
				|| ((oldWheatGradient[3][i]==AI_CASTOR_WHEAT_GRADIENT_PEAK || oldWheatGradient[2][i]==AI_CASTOR_WHEAT_GRADIENT_PEAK) && (oldWheatGradient[1][i]<AI_CASTOR_WHEAT_GRADIENT_PEAK || oldWheatGradient[0][i]<AI_CASTOR_WHEAT_GRADIENT_PEAK))))
		{
			if (oldWheatGradient[1][i]<AI_CASTOR_WHEAT_GRADIENT_NEAR_PEAK || oldWheatGradient[0][i]<AI_CASTOR_WHEAT_GRADIENT_NEAR_PEAK)
				wheatCareMap[0][i]=AI_CASTOR_WHEATCARE_HIGH;
			else
				wheatCareMap[0][i]=AI_CASTOR_WHEATCARE_LOW;
		}
	map->updateGlobalGradient(wheatCareMap[0]);
}

void AICastor::computeWheatGrowthMap()
{
	if (lastWheatGrowthMapComputed==timer)
		return;
	
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *wheatGradient=map->ressourcesGradient[team->teamNumber][CORN][canSwim];
	
	memcpy(wheatGrowthMap, obstacleBuildingMap, size);
	
	for (size_t i=0; i<size; i++)
		if (wheatGradient[i]==AI_CASTOR_WHEAT_GRADIENT_PEAK)
			wheatGrowthMap[i]=AI_CASTOR_WHEAT_GROWTH_BASE+(hydratationMap[i]>>AI_CASTOR_WHEAT_GROWTH_HYDRATATION_SHIFT);

	map->updateGlobalGradient(wheatGrowthMap);

	for (size_t i=0; i<size; i++)
	{
		Uint8 care=wheatCareMap[0][i];
		if (care>AI_CASTOR_WHEAT_CARE_SUBTRACT_THRESHOLD)
		{
			Uint8 *p=&wheatGrowthMap[i];
			Uint8 growth=*p;
			if (growth>care)
				(*p)=growth-care;
			else
				(*p)=AI_CASTOR_WHEAT_GROWTH_MIN;
		}
	}
	lastWheatGrowthMapComputed=timer;
}

void AICastor::computeEnemyPowerMap()
{
	if (lastEnemyPowerMapComputed==timer)
		return;
	lastEnemyPowerMapComputed=timer;
	
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *gradient=enemyPowerMap;
	
	memset(gradient, 0, size);
	
	for (int ti=0; ti<game->mapHeader.getNumberOfTeams(); ti++)
	{
		Team *enemyTeam=game->teams[ti];
		Uint32 me=team->me;
		if ((team->enemies&enemyTeam->me)==0)
			continue;
		Building **enemyBuildings=enemyTeam->myBuildings;
		for (int bi=0; bi<Building::MAX_COUNT; bi++)
		{
			Building *b=enemyBuildings[bi];
			if (b==NULL || ((b->seenByMask&me)==0))
				continue;
			int bx=b->posX;
			int by=b->posY;
			static const int reducer=AI_CASTOR_POWER_STAMP_REDUCER;
			static const int range=AI_CASTOR_ENEMY_POWER_RANGE; // max 32
			{
				Uint8 *gp=&gradient[(bx&wMask)+((by&hMask)<<wDec)];
				Uint16 sum=*gp+(range>>reducer);
				if (sum>AI_CASTOR_UINT8_MAX_VALUE)
					sum=AI_CASTOR_UINT8_MAX_VALUE;
				*gp=sum;
			}
			for (int r=1; r<range; r++)
			{
				for (int dx=-r; dx<=r; dx++)
				{
					Uint8 *gp=&gradient[((bx+dx)&wMask)+(((by -r)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>AI_CASTOR_UINT8_MAX_VALUE)
						sum=AI_CASTOR_UINT8_MAX_VALUE;
					*gp=sum;
				}
				for (int dx=-r; dx<=r; dx++)
				{
					Uint8 *gp=&gradient[((bx+dx)&wMask)+(((by +r)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>AI_CASTOR_UINT8_MAX_VALUE)
						sum=AI_CASTOR_UINT8_MAX_VALUE;
					*gp=sum;
				}
				for (int dy=(1-r); dy<r; dy++)
				{
					Uint8 *gp=&gradient[((bx -r)&wMask)+(((by+dy)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>AI_CASTOR_UINT8_MAX_VALUE)
						sum=AI_CASTOR_UINT8_MAX_VALUE;
					*gp=sum;
				}
				for (int dy=(1-r); dy<r; dy++)
				{
					Uint8 *gp=&gradient[((bx +r)&wMask)+(((by+dy)&hMask)<<wDec)];
					Uint16 sum=*gp+((range-r)>>reducer);
					if (sum>AI_CASTOR_UINT8_MAX_VALUE)
						sum=AI_CASTOR_UINT8_MAX_VALUE;
					*gp=sum;
				}
			}
		}
	}
}

void AICastor::computeEnemyRangeMap()
{
	if (lastEnemyRangeMapComputed==timer)
		return;
	lastEnemyRangeMapComputed=timer;
	
	int w=map->w;
	int h=map->h;
	int wMask=map->wMask;
	int hMask=map->hMask;
	//int hDec=map->hDec;
	int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *gradient=enemyRangeMap;
	
	memcpy(gradient, obstacleUnitMap, size);
	
	for (int ti=0; ti<game->mapHeader.getNumberOfTeams(); ti++)
	{
		Team *enemyTeam=game->teams[ti];
		Uint32 me=team->me;
		
		if ((team->enemies & enemyTeam->me)==0)
			continue;
		Building **enemyBuildings=enemyTeam->myBuildings;
		for (int bi=0; bi<Building::MAX_COUNT; bi++)
		{
			Building *b=enemyBuildings[bi];
			if (b==NULL || ((b->seenByMask&me)==0) || b->type->isBuildingSite)
				continue;
			int bx=b->posX;
			int by=b->posY;
			int bw=b->type->width;
			int bh=b->type->height;
			for (int dy=by; dy<by+bh; dy++)
				for (int dx=bx; dx<bx+bw; dx++)
					gradient[(dx&wMask)+((dy&hMask)<<wDec)]=GRADIENT_AT_GOAL;
		}
	}
	
	map->updateGlobalGradient(gradient);
}

void AICastor::computeEnemyWarriorsMap()
{
	if (lastEnemyWarriorsMapComputed==timer)
		return;
	lastEnemyWarriorsMapComputed=timer;
	if (verbose)
		printf("computeEnemyWarriorsMap()\n");
	
	int w=map->w;
	int h=map->h;
	//int wMask=map->wMask;
	//int hMask=map->hMask;
	//int hDec=map->hDec;
	//int wDec=map->wDec;
	size_t size=w*h;
	Uint8 *gradient=enemyWarriorsMap;
	
	memcpy(gradient, obstacleUnitMap, size);
	for (size_t i=0; i<size; i++)
	{
		if ((map->fogOfWar[i]&team->me)==0)
			continue;
		Uint16 guid=map->cases[i].groundUnit;
		if (guid==NOGUID)
			continue;
		Uint32 teamMask=(1<<(guid>>AI_CASTOR_GUID_TEAM_SHIFT));
		if ((teamMask&team->enemies)==0)
			continue;
		gradient[i]=AI_CASTOR_ENEMY_WARRIOR_GRADIENT_SEED;
	}
	map->updateGlobalGradient(gradient);
}

