// SPDX-License-Identifier: GPL-3.0-or-later
#include "SceneMap.h"

#include "Map.h"

void SceneMap::extract(const Map &map)
{
	w = map.getW();
	h = map.getH();
	wMask = map.getMaskW();
	hMask = map.getMaskH();
	wDec = map.getShiftW();
	sourceIdentity = map.identity();
	sourceKey = &map;
	const size_t size = size_t(w) * h;
	terrain.resize(size);
	resources.resize(size);
	resourcesGrow.resize(size);
	for (size_t i = 0; i < size; ++i)
	{
		const Tile &tile = map.tiles[i];
		terrain[i] = tile.terrain;
		resources[i] = tile.resource;
		resourcesGrow[i] = tile.canResourcesGrow;
	}
	discovered.assign(map.mapDiscovered.begin(), map.mapDiscovered.end());
	if (map.fogOfWar)
		fogOfWar.assign(map.fogOfWar, map.fogOfWar + size);
	else
		fogOfWar.assign(size, 0);
	discovered.resize(size, 0);
	forbiddenView = map.displayedForbiddenView;
	guardAreaView = map.displayedGuardAreaView;
	clearAreaView = map.displayedClearAreaView;
}

bool SceneMap::isMapPartiallyDiscovered(int x1, int y1, int x2, int y2, Uint32 visionMask) const
{
	for (int x = x1; x <= x2; x++)
		for (int y = y1; y <= y2; y++)
			if (isMapDiscovered(x, y, visionMask))
				return true;
	return false;
}
