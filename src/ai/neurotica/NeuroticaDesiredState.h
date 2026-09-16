// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

/*
  Neurotica desired-state field.

  Neurotica is a declarative AI: instead of emitting orders, its policy emits the
  map configuration it *wants*, and NeuroticaReconciler issues whatever orders
  bring the observed state into alignment with it. This header is the schema
  for that field — the single source of truth shared by the policy (which
  produces it), the reconciler (which consumes it) and the dataset writer
  (which labels it).

  Control-loop shape: this is LEVEL-triggered, not edge-triggered. The field is
  re-asserted every policy step and re-diffed from scratch, so a desire that is
  already satisfied produces no order. That is what lets the policy be
  stateless — it never has to remember what it already ordered, because
  re-asserting a satisfied desire is a no-op. The cost of level-triggering is
  thrash when the field flaps between steps; see NeuroticaReconciler for the
  hysteresis that contains it.

  Layout: one Uint8 per cell per plane, row-major, index = y * w + x, matching
  Map's own (x,y) -> index convention modulo the torus wrap. Planes are sized
  w*h and are all present whenever `valid()`. At 8 planes this is 8 bytes/cell,
  so a 128x128 map costs 128 KB per field — cheap enough to rebuild every
  policy step rather than mutate in place.

  ANCHORING: `building` is anchored at the building's TOP-LEFT footprint cell,
  matching OrderCreate(posX, posY) and Building::posX/posY. A 2x2 inn desired
  at (10,10) sets exactly one cell — (10,10) — not four. The reconciler, the
  legality mask and the BC labels must all agree on this or the whole field is
  off by a tile; the M0 round-trip test exists largely to catch that.
*/

#include <SDL_stdinc.h>
#include <vector>

namespace Neurotica
{
	//! Sentinel for "no preference" in the scalar planes that have one. The
	//! building plane has no don't-care: absence of desire is a real desire
	//! (none), and demolition is gated on persistence instead — see
	//! NeuroticaReconciler's demolish rule.
	static constexpr Uint8 DONT_CARE = 255;

	//! IntBuildingType::NB_BUILDING, duplicated so this header stays free of
	//! engine includes. Static-asserted against the real value in the
	//! reconciler, which does include it.
	static constexpr Uint8 IntBuildingTypeCount = 13;

	//! Entries per cell in DesiredState::swarmRatio, one per unit type
	//! (NB_UNIT_TYPE in src/unit/UnitConsts.h). Declared here rather than
	//! included so this header stays free of engine dependencies; the
	//! reconciler static_asserts the two agree.
	static constexpr size_t SWARM_RATIO_STRIDE = 3;

	//! Encoding of DesiredState::priority. Building::priority is signed
	//! (-1 low, 0 normal, +1 high); these planes are unsigned, so the mapping
	//! is explicit rather than a biased integer nobody can read.
	enum PriorityLevel : Uint8
	{
		PRIORITY_LOW = 0,
		PRIORITY_NORMAL = 1,
		PRIORITY_HIGH = 2
	};

	//! Bits in DesiredState::areas. These mirror Map's three per-team area
	//! layers (AreaType in Map.h) but are packed one cell per byte here
	//! because the policy emits them as three independent sigmoids.
	enum AreaBit : Uint8
	{
		AREA_GUARD = 1 << 0,
		AREA_CLEAR = 1 << 1,
		AREA_FORBIDDEN = 1 << 2
	};

	/*!
	  One complete "what I want the map to look like" snapshot.

	  Every plane is independently meaningful; the reconciler diffs each
	  against the corresponding observed quantity and emits the orders that
	  close the gap, in descending `urgency`.
	*/
	struct DesiredState
	{
		Sint32 w = 0;
		Sint32 h = 0;

		//! 0 = no building wanted here; 1..IntBuildingType::NB_BUILDING = the
		//! wanted type, offset by one. Anchored top-left (see header comment).
		std::vector<Uint8> building;

		//! How strongly this cell is wanted for `building`, 0..255.
		//!
		//! This is the learned half of AIEcho's constraint split (see
		//! NeuroticaReconciler's placement note): Echo's calculate_constraint
		//! scores a cell and passes_constraint decides whether it is legal at
		//! all, and the placement is the argmax of the score over the legal
		//! cells. Here the network supplies the score and the engine supplies
		//! legality. Treating the field as an exact coordinate instead throws
		//! that away, and a blocked cell becomes a desire that can never be
		//! satisfied rather than one that relocates.
		std::vector<Uint8> buildingScore;

		//! Wanted building level, 0-based to match BuildingType::level.
		//! DONT_CARE leaves upgrades alone. A level above the building's
		//! current one drives OrderConstruction (upgrade); Neurotica never
		//! downgrades, since the engine has no such operation.
		std::vector<Uint8> level;

		//! Wanted Building::maxUnitWorking. DONT_CARE leaves staffing alone.
		std::vector<Uint8> workers;

		//! Wanted unitWorkingFuture, i.e. staffing once construction finishes.
		std::vector<Uint8> workersFuture;

		//! Wanted flag attraction radius (unitStayRange) for virtual buildings
		//! (war / exploration / clearing flags). DONT_CARE leaves it alone.
		std::vector<Uint8> flagRadius;

		//! Wanted unit-production ratio for a swarm, one entry per unit type
		//! (NB_UNIT_TYPE == 3: worker, explorer, warrior), stored at
		//! index(x,y)*SWARM_RATIO_STRIDE + type. DONT_CARE in the worker slot
		//! leaves the whole swarm alone. Without this plane Neurotica can place a
		//! swarm but not decide what comes out of it, which is most of what a
		//! swarm is for.
		std::vector<Uint8> swarmRatio;

		//! Wanted Building::priority, encoded PRIORITY_LOW/NORMAL/HIGH since
		//! the engine's own value is signed (-1/0/+1) and these planes are
		//! unsigned. DONT_CARE leaves it alone.
		std::vector<Uint8> priority;

		//! Wanted Building::minLevelToFlag — the minimum unit level a flag
		//! will accept. DONT_CARE leaves it alone.
		std::vector<Uint8> minLevelToFlag;

		//! Bitmask of AreaBit. Diffed against Map::isGuardArea/isClearArea/
		//! isForbidden for this team and emitted as OrderAlterArea ADD/DEL.
		std::vector<Uint8> areas;

		//! Execution priority, 0..255, descending. This plane is load-bearing:
		//! the desired field is routinely unaffordable, so *something* has to
		//! choose which diffs happen first. Making it an output plane keeps
		//! that choice learned rather than burying a build-order heuristic in
		//! the reconciler.
		std::vector<Uint8> urgency;

		//! "Do not disturb" strength, 0..255. Shields a cell from the
		//! demolition path even when `building` says none — the policy's way
		//! of expressing commitment to something it already has.
		std::vector<Uint8> commit;

		//! Allocate every plane for a w*h map and zero it. Zeroed means: want
		//! nothing built anywhere, no areas, zero urgency, no commitment —
		//! and, because demolition is persistence-gated, a zeroed field is
		//! inert rather than destructive. That makes it a safe default for a
		//! policy that has not produced anything yet.
		void reset(Sint32 width, Sint32 height)
		{
			w = width;
			h = height;
			const size_t n = size_t(w) * size_t(h);
			building.assign(n, 0);
			buildingScore.assign(n, 0);
			level.assign(n, DONT_CARE);
			workers.assign(n, DONT_CARE);
			workersFuture.assign(n, DONT_CARE);
			flagRadius.assign(n, DONT_CARE);
			swarmRatio.assign(n * SWARM_RATIO_STRIDE, DONT_CARE);
			priority.assign(n, DONT_CARE);
			minLevelToFlag.assign(n, DONT_CARE);
			areas.assign(n, 0);
			urgency.assign(n, 0);
			commit.assign(n, 0);
		}

		bool valid() const
		{
			const size_t n = size_t(w) * size_t(h);
			return w > 0 && h > 0 && building.size() == n && level.size() == n &&
			       workers.size() == n && workersFuture.size() == n &&
			       flagRadius.size() == n && areas.size() == n &&
			       urgency.size() == n && commit.size() == n &&
			       swarmRatio.size() == n * SWARM_RATIO_STRIDE &&
			       priority.size() == n && minLevelToFlag.size() == n &&
			       buildingScore.size() == n;
		}

		//! Row-major index. Callers are responsible for wrapping x and y into
		//! range first (Map::normalizeX/normalizeY) — this deliberately does
		//! not wrap, so an out-of-range access asserts in debug rather than
		//! silently aliasing to the wrong cell.
		size_t index(Sint32 x, Sint32 y) const { return size_t(y) * size_t(w) + size_t(x); }
	};
} // namespace Neurotica
