// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "MapInternal.h"
#include "field/Influence.h"

// Chamfer distance transform with orthogonal=1, diagonal=1 weights (Chebyshev
// distance) on a toroidal grid, for the AIs' own Uint8 helper maps (Castor,
// Warrush). The pathfinding gradients are Uint16 and built by
// Map::propagateGradient (MapGradientPropagation.cpp). Two sweeps per pass — forward (NW, N, NE, W)
// then backward (SE, S, SW, E) — repeated until a full pass writes nothing.
//
// Cell value semantics:
//   - 0 = obstacle, never written
//   - 1 = free, no source contribution
//   - >= 3 = propagation source: a neighbor with value vN lifts this cell to
//     vN - 1 if larger. Floor of 3 ensures cand = vN - 1 >= 2 strictly
//     exceeds a free cell's seed of 1.
//   - 255 is pinned; weaker sources may be raised by stronger contributions.
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
	PERF_SCOPE_TIME(Propagation);
    updateGlobalGradientTask(gradient).run();
}

GAGCore::CooperativeTask Map::updateGlobalGradientTask(Uint8 *gradient)
{
	if(size==0)co_return true;
	field::ConvergentInfluence influence(gradient,{w,h});
	if (!influence.hasSources()) co_return true;
	int passes=0;
	do
	{
		influence.beginPass();
		for (size_t y=0;y<(size_t)h;++y)
		{
			if ((y & 15) == 0) co_await GAGCore::CooperativeTask::checkpoint("[Building gradients]");
			influence.forwardRow(int(y));
		}
		for (size_t y=(size_t)h;y-- > 0;)
		{
			if ((y & 15) == 0) co_await GAGCore::CooperativeTask::checkpoint("[Building gradients]");
			influence.reverseRow(int(y));
		}
		if (++passes >= 256)
		{
			fprintf(stderr, "[chamfer] passes >= 256 - monotonicity violated. w=%d h=%d size=%zu\n",
				(int)w, (int)h, size);
			abort();
		}
	} while (influence.passChanged());
	co_return true;
}
