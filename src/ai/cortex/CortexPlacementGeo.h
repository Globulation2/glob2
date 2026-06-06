// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

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
