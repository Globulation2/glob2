// SPDX-License-Identifier: GPL-3.0-or-later
#include "SceneMap.h"

#include "Map.h"
#include <algorithm>

void SceneMap::extract(const Map &map) { extract(map, map.displayViewportW, map.displayViewportH); }

void SceneMap::extract(const Map &map, int displayW, int displayH, bool includeScriptAreas)
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
    terrainTypes = map.terrainTypes();
	resources.resize(size);
	resourcesGrow.resize(size);
	groundUnits.resize(size);
	airUnits.resize(size);
	buildings.resize(size);
	scriptAreas.resize(includeScriptAreas ? size : 0);
	for (size_t i = 0; i < size; ++i)
	{
		const Tile &tile = map.tiles[i];
		terrain[i] = tile.terrain;
		resources[i] = tile.resource;
		resourcesGrow[i] = tile.canResourcesGrow;
		groundUnits[i] = tile.groundUnit;
		airUnits[i] = tile.airUnit;
		buildings[i] = tile.building;
		if (includeScriptAreas)
		{
			scriptAreas[i] = 0;
			for (int n=0; n<9; ++n)
				if (map.isPointSet(n, int(i)&wMask, int(i>>wDec))) scriptAreas[i] |= 1 << n;
		}
	}
	undermap.resize(size);
	for (size_t i = 0; i < size; ++i)
		undermap[i] = Uint8(map.getUMTerrain(int(i) & wMask, int(i >> wDec)));
	displayViewportW = displayW;
	displayViewportH = displayH;
	discovered.assign(map.mapDiscovered.begin(), map.mapDiscovered.end());
	if (map.fogOfWar)
		fogOfWar.assign(map.fogOfWar, map.fogOfWar + size);
	else
		fogOfWar.assign(size, 0);
	discovered.resize(size, 0);
	forbiddenView = map.displayedForbiddenView;
	guardAreaView = map.displayedGuardAreaView;
	clearAreaView = map.displayedClearAreaView;
	farmAreaView = map.displayedFarmAreaView;
}

bool SceneMap::isMapPartiallyDiscovered(int x1, int y1, int x2, int y2, Uint32 visionMask) const
{
	for (int x = x1; x <= x2; x++)
		for (int y = y1; y <= y2; y++)
			if (isMapDiscovered(x, y, visionMask))
				return true;
	return false;
}

// Same conversions as Map's, reading the extracted viewport bounds.
void SceneMap::mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX, int viewportY) const
{
	int x = (mx - viewportX + w) & wMask;
	int y = (my - viewportY + h) & hMask;
	if (x > (w - 16) && x * 32 >= displayViewportW)
		x -= w;
	if (y > (h - 16) && y * 32 >= displayViewportH)
		y -= h;
	*px = x << 5;
	*py = y << 5;
}

void SceneMap::mapCaseToDisplayableVector(int mx, int my, int *px, int *py, int viewportX, int viewportY, int screenW, int screenH) const
{
	int x = (mx - viewportX + w) & wMask;
	int y = (my - viewportY + h) & hMask;
	if (x > (w / 2 + (screenW / 64)))
		x -= w;
	if (y > (h / 2 + (screenH / 64)))
		y -= h;
	*px = x << 5;
	*py = y << 5;
}
