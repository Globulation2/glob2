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
		// One scored candidate during the scan, before it is copied into the
		// caller's POD BuildCandidate array.
		struct ScoredSpot
		{
			int x;
			int y;
			int score;
			int distToColony; // secondary key for deterministic tie-breaking
		};

		// Returns true if any tile of the footprint [x, x+w) x [y, y+h) borders
		// (8-neighbourhood) a resource tile. Warp-safe via Map's normalization.
		bool footprintBordersResource(const Map& map, int x, int y, int w, int h)
		{
			for (int dx = -1; dx <= w; dx++)
				for (int dy = -1; dy <= h; dy++)
				{
					// Only inspect the one-tile ring around the footprint.
					const bool insideX = (dx >= 0 && dx < w);
					const bool insideY = (dy >= 0 && dy < h);
					if (insideX && insideY)
						continue;
					if (map.isRessource(map.normalizeX(x + dx), map.normalizeY(y + dy)))
						return true;
				}
			return false;
		}

		// Chebyshev distance from the footprint's top-left corner to the nearest
		// live building owned by `team`. Returns -1 when the team has no
		// buildings yet (first placement: distance is meaningless).
		int distanceToNearestBuilding(Game* game, Team* team, int x, int y)
		{
			int best = -1;
			for (int i = 0; i < Building::MAX_COUNT; i++)
			{
				Building* b = team->myBuildings[i];
				if (b == NULL || b->buildingState == Building::DEAD)
					continue;
				int d = game->map.warpDistMax(x, y, b->posX, b->posY);
				if (best < 0 || d < best)
					best = d;
			}
			return best;
		}

		// Chebyshev distance from tile (x, y) to the nearest live SWARM_BUILDING
		// owned by `team`. Returns -1 when the team has no swarms yet. Used to
		// enforce CORTEX_SWARM_MIN_SPACING so two swarms do not share one wheat
		// catchment. Mirrors distanceToNearestBuilding but filtered to swarms.
		// C++: shortTypeNum == IntBuildingType::SWARM_BUILDING (same test as
		// AICortex.cpp:324).
		int distanceToNearestSwarm(Game* game, Team* team, int x, int y)
		{
			int best = -1;
			for (int i = 0; i < Building::MAX_COUNT; i++)
			{
				Building* b = team->myBuildings[i];
				if (b == NULL || b->buildingState == Building::DEAD)
					continue;
				if (b->type == NULL || b->type->shortTypeNum != IntBuildingType::SWARM_BUILDING)
					continue;
				int d = game->map.warpDistMax(x, y, b->posX, b->posY);
				if (best < 0 || d < best)
					best = d;
			}
			return best;
		}

		// Chebyshev distance from tile (x, y) to the nearest live FOOD_BUILDING (inn)
		// owned by `team`. Returns -1 when the team has no inns yet. Used to enforce
		// CORTEX_INN_MIN_SPACING so inns do not pile on top of each other (workers
		// would contend for the same wheat and the colony loses feed coverage).
		// Mirrors distanceToNearestSwarm but filtered to inns.
		int distanceToNearestInn(Game* game, Team* team, int x, int y)
		{
			int best = -1;
			for (int i = 0; i < Building::MAX_COUNT; i++)
			{
				Building* b = team->myBuildings[i];
				if (b == NULL || b->buildingState == Building::DEAD)
					continue;
				if (b->type == NULL || b->type->shortTypeNum != IntBuildingType::FOOD_BUILDING)
					continue;
				int d = game->map.warpDistMax(x, y, b->posX, b->posY);
				if (best < 0 || d < best)
					best = d;
			}
			return best;
		}

		// Translate a Chebyshev distance-to-colony into a placement score.
		// Closer is better; we want a compact colony but not literally stacked,
		// so distance 0 (impossible for a real footprint anyway) and tiny
		// distances are fine, and the score decays linearly with distance.
		// A team with no buildings gets a flat base so the very first building
		// is placed on the first legal, discovered tile in scan order.
		int scoreFromDistance(int distToColony)
		{
			static const int FIRST_BUILDING_BASE = 1000;
			static const int NEAR_COLONY_BASE = 10000;

			if (distToColony < 0)
				return FIRST_BUILDING_BASE;
			// Subtract distance so nearer spots rank higher; clamp at 0 so very
			// far candidates do not go negative and disorder the ranking.
			int score = NEAR_COLONY_BASE - distToColony;
			if (score < 0)
				score = 0;
			return score;
		}

		// Insert one candidate into a best-first top-K buffer. Strict-greater
		// comparison preserves scan order on ties (first-seen wins), keeping the
		// ranking deterministic. `count` is updated in place.
		void insertTopK(ScoredSpot* heap, int& count, const ScoredSpot& spot)
		{
			// Find the insertion index: the first slot whose score is strictly
			// less than the new spot's. Ties keep the incumbent (earlier scan
			// order), so we only move past entries with >= score.
			int pos = count;
			for (int i = 0; i < count; i++)
			{
				if (spot.score > heap[i].score)
				{
					pos = i;
					break;
				}
			}

			if (pos >= CORTEX_BUILD_CANDIDATES)
				return; // not good enough to make the top-K

			// Shift lower-ranked entries down by one, dropping the tail if full.
			int last = (count < CORTEX_BUILD_CANDIDATES) ? count : (CORTEX_BUILD_CANDIDATES - 1);
			for (int i = last; i > pos; i--)
				heap[i] = heap[i - 1];

			heap[pos] = spot;
			if (count < CORTEX_BUILD_CANDIDATES)
				count++;
		}

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

	int placeCandidates(Game* game, Team* team, int buildingType, int level,
	                    BuildCandidate out[CORTEX_BUILD_CANDIDATES])
	{
		// Always leave the output well-defined, even on the error paths below.
		// wheatDist is initialised to -1 (no wheat in reach) matching the
		// makeEmptyObservation() sentinel so the policy never reads garbage on
		// valid == 0 slots.
		for (int i = 0; i < CORTEX_BUILD_CANDIDATES; i++)
		{
			out[i].valid = 0;
			out[i].x = 0;
			out[i].y = 0;
			out[i].score = 0;
			out[i].wheatDist = -1;
		}

		if (game == NULL || team == NULL)
			return 0;

		if (buildingType < 0 || buildingType >= IntBuildingType::NB_BUILDING)
			return 0;

		// Resolve the building footprint. We place the construction SITE (the
		// same as the GUI/Echo build path), so request isBuildingSite == true.
		// Flags (virtual buildings) have no site type and are not placed by this
		// helper — they occupy no ground, so isHardSpaceForBuilding is the wrong
		// gate for them. Bail out if there is no real building footprint here.
		const std::string& typeName = IntBuildingType::reverseConversionMap[buildingType];
		BuildingType* bt = globalContainer->buildingsTypes.getByType(typeName, level, true);
		if (bt == NULL || bt->isVirtual)
			return 0;

		const int w = bt->width;
		const int h = bt->height;
		if (w <= 0 || h <= 0)
			return 0;

		Map& map = game->map;
		const int mapW = map.getW();
		const int mapH = map.getH();

		ScoredSpot heap[CORTEX_BUILD_CANDIDATES];
		int count = 0;

		// Determine up front whether this building type is wheat-fed (swarm or
		// inn). SWARM_BUILDING == CORTEX_BUILD_SWARM == 0;
		// FOOD_BUILDING == CORTEX_BUILD_FOOD == 1.
		// C++: IntBuildingType enum (building/IntBuildingType.h:14-15).
		const bool isWheatFed = (buildingType == IntBuildingType::SWARM_BUILDING ||
		                         buildingType == IntBuildingType::FOOD_BUILDING);
		const bool isSwarm    = (buildingType == IntBuildingType::SWARM_BUILDING);
		const bool isInn      = (buildingType == IntBuildingType::FOOD_BUILDING);

		// Effective footprint used for space reservation. Some buildings grow on
		// upgrade and must reserve room for the final size at placement time, or the
		// upgrade can never fit:
		//   * inn: 2x2 -> 3x3, anchored at the same top-left (constant decLeft),
		//   * racetrack (WALKSPEED) and pool (SWIMSPEED): 4x4 -> 6x6, CENTERED — the
		//     decLeft shrinks (-2 -> -3) so they expand on every side, not just down-
		//     right. grownFootprintBox returns the box offset (gox, goy) from the
		//     placed corner plus its size (ew x eh); for the inn the offset is 0 and
		//     it matches the old grownFootprint. Other types reserve what we place.
		int gox = 0, goy = 0;
		int ew = w, eh = h;
		const bool reserveGrown = isInn
			|| buildingType == IntBuildingType::WALKSPEED_BUILDING
			|| buildingType == IntBuildingType::SWIMSPEED_BUILDING;
		if (reserveGrown)
			grownFootprintBox(bt, gox, goy, ew, eh);

		// Deterministic scan: fixed (x, y) order over every top-left corner.
		for (int x = 0; x < mapW; x++)
		{
			for (int y = 0; y < mapH; y++)
			{
				// Grown-footprint top-left corner: the placed corner shifted by the
				// upgrade box offset (zero for non-growing types and for the inn; up/
				// left for the centered racetrack/pool growth).
				const int gx = x + gox;
				const int gy = y + goy;

				// Canonical engine validity gate — identical to the predicate
				// behind Game::checkHardRoomForBuilding for a non-virtual
				// building, so a resulting OrderCreate will not be rejected. We gate
				// on the GROWN footprint (gx, gy, ew x eh) so the spot also has room
				// for the eventual upgrades; the placed footprint is a subset of it.
				if (!map.isHardSpaceForBuilding(gx, gy, ew, eh))
					continue;

				// Fog-of-war: the footprint must be discovered (mirrors AIEcho's
				// find_location). Check both corners of the grown box, like the
				// engine path does.
				if (!map.isMapDiscovered(map.normalizeX(gx), map.normalizeY(gy),
				                         team->allies) ||
				    !map.isMapDiscovered(map.normalizeX(gx + ew - 1),
				                         map.normalizeY(gy + eh - 1), team->allies))
					continue;

				// Geography rejects for wheat-fed buildings.
				//
				// HARD REJECT (swarm and inn): no CORN within the maximum haul
				// distance. Engine fact: a swarm L0 stalls when its CORN buffer
				// drops below 5 (building/TypeSteps.cpp:31); a unit on a field tile
				// more than ~5 tiles away cannot keep the buffer above the stall
				// line within one production cycle of 150 ticks. CORTEX_WHEAT_MAX_DIST
				// encodes this. Use the cheap bounded scan (cap == WHEAT_MAX_DIST) to
				// avoid scanning far on a reject; the full SCAN_CAP is used only for
				// the retained candidates' wheatDist field at copy-out.
				if (isWheatFed &&
				    nearestCornDist(map, x, y, CORTEX_WHEAT_MAX_DIST) < 0)
					continue;

				// HARD REJECT (swarm only): must sit at least CORTEX_SWARM_MIN_SPACING
				// Chebyshev tiles from every existing live swarm of this team so that
				// two swarms do not compete for the same wheat catchment.
				// distanceToNearestSwarm returns -1 when no swarms exist; skip the
				// reject in that case (first swarm goes wherever wheat exists).
				if (isSwarm)
				{
					const int swarmDist = distanceToNearestSwarm(game, team, x, y);
					if (swarmDist >= 0 && swarmDist < CORTEX_SWARM_MIN_SPACING)
						continue;
				}

				// HARD REJECT (inn only): keep inns at least CORTEX_INN_MIN_SPACING
				// Chebyshev tiles apart so they do not pile on top of each other and
				// split the same wheat catchment. distanceToNearestInn returns -1 when
				// no inn exists yet; the first inn places freely.
				if (isInn)
				{
					const int innDist = distanceToNearestInn(game, team, x, y);
					if (innDist >= 0 && innDist < CORTEX_INN_MIN_SPACING)
						continue;
				}

				// WHEAT-LANE CLEARANCE (non-wheat-fed only): only swarms and inns may
				// sit close to wheat. Every other building type is pushed back beyond
				// CORTEX_WHEAT_CLEAR_DIST so its footprint does not block workers'
				// paths into the field. AI-design rule, no engine analogue.
				if (!isWheatFed && anyCornWithin(map, x, y, w, h, CORTEX_WHEAT_CLEAR_DIST))
					continue;

				// INN SIDE-CLEARANCE (placing an inn): the inn may touch a building on
				// at most CORTEX_INN_MAX_TOUCH_SIDES of its four sides; the rest keep
				// CORTEX_INN_SIDE_CLEARANCE empty tiles so workers can reach it and the
				// wheat behind it. Measured against the GROWN (ew x eh) footprint.
				// innOccupiedSides counts sides already occupied by existing buildings
				// (no hypothetical candidate here — the inn IS the candidate).
				if (isInn &&
				    innOccupiedSides(map, gx, gy, ew, eh, -1, -1, 0, 0) >
				        CORTEX_INN_MAX_TOUCH_SIDES)
					continue;

				// INN SIDE-CLEARANCE (other direction): placing ANY building — swarm,
				// inn, or otherwise — must not open a second occupied side on one of our
				// existing inns. The candidate is offered at its grown footprint
				// (ew x eh) so an inn's expansion tiles are accounted for. Keeps the
				// rule symmetric regardless of build order.
				if (candidateCrowdsInn(game, team, map, gx, gy, ew, eh))
					continue;

				const int distToColony = distanceToNearestBuilding(game, team, x, y);
				int score = scoreFromDistance(distToColony);
				if (footprintBordersResource(map, x, y, w, h))
					score += 250; // resource adjacency bonus

				ScoredSpot spot;
				spot.x = x;
				spot.y = y;
				spot.score = score;
				spot.distToColony = distToColony;
				insertTopK(heap, count, spot);
			}
		}

		// Deterministic final tie-break among equal-(score) candidates that also
		// share the same distance-to-colony: this only fires when two retained
		// spots are genuinely indistinguishable by our heuristic. We perturb the
		// stored score by a syncRand()-derived nudge keyed off the tile so the
		// ordering is still identical on every client but does not always favour
		// the lowest scan coordinate. We do NOT re-sort across distinct scores.
		for (int i = 0; i + 1 < count; i++)
		{
			if (heap[i].score == heap[i + 1].score &&
			    heap[i].distToColony == heap[i + 1].distToColony)
			{
				// Coin flip is drawn from the lockstep RNG, so identical on all
				// machines. Swap (keep both, just order) on heads.
				if ((syncRand() & 1) != 0)
				{
					ScoredSpot tmp = heap[i];
					heap[i] = heap[i + 1];
					heap[i + 1] = tmp;
				}
			}
		}

		// Copy-out: fill the retained candidates' wheatDist using the full scan
		// cap (CORTEX_WHEAT_SCAN_CAP > CORTEX_WHEAT_MAX_DIST) so the signal can
		// report "just out of haul range" for supply-distance expansion decisions
		// in the policy. Applies to all building types; harmless for non-wheat ones
		// (a barracks or school will get -1 if there is no wheat nearby, which the
		// policy ignores).
		for (int i = 0; i < count; i++)
		{
			out[i].valid = 1;
			out[i].x = heap[i].x;
			out[i].y = heap[i].y;
			out[i].score = heap[i].score;
			out[i].wheatDist = nearestCornDist(map, heap[i].x, heap[i].y,
			                                   CORTEX_WHEAT_SCAN_CAP);
		}

		return count;
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
