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
	const std::vector<Case> oldCases = cases;
	const std::vector<Uint32> oldDiscovered = mapDiscovered;
	const std::vector<Uint8> oldUndermap(undermap, undermap + size);
	std::string names[9];
	for (int n = 0; n < 9; n++)
		names[n] = getAreaName(n);

	setSize(oldWDec + addWDec, hDec + addHDec);
	// setSize marks every cell as held by an immobile unit, which the
	// building gradients read as an obstacle; a loaded map has none
	memset(immobileUnits, 255, size);

	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			const size_t src = ((y % oldH) << oldWDec) + (x % oldW);
			const size_t dst = coordToIndex(x, y);
			// units and buildings are placed again per colony, and the
			// per-team zones belong to teams that are rebuilt
			cases[dst] = oldCases[src];
			cases[dst].building = NOGBID;
			cases[dst].groundUnit = NOGUID;
			cases[dst].airUnit = NOGUID;
			cases[dst].forbidden = 0;
			cases[dst].guardArea = 0;
			cases[dst].clearArea = 0;
			mapDiscovered[dst] = oldDiscovered[src];
			undermap[dst] = oldUndermap[src];
		}
	for (int n = 0; n < 9; n++)
		setAreaName(n, names[n]);
}
