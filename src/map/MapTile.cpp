// SPDX-License-Identifier: GPL-3.0-or-later
// Repeating the map arrays over the torus at game setup: see MapTiling.h.
#include "Map.h"

#include <cstring>
#include <string>
#include <vector>

void Map::tile(int rx, int ry)
{
	assert(arraysBuilt);
	int addWDec = 0, addHDec = 0;
	while ((1 << addWDec) < rx) addWDec++;
	while ((1 << addHDec) < ry) addHDec++;
	assert((1 << addWDec) == rx && (1 << addHDec) == ry);

	const int oldW = w, oldH = h, oldWDec = wDec;
	const auto oldResources = resourceCells;
	const auto oldLegacyTerrain = legacyTerrain;
	const auto oldScriptAreas = scriptAreaCells;
	const auto oldTerrain = terrainIds;
    const auto oldStockIndices=resourceStockIndices;
    const auto oldStocks=resourceStocks;
	const std::vector<Uint32> oldDiscovered = mapDiscovered;
	const std::vector<Uint8> oldUndermap(undermap, undermap + size);
	std::string names[9];
	for (int n = 0; n < 9; n++)
		names[n] = getAreaName(n);

	setSize(oldWDec + addWDec, hDec + addHDec);

	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			const size_t src = ((y % oldH) << oldWDec) + (x % oldW);
			const size_t dst = coordToIndex(x, y);
			// units and buildings are placed again per colony, and the
			// per-team zones belong to teams that are rebuilt
			resourceCells[dst] = oldResources[src];
			legacyTerrain[dst] = oldLegacyTerrain[src];
			scriptAreaCells[dst] = oldScriptAreas[src];
			terrainIds[dst] = oldTerrain[src];
			occupancyCells[dst].building = NOGBID;
			occupancyCells[dst].groundUnit = NOGUID;
			occupancyCells[dst].airUnit = NOGUID;
			areaCells[dst].forbidden = 0;
			areaCells[dst].guard = 0;
			areaCells[dst].clear = 0;
			areaCells[dst].farm = 0;
			mapDiscovered[dst] = oldDiscovered[src];
			undermap[dst] = oldUndermap[src];
		}
    rebuildResourceState();
    if (!oldStockIndices.empty())
        for (int y=0;y<h;++y) for (int x=0;x<w;++x)
        {
            const size_t src=((y%oldH)<<oldWDec)+(x%oldW);
            if (!oldStockIndices[src]) continue;
            const size_t dst=coordToIndex(x,y);
            const auto before=resourceMaterialMaskAt(dst);
            resourceStocks[resourceStockIndices[dst]-1]=oldStocks[oldStockIndices[src]-1];
            refreshResourceTotal(dst); materialStockChanged(dst,before);
        }
	rebuildTerrainCounts();
	finishTerrainEdit();
	for (int n = 0; n < 9; n++)
		setAreaName(n, names[n]);
}
