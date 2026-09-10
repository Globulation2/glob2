// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "MapInternal.h"

#include <algorithm>

// Chamfer distance transform with orthogonal=1, diagonal=1 weights (Chebyshev
// distance) on a toroidal grid, for the AIs' own Uint8 helper maps (Castor,
// Warrush). The pathfinding gradients are Uint16 and built by
// Map::propagateGradient (MapGradientField.cpp). Two sweeps per pass — forward (NW, N, NE, W)
// then backward (SE, S, SW, E) — repeated until a full pass writes nothing.
//
// Cell value semantics:
//   - 0 = obstacle, never written
//   - 1 = free, no source contribution
//   - >= 3 = propagation source: a neighbor with value vN lifts this cell to
//     vN - 1 if larger. Floor of 3 ensures cand = vN - 1 >= 2 strictly
//     exceeds a free cell's seed of 1.
//   - sources keep their seed value (cand = vN - 1 < vN never raises it).
//
// Convergence bound: Borgefors 1986 establishes one forward+backward pass
// suffices on a non-toroidal grid *without obstacles*. With obstacles forcing
// path bends and a toroidal wraparound seam, the per-pass propagation can
// only advance along one scan direction, so a path with K direction changes
// (e.g. snaking around a mountain) needs ~K/2 passes. Empirically the gradient
// corpus and G2.game converge in well under 32 passes.
//
// The cap is set to 256 — every write strictly increases a cell value
// (monotonicity), values are bounded by 255, and the propagation floor is 2,
// so a single propagation chain is at most 253 cells long. 256 is the
// theoretical ceiling: any chain longer than that violates monotonicity, so
// the cap acts as a tripwire for that invariant rather than a real-workload
// throttle. On correct code the loop exits in a handful of passes.
void Map::updateGlobalGradient(Uint8 *gradient)
{
	// Values below 3 cannot raise a free cell above its seed of 1.
	// Without a stronger source, the initialized buffer is already the final field.
	if (std::none_of(gradient, gradient + size, [](Uint8 value) { return value >= 3; }))
		return;

	int passes = 0;
	bool changed;
	do
	{
		changed = false;

		// Only the strongest neighbor matters: subtracting one preserves their order.
		// Keep the in-place sweep order, including reads across the toroidal seams.
		for (size_t y = 0; y < (size_t)h; y++)
		{
			Uint8* row = gradient + (y << wDec);
			const Uint8* previousRow = gradient + (((y - 1) & hMask) << wDec);
			for (size_t x = 0; x < (size_t)w; x++)
			{
				const Uint8 g = row[x];
				// Obstacles stay zero; a goal is already at the maximum possible value.
				if (g == GRADIENT_FORBIDDEN || g == 255)
					continue;
				const size_t xl = (x - 1) & wMask;
				const size_t xr = (x + 1) & wMask;
				// Forward neighbors: NW, N, NE, W.
				const Uint8 neighborMax = std::max(std::max(previousRow[xl], previousRow[x]),
					std::max(previousRow[xr], row[xl]));
				if (neighborMax >= 3 && neighborMax - 1 > g)
				{
					row[x] = neighborMax - 1;
					changed = true;
				}
			}
		}

		for (size_t y = (size_t)h; y-- > 0; )
		{
			Uint8* row = gradient + (y << wDec);
			const Uint8* nextRow = gradient + (((y + 1) & hMask) << wDec);
			for (size_t x = (size_t)w; x-- > 0; )
			{
				const Uint8 g = row[x];
				if (g == GRADIENT_FORBIDDEN || g == 255)
					continue;
				const size_t xl = (x - 1) & wMask;
				const size_t xr = (x + 1) & wMask;
				// Backward neighbors: SE, S, SW, E.
				const Uint8 neighborMax = std::max(std::max(nextRow[xr], nextRow[x]),
					std::max(nextRow[xl], row[xr]));
				if (neighborMax >= 3 && neighborMax - 1 > g)
				{
					row[x] = neighborMax - 1;
					changed = true;
				}
			}
		}

		passes++;
		if (passes >= 256)
		{
			fprintf(stderr, "[chamfer] passes >= 256 - monotonicity violated. w=%d h=%d size=%zu\n",
				(int)w, (int)h, size);
			abort();
		}
	} while (changed);
}


Uint16 *Map::getResourceGradient(int teamNumber, int resourceType, int swimClass)
{
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
	Uint16 *gradient=resourcesGradient[teamNumber][resourceType][swimClass];
	seedResourcesGradient(teamNumber, resourceType, swimClass, gradient);
	propagateGradient(gradient, swimClass);
}

void Map::seedResourcesGradient(int teamNumber, Uint8 resourceType, int swimClass, Uint16 *gradient)
{
	assert(gradient);
	bool canSwim = swimClass > 0;

	Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	assert(globalContainer);
	for (size_t i=0; i<size; i++)
	{
		const Tile& c=tiles[i];
		if (c.forbidden & teamMask)
			gradient[i]=GRADIENT_FORBIDDEN;
		else if(immobileUnits[i] != IMMOBILE_UNIT_NONE)
			gradient[i]=GRADIENT_FORBIDDEN;
		else if (c.resource.type==NO_RES_TYPE)
		{
			if (c.building!=NOGBID)
				gradient[i]=GRADIENT_FORBIDDEN;
			else if (!canSwim && isWater(i))
				gradient[i]=GRADIENT_FORBIDDEN;
			else
				gradient[i]=GRADIENT_UNREACHABLE;
		}
		else if (c.resource.type==resourceType)
		{
			if (globalContainer->resourcesTypes.get(resourceType)->visibleToBeCollected && !(fogOfWar[i]&teamMask))
				gradient[i]=GRADIENT_FORBIDDEN;
			else
				gradient[i]=GRADIENT_AT_GOAL;
		}
		else
			gradient[i]=GRADIENT_FORBIDDEN;
	}

}
