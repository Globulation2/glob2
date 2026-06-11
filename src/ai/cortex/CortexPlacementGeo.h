// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include <SDL_stdinc.h>

class Game;
class Team;
class Map;
struct BuildingType;

// AICortex placement geometry helpers.
//
// Split out of CortexPlacement.cpp (which was already at the 500-line cap) so the
// inn worker-access / wheat-lane spacing rules live in their own translation unit.
// These are AI-design placement constraints with no engine analogue: the engine
// itself never enforces clearance around an inn or wheat, so Cortex applies them
// at its single placement chokepoint (Cortex::placeCandidates).
//
// All helpers are deterministic (fixed-order tile scans, team building array
// indexed by index, warp-safe via Map::normalizeX/Y, no rand, no pointer reads).

namespace Cortex
{
	/// One scored candidate during a placement/target scan, before it is copied
	/// into the caller's POD BuildCandidate array. Shared by placeCandidates
	/// (CortexPlacementCandidates.cpp) and placeFlagTargets (CortexPlacement.cpp)
	/// so the two ranked-insert scans agree on the intermediate record layout.
	struct ScoredSpot
	{
		int x;
		int y;
		int score;
		int distToColony; // secondary key for deterministic tie-breaking
	};

	/// Chebyshev distance from the footprint's top-left corner to the nearest
	/// live building owned by `team`. Returns -1 when the team has no buildings
	/// yet (first placement: distance is meaningless). Shared by placeCandidates
	/// and placeFlagTargets.
	int distanceToNearestBuilding(Game* game, Team* team, int x, int y);

	/// Translate a Chebyshev distance-to-colony into a placement score. Closer is
	/// better; we want a compact colony but not literally stacked, so the score
	/// decays linearly with distance. A team with no buildings (distToColony < 0)
	/// gets a flat base. Shared by placeCandidates and placeFlagTargets.
	int scoreFromDistance(int distToColony);

	/// Counts how many of an inn's four sides are "occupied" — i.e. have a building
	/// within CORTEX_INN_SIDE_CLEARANCE tiles of that edge. The inn footprint is
	/// (innX, innY) top-left, innW x innH. A hypothetical candidate footprint may be
	/// supplied (candW > 0): tiles it would cover count as occupied too, which lets
	/// the caller test "would placing this building open a new side on the inn?".
	/// Pass candW <= 0 (e.g. 0) to count only buildings already on the map.
	///
	/// Corner-aware: a tile in the clearance frame that is outside the footprint on
	/// BOTH axes (a diagonal corner) counts toward both adjacent sides, so a building
	/// parked at a corner occupies two sides. Returns 0..4.
	int innOccupiedSides(const Map& map, int innX, int innY, int innW, int innH,
	                     int candX, int candY, int candW, int candH);

	/// True if any CORN (wheat) tile lies within `dist` Chebyshev tiles of the
	/// footprint (innX, innY, w x h). Used to keep non-wheat-fed buildings off the
	/// wheat lanes. Early-outs on the first CORN found.
	bool anyCornWithin(const Map& map, int x, int y, int w, int h, int dist);

	/// Counts the HARVESTABLE wheat tiles within `dist` Chebyshev tiles of the
	/// footprint (x, y, w x h): tiles that are CORN AND not forbidden for `teamMask`.
	/// Depleted tiles (no longer CORN) and the team's own checkerboard-protected
	/// (forbidden) half of a field are excluded, so the result is the live wheat the
	/// team's workers can actually take. Used by placeCandidates to require a real
	/// cluster of harvestable wheat (CORTEX_WHEAT_MIN_TILES) around a new swarm/inn.
	int countHarvestableCornWithin(const Map& map, Uint32 teamMask,
	                               int x, int y, int w, int h, int dist);

	/// Forbidden-BLIND corn-tile count within `dist` Chebyshev tiles of the footprint:
	/// every CORN tile regardless of the forbidden mask. (countHarvestableCornWithin
	/// minus this is the forbidden-but-present corn.) Diagnostic discriminator between
	/// checkerboard-forbidding and field depletion; no policy reads it.
	int countCornWithin(const Map& map, int x, int y, int w, int h, int dist);

	/// Fills (w, h) with the LARGEST footprint a building of type `bt` can grow into
	/// by walking its upgrade chain (BuildingType::nextLevel). For an inn this yields
	/// 3 x 3 (the top-level inn2 footprint, BuildingsPartA.cpp); for a type that never
	/// grows it returns its own width/height. Growth is anchored at the top-left
	/// corner (decLeft/decTop are constant across inn levels, so the footprint expands
	/// toward +x/+y), so the grown footprint shares the placed building's (posX, posY).
	void grownFootprint(const BuildingType* bt, int& w, int& h);

	/// Bounding box, RELATIVE to the placed level-0 top-left corner, that covers the
	/// building's footprint at EVERY level of its upgrade chain. Unlike grownFootprint
	/// (size only, assumes same-corner growth), this accounts for the engine
	/// re-centering the footprint on upgrade (building/Update.cpp:423 — new top-left =
	/// pos + nextBt->decLeft - type->decLeft, and the center is invariant across the
	/// whole chain). A building whose decLeft shrinks as it grows — racetrack/pool go
	/// 4x4 decLeft -2 -> 6x6 decLeft -3 — expands in ALL directions, so its reservation
	/// must shift up/left. Returns the offset (ox, oy) of the box from the placed
	/// corner (<= 0 when it grows up/left) and its size (w, h). For a type that never
	/// grows, or grows from a fixed corner (the inn, constant decLeft), ox == oy == 0
	/// and (w, h) equals grownFootprint — so callers can use this uniformly.
	void grownFootprintBox(const BuildingType* bt, int& ox, int& oy, int& w, int& h);

	/// True if placing a building of footprint (x, y, w x h) would push one of
	/// `team`'s existing inns past CORTEX_INN_MAX_TOUCH_SIDES occupied sides. Only
	/// rejects when the candidate actually WORSENS an inn's count, so a pre-existing
	/// violation does not block every nearby placement.
	bool candidateCrowdsInn(Game* game, Team* team, const Map& map,
	                        int x, int y, int w, int h);

	/// Chebyshev edge-to-edge gap between two warp-wrapped boxes A (ax, ay, aw x ah)
	/// and B (bx, by, bw x bh) on a map of size (mapW, mapH). Returns 0 when the boxes
	/// overlap or merely touch, otherwise the number of empty tiles in the larger of
	/// the two axis gaps (Chebyshev == max of the per-axis gaps). All-integer and
	/// warp-safe — deterministic for lockstep.
	int rectEdgeChebyshev(int ax, int aw, int ay, int ah,
	                      int bx, int bw, int by, int bh, int mapW, int mapH);

	/// True iff two warp-wrapped boxes A (ax, ay, aw x ah) and B (bx, by, bw x bh)
	/// share at least one tile on a map of size (mapW, mapH). All-integer, warp-safe.
	bool rectsOverlap(int ax, int aw, int ay, int ah,
	                  int bx, int bw, int by, int bh, int mapW, int mapH);

	/// True if the candidate's grown footprint (cgx, cgy, cew x ceh) overlaps the
	/// still-reserved grown footprint of any existing live inn (FOOD_BUILDING),
	/// racetrack (WALKSPEED_BUILDING) or pool (SWIMSPEED_BUILDING) owned by `team`.
	/// These three types grow on upgrade and reserved that room at placement; a new
	/// building must not occupy tiles they will expand into. Uses grownFootprintBox on
	/// each existing building's CURRENT type, anchored at its (posX, posY), so it is
	/// correct whether the building is level 0 or already partly upgraded. Warp-safe.
	bool candidateOverlapsReservedExpansion(Game* game, Team* team, const Map& map,
	                                        int cgx, int cgy, int cew, int ceh);
}
