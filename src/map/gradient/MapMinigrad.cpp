// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Unit.h"
#include "MapInternal.h"

#include <algorithm>
#include <valarray>
#include <Stream.h>
#include <queue>


// 5x5 minigrad direction queries (directionFromMinigrad, directionByMinigrad)

namespace {

constexpr int MINIGRAD_W            = 5;
constexpr int MINIGRAD_AREA         = MINIGRAD_W * MINIGRAD_W;          // 25
constexpr int MINIGRAD_CENTER_COORD = MINIGRAD_W / 2;                   // 2
constexpr int MINIGRAD_CENTER_INDEX = MINIGRAD_CENTER_COORD             // 12
                                    + MINIGRAD_CENTER_COORD * MINIGRAD_W;
constexpr int MINIGRAD_DIRECTIONS   = 8;
constexpr int MINIGRAD_DIAGONAL_FAR = 5;

// Convert a (col, row) offset relative to the centre of the 5x5 minigrad
// into a linear index into miniGrad[].  rx, ry are in [-2, 2].
constexpr int minigradIndex(int rx, int ry)
{
	return (MINIGRAD_CENTER_COORD + rx) + (MINIGRAD_CENTER_COORD + ry) * MINIGRAD_W;
}

// Eight directions probed by directionFromMinigrad, in scoring order.
// Diagonals first (indices 0..3), then cardinals (4..7).  The scoring loop
// uses `if (maxg <= g)` so later indices win ties — cardinals therefore
// beat diagonals on equal scores.  Re-ordering this table would shift
// tie-break outcomes and diverge replays; preserve the original layout.
//   centre        : inner-ring cell (one step from grid centre) whose own
//                   gradient seeds the direction's score.
//   far / farCount: outer-ring arc beyond `centre`.  A diagonal walks a
//                   5-cell L; a cardinal walks a 3-cell row.
//   canonicalDir  : value passed to Unit::dxDyFromDirection when this
//                   direction wins.  Matches the original maxd->stdd map:
//                   diagonals 0..3 -> 0,2,4,6 ; cardinals 4..7 -> 1,3,5,7.
struct MinigradDirection {
	int centreCol;
	int centreRow;
	int farCount;
	int far[MINIGRAD_DIAGONAL_FAR][2];
	int canonicalDir;
};

constexpr MinigradDirection minigradDirections[MINIGRAD_DIRECTIONS] = {
	// NW diagonal
	{ 1, 1, 5, { {0,2}, {0,1}, {0,0}, {1,0}, {2,0} }, 0 },
	// NE diagonal
	{ 3, 1, 5, { {2,0}, {3,0}, {4,0}, {4,1}, {4,2} }, 2 },
	// SE diagonal
	{ 3, 3, 5, { {4,2}, {4,3}, {4,4}, {3,4}, {2,4} }, 4 },
	// SW diagonal
	{ 1, 3, 5, { {2,4}, {1,4}, {0,4}, {0,3}, {0,2} }, 6 },
	// N cardinal — far slots 3,4 unused (farCount=3).
	{ 2, 1, 3, { {1,0}, {2,0}, {3,0}, {0,0}, {0,0} }, 1 },
	// E cardinal
	{ 3, 2, 3, { {4,1}, {4,2}, {4,3}, {0,0}, {0,0} }, 3 },
	// S cardinal
	{ 2, 3, 3, { {1,4}, {2,4}, {3,4}, {0,0}, {0,0} }, 5 },
	// W cardinal
	{ 1, 2, 3, { {0,1}, {0,2}, {0,3}, {0,0}, {0,0} }, 7 },
};

// Score one direction: pack (max << 8) | mxd, where mxd is the centre
// cell's own gradient and max is min-clamped to 1 then maxed over the
// far arc — but only when the centre is actively propagating (neither
// forbidden nor at-goal).  Otherwise max == mxd and the score collapses
// to the centre value duplicated into both bytes.
inline Uint32 scoreMinigradDirection(const Uint8 miniGrad[MINIGRAD_AREA],
                                     const MinigradDirection& dir)
{
	const Uint8 mxd = miniGrad[dir.centreCol + dir.centreRow * MINIGRAD_W];
	Uint8 max = mxd;
	if (max && max != GRADIENT_AT_GOAL)
	{
		max = 1;
		for (int i = 0; i < dir.farCount; ++i)
			UPDATE_MAX(max, miniGrad[dir.far[i][0] + dir.far[i][1] * MINIGRAD_W]);
	}
	return (static_cast<Uint32>(max) << 8) | mxd;
}

} // namespace

bool Map::directionFromMinigrad(Uint8 miniGrad[25], int *dx, int *dy, const bool strict) const
{
	Uint32 maxs[MINIGRAD_DIRECTIONS];
	for (int d = 0; d < MINIGRAD_DIRECTIONS; ++d)
		maxs[d] = scoreMinigradDirection(miniGrad, minigradDirections[d]);

	int centerg = miniGrad[MINIGRAD_CENTER_INDEX];
	centerg = (centerg << 8) | centerg;
	int maxg = 0;
	int maxd = MINIGRAD_DIRECTIONS;  // sentinel; only reachable if every maxs[d] is 0
	bool good = false;
	for (int d = 0; d < MINIGRAD_DIRECTIONS; ++d)
	{
		int g = maxs[d];
		if (strict ? (g > centerg) : (g && g != centerg))
			good = true;
		if (maxg <= g)
		{
			maxg = g;
			maxd = d;
		}
	}

	if (!good)
		return false;

	const int stdd = (maxd < MINIGRAD_DIRECTIONS) ? minigradDirections[maxd].canonicalDir : 8;
	Unit::dxDyFromDirection(stdd, dx, dy);
	return true;
}

bool Map::directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int *dx, int *dy, const Uint8 *gradient, bool strict) const
{
	Uint8 miniGrad[MINIGRAD_AREA];
	miniGrad[MINIGRAD_CENTER_INDEX] = gradient[x + y * w];
	for (int di = 0; di < 16; di++)
	{
		int rx = tabFar[di][0];
		int ry = tabFar[di][1];
		int xg = x + rx;
		int yg = y + ry;
		int g = gradient[coordToIndex(xg, yg)];
		if (g == GRADIENT_FORBIDDEN || g == GRADIENT_AT_GOAL || isFreeForGroundUnit(xg, yg, canSwim, teamMask))
			miniGrad[minigradIndex(rx, ry)] = g;
		else
			miniGrad[minigradIndex(rx, ry)] = GRADIENT_FORBIDDEN;
	}
	for (int di = 0; di < 8; di++)
	{
		int rx = tabClose[di][0];
		int ry = tabClose[di][1];
		int xg = x + rx;
		int yg = y + ry;
		int g = gradient[coordToIndex(xg, yg)];
		if (g == GRADIENT_FORBIDDEN || isFreeForGroundUnit(xg, yg, canSwim, teamMask))
			miniGrad[minigradIndex(rx, ry)] = g;
		else
			miniGrad[minigradIndex(rx, ry)] = GRADIENT_FORBIDDEN;
	}
	return directionFromMinigrad(miniGrad, dx, dy, strict);
}

bool Map::directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int bx, int by, int *dx, int *dy, Uint8 localGradient[1024], bool strict) const
{
	Uint8 miniGrad[MINIGRAD_AREA];
	for (int ry = 0; ry < MINIGRAD_W; ry++)
		for (int rx = 0; rx < MINIGRAD_W; rx++)
		{
			int gx = (x + rx - MINIGRAD_CENTER_COORD) & wMask;
			int gy = (y + ry - MINIGRAD_CENTER_COORD) & hMask;
			int lx = (x - bx + LOCAL_GRID_CENTER + rx - MINIGRAD_CENTER_COORD) & wMask;
			int ly = (y - by + LOCAL_GRID_CENTER + ry - MINIGRAD_CENTER_COORD) & hMask;
			if (lx == wMask)
			{
				gx = (gx + 1) & wMask;
				lx = 0;
			}
			else if (lx == LOCAL_GRID_W)
			{
				gx = (gx - 1) & wMask;
				lx = LOCAL_GRID_W - 1;
			}
			if (ly == hMask)
			{
				gy = (gy + 1) & hMask;
				ly = 0;
			}
			else if (ly == LOCAL_GRID_W)
			{
				gy = (gy - 1) & hMask;
				ly = LOCAL_GRID_W - 1;
			}
			assert(lx >= 0);
			assert(ly >= 0);
			assert(lx < LOCAL_GRID_W);
			assert(ly < LOCAL_GRID_W);
			int g = localGradient[lx + (ly << LOCAL_GRID_SHIFT)];
			if (g == GRADIENT_FORBIDDEN || g == GRADIENT_AT_GOAL
			    || (rx == MINIGRAD_CENTER_COORD && ry == MINIGRAD_CENTER_COORD)
			    || isFreeForGroundUnit(gx, gy, canSwim, teamMask))
				miniGrad[rx + ry * MINIGRAD_W] = g;
			else
				miniGrad[rx + ry * MINIGRAD_W] = GRADIENT_FORBIDDEN;
		}
	for (int ry = 1; ry <= 3; ry++)
		for (int rx = 1; rx <= 3; rx++)
			if (miniGrad[rx + ry * MINIGRAD_W] == GRADIENT_AT_GOAL)
			{
				int gx = (x + rx - MINIGRAD_CENTER_COORD) & wMask;
				int gy = (y + ry - MINIGRAD_CENTER_COORD) & hMask;
				if (!isFreeForGroundUnit(gx, gy, canSwim, teamMask))
					miniGrad[rx + ry * MINIGRAD_W] = GRADIENT_FORBIDDEN;
			}
	return directionFromMinigrad(miniGrad, dx, dy, strict);
}


