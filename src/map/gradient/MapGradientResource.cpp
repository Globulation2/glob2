// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/GradientRuntime.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "MapInternal.h"

#include <mutex>

Uint16 *Map::getResourceGradient(int teamNumber, int resourceType, int swimClass)
{
	// AI workers may request the same lazy field concurrently. Cover both
	// allocation and pipeline invalidation before publishing the pointer.
	std::lock_guard<std::mutex> lock(resourcesGradientMutex);
	Uint16 *&gradient = resourcesGradient[teamNumber][resourceType][swimClass];
	if (gradient == NULL)
	{
		gradient = new Uint16[size];
		updateResourcesGradient(teamNumber, resourceType, swimClass);
	}
	return gradient;
}

void Map::updateResourcesGradient(int teamNumber, Uint8 resourceType, int swimClass)
{
	PERF_SCOPE_TIME(ResourceGradient);
	gradientRuntime->pipeline.invalidate(&resourcesGradient[teamNumber][resourceType][swimClass]);
	Uint16 *gradient = resourcesGradient[teamNumber][resourceType][swimClass];
	seedResourcesGradient(teamNumber, resourceType, swimClass, gradient);
	propagateGradient(gradient, swimClass);
}

void Map::seedResourcesGradient(int teamNumber, Uint8 resourceType, int swimClass, Uint16 *gradient)
{
	assert(gradient);
	bool canSwim = swimClass > 0;

	const Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	assert(globalContainer);
	// Only fogged resources of a type that must be seen to be collected are hidden.
	const bool hideFogged = globalContainer->resourcesTypes.get(resourceType)->visibleToBeCollected;
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
			if (c.building!=NOGBID || (!terrainPropertiesAt(i).walkable && !(canSwim && terrainPropertiesAt(i).swimmable)))
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

}
