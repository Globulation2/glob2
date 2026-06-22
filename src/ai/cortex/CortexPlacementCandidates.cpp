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

// AICortex placement candidate scan.
//
// Split out of CortexPlacement.cpp (which was over the 500-line cap) so the big
// tile-scan + ranking of placeCandidates lives in its own translation unit. The
// ScoredSpot record and the distanceToNearestBuilding / scoreFromDistance scoring
// helpers are shared with placeFlagTargets (CortexPlacement.cpp) and therefore
// live in CortexPlacementGeo.h; the helpers below are used only by placeCandidates.
//
// See CortexPlacement.cpp for the full design notes on the placement machinery,
// scoring, and determinism this scan obeys.

namespace Cortex
{
	namespace
	{
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

		// Chebyshev edge-to-edge gap from the candidate footprint (x, y, w x h) to the
		// nearest live building owned by `team`. Returns -1 when the team has no
		// buildings (first placement: the cap is meaningless). Warp-safe.
		//
		// distanceToNearestBuilding above measures CORNER-to-corner Chebyshev distance,
		// which inflates with the footprint size; this measures EDGE-to-edge gap (0 when
		// the boxes touch), the right quantity for the hard "stay clustered" cap so a
		// large building is not penalised for its own extent.
		int nearestBuildingEdgeDist(Game* game, Team* team, const Map& map,
		                            int x, int y, int w, int h)
		{
			const int mapW = map.getW();
			const int mapH = map.getH();
			int best = -1;
			for (int i = 0; i < Building::MAX_COUNT; i++)
			{
				Building* b = team->myBuildings[i];
				if (b == NULL || b->buildingState == Building::DEAD)
					continue;
				if (b->type == NULL)
					continue;
				const int g = rectEdgeChebyshev(x, w, y, h,
				                                b->posX, b->type->width,
				                                b->posY, b->type->height, mapW, mapH);
				if (best < 0 || g < best)
					best = g;
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
	} // namespace

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

				// HARD REJECT (swarm and inn): the field must hold a real CLUSTER of
				// harvestable wheat, not just the single tile the check above needs.
				// Require at least CORTEX_WHEAT_MIN_TILES wheat tiles that will SURVIVE
				// the protection checkerboard (the open-parity half) within
				// CORTEX_WHEAT_MIN_TILES_RADIUS of the footprint. We count the SURVIVING
				// set, not the live non-forbidden set, so the gate measures the field as
				// it WILL be once the checkerboard settles: a candidate near freshly-
				// revealed wheat (not yet painted) no longer passes on the full field only
				// to have the reconcile paint half of it away and leave the inn below the
				// threshold within a cycle. Depleted tiles are no longer CORN, so this
				// still rejects a swarm hugging a nearly-exhausted patch and an inn dropped
				// on a field whose wheat is already gone (both observed in play).
				if (isWheatFed &&
				    countSurvivingCornWithin(map, x, y, w, h,
				                             CORTEX_WHEAT_MIN_TILES_RADIUS)
				        < CORTEX_WHEAT_MIN_TILES)
					continue;

				// HARD REJECT (swarm only): the swarm's footprint EDGE must sit within
				// CORTEX_SWARM_WHEAT_EDGE_DIST tiles of a CORN tile. This is STRICTER
				// than the shared corner-based nearestCornDist check above (which still
				// gates inns): a swarm spawns the haulers that feed the whole colony, so
				// it must hug the wheat far more tightly than an inn does. anyCornWithin
				// is edge-aware (it scans the footprint expanded by `dist`), so this is
				// measured from the footprint edge, not the top-left corner.
				if (isSwarm && !anyCornWithin(map, x, y, w, h, CORTEX_SWARM_WHEAT_EDGE_DIST))
					continue;

				// HARD REJECT (inn only): the inn's GROWN footprint edge must sit within
				// CORTEX_INN_WHEAT_EDGE_DIST tiles of a HARVESTABLE (surviving-parity) CORN
				// tile. Unlike the cluster gate above (which counts surviving corn within a
				// wide radius of the PLACED 2x2 to prove a real field exists), this measures
				// from the GROWN box (gx, gy, ew x eh) so the expansion area is included: the
				// inn — at its final size — hugs the wheat with at most a one-tile gap and
				// never blocks the lane its haulers use to reach the field. countSurviving
				// CornWithin scans the box expanded by `dist`, so dist == 1 means "wheat
				// touching or one tile off the grown edge".
				if (isInn &&
				    countSurvivingCornWithin(map, gx, gy, ew, eh, CORTEX_INN_WHEAT_EDGE_DIST)
				        < 1)
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

				// EDGE-DISTANCE CAP (non-wheat-fed only): keep tech/military buildings
				// clustered with the colony. The soft compactness score alone can let a
				// far-flung spot win; this hard cap forbids any spot whose footprint edge
				// is more than CORTEX_MAX_BUILD_EDGE_DIST tiles from the nearest existing
				// building. Skipped when the team has no buildings yet (first placement).
				if (!isInn && !isSwarm)
				{
					const int edgeDist = nearestBuildingEdgeDist(game, team, map, x, y, w, h);
					if (edgeDist >= 0 && edgeDist > CORTEX_MAX_BUILD_EDGE_DIST)
						continue;
				}

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

				// RESERVED-EXPANSION CLEARANCE: do not place into the expansion tiles an
				// existing inn / racetrack / pool reserved for its own upgrades. Those
				// types grow on upgrade but only occupy their current footprint on the
				// map, so the soft checks above can let a new building land in the ring
				// they will expand into and block the upgrade. We test the candidate's
				// own GROWN box (gx, gy, ew x eh) against each existing growable type's
				// reserved box, so neither side's future expansion collides.
				if (candidateOverlapsReservedExpansion(game, team, map, gx, gy, ew, eh))
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
} // namespace Cortex
