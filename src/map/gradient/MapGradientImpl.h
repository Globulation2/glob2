// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Private header: body of Map::updateGlobalGradient. The chamfer template
// definition has to be visible at every cross-TU call site (Building, Area,
// Global, Resource), which is why it lives here rather than in a .cpp.
#pragma once
#include "Map.h"
#include "MapInternal.h"

// LogFileManager.h does `#define fprintf if(false)fprintf` to silence stale
// log code. We need real fprintf for the non-convergence dump below, so undo
// the macro defensively if any include drags it in.
#ifdef fprintf
#undef fprintf
#endif

// Chamfer distance transform with orthogonal=1, diagonal=1 weights (Chebyshev
// distance). Two sweeps per pass — forward (NW, N, NE, W) then backward
// (SE, S, SW, E) — repeated until a full pass writes nothing.
//
// Saturation rules:
//   - cells with value 0 are obstacles and never written
//   - sources keep their seed value (a propagation candidate cand = vn - 1
//     is always lower than vn, never raises a higher cell)
//   - propagation floor is 2: a neighbor whose value is < 3 cannot lift this
//     cell (cand = vn - 1 must be >= 2 to exceed any free cell's seed of 1).
template<typename Tint> void Map::updateGlobalGradient(Uint8 *gradient, GradientType /*gradientType*/, bool /*canSwim*/)
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
		if (passes >= 32)
		{
			FILE* dump = fopen("/tmp/glob2-gradient-divergence.txt", "w");
			if (dump)
			{
				fprintf(dump, "[chamfer] passes >= 32 - algorithm not converging. w=%d h=%d size=%zu\n",
					(int)w, (int)h, size);
				fclose(dump);
			}
			abort();
		}
	} while (changed);
}
