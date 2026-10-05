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

#include "SeedTerrain.h"
#include <mutex>
#include <type_traits>

Uint16 *Map::getResourceGradient(int teamNumber, int resourceType, int swimClass, bool withMarkets)
{
	withMarkets = withMarkets && marketsV2Enabled();
	// Keep colonies without markets on the original field and refresh schedule.
	if (withMarkets && game->teams[teamNumber]->canExchange.empty()) withMarkets=false;
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
	assert(gradient);
	if (!gradientRuntime->resourceSeeds.trySeed(*this, teamNumber, resourceType, swimClass, gradient, withMarkets))
		seedResourcesGradientDirect(teamNumber, resourceType, swimClass, gradient, withMarkets);
}

void Map::seedResourcesGradientDirect(int teamNumber, Uint8 resourceType, int swimClass, Uint16 *gradient, bool withMarkets)
{
	withMarkets = withMarkets && marketsV2Enabled();
	assert(gradient);
	const bool canSwim = swimClass > 0;

	const Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	assert(globalContainer);
	// Only fogged resources of a type that must be seen to be collected are hidden.
	const bool hideFogged = globalContainer->resourcesTypes.get(resourceType)->visibleToBeCollected;
	const Tile *tile = tiles.data();
	const Uint8 *immobile = immobileUnits;
	const Uint32 *fog = fogOfWar;
	gradient_preparation::withTerrain(*this, canSwim, [&](auto terrainAt) {
		// Compile-time tags select four kernels, removing market and visibility
		// policy branches from the cell loop. Initialization joins before returning.
		auto seed = [&](auto marketsTag, auto hideFoggedTag) {
			initializeGradientCells([&](size_t begin, size_t end) {
				for (size_t i = begin; i < end; ++i)
				{
					const Tile &c = tile[i];
					Uint16 value = GRADIENT_FORBIDDEN;
					if (!(c.forbidden & teamMask) && immobile[i] == IMMOBILE_UNIT_NONE)
					{
						if (c.resource.type == NO_RES_TYPE)
						{
							if (c.building == NOGBID)
								value = terrainAt(i).open;
							else if constexpr (decltype(marketsTag)::value)
							{
								if (isStockedMarketTile(c.building, teamNumber, resourceType))
									value = GRADIENT_MARKET_SEED;
							}
						}
						else if (c.resource.type == resourceType)
						{
							// Resource goals override terrain/buildings, but not
							// the forbidden/immobile blockers checked above.
							value = GRADIENT_AT_GOAL;
							if constexpr (decltype(hideFoggedTag)::value)
								if (!(fog[i] & teamMask)) value = GRADIENT_FORBIDDEN;
						}
					}
					gradient[i] = value;
				}
			});
		};
		if (withMarkets)
		{
			if (hideFogged) seed(std::true_type{}, std::true_type{});
			else seed(std::true_type{}, std::false_type{});
		}
		else
		{
			if (hideFogged) seed(std::false_type{}, std::true_type{});
			else seed(std::false_type{}, std::false_type{});
		}
	});
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
