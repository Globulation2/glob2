// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/GradientRuntime.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Building.h"
#include "Game.h"
#include "Team.h"
#include "MapInternal.h"

#include <mutex>
#include <array>

Uint16 *Map::getResourceGradient(int teamNumber, int resourceType, int swimClass, bool withMarkets)
{
	withMarkets = withMarkets && marketsV2Enabled();
	// Keep colonies without markets on the original field and refresh schedule.
	if (withMarkets && game->teams[teamNumber]->stockSuppliers.empty()) withMarkets=false;
	// AI workers may request the same lazy field concurrently. Cover both
	// allocation and pipeline invalidation before publishing the pointer.
	std::lock_guard<std::mutex> lock(resourcesGradientMutex);
	Uint16 *&gradient = withMarkets ? marketResourcesGradient[teamNumber][resourceType][swimClass] : resourcesGradient[teamNumber][resourceType][swimClass];
	if (gradient == NULL)
	{
		gradient = new Uint16[size];
		updateResourcesGradient(teamNumber, resourceType, swimClass, withMarkets);
	}
	return gradient;
}

void Map::updateResourcesGradient(int teamNumber, Uint8 resourceType, int swimClass, bool withMarkets)
{
	withMarkets = withMarkets && marketsV2Enabled();
	PERF_SCOPE_TIME(ResourceGradient);
	auto &slot = withMarkets ? marketResourcesGradient[teamNumber][resourceType][swimClass] : resourcesGradient[teamNumber][resourceType][swimClass];
	gradientRuntime->pipeline.invalidate(&slot);
	Uint16 *gradient = slot;
	seedResourcesGradient(teamNumber, resourceType, swimClass, gradient, withMarkets);
	propagateGradient(gradient, swimClass);
	if (withMarkets) marketGradientDirty[teamNumber][resourceType][swimClass]=false;
}

void Map::seedResourcesGradient(int teamNumber, Uint8 resourceType, int swimClass, Uint16 *gradient, bool withMarkets)
{
	withMarkets = withMarkets && marketsV2Enabled();
	assert(gradient);
	bool canSwim = swimClass > 0;

	const Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	assert(globalContainer);
	// Only fogged resources of a type that must be seen to be collected are hidden.
	const bool hideFogged = globalContainer->resourcesTypes.get(resourceType)->visibleToBeCollected;
	// Compile supplier eligibility and cost once; the map-sized loop only reads
	// compact instance-indexed values, never the building catalog.
	std::array<Uint16, Building::MAX_COUNT> supplierSeeds;
	if (withMarkets)
	{
		supplierSeeds.fill(GRADIENT_FORBIDDEN);
		for (const Building *supplier : game->teams[teamNumber]->stockSuppliers)
			if (supplier->buildingState == Building::ALIVE && supplier->type->runtimeSuppliesStock
				&& supplier->type->maxResource[resourceType] > 0 && supplier->availableResource(resourceType) > 0)
				supplierSeeds[Building::GIDtoID(supplier->gid)] = std::max<int>(GRADIENT_UNREACHABLE + 1,
					GRADIENT_AT_GOAL - supplier->type->semantics.market.pickupPenalty * GRADIENT_STEP);
	}
	const Tile *tile = tiles.data();
	const Uint8 *immobile = immobileUnits;
	const Uint32 *fog = fogOfWar;
	initializeGradientCells([&](size_t begin, size_t end) {
	for (size_t i=begin; i<end; i++)
	{
		const Tile& c=tile[i];
		Uint16 value;
		if ((c.forbidden & teamMask) || immobile[i] != IMMOBILE_UNIT_NONE)
			value=GRADIENT_FORBIDDEN;
		else if (c.resource.type==NO_RES_TYPE)
		{
			if (c.building!=NOGBID)
				value=withMarkets && Building::GIDtoTeam(c.building) == teamNumber
					? supplierSeeds[Building::GIDtoID(c.building)] : GRADIENT_FORBIDDEN;
			else if (!terrainPropertiesAt(i).walkable && !(canSwim && terrainPropertiesAt(i).swimmable))
				value=GRADIENT_FORBIDDEN;
			else
				value=GRADIENT_UNREACHABLE;
		}
		else if (c.resource.type==resourceType)
			value=(hideFogged && !(fog[i]&teamMask)) ? GRADIENT_FORBIDDEN : GRADIENT_AT_GOAL;
		else
			value=GRADIENT_FORBIDDEN;
		gradient[i]=value;
	}
	});
	// Overlay providers have no tile occupancy entry. Seed their small
	// footprints after the cell loop, retaining the stock-catalog fast path.
	if (withMarkets && game->buildingsTypes.usesOverlaySuppliers())
		for (const Building* supplier : game->teams[teamNumber]->stockSuppliers)
		{
			const Uint16 seed = supplierSeeds[Building::GIDtoID(supplier->gid)];
			if (supplier->type->semantics.occupiesGround || seed <= GRADIENT_UNREACHABLE) continue;
			for (int y=0; y<supplier->type->height; ++y)
				for (int x=0; x<supplier->type->width; ++x)
				{
					const size_t i = coordToIndex(supplier->posX+x, supplier->posY+y);
					if (!(tile[i].forbidden & teamMask) && immobile[i] == IMMOBILE_UNIT_NONE)
						gradient[i] = std::max(gradient[i], seed);
				}
		}
}

void Map::dirtyMarketGradients(int teamNumber, int resourceType)
{
	if (!marketsV2Enabled()) return;
	for (int s=0; s<SWIM_CLASS_COUNT; ++s)
		{
			marketGradientDirty[teamNumber][resourceType][s]=true;
			gradientRuntime->pipeline.invalidate(&marketResourcesGradient[teamNumber][resourceType][s]);
		}
}
