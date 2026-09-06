// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "MapInternal.h"

// Chamfer distance transform with orthogonal=1, diagonal=1 weights (Chebyshev
// distance) on a toroidal grid. Two sweeps per pass — forward (NW, N, NE, W)
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
	int passes = 0;
	bool changed;
	do
	{
		changed = false;

		// Forward sweep: in-set neighbors are NW, N, NE, W (already visited).
		for (size_t y = 0; y < (size_t)h; y++)
		{
			size_t yu = ((y - 1) & hMask);
			for (size_t x = 0; x < (size_t)w; x++)
			{
				Uint8 g = gradient[(y << wDec) | x];
				if (g == 0)
					continue;
				size_t xl = ((x - 1) & wMask);
				size_t xr = ((x + 1) & wMask);
				Uint8 best = g;
				Uint8 vNW = gradient[(yu << wDec) | xl];
				Uint8 vN  = gradient[(yu << wDec) | x ];
				Uint8 vNE = gradient[(yu << wDec) | xr];
				Uint8 vW  = gradient[(y  << wDec) | xl];
				if (vNW >= 3 && (Uint8)(vNW - 1) > best) best = vNW - 1;
				if (vN  >= 3 && (Uint8)(vN  - 1) > best) best = vN  - 1;
				if (vNE >= 3 && (Uint8)(vNE - 1) > best) best = vNE - 1;
				if (vW  >= 3 && (Uint8)(vW  - 1) > best) best = vW  - 1;
				if (best != g)
				{
					gradient[(y << wDec) | x] = best;
					changed = true;
				}
			}
		}

		// Backward sweep: in-set neighbors are SE, S, SW, E (already visited).
		for (size_t y = (size_t)h; y-- > 0; )
		{
			size_t yd = ((y + 1) & hMask);
			for (size_t x = (size_t)w; x-- > 0; )
			{
				Uint8 g = gradient[(y << wDec) | x];
				if (g == 0)
					continue;
				size_t xl = ((x - 1) & wMask);
				size_t xr = ((x + 1) & wMask);
				Uint8 best = g;
				Uint8 vSE = gradient[(yd << wDec) | xr];
				Uint8 vS  = gradient[(yd << wDec) | x ];
				Uint8 vSW = gradient[(yd << wDec) | xl];
				Uint8 vE  = gradient[(y  << wDec) | xr];
				if (vSE >= 3 && (Uint8)(vSE - 1) > best) best = vSE - 1;
				if (vS  >= 3 && (Uint8)(vS  - 1) > best) best = vS  - 1;
				if (vSW >= 3 && (Uint8)(vSW - 1) > best) best = vSW - 1;
				if (vE  >= 3 && (Uint8)(vE  - 1) > best) best = vE  - 1;
				if (best != g)
				{
					gradient[(y << wDec) | x] = best;
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


void Map::updateRessourcesGradient(int teamNumber, Uint8 ressourceType, bool canSwim)
{
	Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
	assert(gradient);

	Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	assert(globalContainer);
	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.forbidden & teamMask)
			gradient[i]=GRADIENT_FORBIDDEN;
		else if(immobileUnits[i] != 255)
			gradient[i]=GRADIENT_FORBIDDEN;
		else if (c.ressource.type==NO_RES_TYPE)
		{
			if (c.building!=NOGBID)
				gradient[i]=GRADIENT_FORBIDDEN;
			else if (!canSwim && (c.terrain>=256 && c.terrain<16+256)) //!canSwim && isWater
				gradient[i]=GRADIENT_FORBIDDEN;
			else
				gradient[i]=GRADIENT_UNREACHABLE;
		}
		else if (c.ressource.type==ressourceType)
		{
			if (globalContainer->ressourcesTypes.get(ressourceType)->visibleToBeCollected && !(fogOfWar[i]&teamMask))
				gradient[i]=GRADIENT_FORBIDDEN;
			else
				gradient[i]=GRADIENT_AT_GOAL;
		}
		else
			gradient[i]=GRADIENT_FORBIDDEN;
	}

	updateGlobalGradient(gradient);
}

