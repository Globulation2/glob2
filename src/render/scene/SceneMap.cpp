// SPDX-License-Identifier: GPL-3.0-or-later
#include "SceneMap.h"

#include "Map.h"
#include "Game.h"
#include "TerrainRegistry.h"
#include <algorithm>
#include <bit>

void SceneMap::extract(const Map &map)
{
	extract(map, map.displayViewportW, map.displayViewportH);
}

void SceneMap::extract(const Map &map, int displayW, int displayH, bool includeScriptAreas)
{
	tick = map.game ? map.game->stepCounter : 0;
	registry = map.frozenTerrainRegistry();
	resourceDefinitions = map.frozenResourceRegistry();
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
	terrainAppearances.resize(size);
	resources.resize(size);
	multiStocks.clear();
	if (!multiStockIndices.empty()) multiStockIndices.assign(size, UINT32_MAX);
	presentMaterials = 0;
	resourcesGrow.resize(size);
	groundUnits.resize(size);
	airUnits.resize(size);
	buildings.resize(size);
	scriptAreas.resize(includeScriptAreas ? size : 0);
	for (size_t i = 0; i < size; ++i)
	{
		const Tile &tile = map.getTile(i);
		terrainAppearances[i] = registry->appearance(terrainTypes[i]);
		terrain[i] = tile.terrain;
		resources[i] = tile.resource;
		if (tile.resource.type != NO_RES_TYPE)
		{
			const auto mask = resourceDefinitions->properties(static_cast<ResourceId>(tile.resource.type)).materialMask;
			presentMaterials |= mask;
			if (std::popcount(mask) > 1)
			{
				if (multiStockIndices.empty()) multiStockIndices.assign(size, UINT32_MAX);
				multiStockIndices[i] = multiStocks.size();
				auto& stock = multiStocks.emplace_back();
				for (unsigned m = 0; m < MaterialCount; ++m) stock[m] = map.materialAmountAt(i, m);
			}
		}
		resourcesGrow[i] = tile.canResourcesGrow;
		groundUnits[i] = tile.groundUnit;
		airUnits[i] = tile.airUnit;
		buildings[i] = tile.building;
		if (includeScriptAreas)
		{
			scriptAreas[i] = 0;
			for (int n = 0; n < 9; ++n)
				if (map.isPointSet(n, int(i) & wMask, int(i >> wDec)))
					scriptAreas[i] |= 1 << n;
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
void SceneMap::mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX,
									int viewportY) const
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

void SceneMap::mapCaseToDisplayableVector(int mx, int my, int *px, int *py, int viewportX,
										  int viewportY, int screenW, int screenH) const
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

SceneMap::SceneMap() : registry(TerrainRegistry::builtins()), resourceDefinitions(ResourceRegistry::availableDefaults()) {}

const TerrainPresentation &SceneMap::terrainPresentation(TerrainType type) const
{
	return registry->presentation(type);
}

bool SceneMap::isHardSpaceForBuilding(int x, int y, int w, int h) const
{
	for (int yi = y; yi < y + h; yi++)
		for (int xi = x; xi < x + w; xi++)
		{
			const size_t i = coordToIndex(xi, yi);
			if ((resources[i].type != NO_RES_TYPE && resourceDefinitions->properties(static_cast<ResourceId>(resources[i].type)).blocksBuilding) || buildings[i] != 0xFFFF ||
				!registry->properties(terrainTypes[i]).buildable)
				return false;
		}
	return true;
}

Uint16 SceneMap::materialAmountAt(size_t index, unsigned material) const
{
	if (!validMaterial(material) || resources[index].type == NO_RES_TYPE) return 0;
	const auto& p = resourceDefinitions->properties(static_cast<ResourceId>(resources[index].type));
	if (!(p.materialMask & (1u << material))) return 0;
	if (!multiStockIndices.empty() && multiStockIndices[index] != UINT32_MAX)
		return multiStocks[multiStockIndices[index]][material];
	return static_cast<Uint16>(resources[index].amount);
}
