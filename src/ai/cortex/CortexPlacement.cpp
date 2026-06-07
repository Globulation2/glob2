// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexPlacement.h"

#include "CortexPlacementGeo.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Utilities.h"
#include "building/Building.h"
#include "game/entities/BuildingType.h"
#include "map/Map.h"
#include "team/Team.h"

// AICortex placement/scoring helper.
//
// This is the spatial-reasoning slice the direct binding does not inherit from
// Echo. It answers "where could I put a building of this type?", ranked
// best-first, and surfaces a bounded set of candidates so the policy never has
// to emit an unbounded (x, y) action.
//
// We deliberately reuse the engine's own placement machinery rather than invent
// a new one:
//
//   * Footprint + corner convention come from BuildingType (width/height) and
//     match how AIEcho's BuildingOrder::find_location scans (ai/echo/
//     BuildingOrder.cpp:104-168) and how it feeds the result straight into an
//     OrderCreate (ai/echo/Echo.cpp:176). The (x, y) we return is the tile of
//     the footprint's top-left corner, exactly the coordinate OrderCreate
//     consumes (see also gui/GameGUIToolManager.cpp:387 and Order.h:75).
//
//   * The validity gate is the engine's canonical predicate for a real
//     (non-virtual) building: Map::isHardSpaceForBuilding(x, y, w, h). That is
//     precisely what Game::checkHardRoomForBuilding(x, y, bt) calls
//     (Game_editor.cpp:401-407) on the GUI build path, so any candidate we emit
//     will pass the same check the engine runs before accepting an OrderCreate.
//     We additionally gate on fog-of-war discovery of the footprint corners,
//     mirroring find_location's isMapDiscovered check, so the order is not
//     silently dropped for being placed on unseen terrain.
//
// Scoring (hand-crafted, seeds the later ML/hand-rules policy): prefer compact
// colonies. A candidate's base score is higher the closer it sits to the
// nearest existing live building of the team (Chebyshev / warpDistMax), with a
// small bonus when the footprint borders a resource tile (the cheap stand-in
// for Echo's resource gradients). Higher score == better.
//
// Determinism (this runs inside lockstep): we iterate the team building array by
// index (never an std::set), scan map tiles in fixed (x, y) order, break ties
// strictly by scan order (first-seen wins on equal score), and use syncRand()
// only as a final, fully-deterministic tie-break when even the score AND the
// distance-to-colony are identical, so two equally good far-apart spots do not
// always collapse to the lowest coordinate. We never read wall-clock or pointer
// identity. BH-400 note: Echo's find_location reports "no placement" and "best
// at (0,0)" with the same (0,0) return; we never inherit that ambiguity because
// emptiness is carried by the valid flag / return count, never by testing for
// (0,0).

namespace Cortex
{
	namespace
	{
		// Same strict-greater, first-seen-wins ranked insert as insertTopK, but
		// bounded at an arbitrary K passed by the caller. placeFlagTargets needs
		// K == CORTEX_FLAG_TARGETS (8) whereas insertTopK is hardwired to
		// CORTEX_BUILD_CANDIDATES (4); rather than retune the build path we keep a
		// parallel, parameterized version so both K values stay deterministic.
		void insertTopKBounded(ScoredSpot* heap, int& count, int k, const ScoredSpot& spot)
		{
			// First slot whose score is strictly less than the new spot's. Ties
			// keep the incumbent (earlier scan order), so we only move past >= score.
			int pos = count;
			for (int i = 0; i < count; i++)
			{
				if (spot.score > heap[i].score)
				{
					pos = i;
					break;
				}
			}

			if (pos >= k)
				return; // not good enough to make the top-K

			// Shift lower-ranked entries down by one, dropping the tail if full.
			int last = (count < k) ? count : (k - 1);
			for (int i = last; i > pos; i--)
				heap[i] = heap[i - 1];

			heap[pos] = spot;
			if (count < k)
				count++;
		}
	} // namespace

	// Chebyshev distance from tile (x, y) to the nearest CORN (wheat) resource
	// tile, found by an outward "ring" scan capped at `cap`. Shared utility used
	// by placeCandidates (wheatDist of each retained BuildCandidate) and by
	// Cortex::observe (TrackedBuilding::nearestWheatDist for each tracked swarm
	// and inn), so the wheat-proximity metric is defined in exactly one place.
	//
	// Algorithm: for r = 0, 1, 2, ..., cap, iterate every tile at EXACTLY
	// Chebyshev distance r from (x, y) — the square ring of side 2r+1. Return
	// the first r at which a CORN tile is found. The scan terminates immediately
	// on the first hit at the current radius, not at the first hit overall, so we
	// never report a radius larger than the true minimum. Return -1 if no CORN is
	// found within `cap`.
	//
	// CORN detection: map.getRessource(x, y).type == CORN — identical to the
	// isCorn() predicate in CortexWheat.cpp (anonymous namespace, line ~29) so
	// the two subsystems agree on what counts as wheat.
	// C++: Ressource.h:#define CORN 1; Map::getRessource (map/Map.h:302).
	//
	// Determinism: fixed ring/scan order (top row → right col → bottom row →
	// left col, no rand, no pointer reads), warp-safe via normalizeX/normalizeY.
	int nearestCornDist(const Map& map, int x, int y, int cap)
	{
		for (int r = 0; r <= cap; r++)
		{
			if (r == 0)
			{
				// Centre tile: radius-0 ring is just (x, y) itself.
				if (map.getRessource(map.normalizeX(x), map.normalizeY(y)).type == CORN)
					return 0;
				continue;
			}

			// Iterate the square ring at Chebyshev distance exactly r.
			// The ring has four sides; adjacent corners are shared — use half-
			// open intervals to avoid double-counting corners.
			//
			// Top row:    y - r,  x in [x-r, x+r)   (left-to-right, right col excluded)
			// Right col:  x + r,  y in [y-r, y+r)   (top-to-bottom, bottom row excluded)
			// Bottom row: y + r,  x in (x-r, x+r]   (right-to-left, left col excluded)
			// Left col:   x - r,  y in (y-r, y+r]   (bottom-to-top, top row excluded)

			// Top row: (x-r .. x+r-1, y-r)
			for (int dx = -r; dx < r; dx++)
			{
				const int nx = map.normalizeX(x + dx);
				const int ny = map.normalizeY(y - r);
				if (map.getRessource(nx, ny).type == CORN)
					return r;
			}
			// Right column: (x+r, y-r .. y+r-1)
			for (int dy = -r; dy < r; dy++)
			{
				const int nx = map.normalizeX(x + r);
				const int ny = map.normalizeY(y + dy);
				if (map.getRessource(nx, ny).type == CORN)
					return r;
			}
			// Bottom row: (x+r .. x-r+1, y+r) — right-to-left
			for (int dx = r; dx > -r; dx--)
			{
				const int nx = map.normalizeX(x + dx);
				const int ny = map.normalizeY(y + r);
				if (map.getRessource(nx, ny).type == CORN)
					return r;
			}
			// Left column: (x-r, y+r .. y-r+1) — bottom-to-top
			for (int dy = r; dy > -r; dy--)
			{
				const int nx = map.normalizeX(x - r);
				const int ny = map.normalizeY(y + dy);
				if (map.getRessource(nx, ny).type == CORN)
					return r;
			}
		}
		return -1; // no CORN within cap tiles
	}

	// AICortex war-flag offense-target surface.
	//
	// Answers "which enemy buildings should our warriors attack next?", ranked so
	// slot 0 is the closest reachable target — the one a war flag would summon
	// warriors onto fastest. The policy then picks a target by slot index, never
	// an unbounded coordinate (same discrete-action rule as placeCandidates).
	//
	// FAIRNESS GATE (no fog-of-war cheat): we only ever consider enemy buildings
	// the team has legitimately discovered. Each Building carries the engine's own
	// per-team discovery record, seenByMask (building/Building.h:560), and we
	// include a building ONLY when (b->seenByMask & team->me) != 0. We never read
	// unfogged enemy state — an undiscovered enemy base is invisible to this scan,
	// exactly as it is on the player's minimap. The same enemy/alive test as the
	// observation opponents loop (CortexObservation.cpp:171-172) selects which
	// teams to scan: an enemy is (team->enemies & other->me) != 0 and alive is
	// other->isAlive.
	//
	// SCORING (nearer == higher): we reuse scoreFromDistance on the Chebyshev
	// distance from the enemy building to our NEAREST live building
	// (distanceToNearestBuilding, passed OUR team). Its "near my colony" semantics
	// are exactly reachability for an offense flag, so a closer enemy building
	// scores higher and lands in an earlier slot. If we somehow have no buildings
	// (mid-game this should not happen) distanceToNearestBuilding returns -1 and
	// scoreFromDistance falls back to a flat base, so every discovered target still
	// gets a well-defined, equal score and ranking degrades to scan order.
	//
	// DETERMINISM: teams are iterated by index over game->teams[], buildings by
	// index over other->myBuildings[] (never an std::set); ties break first by scan
	// order (strict-greater insert) and finally by syncRand() — never rand(), never
	// wall-clock — exactly as placeCandidates does.
	int placeFlagTargets(Game* game, Team* team, BuildCandidate out[CORTEX_FLAG_TARGETS])
	{
		// Always leave the output well-defined, even on the error paths below.
		for (int i = 0; i < CORTEX_FLAG_TARGETS; i++)
		{
			out[i].valid = 0;
			out[i].x = 0;
			out[i].y = 0;
			out[i].score = 0;
		}

		if (game == NULL || team == NULL)
			return 0;

		ScoredSpot heap[CORTEX_FLAG_TARGETS];
		int count = 0;

		// Enumerate enemy teams strictly by index.
		for (int i = 0; i < game->teamsCount(); i++)
		{
			Team* other = game->teams[i];
			if (other == NULL)
				continue;
			const bool isEnemy = (team->enemies & other->me) != 0;
			if (!isEnemy || !other->isAlive)
				continue;

			// Scan this enemy's buildings by index (never an std::set).
			for (int j = 0; j < Building::MAX_COUNT; j++)
			{
				Building* b = other->myBuildings[j];
				if (b == NULL || b->buildingState == Building::DEAD)
					continue;

				// Fairness gate: only buildings we have legitimately seen. An
				// undiscovered enemy building is invisible to this scan.
				if ((b->seenByMask & team->me) == 0)
					continue;

				// Distance from the enemy building to our nearest live building;
				// nearer enemies score higher (slot 0 == closest reachable).
				const int distToColony = distanceToNearestBuilding(game, team, b->posX, b->posY);
				const int score = scoreFromDistance(distToColony);

				ScoredSpot spot;
				spot.x = b->posX;
				spot.y = b->posY;
				spot.score = score;
				spot.distToColony = distToColony;
				insertTopKBounded(heap, count, CORTEX_FLAG_TARGETS, spot);
			}
		}

		// Deterministic final tie-break among retained targets that are
		// indistinguishable by both score AND distance-to-colony. The coin flip
		// comes from the lockstep RNG, so it is identical on every client and just
		// reorders equals — we never re-sort across distinct scores.
		for (int i = 0; i + 1 < count; i++)
		{
			if (heap[i].score == heap[i + 1].score &&
			    heap[i].distToColony == heap[i + 1].distToColony)
			{
				if ((syncRand() & 1) != 0)
				{
					ScoredSpot tmp = heap[i];
					heap[i] = heap[i + 1];
					heap[i + 1] = tmp;
				}
			}
		}

		for (int i = 0; i < count; i++)
		{
			out[i].valid = 1;
			out[i].x = heap[i].x;
			out[i].y = heap[i].y;
			out[i].score = heap[i].score;
		}

		return count;
	}
} // namespace Cortex
