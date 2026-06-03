// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexPlacement.h"

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
	} // namespace

	int placeCandidates(Game* game, Team* team, int buildingType, int level,
	                    BuildCandidate out[CORTEX_BUILD_CANDIDATES])
	{
		// Always leave the output well-defined, even on the error paths below.
		for (int i = 0; i < CORTEX_BUILD_CANDIDATES; i++)
		{
			out[i].valid = 0;
			out[i].x = 0;
			out[i].y = 0;
			out[i].score = 0;
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

		// Deterministic scan: fixed (x, y) order over every top-left corner.
		for (int x = 0; x < mapW; x++)
		{
			for (int y = 0; y < mapH; y++)
			{
				// Canonical engine validity gate — identical to the predicate
				// behind Game::checkHardRoomForBuilding for a non-virtual
				// building, so a resulting OrderCreate will not be rejected.
				if (!map.isHardSpaceForBuilding(x, y, w, h))
					continue;

				// Fog-of-war: the footprint must be discovered (mirrors AIEcho's
				// find_location). Check both corners, like the engine path does.
				if (!map.isMapDiscovered(x, y, team->allies) ||
				    !map.isMapDiscovered(map.normalizeX(x + w - 1),
				                         map.normalizeY(y + h - 1), team->allies))
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
