// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "BuildingType.h"
#include "Game.h"
#include "Unit.h"
#include "MapInternal.h"
#include "gradient/GradientRuntime.h"
#include "BuildingGradientSearch.h"
#include "BuildingGradientStats.h"
#include <algorithm>
#include <array>
#include "Team.h"



void Map::finishBuildingGradient(Building *building, int swimClass, BuildingRoute route) const
{
	if (auto &search = building->globalGradientSearch[building->routeSlot(swimClass, route)]) search->finish();
}

// updateGlobalGradient(Building*): the full-map gradient toward a building, a
// flag's zone or, for a clearing flag, the clearable resources in its range.

void Map::updateGlobalGradient(Building *building, int swimClass, BuildingRoute route)
{
	PERF_SCOPE_TIME(BuildingGradient);
	route = building->resolveRoute(route);
	const int slot = building->routeSlot(swimClass, route);
	const int access = building->routeAccess(swimClass, route);
	assert(building);
	assert(building->type);
	int posX=building->posX;
	int posY=building->posY;
	int posW=building->type->width;
	Uint32 teamMask=building->owner->me;
	Uint16 bgid=building->gid;
	bool canSwim=swimClass>0;

	Uint16 *gradient=building->globalGradient[slot];
	assert(gradient);
	// Diagnostics read the replaced field and its search before reinitialization.
	if (gradientStats) gradientStats->fieldRebuilding(*this, *building, slot, access, game->stepCounter, topologyGeneration);
	// A synchronous build supersedes any queued or pending scheduled refresh.
	building->supersedeGradient(slot);
	gradientRuntime->countSynchronous();
	if (const auto &previous = building->globalGradientSearch[slot])
		building->settledCostHint[slot] = Uint16(std::min(previous->requiredCost(), 0xFFFE));
	// A rebuild replaces the old search and its frozen terrain snapshot.
	// Keep bucket capacity when possible; a locked field has no pending search.
	building->dirtyGradient[slot]=false;
	building->lastGlobalGradientUpdateStepCounter[slot]=game->stepCounter;
	building->gradientGeneration[slot]=topologyGeneration;

	if (route == BuildingRoute::Footprint)
	{
		// Ordinary buildings have no prepainted flag goals: write each cell's
		// final initial value directly, without clearing the whole field first.
		initializeGradientCells([&](size_t begin, size_t end) {
		for (size_t i=begin; i<end; ++i)
		{
			if (occupancyCells[i].building!=NOGBID)
				gradient[i]=occupancyCells[i].building==bgid ? GRADIENT_AT_GOAL : GRADIENT_FORBIDDEN;
			else if ((areaCells[i].forbidden&teamMask) || resourceBlocksGround(i) ||
			         occupancyCells[i].immobileUnit!=IMMOBILE_UNIT_NONE || (!terrainPropertiesAt(i).walkable && !(canSwim && terrainPropertiesAt(i).swimmable)))
				gradient[i]=GRADIENT_FORBIDDEN;
			else
				gradient[i]=GRADIENT_UNREACHABLE;
		}
		});
		if (!building->type->semantics.occupiesGround)
			for (int y=0; y<building->type->height; ++y)
				for (int x=0; x<posW; ++x)
					gradient[coordToIndex(posX+x,posY+y)]=GRADIENT_AT_GOAL;
	}
	else
	{
		const bool isClearingFlag=route == BuildingRoute::Clearing;
		const bool isWarFlag=route == BuildingRoute::Combat;
		std::fill(gradient, gradient+size, GRADIENT_UNREACHABLE);
		if (!isClearingFlag)
		{
			int r=building->unitStayRange;
			int r2=r*r;
			for (int yi=-r; yi<=r; yi++)
			{
				int yi2=(yi*yi);
				for (int xi=-r; xi<=r; xi++)
					if (yi2+(xi*xi)<=r2)
					{
						size_t addr = coordToIndex(posX+w+xi, posY+h+yi);
						if(gradient[addr] == GRADIENT_UNREACHABLE)
							gradient[addr] = GRADIENT_AT_GOAL;
					}
			}
		}
		else
		{
			bool anyResourceToClear=false;
			int r=building->unitStayRange;
			int r2=r*r;
			for (int yi=-r; yi<=r; yi++)
			{
				int yi2=(yi*yi);
				for (int xi=-r; xi<=r; xi++)
					if (yi2+(xi*xi)<=r2)
					{
						size_t addr = coordToIndex(posX+w+xi, posY+h+yi);
						if(resourceCells[addr].resource.type != NO_RES_TYPE && resourcePropertiesByIndex(resourceCells[addr].resource.type).clearable &&
						   isClearableResourceForMaterials(int(addr & wMask), int(addr >> wDec), building->clearingMaterials))
						{
							if(gradient[addr] == GRADIENT_UNREACHABLE)
								gradient[addr] = GRADIENT_AT_GOAL;
							anyResourceToClear=true;
						}
					}
			}
			building->anyResourceToClear[canSwim] = anyResourceToClear ? 1 : 2;
		}

		initializeGradientCells([&](size_t begin, size_t end) {
			for (size_t wyx=begin; wyx<end; ++wyx)
			{

				if (occupancyCells[wyx].building==NOGBID)
				{
					if (areaCells[wyx].forbidden&teamMask)
						gradient[wyx] = GRADIENT_FORBIDDEN;
					else if (resourceBlocksGround(wyx) && !(isClearingFlag && gradient[wyx]==GRADIENT_AT_GOAL))
						gradient[wyx] = GRADIENT_FORBIDDEN;
					else if(occupancyCells[wyx].immobileUnit != IMMOBILE_UNIT_NONE)
						gradient[wyx] = GRADIENT_FORBIDDEN;
					//Clearing flags don't consider water an obstacle so long as that piece of
					//water is under the flag, like algae
					else if (!terrainPropertiesAt(wyx).walkable && !(canSwim && terrainPropertiesAt(wyx).swimmable) && (!isClearingFlag || gradient[wyx] != GRADIENT_AT_GOAL))
						gradient[wyx] = GRADIENT_FORBIDDEN;
				}
				else
				{
					// Attraction never uses its own occupied footprint as a goal.
					// Combat may route through enemy buildings, clearing may not.
					if(!isWarFlag || (1<<Building::GIDtoTeam(occupancyCells[wyx].building)) & (building->owner->allies))
						gradient[wyx] = GRADIENT_FORBIDDEN;
					else if(gradient[wyx]!=GRADIENT_AT_GOAL)
						gradient[wyx] = GRADIENT_UNREACHABLE;
				}
			}
		});
	}

	if (route == BuildingRoute::Footprint)
	{
		// Spiral around the building footprint corner; start one cell NW of the building origin
		// (toroidal wrap), stride posW+1 so we cover the perimeter.
		bool reachable=false;
		if (posW == building->type->height)
			reachable = spiralFindNonZero(gradient, (posX-1)&wMask, (posY-1)&hMask, posW+1, wMask, hMask, wDec);
		else
		{
			const int height=building->type->height;
			for (int x=-1; x<=posW && !reachable; ++x)
				reachable=gradient[coordToIndex(posX+x,posY-1)]>GRADIENT_FORBIDDEN || gradient[coordToIndex(posX+x,posY+height)]>GRADIENT_FORBIDDEN;
			for (int y=0; y<height && !reachable; ++y)
				reachable=gradient[coordToIndex(posX-1,posY+y)]>GRADIENT_FORBIDDEN || gradient[coordToIndex(posX+posW,posY+y)]>GRADIENT_FORBIDDEN;
		}
		building->locked[access] = !reachable;
		if (!reachable)
		{
			recycleBuildingGradientSearch(std::move(building->globalGradientSearch[slot]));
			return;
		}
	}
	else
		building->locked[access]=false;

	auto &search = building->globalGradientSearch[slot];
	if (!search) search = acquireBuildingGradientSearch();
	search->begin(*this, gradient, swimClass);
}
