// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "AICastor.h"
#include "ai/observation/WorldQueries.h"
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
	PERF_SCOPE_TIME(AIObserve);
	int w=observation->width;
	int h=observation->height;
	size_t size=w*h;
	Uint32 teamMask=observedTeam->view->mask;
	for (size_t i=0; i<size; i++)
	{
		if (observation->occupancyAt(i).building!=NOGBID)
			obstacleUnitMap[i]=0;
		else if (observation->resourceAt(i).resource.type!=NO_RES_TYPE)
			obstacleUnitMap[i]=0;
		else if (observation->areasAt(i).forbidden&teamMask)
			obstacleUnitMap[i]=0;
		else
		{
			const auto& terrain=observation->terrain->properties(observation->terrainAt(i).type);
			if (!terrain.walkable && !(canSwim && terrain.swimmable))
				obstacleUnitMap[i]=0;
			else
				obstacleUnitMap[i]=1;
		}
	}
}


void AICastor::computeObstacleBuildingMap()
{
	PERF_SCOPE_TIME(AIObserve);
	int w=observation->width;
	int h=observation->height;
	size_t size=w*h;
	for (size_t i=0; i<size; i++)
	{
		if (observation->occupancyAt(i).building!=NOGBID)
			obstacleBuildingMap[i]=0;
		else  if (!observation->terrain->properties(observation->terrainAt(i).type).buildable)
			obstacleBuildingMap[i]=0;
		else if (observation->resourceAt(i).resource.type!=NO_RES_TYPE)
			obstacleBuildingMap[i]=0;
		else
			obstacleBuildingMap[i]=1;
	}
}

void AICastor::computeSpaceForBuildingMap(int max)
{
	PERF_SCOPE_TIME(AIObserve);
	int w=observation->width;
	int h=observation->height;
	int wMask=(observation->width-1);
	int hMask=(observation->height-1);
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
}

void AICastor::computeBuildingNeighbourMapOfBuilding(int bx, int by, int bw, int bh, int dw, int dh)
{
	int wMask=(observation->width-1);
	int hMask=(observation->height-1);
	int wDec=std::countr_zero(unsigned(observation->width));
	
	Uint8 *gradient=buildingNeighbourMap;
	
	//Uint8 *wheatGradient=queries->resourcesGradient[teamNumber][WHEAT][canSwim];
	
	// we skip building with already a neighbour:
	bool neighbour=false;
	//bool wheat=false;
	for (int xi=bx-1; xi<=bx+bw; xi++)
	{
		int index;
		index=(xi&wMask)+(((by-1 )&hMask)<<wDec);
		if (observation->occupancyAt(index).building!=NOGBID)
			neighbour=true;
		//if (wheatGradient[index]==255)
		//	wheat=true;
		index=(xi&wMask)+(((by+bh)&hMask)<<wDec);
		if (observation->occupancyAt(index).building!=NOGBID)
			neighbour=true;
		//if (wheatGradient[index]==255)
		//	wheat=true;
	}
	if (!neighbour)
		for (int yi=by-1; yi<=by+bh; yi++)
		{
			int index;
			index=((bx-1 )&wMask)+((yi&hMask)<<wDec);
			if (observation->occupancyAt(index).building!=NOGBID)
				neighbour=true;
			//if (wheatGradient[index]==255)
			//	wheat=true;
			index=((bx+bw)&wMask)+((yi&hMask)<<wDec);
			if (observation->occupancyAt(index).building!=NOGBID)
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
	PERF_SCOPE_TIME(AIObserve);
	int w=observation->width;
	int h=observation->height;
	
	int wDec=std::countr_zero(unsigned(observation->width));
	
	int wMask=(observation->width-1);
	int hMask=(observation->height-1);
	
	Uint8 *gradient=buildingNeighbourMap;
	Uint32 visionMask=observedTeam->view->mask;
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			for (int dy=0; dy<dh; dy++)
				for (int dx=0; dx<dw; dx++)
				{
					size_t index=(((y+dy)&hMask)<<wDec)+((x+dx)&wMask);
					if ((observation->visibilityAt(index).discovered&visionMask))
						goto doubleBreak;
				}
			gradient[(y<<wDec)+x]=AI_CASTOR_NEIGHBOUR_OUT_OF_VISION;
			continue;
		doubleBreak:
			gradient[(y<<wDec)+x]=0;
		}
	

	for (Sint32 ti=0; ti<observation->teams.size(); ti++)
	{
		TeamObservation *neighbourTeam=teamAt(ti);
		assert(neighbourTeam);
		if (!neighbourTeam)
			continue;
		const auto& myBuildings=neighbourTeam->myBuildings;
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			const AIEngine::BuildingView *b=myBuildings[i];
			if (b && !queries->kind(*b).resolvedType.isVirtual)
			{
				int bx=b->posX;
				int by=b->posY;
				int bw=queries->kind(*b).resolvedType.width;
				int bh=queries->kind(*b).resolvedType.height;
				computeBuildingNeighbourMapOfBuilding(bx, by, bw, bh, dw, dh);
			}
		}
	}
	
	for (auto bpi=observation->buildProjects.begin(); bpi!=observation->buildProjects.end(); ++bpi)
	{
		int bx=bpi->posX&(observation->width-1);
		int by=bpi->posY&(observation->height-1);
		Sint32 typeNum=(bpi->typeNum);
		const BuildingType *bt=(&queries->kind(typeNum).resolvedType);
		int bw=bt->width;
		int bh=bt->height;
		computeBuildingNeighbourMapOfBuilding(bx, by, bw, bh, dw, dh);
	}
}

void AICastor::computeWorkPowerMap()
{
	PERF_SCOPE_TIME(AIObserve);
	int w=observation->width;
	int h=observation->height;
	int wMask=(observation->width-1);
	int hMask=(observation->height-1);
	int wDec=std::countr_zero(unsigned(observation->width));
	size_t size=w*h;
	Uint8 *gradient=workPowerMap;
	Uint8 maxRange=AI_CASTOR_WORK_POWER_MAX_RANGE;
	if (maxRange>w/AI_CASTOR_HALFMAP_DIV)
		maxRange=w/AI_CASTOR_HALFMAP_DIV;
	if (maxRange>h/AI_CASTOR_HALFMAP_DIV)
		maxRange=h/AI_CASTOR_HALFMAP_DIV;
	
	memset(gradient, 0, size);
	
	const auto& myUnits=observedTeam->myUnits;
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		const AIEngine::UnitView *u=myUnits[i];
		if (u && u->typeNum==WORKER && u->medical==0 && u->activity!=Unit::ACT_UPGRADING)
		{
			int range=((u->hungry-u->trigHungry)>>AI_CASTOR_HUNGER_RANGE_SHIFT)/u->hungriness;
			if (range<0)
				continue;
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
	PERF_SCOPE_TIME(AIObserve);
	int w=observation->width;
	int h=observation->height;
	int wMask=(observation->width-1);
	int hMask=(observation->height-1);
	int wDec=std::countr_zero(unsigned(observation->width));
	size_t size=w*h;
	Uint8 *gradient=workRangeMap;
	
	memcpy(gradient, obstacleUnitMap, size);
	
	const auto& myUnits=observedTeam->myUnits;
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		const AIEngine::UnitView *u=myUnits[i];
		if (u && u->typeNum==WORKER && u->medical==0 && u->activity!=Unit::ACT_UPGRADING)
		{
			int range=((u->hungry-u->trigHungry)>>AI_CASTOR_HUNGER_RANGE_SHIFT)/u->hungriness;
			if (range<0)
				continue;
			if (range>AI_CASTOR_GRADIENT_WALL)
				range=AI_CASTOR_GRADIENT_WALL;
			int index=(u->posX&wMask)+((u->posY&hMask)<<wDec);
			gradient[index]=(Uint8)range;
		}
	}
	
	updateGlobalGradient(gradient);
}


void AICastor::computeWorkAbilityMap()
{
	PERF_SCOPE_TIME(AIObserve);
	int w=observation->width;
	int h=observation->height;
	size_t size=w*h;
	
	for (size_t i=0; i<size; i++)
	{
		Uint8 workPower=workPowerMap[i];
		Uint8 workRange=workRangeMap[i];
		
		Uint32 workAbility=((workPower*workRange)>>AI_CASTOR_WORK_ABILITY_NORM_SHIFT);
		if (workAbility>AI_CASTOR_GRADIENT_WALL)
			workAbility=AI_CASTOR_GRADIENT_WALL;

		workAbilityMap[i]=(Uint8)workAbility;
	}
}

void AICastor::computeHydratationMap()
{
	PERF_SCOPE_TIME(AIObserve);
	int w=observation->width;
	int h=observation->height;
	int wMask=(observation->width-1);
	int hMask=(observation->height-1);
	int wDec=std::countr_zero(unsigned(observation->width));
	size_t size=w*h;
	
	Uint16 *gradient=(Uint16 *)malloc(2*size);
	memset(gradient, 0, 2*size);
	static const int range=AI_CASTOR_HYDRATATION_RANGE;
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			if (terrainProvidesFertility(queries->terrainPropertiesAt(x,y)))
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
		if (value<AI_CASTOR_GRADIENT_WALL)
			hydratationMap[i]=value;
		else
			hydratationMap[i]=AI_CASTOR_GRADIENT_WALL;
	}
	free(gradient);
}

void AICastor::computeNotGrassMap()
{
	PERF_SCOPE_TIME(AIObserve);
	int w=observation->width;
	int h=observation->height;
	size_t size=w*h;
	
	memset(notGrassMap, 0, size);
	
	for (size_t i=0; i<size; i++)
	{
		// Habitat replaces the historical >16 sprite test, including its
		// accidental treatment of transition sprite 16 as a wheat tile.
		if (!(observation->terrain->properties(observation->terrainAt(i).type).allowedResources & (1u<<WHEAT)))
			notGrassMap[i]=AI_CASTOR_GRADIENT_OBSTACLE_NO_OBSTACLE;
	}
	
	updateGlobalGradientNoObstacle(notGrassMap);
}

void AICastor::computeWheatCareMap()
{
	PERF_SCOPE_TIME(AIObserve);
	int w=observation->width;
	int h=observation->height;
	size_t size=w*h;
	size_t sizeMask=(size-1);
	//Uint8 *wheatGradient=queries->resourcesGradient[teamNumber][WHEAT][canSwim];
	
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
	queries->updateGlobalGradient(wheatCareMap[0]);
}

// The map's resource gradients are Uint16 with GRADIENT_STEP per tile (MapInternal.h);
// Castor's wheat maps and thresholds keep the historical 8-bit scale of 255 - tiles.
namespace
{
Uint8 castorWheatGradient(Uint16 g)
{
	if (g<=GRADIENT_UNREACHABLE)
		return (Uint8)g;
	int tiles=gradientTiles(g);
	return (Uint8)(tiles>AI_CASTOR_WHEAT_GRADIENT_PEAK-2 ? 2 : AI_CASTOR_WHEAT_GRADIENT_PEAK-tiles);
}
}

Uint8 AICastor::wheatGradientAt(size_t index)
{
	return castorWheatGradient(queries->resourceGradient(teamNumber,WHEAT,canSwim ? Map::SWIM_CLASS_EVEN : 0).data()[index]);
}

void AICastor::copyWheatGradient(Uint8* destination)
{
	const size_t size=observation->width*observation->height;
	if (!size) return;
	const auto* gradient=queries->resourceGradient(teamNumber,WHEAT,canSwim ? Map::SWIM_CLASS_EVEN : 0).data();
	for (size_t i=0; i<size; ++i)
		destination[i]=castorWheatGradient(gradient[i]);
}

void AICastor::computeWheatGrowthMap()
{
	PERF_SCOPE_TIME(AIObserve);
	if (lastWheatGrowthMapComputed==timer)
		return;
	
	int w=observation->width;
	int h=observation->height;
	size_t size=w*h;
	memcpy(wheatGrowthMap, obstacleBuildingMap, size);
	
	const auto* gradient=size ? queries->resourceGradient(teamNumber,WHEAT,canSwim ? Map::SWIM_CLASS_EVEN : 0).data() : nullptr;
	for (size_t i=0; i<size; i++)
		if (castorWheatGradient(gradient[i])==AI_CASTOR_WHEAT_GRADIENT_PEAK)
			wheatGrowthMap[i]=AI_CASTOR_WHEAT_GROWTH_BASE+(hydratationMap[i]>>AI_CASTOR_WHEAT_GROWTH_HYDRATATION_SHIFT);

	queries->updateGlobalGradient(wheatGrowthMap);

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
	PERF_SCOPE_TIME(AIObserve);
	if (lastEnemyPowerMapComputed==timer)
		return;
	lastEnemyPowerMapComputed=timer;
	
	int w=observation->width;
	int h=observation->height;
	int wMask=(observation->width-1);
	int hMask=(observation->height-1);
	int wDec=std::countr_zero(unsigned(observation->width));
	size_t size=w*h;
	Uint8 *gradient=enemyPowerMap;
	
	memset(gradient, 0, size);
	
	for (int ti=0; ti<observation->teams.size(); ti++)
	{
		TeamObservation *enemyTeam=teamAt(ti);
		Uint32 me=observedTeam->view->mask;
		if ((observedTeam->view->enemies&enemyTeam->view->mask)==0)
			continue;
		const auto& enemyBuildings=enemyTeam->myBuildings;
		for (int bi=0; bi<Building::MAX_COUNT; bi++)
		{
			const AIEngine::BuildingView *b=enemyBuildings[bi];
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
	PERF_SCOPE_TIME(AIObserve);
	if (lastEnemyRangeMapComputed==timer)
		return;
	lastEnemyRangeMapComputed=timer;
	
	int w=observation->width;
	int h=observation->height;
	int wMask=(observation->width-1);
	int hMask=(observation->height-1);
	int wDec=std::countr_zero(unsigned(observation->width));
	size_t size=w*h;
	Uint8 *gradient=enemyRangeMap;
	
	memcpy(gradient, obstacleUnitMap, size);
	
	for (int ti=0; ti<observation->teams.size(); ti++)
	{
		TeamObservation *enemyTeam=teamAt(ti);
		Uint32 me=observedTeam->view->mask;
		
		if ((observedTeam->view->enemies & enemyTeam->view->mask)==0)
			continue;
		const auto& enemyBuildings=enemyTeam->myBuildings;
		for (int bi=0; bi<Building::MAX_COUNT; bi++)
		{
			const AIEngine::BuildingView *b=enemyBuildings[bi];
			if (b==NULL || ((b->seenByMask&me)==0) || queries->kind(*b).resolvedType.isBuildingSite)
				continue;
			int bx=b->posX;
			int by=b->posY;
			int bw=queries->kind(*b).resolvedType.width;
			int bh=queries->kind(*b).resolvedType.height;
			for (int dy=by; dy<by+bh; dy++)
				for (int dx=bx; dx<bx+bw; dx++)
					gradient[(dx&wMask)+((dy&hMask)<<wDec)]=AI_CASTOR_GRADIENT_WALL;
		}
	}
	
	if(observation->terrainMovementModifiers) updateGlobalGradient(gradient);
	else queries->updateGlobalGradient(gradient);
}

void AICastor::computeEnemyWarriorsMap()
{
	PERF_SCOPE_TIME(AIObserve);
	if (lastEnemyWarriorsMapComputed==timer)
		return;
	lastEnemyWarriorsMapComputed=timer;
	if (verbose)
		bufferedDiagnostics.push_back({"", "", "computeEnemyWarriorsMap()\n"});
	
	int w=observation->width;
	int h=observation->height;
	size_t size=w*h;
	Uint8 *gradient=enemyWarriorsMap;
	
	memcpy(gradient, obstacleUnitMap, size);
	for (size_t i=0; i<size; i++)
	{
		if ((observation->visibilityAt(i).visible&observedTeam->view->mask)==0)
			continue;
		Uint16 guid=observation->occupancyAt(i).groundUnit;
		if (guid==NOGUID)
			continue;
		Uint32 teamMask=(1<<(guid>>AI_CASTOR_GUID_TEAM_SHIFT));
		if ((teamMask&observedTeam->view->enemies)==0)
			continue;
		gradient[i]=AI_CASTOR_ENEMY_WARRIOR_GRADIENT_SEED;
	}
	if(observation->terrainMovementModifiers) updateGlobalGradient(gradient);
	else queries->updateGlobalGradient(gradient);
}

