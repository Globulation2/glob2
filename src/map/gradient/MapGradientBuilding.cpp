// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "BuildingType.h"
#include "Game.h"
#include "Unit.h"
#include "MapInternal.h"
#include "BuildingGradientSearch.h"
#include "BuildingGradientDiagnostics.h"
#include <algorithm>
#include <array>
#include "Team.h"



void Map::finishBuildingGradient(Building *building, int swimClass, BuildingRoute route, const char *caller) const
{
	if (auto &search = building->globalGradientSearch[building->routeSlot(swimClass, route)]) search->finish(caller);
}

// updateGlobalGradient(Building*): the full-map gradient toward a building, a
// flag's zone or, for a clearing flag, the clearable resources in its range.
// updateRoundTripGradient: the gradient of the trip to a resource and on to
// the building.

void Map::updateGlobalGradient(Building *building, int swimClass, BuildingRoute route, const char *reason)
{
	PERF_SCOPE_TIME(BuildingGradient);
	BuildingGradientDiagnostics::Scope evidence(buildingGradientDiagnostics(),building->gid,swimClass,"rebuild",reason,topologyGeneration,topologyGeneration);
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
			const Tile& tile=tiles[i];
			if (tile.building!=NOGBID)
				gradient[i]=tile.building==bgid ? GRADIENT_AT_GOAL : GRADIENT_FORBIDDEN;
			else if ((tile.forbidden&teamMask) || tile.resource.type!=NO_RES_TYPE ||
			         immobileUnits[i]!=IMMOBILE_UNIT_NONE || (!terrainPropertiesAt(i).walkable && !(canSwim && terrainPropertiesAt(i).swimmable)))
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
						if(tiles[addr].resource.type < BASIC_COUNT && building->clearingResources[tiles[addr].resource.type])
						{
							if(gradient[addr] == GRADIENT_UNREACHABLE)
								gradient[addr] = GRADIENT_AT_GOAL;
							anyResourceToClear=true;
						}
					}
			}
			building->anyResourceToClear[buildingPipelineEnabled() ? access : int(canSwim)] = anyResourceToClear ? 1 : 2;
		}

		initializeGradientCells([&](size_t begin, size_t end) {
			for (size_t wyx=begin; wyx<end; ++wyx)
			{
				const Tile& c=tiles[wyx];
				if (c.building==NOGBID)
				{
					if (c.forbidden&teamMask)
						gradient[wyx] = GRADIENT_FORBIDDEN;
					else if (c.resource.type!=NO_RES_TYPE && !(isClearingFlag && gradient[wyx]==GRADIENT_AT_GOAL))
						gradient[wyx] = GRADIENT_FORBIDDEN;
					else if(immobileUnits[wyx] != IMMOBILE_UNIT_NONE)
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
					if(!isWarFlag || (1<<Building::GIDtoTeam(c.building)) & (building->owner->allies))
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
	search->begin(*this, gradient, swimClass, building->gid);
}


void Map::updateRoundTripGradient(Building *building, int resourceType, int swimClass)
{
	PERF_SCOPE_TIME(RoundTripGradient);
	BuildingGradientDiagnostics::Scope evidence(buildingGradientDiagnostics(),building->gid,swimClass,"round_trip","refresh",topologyGeneration,building->gradientGeneration[swimClass]);
	// Only construction needs the parent in full; reading a cached round-trip
	// field must not force a newly refreshed walking field to finish.
	finishBuildingGradient(building, swimClass, BuildingRoute::Footprint,"round_trip_parent");
	recycleBuildingGradientSearch(std::move(building->roundTripGradientSearch[resourceType][swimClass]));
	Uint16 *gradient=building->roundTripGradient[resourceType][swimClass];
	assert(gradient);
	building->roundTripGradientStep[resourceType][swimClass]=game->stepCounter;
	const Uint16 *toBuilding=building->globalGradient[swimClass];
	// Markets replenish from natural resource tiles; other buildings may use stock.
	const unsigned modes=resourceSupplyModes(building,resourceType);
	const bool withMarkets=modes!=0;
	const Uint16 *toResource=getResourceGradient(building->owner->teamNumber, resourceType, swimClass, withMarkets, building);
	// Same obstacles as the resource gradient. A resource tile is seeded with
	// the cost of carrying from the cheapest free cell next to it, where the
	// unit harvests, to the building. A stocked market's tile is a goal as
	// well, its seed the detour dearer.
	const auto visitSuppliers=[&](auto visit) {
		if (modes&1) for (const Building* supplier : building->owner->stockSuppliers) visit(supplier);
		if (modes&2) for (const Building* supplier : building->owner->directStockSuppliers)
			if (!(modes&1) || !(supplier->runtime->suppliesStockMask&(1u<<resourceType))) visit(supplier);
	};
	std::array<int, Building::MAX_COUNT> supplierPenalties;
	if (withMarkets)
	{
		supplierPenalties.fill(0);
		visitSuppliers([&](const Building* supplier) { supplierPenalties[Building::GIDtoID(supplier->gid)] = supplier->type->semantics.market.pickupPenalty * GRADIENT_STEP; });
	}
	Uint16 bestSeed=GRADIENT_UNREACHABLE;
	for (size_t i=0; i<size; i++)
	{
		const bool marketGoal=withMarkets && tiles[i].building!=NOGBID && toResource[i]>GRADIENT_UNREACHABLE;
		if (toResource[i]!=GRADIENT_AT_GOAL && !marketGoal)
		{
			gradient[i]=toResource[i]==GRADIENT_FORBIDDEN ? GRADIENT_FORBIDDEN : GRADIENT_UNREACHABLE;
			continue;
		}
		size_t x=i&wMask;
		size_t y=i>>wDec;
		Uint16 best=GRADIENT_UNREACHABLE;
		for (int d=0; d<8; d++)
		{
			size_t n=coordToIndex(x+tabClose[d][0], y+tabClose[d][1]);
			if (toResource[n]>GRADIENT_UNREACHABLE && toBuilding[n]>best)
				best=toBuilding[n];
		}
		if (marketGoal && best>GRADIENT_UNREACHABLE)
			best=std::max<int>(GRADIENT_UNREACHABLE+1, best-supplierPenalties[Building::GIDtoID(tiles[i].building)]);
		gradient[i]=best;
		if (best>bestSeed)
			bestSeed=best;
	}
	if (withMarkets && ((modes&2) || game->buildingsTypes.usesOverlaySuppliers()))
		visitSuppliers([&](const Building* supplier) {
			if (supplier->runtime->has(BuildingRuntimeTraits::OccupiesGround) || !stockSupplierEligible(supplier,building,resourceType,modes)) return;
			for (int y=0; y<supplier->type->height; ++y)
				for (int x=0; x<supplier->type->width; ++x)
				{
					const size_t i = coordToIndex(supplier->posX+x, supplier->posY+y);
					if (toResource[i] <= GRADIENT_UNREACHABLE) continue;
					Uint16 best = GRADIENT_UNREACHABLE;
					for (int d=0; d<8; ++d)
					{
						const size_t n = coordToIndex(supplier->posX+x+tabClose[d][0], supplier->posY+y+tabClose[d][1]);
						if (toResource[n] > GRADIENT_UNREACHABLE) best = std::max(best, toBuilding[n]);
					}
					if (best > GRADIENT_UNREACHABLE)
						best = std::max<int>(GRADIENT_UNREACHABLE+1, best-supplierPenalties[Building::GIDtoID(supplier->gid)]);
					gradient[i] = std::max(gradient[i], best);
					bestSeed = std::max(bestSeed, best);
				}
		});
	// Units farther than this from the cheapest fetch are scored by the plain
	// distances instead (the callers fall back when a cell is unreachable
	// here), which keeps the build small on big maps.
	constexpr int ROUND_TRIP_RANGE=128*GRADIENT_STEP;
	propagateGradient(gradient, swimClass, GRADIENT_AT_GOAL-bestSeed+ROUND_TRIP_RANGE);
}
