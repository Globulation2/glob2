// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include "CortexTypes.h" // WHEAT_PARITY and the other wheat tunables.

#include "Brush.h"        // BrushAccumulator (the live wrapper builds the masks).
#include <SDL_stdinc.h>
#include <vector>

class Map;
class Player;

// AICortex wheat-sustainability geometry.
//
// Cortex paints a checkerboard `forbidden` pattern over its wheat (CORN) so
// workers harvest one half while the protected half stays full and reseeds it
// (forbidden blocks harvest, MapGradientGlobal.cpp:135, but NOT growth,
// MapStep.cpp:80). See docs/AI/cortex/wheat-protection-plan.md.
//
// Two layers live here:
//   * scanWheatForbidden(...) — the PURE geometry + reconcile core. It takes
//     explicit inputs (no Player*/Game*), so the same code serves both the live
//     path and the headless `-dump-wheat` debug tool. It emits no Orders.
//   * reconcileWheatForbidden(Player*, ...) — the live wrapper. It derives the
//     team mask, consumer (inn) seeds, and colony region from the player, calls
//     scanWheatForbidden, and (when asked) accumulates the ADD/DEL tile lists
//     into BrushAccumulators ready for OrderAlterateForbidden. It still emits no
//     Orders — the action layer (AICortex::translateAction) does that.
//
// Determinism (this runs inside lockstep): index-ordered scans, a 0-1 BFS with a
// fixed neighbour order, parity by (x+y)&1, and no rand()/syncRand() — N (the
// open margin) is supplied by the caller (drawn once, upstream, via syncRand).

namespace Cortex
{
	// Per-tile classification of CORN in the scanned territory, for the debug
	// overlay and for deriving the desired forbidden set.
	enum WheatClass
	{
		WC_NONE         = 0, //!< not reachable field wheat (or non-CORN)
		WC_OPEN_MARGIN  = 1, //!< depth <= openMargin: harvestable, never painted
		WC_CHECKER_OPEN = 2, //!< depth > openMargin, (x+y)&1 != PARITY: harvest half
		WC_FORBIDDEN    = 3  //!< depth > openMargin, (x+y)&1 == PARITY: painted wall
	};

	struct WheatScanResult
	{
		// Map-index tile lists. `desired` is the set we want forbidden this cycle.
		std::vector<int> desired;
		// Reconcile diffs against the team's CURRENT forbidden paint (our building
		// footprints excluded, since the engine auto-forbids those):
		std::vector<int> add; //!< desired - current
		std::vector<int> del; //!< current - desired; fog & visible-unreachable wheat keep paint
		Sint32 addCount = 0;
		Sint32 delCount = 0;
		Sint32 forbiddenCount = 0; //!< == desired.size()
		Sint32 openCount = 0;      //!< WC_OPEN_MARGIN tile count
		Sint32 fieldTileCount = 0; //!< reachable CORN tiles (all classes)
		Sint32 componentCount = 0; //!< connected components among reachable CORN
		// Debug overlays, sized map.getW()*map.getH() (empty unless wantDebug):
		std::vector<Uint8>  classOf; //!< WheatClass per map index
		std::vector<Sint16> depthOf; //!< CORN wheat-depth per map index, -1 = none
	};

	//! Compute the desired checkerboard forbidden set for one team's wheat, plus
	//! the ADD/DEL reconcile against the team's current paint.
	//!
	//!   teamMask      : our team bit (1<<teamNumber) — forbidden + FOW vision mask.
	//!   teamNumber    : used to exclude OUR building footprints from the current set.
	//!   consumerSeeds : map indices of consuming buildings (inn tiles), or a single
	//!                   start-position index as a fallback. The 0-1 BFS floods out
	//!                   from these over walkable terrain, counting only wheat tiles,
	//!                   so the open/protected bands run parallel to the harvest edge.
	//!   box*          : territory region (inclusive); clamped to the map.
	//!   openMargin    : N — CORN with wheat-depth <= N stays open (unpainted).
	//!   ignoreFOW     : true skips the isFOWDiscovered gate (static-map debug).
	//!   wantDebug     : true fills classOf/depthOf.
	//!   liftAll       : WHEAT-BLITZ override. When true, NO reachable wheat tile is
	//!                   classified WC_FORBIDDEN (treated WC_CHECKER_OPEN instead), so
	//!                   `desired` stays empty — the reconcile then un-forbids the WHOLE
	//!                   field (del = all current paint, add empty) for a one-time food
	//!                   burst. The index-ordered scan / BFS / add-del diff are otherwise
	//!                   identical, so determinism is preserved. Default false leaves
	//!                   every existing caller unchanged.
	WheatScanResult scanWheatForbidden(
		Map& map, Uint32 teamMask, int teamNumber,
		const std::vector<int>& consumerSeeds,
		int boxMinX, int boxMinY, int boxMaxX, int boxMaxY,
		int openMargin, bool ignoreFOW, bool wantDebug, bool liftAll = false);

	//! Result of the live reconcile: the diff counts plus (when buildMasks) the
	//! two BrushAccumulators ready to hand to OrderAlterateForbidden(MODE_ADD/DEL).
	struct WheatReconcile
	{
		Sint32 addCount = 0; //!< tiles to newly forbid (desired - current).
		Sint32 delCount = 0; //!< tiles to un-forbid (current - desired).
		BrushAccumulator add; //!< populated only when buildMasks == true.
		BrushAccumulator del; //!< populated only when buildMasks == true.
	};

	//! Live wrapper around scanWheatForbidden: derive the team mask, consumer
	//! (inn) seeds, and colony region from `player`, scan at open margin N, and
	//! return the reconcile diff. Always fills the counts; only builds the
	//! BrushAccumulator masks when `buildMasks` is true (the observation path
	//! wants counts only; the action path wants the masks). Emits no Orders.
	//! `liftAll` (default false) is passed through to scanWheatForbidden: when true
	//! the whole field is un-forbidden for the wheat-blitz food burst (only the DEL
	//! mask is non-empty). Default false keeps every existing caller unchanged.
	WheatReconcile reconcileWheatForbidden(Player* player, int openMargin, bool buildMasks,
	                                       bool liftAll = false);
}
