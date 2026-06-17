// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include <SDL_stdinc.h>

// AICortex tunable constants and enums shared across the observation -> policy
// -> action layers. Split out of CortexTypes.h (the umbrella public header) so
// each header stays under 500 lines; CortexTypes.h includes this near the top,
// before the POD struct definitions that use these size constants for their
// fixed-shape arrays. Self-contained: depends on nothing but <SDL_stdinc.h>.

namespace Cortex
{
	/// Fixed upper bound on enemy team slots in an Observation. 32 ==
	/// Team::MAX_COUNT_ON_DISK; it is a safe over-bound on the live team ceiling
	/// (Team::MAX_COUNT == 12), so every possible enemy team always has a slot.
	/// CortexObservation.cpp static_asserts MAX_ENEMY_SLOTS >= Team::MAX_COUNT.
	/// Unused slots are flagged inactive rather than omitted (fixed shape).
	static const int MAX_ENEMY_SLOTS = 32;

	/// Mirrors UnitConsts.h NB_UNIT_LEVELS (per-skill upgrade level buckets, 0..3).
	static const int CORTEX_UNIT_LEVELS = 4;
	/// Mirrors UnitConsts.h NB_UNIT_TYPE (WORKER, EXPLORER, WARRIOR). Indexes the
	/// swarm production-ratio triple. CortexObservation.cpp static_asserts it.
	static const int CORTEX_UNIT_TYPES = 3;
	/// Self-imposed upper bound on a single swarm production-ratio entry. Mirrors
	/// the GUI's MAX_RATIO_RANGE (gui/GameGUI.h) convention — the engine itself
	/// caps nothing (OrderModifySwarm writes ratios verbatim), so this is Cortex's
	/// own bound, not an engine constant; no static_assert needed.
	static const int CORTEX_MAX_RATIO = 16;
	/// Mirrors IntBuildingType::NB_BUILDING (number of distinct building types).
	static const int CORTEX_BUILDING_TYPES = 13;
	/// Mirrors TeamStat.h NB_BUILDING_LONG_LEVELS. The "long level" packs both a
	/// building's level and whether it is still a site into one index via
	/// longLevel = (level << 1) + 1 - isBuildingSite, range 0..5. So odd indices
	/// (1,3,5) are FINISHED buildings at level 0,1,2 and even indices (0,2,4) are
	/// their construction SITES. Use the cortex* helpers below rather than
	/// reading raw slots, so the encoding lives in exactly one place.
	static const int CORTEX_BUILDING_LONG_LEVELS = 6;

	// Cortex-local mirrors of the IntBuildingType::Number values the policy
	// names directly. The policy layer must not include engine headers, so it
	// refers to building types through these; CortexObservation.cpp
	// static_asserts each against IntBuildingType so they cannot drift.
	static const int CORTEX_BUILD_SWARM     = 0; ///< IntBuildingType::SWARM_BUILDING
	static const int CORTEX_BUILD_FOOD      = 1; ///< IntBuildingType::FOOD_BUILDING (inn)
	static const int CORTEX_BUILD_HEAL      = 2; ///< IntBuildingType::HEAL_BUILDING (hospital)
	static const int CORTEX_BUILD_WALKSPEED = 3; ///< IntBuildingType::WALKSPEED_BUILDING (racetrack; trains WALK)
	static const int CORTEX_BUILD_SWIMSPEED = 4; ///< IntBuildingType::SWIMSPEED_BUILDING (swimming pool; trains SWIM)
	static const int CORTEX_BUILD_ATTACK    = 5; ///< IntBuildingType::ATTACK_BUILDING (barracks)
	static const int CORTEX_BUILD_SCIENCE   = 6; ///< IntBuildingType::SCIENCE_BUILDING (school; trains BUILD+HARVEST)

	/// Engine building-priority values (Building::priority; OrderChangePriority).
	/// -1 = low, 0 = normal, +1 = high. The panic defense raises a swarm to HIGH so
	/// it wins worker contention while it pumps warriors, then restores NORMAL.
	static const int CORTEX_PRIORITY_LOW    = -1;
	static const int CORTEX_PRIORITY_NORMAL = 0;
	static const int CORTEX_PRIORITY_HIGH   = 1;
	/// Sentinel for CortexAction::priorityTarget when the action is not a
	/// SET_PRIORITY (outside the valid -1/0/+1 range so it can never be mistaken
	/// for a real target).
	static const int CORTEX_PRIORITY_NONE   = -2;

	/// Number of candidate build locations the placement helper surfaces per
	/// building type into the observation. The policy chooses a building type
	/// plus one of these K slots, keeping the action space a fixed-size discrete
	/// distribution (ML rule: no unbounded "(x, y)" — see README action rules).
	static const int CORTEX_BUILD_CANDIDATES = 4;

	/// Number of war-flag target candidates surfaced into the observation
	/// (discovered enemy buildings, nearest-first). Same discrete-slot rationale
	/// as CORTEX_BUILD_CANDIDATES: the policy picks a target by slot index, never
	/// an unbounded coordinate. Cortex-local bound, no engine mirror.
	static const int CORTEX_FLAG_TARGETS = 8;
	/// Self-imposed upper bound on a war flag's attraction radius (the building's
	/// unitStayRange). The engine clamps nothing, so this is Cortex's bound for a
	/// discrete/normalizable action param; the action layer clamps to it. Roughly
	/// the GUI's war-flag scale (Settings defaultFlagRadius[war] == 4).
	static const int CORTEX_MAX_FLAG_RADIUS = 16;
	/// Upper bound on warriors assigned to one war flag. A war flag is a building
	/// and its summon count is its maxUnitWorking (the worker request), so it is
	/// bound by the SAME engine ceiling as any other building: the executor asserts
	/// numberRequested <= MAX_BUILDING_WORKER_REQUEST (== 20, Game.h /
	/// Game_orders.cpp:206) and the GUI war-flag scrollbox caps at MAX_UNIT_WORKING
	/// (== 20, gui/GameGUI.h). A flag ordered with 32 trips that assert in a debug
	/// build (and requests more than the engine can ever staff in release), so this
	/// MUST equal the engine cap, not exceed it. Mirrors CORTEX_MAX_BUILDING_WORKERS.
	static const int CORTEX_MAX_FLAG_UNITS = 20;

	/// Offense-hold hysteresis posture (the thrash damper). The single source of
	/// truth for the posture the flag is committed to: shared by the observation
	/// (CortexObservation.flagPosture echoes it so the PURE policy can reason about
	/// the in-progress commitment), the policy (which now makes the hold-vs-recall
	/// decision), and the action layer (which OWNS and mutates the state when it
	/// actually places a flag). AICortex::FlagPosture aliases these so the action
	/// layer keeps its established enumerator names while the value stays single-
	/// sourced here. POD-friendly: stored in the observation as a plain Sint32.
	enum CortexFlagPosture
	{
		CORTEX_POSTURE_NONE    = 0,
		CORTEX_POSTURE_OFFENSE = 1,
		CORTEX_POSTURE_DEFENSE = 2
	};
	/// How many of our buildings must be under attack AT ONCE for a defensive
	/// recall to override an in-progress offense hold. A single transient hit
	/// (1 building) is "harassment" and does not break the push; multiple buildings
	/// under fire is a real base assault that earns the recall. Shared by the policy
	/// (the hold-vs-recall decision, moved here from the action layer) — the value
	/// must match AICortex::DEFENSE_SERIOUS_BUILDINGS, which aliases it.
	static const int CORTEX_DEFENSE_SERIOUS_BUILDINGS = 2;

	/// Radius (warp-safe Chebyshev tiles) around a building taking fire within which
	/// VISIBLE enemy units are counted as the "threat" sizing the defensive recall.
	/// The defense flag is sized to CORTEX_DEFENSE_THREAT_MULTIPLE x that count, so
	/// the recall scales to the actual assaulting force instead of a fixed number.
	/// Set to the defense flag's own stay-range (DEFENSE_FLAG_RADIUS == 5 in
	/// CortexPolicyCombat.cpp) so we count exactly the enemies the planted flag will
	/// engage. Cortex-local tunable; the engine clamps nothing.
	static const int CORTEX_THREAT_SCAN_RADIUS = 5;
	/// Multiplier on the visible threat count that sets the defensive recall's summon
	/// size: enough warriors to overpower the assault, not just match it. Clamped to
	/// [1, CORTEX_MAX_FLAG_UNITS] at the action layer.
	static const int CORTEX_DEFENSE_THREAT_MULTIPLE = 3;

	/// How many of our own units must be under fire AT ONCE for a defensive recall to
	/// override an in-progress offense hold, the unit-side companion of
	/// CORTEX_DEFENSE_SERIOUS_BUILDINGS. A base assault picks our units off well before
	/// it caves a second building (measured: the Muka collapse runs with
	/// unitsUnderAttack in the teens while buildingsUnderAttack stays 0-1), so gating
	/// the serious-defense override on buildings ALONE lets the harasser butcher the
	/// colony before the army is recalled. Either trigger now earns the override.
	static const int CORTEX_DEFENSE_SERIOUS_UNITS = 6;

	// --- wheat-protection tuning (all tunable AI design choices) -----------
	// Cortex paints a checkerboard `forbidden` pattern over its wheat (CORN) so
	// workers harvest one half while the protected half stays full and reseeds it
	// (forbidden blocks harvest, MapGradientGlobal.cpp:135, but NOT growth,
	// MapStep.cpp:80). See docs/AI/cortex/wheat-protection-plan.md and the
	// geometry core in CortexWheat.h/.cpp.

	/// Open margin N range. The open margin is DISABLED: every reachable row of
	/// wheat gets the checkerboard, with no exempt rows nearest the harvest source.
	/// Pinned to 0 (min == max) so the per-game seed always yields 0 and the
	/// classification in CortexWheat.cpp never exempts a row. Kept as named
	/// constants (rather than ripping the field out of the observation/action POD
	/// and the AICortex save/replay layout) so the struct layout and version stay
	/// put; the value is inert. See scanWheatForbidden for where the margin used to
	/// gate classification.
	static const int WHEAT_OPEN_MARGIN_MIN = 0;
	static const int WHEAT_OPEN_MARGIN_MAX = 0;
	/// Checkerboard parity: a reachable CORN tile with depth > N is painted
	/// forbidden when (x+y)&1 == WHEAT_PARITY. A fixed constant for determinism
	/// (0 vs 1 is arbitrary). Used by the geometry core (CortexWheat.cpp).
	static const int WHEAT_PARITY = 0;
	/// How far past our building bounding box the wheat scan reaches, in tiles.
	/// Catches field tiles extending just beyond the outermost colony building;
	/// smaller than the -dump-wheat debug tool's larger fake margin because the
	/// live colony bbox already includes the inn built next to the field.
	static const int WHEAT_REGION_MARGIN = 10;

	// --- closed-loop wheat-economy tuning (v6, all tunable AI design choices) ---
	// The economy is a closed loop on each wheat-fed building's own CORN buffer.
	// The lever is per-building maxUnitWorking, set via OrderModifyBuilding (the
	// same lever AICastor uses, ai/castor/Control.cpp:227-272). Engine facts the
	// thresholds are derived from (game/entities/BuildingsPartA.cpp):
	//   Swarm L0: holds 20 CORN, costs ressourceForOneUnit==5 per unit, makes one
	//             unit / unitProductionTime==150 ticks, and STALLS outright when
	//             ressources[CORN] < 5 (building/TypeSteps.cpp:31). Worker count only
	//             refills the buffer; it does NOT speed production (timeout-gated).
	//   Inn  L0: holds 10 CORN, feeds maxUnitInside==4 units, 1 CORN per unit per
	//            timeToFeedUnit==24 ticks (~5x a swarm's draw per tick) — hungrier.
	// The policy nudges maxUnitWorking by ONLY +/-1 per decision cycle and only
	// outside a deadband, so the chunky 5-CORN production self-damps without an
	// explicit per-building timer (the user's "don't adjust too often" constraint).

	/// Max swarms / inns the observation tracks individually (bounded POD arrays).
	/// This is ALSO the swarm BUILD cap (scoreSecondSwarm): a swarm past this count
	/// would be built but never individually tracked — invisible to the fresh-patch
	/// trigger (anySwarmWantsFreshPatch) and the per-swarm hauler tuning — so we stop
	/// expanding at the tracking wall. There is no separate arbitrary count cap; WHEN
	/// and WHERE a swarm is added is governed by the placement gate (spacing +
	/// fresh-wheat candidate + spare labour), not by a fixed number. Inns can be more
	/// numerous, so their bound is larger. Buildings past these counts (index-scan
	/// order) are simply not individually tuned.
	static const int CORTEX_MAX_TRACKED_SWARMS = 8;
	static const int CORTEX_MAX_TRACKED_INNS   = 16;
	/// Construction sites we individually track to pour idle workers into (Rule:
	/// raise a site's worker cap toward the free-worker count, bounded by deliveries
	/// left). A colony rarely has many sites open at once; extras are not tuned.
	static const int CORTEX_MAX_TRACKED_SITES  = 24;
	/// Hard engine ceiling on any building's worker request: the executor asserts
	/// numberRequested <= MAX_BUILDING_WORKER_REQUEST (Game.h / Game_orders.cpp:206),
	/// so EVERY maxUnitWorking we order must be clamped to this. Mirrors the engine
	/// constant; CortexObservation.cpp static_asserts they stay equal. A 6x6 racetrack
	/// site needs far more than 20 deliveries, so the idle-worker pour can exceed it.
	static const int CORTEX_MAX_BUILDING_WORKERS = 20;

	/// Swarm buffer control. Add a hauler at/below ADD_LO (one unit from a stall),
	/// drop one at/above REM_HI (saturated: 3 units already buffered). The cap is
	/// the user's early/mid ceiling; it lifts late-game (below).
	static const int CORTEX_SWARM_CORN_ADD_LO = 5;
	static const int CORTEX_SWARM_CORN_REM_HI = 15;
	static const int CORTEX_SWARM_WORKER_MIN  = 1;
	static const int CORTEX_SWARM_WORKER_CAP  = 7;
	/// Late-game escape hatch for the swarm cap: once the workers' BUILD skill has
	/// matured AND idle workers exist to spare, lift the 7-worker ceiling. NOTE:
	/// maxBuildLevel is 0-based and caps at CORTEX_UNIT_LEVELS-1 == 3, which is the
	/// player-facing "school level 4" (1-indexed display) the spec refers to.
	static const int CORTEX_SWARM_CAP_LIFT_BUILDLEVEL = 3;
	static const int CORTEX_SWARM_WORKER_CAP_LATE     = 12;

	/// Inn hauler ceiling bounds. The inn's maxUnitWorking is set to its collectable
	/// restock demand (TrackedBuilding::restockTripsNeeded — corn + in-sight fruit
	/// deficit in trips), clamped to [MIN, CAP]. The engine self-regulates the actual
	/// hauler count below this ceiling each tick (Building::desiredNumberOfWorkers), so
	/// these are just the floor (always keep one maintaining hauler) and the cap (never
	/// pull more than this many onto a single inn, however large its deficit).
	static const int CORTEX_INN_WORKER_MIN  = 1;
	static const int CORTEX_INN_WORKER_CAP  = 6;

	/// Post-build settle window for a freshly finished inn. A new inn finishes with
	/// an EMPTY corn buffer (0/10) and the engine's default worker count (2 for a
	/// level-0 inn, Settings.cpp). Left ungated, the worker-tuning loop (Priority
	/// 1.5) reads corn==0 < ADD_LO the very next decision cycle and spikes the
	/// worker count upward immediately — chasing a buffer that simply has not been
	/// filled yet. Hold the inn at its as-built worker count for this many ticks
	/// (25 ticks/s × 60 s = one minute) so its first haulers can fill the buffer
	/// before the loop is allowed to react to the level. Counted from when Cortex
	/// first OBSERVES the inn finished (AICortex stamps it; up to one OBSERVE_INTERVAL
	/// of detection latency, negligible against a 1500-tick window).
	static const int CORTEX_INN_TUNE_DELAY_TICKS = 1500;

	/// How many units one inn actually sustains — the FEED-capacity metric for the
	/// second-inn build gate (Priority 2). NOT maxUnitInside: that is only how many
	/// globs can stand inside eating AT ONCE, not the population an inn keeps fed.
	///
	/// Derivation (all from the engine, verified): a worker drains from full
	/// (HUNGRY_MAX 150000) to its eat trigger (trigHungry = HUNGRY_MAX/4 = 37500) at
	/// hungryness 350/tick while active → it needs feeding once per
	/// (150000-37500)/350 ≈ 321 ticks (UnitMedical.cpp:53,197; Unit.cpp:99). One
	/// feeding occupies a single inn slot for timeToFeedUnit ticks and refills it to
	/// full (UnitDisplacement.cpp:294,321). An inn has maxUnitInside such slots, so
	/// its zero-travel throughput ceiling is
	///     maxUnitInside × (WORK_TICKS + timeToFeedUnit) / timeToFeedUnit
	/// units (L0: 4×345/24 ≈ 57; L1: 7×336/15 ≈ 156; L2: 17×330/9 ≈ 623). We then
	/// take half (SAFETY_NUM/DEN) as the design figure, reserving headroom for walk
	/// time to/from the inn, slot queueing, and corn-supply lag — so an inn is
	/// "good for" ~28 units (L0), ~78 (L1), ~311 (L2), versus the old 4/7/17.
	static const int CORTEX_UNIT_WORK_TICKS_PER_FEED = 321;
	static const int CORTEX_INN_CAPACITY_SAFETY_NUM  = 1;
	static const int CORTEX_INN_CAPACITY_SAFETY_DEN  = 2;

	/// Sustainable population one inn (with maxUnitInside slots and timeToFeedUnit
	/// feed time) keeps fed — see CORTEX_UNIT_WORK_TICKS_PER_FEED for the model.
	/// Pure function of the building type's two feeding numbers; no engine state.
	inline int cortexInnUnitSupport(int maxUnitInside, int timeToFeedUnit)
	{
		if (timeToFeedUnit <= 0 || maxUnitInside <= 0)
			return maxUnitInside; // degenerate type: fall back to the raw slot count.
		const long long ceiling = static_cast<long long>(maxUnitInside)
			* (CORTEX_UNIT_WORK_TICKS_PER_FEED + timeToFeedUnit) / timeToFeedUnit;
		const long long support = ceiling * CORTEX_INN_CAPACITY_SAFETY_NUM
			/ CORTEX_INN_CAPACITY_SAFETY_DEN;
		return support > 0 ? static_cast<int>(support) : 1; // always good for ≥1.
	}

	/// Placement geometry for wheat-fed buildings. A new swarm must sit at least
	/// MIN_SPACING from any existing swarm (so two swarms don't share one wheat
	/// catchment) and no farther than WHEAT_MAX_DIST from a CORN tile (beyond that
	/// the 5-CORN/150-tick haul can't keep the buffer above the stall line). Inns
	/// honour WHEAT_MAX_DIST too (same haul logic, hungrier). Chebyshev tiles.
	static const int CORTEX_SWARM_MIN_SPACING = 6;
	static const int CORTEX_WHEAT_MAX_DIST    = 5;
	/// A swarm's footprint edge must sit within this many Chebyshev tiles of a CORN
	/// tile so haulers reach wheat in one short trip. Stricter than the shared
	/// CORTEX_WHEAT_MAX_DIST corner check (which still gates inns); measured from the
	/// footprint EDGE via anyCornWithin, not the top-left corner. AI-design rule.
	static const int CORTEX_SWARM_WHEAT_EDGE_DIST = 2;
	/// A new swarm or inn must have at least CORTEX_WHEAT_MIN_TILES HARVESTABLE wheat
	/// (CORN) tiles within CORTEX_WHEAT_MIN_TILES_RADIUS Chebyshev tiles of its
	/// footprint edge. "Harvestable" == CORN AND not forbidden for this team: the
	/// checkerboard wheat-protection paint forbids half the field (forbidden blocks
	/// harvest, MapGradientGlobal.cpp:135), and depleted tiles are no longer CORN at
	/// all. The other wheat gates (nearestCornDist / anyCornWithin) only require ONE
	/// CORN tile in reach and count forbidden tiles, which let a swarm land next to a
	/// nearly-exhausted patch and an inn land on a field whose harvestable half was
	/// already gone. Requiring a real cluster of harvestable wheat keeps wheat-fed
	/// buildings on a field that can actually sustain them. AI-design rule, no engine
	/// analogue.
	static const int CORTEX_WHEAT_MIN_TILES        = 5;
	static const int CORTEX_WHEAT_MIN_TILES_RADIUS = 5;
	/// Runtime wheat-starvation throttle for an EXISTING swarm (the worker-tuning loop,
	/// CortexPolicy Priority 1.5): while a swarm has fewer than
	/// CORTEX_SWARM_WHEAT_STARVED_TILES non-forbidden CORN tiles within
	/// CORTEX_SWARM_WHEAT_STARVED_RADIUS Chebyshev tiles of its footprint, cap its
	/// worker count at CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP — extra haulers cannot
	/// find wheat to harvest and just idle or thrash the depleted patch. A wider radius
	/// than the placement gate (the swarm is already there; this watches its catchment
	/// drain over time, not just the spot it was built on). AI-design rule.
	static const int CORTEX_SWARM_WHEAT_STARVED_TILES      = 5;
	static const int CORTEX_SWARM_WHEAT_STARVED_RADIUS     = 10;
	static const int CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP = 1;
	/// Bound on the nearest-CORN radial scan (CortexPlacement::nearestCornDist):
	/// tiles beyond this report "no wheat in reach" (-1). A few past WHEAT_MAX_DIST
	/// so the supply-distance expansion trigger can still measure "just out of range".
	static const int CORTEX_WHEAT_SCAN_CAP = 12;
	/// Supply-distance expansion trigger: a swarm pinned at its worker cap whose
	/// nearest CORN is beyond this has outrun its local wheat — warrant a NEW swarm
	/// near fresh wheat rather than piling more haulers onto the starved one.
	static const int CORTEX_SWARM_SUPPLY_RADIUS = 5;

	/// Inn worker-access clearance (AI-design placement rule, no engine analogue).
	/// An inn may touch a building on at most CORTEX_INN_MAX_TOUCH_SIDES of its four
	/// sides — the side that connects it to the colony. Every other side keeps at
	/// least CORTEX_INN_SIDE_CLEARANCE empty tiles between the inn and any building so
	/// workers can stream to the inn and to the wheat behind it. Diagonal corners
	/// count toward both adjacent sides, so a corner building occupies two sides.
	/// Chebyshev tiles. Enforced in both directions by Cortex::placeCandidates:
	/// placing an inn checks its own sides; placing any other building checks it does
	/// not open a second side on an existing inn.
	static const int CORTEX_INN_MAX_TOUCH_SIDES = 1;
	static const int CORTEX_INN_SIDE_CLEARANCE  = 2;
	/// Minimum Chebyshev spacing between two inns. Inns piled together split one
	/// wheat catchment and waste feed coverage, so a new inn must sit at least this
	/// far from every existing inn (same rationale as CORTEX_SWARM_MIN_SPACING).
	static const int CORTEX_INN_MIN_SPACING     = 6;
	/// Wheat-lane clearance: only swarms and inns may sit within this many Chebyshev
	/// tiles of a CORN tile. Every other building type is pushed back to a distance
	/// > CORTEX_WHEAT_CLEAR_DIST so it does not block workers harvesting the field.
	/// Inclusive reject (distance == CLEAR_DIST is still "within").
	static const int CORTEX_WHEAT_CLEAR_DIST    = 4;
	/// Maximum Chebyshev gap between a NEW non-wheat-fed building's footprint EDGE and
	/// the nearest existing building's footprint edge. Keeps tech/military buildings
	/// (school, racetrack, hospital, pool, barracks) clustered with the colony instead
	/// of being placed far away by the soft compactness score alone. Inns and swarms
	/// are exempt (they follow the wheat, not the colony). AI-design rule.
	static const int CORTEX_MAX_BUILD_EDGE_DIST = 10;

	// --- swim / pool tuning (v9, all tunable AI design choices) -------------
	// A swimming pool (SWIMSPEED_BUILDING) trains the SWIM ability of WORKERS and
	// WARRIORS (explorers fly and have zero SWIM performance — game/entities/Race.cpp).
	// A swimmer crosses water tiles that block a non-swimmer entirely
	// (Map::isHardSpaceForGroundUnit's canSwim gate), which (a) unlocks ALGA — a
	// basic food resource that grows only on water and so is reachable only by
	// swimmers — and (b) opens water-separated land: fresh wheat patches across a
	// channel, and a shorter/only route to a water-locked enemy. We build a pool
	// when an explorer has revealed reachable algae OR when allowing swim materially
	// expands the colony's reachable area (mirrors the intent of AICastor's
	// computeNeedSwim, ai/castor/State.cpp:82-109 — though we use the correct
	// direction: swim WANTED when the swim-reach is the larger of the two).

	/// Reach-expansion gate: build a pool when the swim-reachable tile count exceeds
	/// the land-only count by more than NUMER/DENOM (here 7/5 == 1.4, i.e. swimming
	/// reaches >40% more of the map than walking does). Integer ratio so the test is
	/// exact and deterministic: swimWaterReach * DENOM > swimLandReach * NUMER.
	static const int CORTEX_SWIM_REACH_GAIN_NUMER = 7;
	static const int CORTEX_SWIM_REACH_GAIN_DENOM = 5;
	/// Bound on the reach flood-fill radius (Chebyshev tiles from the colony). Keeps
	/// the twice-per-decision-cycle BFS cheap and scopes "reach" to the colony's
	/// vicinity ("relative proximity"), so a far-off lake the colony will never work
	/// does not by itself justify a pool. Generous enough to span a normal starter
	/// region plus the water channel beside it.
	static const int CORTEX_SWIM_REACH_RADIUS = 24;
}
