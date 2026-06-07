// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include <SDL_stdinc.h>

// AICortex shared data types: the Observation and Action structs that sit
// between the three layers (observation -> policy -> action). See
// docs/AI/cortex/README.md for the design rationale.
//
// Both structs are deliberately POD (no pointers, no std::string, no virtuals)
// so that a future ML policy can serialize an Observation straight to a tensor
// and an Action straight from a discrete distribution. Every field is
// fixed-shape and bounded; per-enemy data lives in a fixed-size array padded
// with the "no enemy in this slot" sentinel. Bump the *_VERSION constant on any
// layout change so trained models can bind to a known shape.
//
// CONTRACT NOTE (the frozen Phase-1 economy layout): CortexObservation is a
// *superset* of every fact Nicowar's check_phases() decides on
// (glob2/src/ai/nicowar/Phases.cpp), so the policy never has to reach past this
// struct into Game*. The field comments name the exact TeamStat / Game source
// each value mirrors. The cortex-local CORTEX_* size constants mirror the engine
// constants (NB_UNIT_LEVELS, IntBuildingType::NB_BUILDING, NB_BUILDING_LONG_LEVELS,
// Team count); CortexObservation.cpp static_asserts that they stay in sync so
// this header itself stays free of heavy engine includes.

namespace Cortex
{
	/// Layout version of CortexObservation. Bump on any field add/remove/resize.
	/// v1 was the first economy-phase layout. v2 (2026-06-02) dropped the dead
	/// totalFood/totalFoodCapacity mirrors (TeamStat never writes them — always 0)
	/// and added feedCapacity + swarmsProducing for the production-throttle.
	/// v3 (2026-06-03, Phase-3 combat increment) filled the visibility-gated enemy
	/// intel (EnemySlot.totalUnit/totalBuilding) and added the war-flag targeting
	/// surface (flagTargets[], defenseTarget), own-army signals
	/// (attackStrengthLevel[], warFlagsActive) and the defense trigger
	/// (unitsUnderAttack/buildingsUnderAttack).
	/// v4 (2026-06-04, Phase-2 upgrade increment) added the upgrade-decision
	/// signals: maxBuildLevel (== team->maxBuildLevel(), the engine gate on whether
	/// a building can be upgraded) and upgradableCount[] (per-type count of
	/// finished instances that pass the full engine Upgradable predicate right now).
	/// v5 (2026-06-04, wheat-protection increment) added the wheat-sustainability
	/// signals: wheatOpenMargin (the seeded open-margin N; the wheat executor reads it),
	/// wheatProtectAddCount/wheatProtectDelCount (the reconcile diff against the
	/// team's current forbidden paint), and swarmsProducingExplorer (so the policy
	/// can revert the early-explorer mix cleanly without reading raw swarm ratios).
	/// v6 (2026-06-05, closed-loop wheat-economy increment) added the per-building
	/// economy signals: TrackedBuilding arrays for our swarms (trackedSwarms[]) and
	/// inns (trackedInns[]) carrying each building's gid, CORN buffer, maxCorn,
	/// maxUnitWorking, occupancy, and nearest-CORN distance — the inputs to the
	/// worker-tuning control loop and the supply-distance expansion gate — plus
	/// BuildCandidate.wheatDist (a candidate site's distance to the nearest wheat)
	/// for the min-swarm-spacing / max-wheat-distance placement constraints.
	/// v7 (2026-06-05, pre-combat panic-defense increment) added the signals the
	/// economy-phase panic response needs: swarmsProducingWarrior (count of swarms
	/// already flipped to 100%-warrior production, so the policy knows when the flip
	/// is done) and TrackedBuilding.priority (each swarm/inn's engine priority
	/// -1/0/+1, so the policy can raise swarms to high priority under attack and
	/// restore them afterwards). HEAL_BUILDING (hospital) build candidates are now
	/// surfaced too, for the panic hospital build.
	/// v8 (2026-06-05, expand-vs-upgrade increment) added walkLevel[] — the WALK
	/// ability-level histogram (stat->upgradeState[WALK][lvl], i.e. workers AND
	/// warriors; explorers have zero WALK performance so never contribute). It is
	/// the racetrack (WALKSPEED) expand-vs-upgrade gate's "% of speed-eligible units
	/// already maxed at the current racetrack level" numerator, the WALK analogue of
	/// the existing buildLevel[] (school gate) and attackStrengthLevel[] (barracks
	/// gate) slices.
	/// v9 (2026-06-05, swim/pool increment) added the swimming-pool decision signals:
	/// algaeDiscovered (an explorer has revealed a takeable ALGA tile — a swim-only
	/// food source), and swimLandReach/swimWaterReach (tiles reachable from the colony
	/// WITHOUT vs WITH swimming, from a bounded flood-fill in CortexWater). The policy
	/// builds a swimming pool when algae is in reach OR swimming meaningfully expands
	/// the reachable area (the Castor-style reach-expansion predicate). SWIMSPEED
	/// build candidates are surfaced too, for the pool placement.
	/// v10 (2026-06-05, idle-worker-into-construction increment) added the
	/// construction-site signals: trackedSites[]/siteCount, each carrying a site's
	/// gid, current maxUnitWorking, and deliveriesLeft (resource hauler-trips still
	/// needed) — the inputs to the rule that pours free workers into in-progress
	/// builds, bounded by the work remaining.
	/// v11 (2026-06-05, worker-surplus production-throttle) added
	/// swarmsProducingWorker - the count of finished swarms whose WORKER ratio is
	/// nonzero, the symmetric peer of swarmsProducingExplorer/Warrior. It lets the
	/// policy stop minting workers (warriors-and-scout mix) once idle labour piles
	/// up and resume once it drains, reading the on/off state without raw ratios.
	/// v12 (2026-06-06, wheat-starved swarm throttle) added
	/// TrackedBuilding.harvestableWheatNearby — the count of non-forbidden CORN tiles
	/// within CORTEX_SWARM_WHEAT_STARVED_RADIUS of a swarm's footprint (filled for
	/// swarms; -1 for inns / when unknown). The worker-tuning loop caps a swarm at
	/// CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP workers while this is below
	/// CORTEX_SWARM_WHEAT_STARVED_TILES — no point staffing more haulers than there is
	/// reachable wheat to harvest.
	/// v13 (2026-06-06, inn hauler ceiling = collectable demand) added
	/// TrackedBuilding.restockTripsNeeded — for inns, the sum over every stocked
	/// resource of its (capacity - stock)/multiplier deficit in hauler TRIPS, counting
	/// ONLY resources currently collectable from the inn (Map::ressourceAvailable: corn
	/// that is reachable, fruit whose tile is in sight — fruit is visibleToBeCollected,
	/// so fogged fruit is excluded). The worker-tuning loop sets the inn's maxUnitWorking
	/// to this (clamped), so the hauler ceiling scales with inn level and includes fruit
	/// without over-staffing for fogged/unreachable resources. -1 for swarms / unknown.
	/// v15 (2026-06-06, offense-hold relocation) added flagPosture + offenseHoldUntil:
	/// the action layer's RAM-only hysteresis state, echoed into the observation so the
	/// PURE policy makes the hold-vs-recall (thrash-damper) decision itself instead of
	/// it living in the downstream action layer. AICortex still OWNS/mutates the state
	/// on flag placement and persists it; these obs fields are a per-cycle read-only
	/// mirror (not serialized — the observation is recomputed every cycle).
	static const Uint32 OBSERVATION_VERSION = 15;
	/// Layout version of CortexAction. Bump on any field add/remove/resize.
	/// v2 (2026-06-02) added ACTION_SET_PRODUCTION + productionRatio[].
	/// v3 (2026-06-03) added the war-flag action kinds (ACTION_PLACE_WAR_FLAG,
	/// ACTION_PLACE_DEFENSE_FLAG, ACTION_CLEAR_FLAGS) + flagRadius/unitCount.
	/// v4 (2026-06-04) added ACTION_UPGRADE_BUILDING (upgrade an existing finished
	/// building to its next level via OrderConstruction; reuses buildingType).
	/// v5 (2026-06-04) added ACTION_PROTECT_WHEAT + wheatOpenMargin (paint the
	/// checkerboard forbidden pattern over our wheat for sustainability). REMOVED in
	/// v10 — wheat upkeep moved to a per-cycle parallel pass (see below).
	/// v6 (2026-06-05) added ACTION_TUNE_WORKERS + swarmWorkers[]/innWorkers[]:
	/// the per-tracked-building desired maxUnitWorking (-1 == leave unchanged),
	/// applied via OrderModifyBuilding with action-layer dedup. Arrays are indexed
	/// in lockstep with obs.trackedSwarms[]/trackedInns[].
	/// v7 (2026-06-05) added ACTION_SET_PRIORITY + priorityTarget: set every tracked
	/// swarm's engine priority to priorityTarget (-1/0/+1) via OrderChangePriority,
	/// action-layer dedup'd against each building's current priority. The panic
	/// defense raises swarms to +1 under attack and drops them back to 0 after.
	/// v8 (2026-06-05) split ACTION_SET_PRIORITY into priorityTarget (the FIRST/
	/// primary swarm) + priorityRest (all other swarms): the primary swarm is held at
	/// HIGH in steady state so it wins worker contention, while later swarms stay
	/// NORMAL. Panic still raises every swarm to HIGH (first == rest).
	/// v9 (2026-06-05) added siteWorkers[] to ACTION_TUNE_WORKERS: desired worker cap
	/// per construction site (trackedSites[]), to pour idle workers into in-progress
	/// builds. Applied via OrderModifyBuilding with the same per-target dedup.
	/// v10 (2026-06-06) removed ACTION_PROTECT_WHEAT + the wheatOpenMargin field
	/// (the v5 additions): wheat-forbidden upkeep no longer competes for the cycle's
	/// single action slot. It now runs every decision cycle in parallel with the
	/// primary action — CortexPolicy::wantWheatProtection decides, AICortex::
	/// enqueueWheatForbidden paints — reading the open-margin from the AICortex member.
	static const Uint32 ACTION_VERSION = 10;

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
	/// Self-imposed upper bound on warriors assigned to one war flag (the flag's
	/// maxUnitWorking). Cortex's own bound for the action param, not an engine cap.
	static const int CORTEX_MAX_FLAG_UNITS = 32;

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
	/// Over-bounds the economy (MAX_SWARMS==3) and combat (COMBAT_MAX_SWARMS==5)
	/// swarm caps; inns can be more numerous, so its bound is larger. Buildings
	/// past these counts (index-scan order) are simply not individually tuned.
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

	/// Upper bound on swarms the colony will grow to (recovery + wheat-patch
	/// expansion). A team starts with one swarm; once the opening build-out is done
	/// the policy may add more, one per newly-discovered nearby wheat patch (the
	/// placement helper already forces CORTEX_SWARM_MIN_SPACING between swarms, so a
	/// second swarm necessarily sits on a DIFFERENT patch). Capped low to avoid a
	/// swarm sprawling onto every wheat tile on the map. Tunable AI design choice.
	static const int CORTEX_MAX_SWARMS = 3;

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

	/// One enemy team projected into the observation. POD, bounded.
	struct EnemySlot
	{
		Sint32 active;        ///< 0 = no enemy in this slot (sentinel), 1 = present and alive.
		Sint32 teamNumber;    ///< Engine team id, or -1 when inactive.
		Sint32 totalUnit;     ///< Count of this enemy's units on tiles CURRENTLY visible to us (FOW-gated; not ground truth).
		Sint32 totalBuilding; ///< Count of this enemy's buildings we have ever discovered (Building::seenByMask & team->me).
		Sint32 prestige;      ///< Always 0 — prestige is not a visible signal, left unfilled to avoid a fog-of-war cheat.
	};

	/// One candidate location for placing a building, produced by the placement
	/// helper (Cortex::placeCandidates) and surfaced in the observation. POD.
	struct BuildCandidate
	{
		Sint32 valid; ///< 0 = empty slot (no candidate here), 1 = usable location.
		Sint32 x;     ///< Map tile x of the building's top-left corner. Valid only if valid==1.
		Sint32 y;     ///< Map tile y.
		Sint32 score; ///< Relative placement score; higher is better. Ranking only, not normalized.
		Sint32 wheatDist; ///< Chebyshev distance from this footprint to the nearest CORN tile, or -1 if none within CORTEX_WHEAT_SCAN_CAP. Meaningful for wheat-fed sites (swarm/inn); left -1 for flag targets. Filled by placeCandidates.
	};

	/// One of our own existing wheat-fed buildings (a swarm or an inn), projected
	/// into the observation so the pure policy can run the per-building worker-tuning
	/// control loop. POD, bounded; gid lets the action layer target an
	/// OrderModifyBuilding without the policy holding a pointer.
	struct TrackedBuilding
	{
		Sint32 valid;          ///< 0 = empty slot.
		Sint32 gid;            ///< Building::gid (OrderModifyBuilding target), or -1 when invalid.
		Sint32 corn;           ///< ressources[CORN] — the current wheat buffer driving the control loop.
		Sint32 maxCorn;        ///< type->maxRessource[CORN] — the buffer ceiling.
		Sint32 maxUnitWorking; ///< Current maxUnitWorking (worker request); the value the loop nudges +/-1.
		Sint32 unitsInside;    ///< unitsInside.size() — occupancy (inns: units feeding/queued).
		Sint32 maxUnitInside;  ///< type->maxUnitInside — occupancy ceiling.
		Sint32 nearestWheatDist; ///< Chebyshev to the nearest CORN tile (supply-distance expansion signal), or -1 if none within CORTEX_WHEAT_SCAN_CAP.
		Sint32 harvestableWheatNearby; ///< Swarms only: count of non-forbidden CORN tiles within CORTEX_SWARM_WHEAT_STARVED_RADIUS of the footprint (the wheat-starved worker-throttle signal). -1 for inns / when unknown (game absent).
		Sint32 restockTripsNeeded; ///< Inns only: collectable restock demand in hauler trips (Σ over stocked resources of (cap-stock)/multiplier, counting only resources currently reachable/in-sight via Map::ressourceAvailable). The inn-hauler ceiling. -1 for swarms / when unknown (game absent).
		Sint32 priority;       ///< Building::priority (-1/0/+1) — lets the policy raise/restore swarm priority for the panic defense.
		Sint32 ticksSinceFinished; ///< Inns only: ticks since Cortex first saw this inn finished (the post-build tune-cooldown clock); -1 = unknown / not tracked. Stamped by AICortex after observe(); swarms leave it -1.
	};

	/// One of our own CONSTRUCTION SITES (a new build or an in-progress upgrade),
	/// projected into the observation so the policy can pour idle workers into it.
	/// POD, bounded; gid targets an OrderModifyBuilding without holding a pointer.
	struct TrackedSite
	{
		Sint32 valid;          ///< 0 = empty slot.
		Sint32 gid;            ///< Building::gid (OrderModifyBuilding target), or -1 when invalid.
		Sint32 maxUnitWorking; ///< Current maxUnitWorking (worker cap) on the site.
		Sint32 deliveriesLeft; ///< Resource hauler-trips still needed to finish the site = sum over resources of ceil((maxRessource-ressources)/multiplier). Caps how many workers can usefully build it.
	};

	/// The full feature vector handed to the policy layer. Built by
	/// Cortex::observe(); read by CortexPolicy::decide(). The policy must read
	/// ONLY this struct — never Game* directly (see README anti-pattern).
	struct CortexObservation
	{
		Uint32 version;       ///< == OBSERVATION_VERSION; lets the policy reject stale layouts.
		Sint32 valid;         ///< 0 when no observation has been taken yet.

		Sint32 tick;          ///< game->stepCounter at observation time.

		// --- own economy: population (TeamStat unless noted) ---
		Sint32 totalUnit;     ///< stat->totalUnit.
		Sint32 workers;       ///< stat->numberUnitPerType[WORKER].
		Sint32 explorers;     ///< stat->numberUnitPerType[EXPLORER].
		Sint32 warriors;      ///< stat->numberUnitPerType[WARRIOR].
		Sint32 freeWorkers;   ///< stat->isFree[WORKER] (workers not assigned a job).
		Sint32 totalFree;     ///< stat->totalFree (idle units of all types).
		Sint32 totalNeeded;   ///< stat->totalNeeded (jobs requested across all buildings).

		// --- own economy: food / health pressure ---
		Sint32 totalBuilding;     ///< stat->totalBuilding (finished buildings only).
		Sint32 feedCapacity;      ///< Units the colony's inns can feed: sum of type->maxUnitInside over finished buildings with maxUnitWorking && type->canFeedUnit (mirrors AICastor's foodSum). The live food-supply signal — totalFood/totalFoodCapacity were dropped (TeamStat never populates them).
		Sint32 starvingUnits;     ///< team->stats.getStarvingUnits() == stat->needFoodCritical (hungry AND losing HP).
		Sint32 needFood;          ///< stat->needFood (hungry, not being fed, HP still full — early warning).
		Sint32 needFoodCritical;  ///< stat->needFoodCritical (== starvingUnits).
		Sint32 needFoodNoInns;    ///< stat->needFoodNoInns (hungry, not upgrading).
		Sint32 needHeal;          ///< stat->needHeal.

		// --- own economy: prestige ---
		Sint32 prestige;          ///< team->prestige.

		// --- production state ---
		// Count of FINISHED swarms whose total ratio (WORKER+EXPLORER+WARRIOR) is
		// nonzero, i.e. currently producing units. Lets the pure policy tell
		// whether a halt/resume ACTION_SET_PRODUCTION is actually needed without
		// reading raw per-swarm ratios — so it doesn't re-emit the order every cycle.
		Sint32 swarmsProducing;

		// --- training / upgrade levels, indexed by unit level 0..CORTEX_UNIT_LEVELS-1 ---
		// Each array mirrors one TeamStat upgrade slice that Nicowar's phases read.
		Sint32 buildLevel[CORTEX_UNIT_LEVELS];               ///< stat->upgradeState[BUILD][lvl] (any unit type; only workers have BUILD performance, so effectively per-worker). School (SCIENCE) expand-vs-upgrade gate.
		Sint32 walkLevel[CORTEX_UNIT_LEVELS];                ///< stat->upgradeState[WALK][lvl] (workers AND warriors; explorers have zero WALK performance). Racetrack (WALKSPEED) expand-vs-upgrade gate. Denominator is workers+warriors.
		Sint32 attackSpeedLevel[CORTEX_UNIT_LEVELS];         ///< stat->upgradeState[ATTACK_SPEED][lvl] (== trained-warrior count by level).
		Sint32 attackStrengthLevel[CORTEX_UNIT_LEVELS];      ///< stat->upgradeState[ATTACK_STRENGTH][lvl]. A warrior joins a flag only if its min(ATTACK_SPEED,ATTACK_STRENGTH) level clears the flag's minLevelToFlag.
		Sint32 workerSwimLevel[CORTEX_UNIT_LEVELS];          ///< stat->upgradeStatePerType[WORKER][SWIM][lvl]; index 0 == cannot swim.
		Sint32 explorerMagicGroundLevel[CORTEX_UNIT_LEVELS]; ///< stat->upgradeStatePerType[EXPLORER][MAGIC_ATTACK_GROUND][lvl].

		// --- upgrade-decision signals (Phase-2 v4) ---
		// maxBuildLevel == team->maxBuildLevel() (team/TeamRouting.cpp:245-259): the
		// highest BUILD level among our workers (0..CORTEX_UNIT_LEVELS-1). This is the
		// exact engine gate on whether a building can be upgraded — a finished
		// building at level L can be upgraded only when maxBuildLevel > L
		// (gui/GameGUIInput.cpp:426). It rises when workers train BUILD at a school,
		// so it is the dependency that ties "build a school" to "now I can upgrade".
		Sint32 maxBuildLevel;
		// Per building type: count of FINISHED instances that pass the full engine
		// Upgradable predicate RIGHT NOW — ALIVE, not a site, hp == hpMax, no
		// construction in progress (constructionResultState == NO_CONSTRUCTION),
		// nextLevel != -1 (not already max level 2), maxBuildLevel > type->level, and
		// the larger next-level footprint fits (isHardSpaceForBuildingSite(UPGRADE)).
		// Lets the pure policy ask "can I upgrade a barracks/school?" without
		// re-deriving the engine's spatial/hp predicates. Bounded by Building::MAX_COUNT.
		Sint32 upgradableCount[CORTEX_BUILDING_TYPES];

		// --- buildings: full per-type, per-long-level histogram ---
		// Direct mirror of stat->numberBuildingPerTypePerLevel. Read it through
		// the cortex* helpers (finished vs site, by level) below.
		Sint32 buildingCountPerLevel[CORTEX_BUILDING_TYPES][CORTEX_BUILDING_LONG_LEVELS];

		// --- candidate build locations, per building type ---
		// Filled by the placement helper for the building types the AI may build;
		// other types' slots stay valid==0. ACTION_BUILD.locationSlot indexes the
		// second dimension for ACTION_BUILD.buildingType.
		BuildCandidate buildCandidates[CORTEX_BUILDING_TYPES][CORTEX_BUILD_CANDIDATES];

		// --- combat: war-flag targeting surface (Phase-3 v3) ---
		// OFFENSE targets: discovered enemy buildings, nearest-first (BuildCandidate
		// reused — x/y is the enemy building's tile, score ranks proximity). Filled
		// ONLY from buildings we have legitimately seen (Building::seenByMask), never
		// from unfogged truth. ACTION_PLACE_WAR_FLAG.locationSlot indexes this array.
		BuildCandidate flagTargets[CORTEX_FLAG_TARGETS];
		// DEFENSE target: the single best spot to recall the army to — the position
		// of the friendly building currently taking the most fire (underAttackTimer).
		// valid==0 when nothing is under attack. ACTION_PLACE_DEFENSE_FLAG uses this.
		BuildCandidate defenseTarget;
		// Count of our own live WAR_FLAG virtual buildings (reading our OWN state is
		// not a cheat). Lets the policy/action layer know a flag already exists so it
		// moves/clears rather than stacking duplicates.
		Sint32 warFlagsActive;
		// Count of VISIBLE enemy units currently inside our war flag's stay-range (the
		// flag's own unitStayRange, warp-safe Chebyshev distance). Drives the hold-only
		// straggler grace: while >0 the policy holds a purposeless flag in place so the
		// army finishes off survivors before the flag is retired. FOW-gated like
		// enemies[].totalUnit, so it is never a fog-of-war cheat. 0 when no flag exists.
		Sint32 enemyUnitsNearFlag;
		// Defense triggers: how many of our own units / buildings are currently under
		// attack (underAttackTimer > 0). Nonzero => recall the army to defend.
		Sint32 unitsUnderAttack;
		Sint32 buildingsUnderAttack;

		// --- offense-hold hysteresis state (v15) ---
		// The flag posture the action layer last committed (a CortexFlagPosture), and
		// the tick until which an OFFENSE commitment is protected from a minor-
		// harassment defensive recall (0 == no hold active). These mirror the AICortex
		// RAM-only members of the same names: AICortex OWNS and MUTATES the state when
		// it actually places a flag (re-arming offenseHoldUntil on a war-flag placement
		// is an execution side-effect), and echoes the current values into the
		// observation each cycle BEFORE policy.decide() so the PURE policy can make the
		// hold-vs-recall hysteresis decision itself instead of having it hidden in the
		// downstream executor. Not derived from engine state and NOT serialized via the
		// observation (the observation is recomputed every cycle and never saved); the
		// canonical persisted copy lives in AICortex (load/save), unchanged.
		Sint32 flagPosture;     ///< == AICortex.flagPosture (a CortexFlagPosture).
		Sint32 offenseHoldUntil;///< == AICortex.offenseHoldUntil (tick the offense hold expires; 0 == none).

		// --- wheat sustainability (v5) ---
		// The open margin N drawn once per game (AICortex, via syncRand) and echoed
		// through the observation so the pure policy reads it like any other feature.
		// It is the ML seam (a learned policy later OUTPUTS N here instead of echoing
		// the seeded value); the wheat executor reads it each cycle via the AICortex
		// member to drive the checkerboard scan.
		Sint32 wheatOpenMargin;
		// Reconcile diff between the checkerboard we WANT over our wheat right now and
		// the team's CURRENT forbidden paint (footprints excluded). Counts-only summary
		// from a bounded colony-region scan; the full tile masks are rebuilt in the
		// action layer. CortexPolicy::wantWheatProtection returns true only when either is > 0.
		Sint32 wheatProtectAddCount; ///< tiles to newly forbid (desired - current).
		Sint32 wheatProtectDelCount; ///< tiles to un-forbid (current - desired: wheat gone/out of view).
		// Count of FINISHED swarms whose EXPLORER production ratio is nonzero. Lets the
		// pure policy revert the one-shot early-explorer mix back to workers-only after
		// the explorer is made, without reading raw per-swarm ratios (which it can't).
		Sint32 swarmsProducingExplorer;
		// Count of FINISHED swarms producing 100% warriors (WARRIOR ratio nonzero AND
		// WORKER+EXPLORER both zero). Lets the panic defense tell when the flip to
		// all-warrior production is complete, so it stops re-issuing and moves on to
		// the next panic step — without reading raw per-swarm ratios.
		Sint32 swarmsProducingWarrior;
		// Count of FINISHED swarms whose WORKER production ratio is nonzero. The
		// symmetric counterpart of the explorer/warrior counts above: it lets the
		// pure policy read the worker-surplus suppression state (workers on vs off)
		// without reaching at raw per-swarm ratios, so it can both apply and revert
		// the "stop minting workers while idle labour piles up" mix with the same
		// per-cycle dedup the explorer slice uses.
		Sint32 swarmsProducingWorker;

		// --- closed-loop wheat economy (v6) ---
		// Our own FINISHED swarms and inns, one TrackedBuilding each (index-scan
		// order over team->myBuildings, capped at the array bounds). The pure policy
		// reads each building's CORN buffer + maxUnitWorking to nudge worker counts
		// (ACTION_TUNE_WORKERS) and its nearestWheatDist to decide expansion. Reading
		// our OWN buildings is not a fog cheat. *Count is the number of valid entries.
		Sint32 swarmCount;
		TrackedBuilding trackedSwarms[CORTEX_MAX_TRACKED_SWARMS];
		Sint32 innCount;
		TrackedBuilding trackedInns[CORTEX_MAX_TRACKED_INNS];

		// Our own construction sites (new builds + in-progress upgrades), one
		// TrackedSite each. The policy raises each site's worker cap toward the free-
		// worker count, bounded by the deliveries still needed (ACTION_TUNE_WORKERS).
		Sint32 siteCount;
		TrackedSite trackedSites[CORTEX_MAX_TRACKED_SITES];

		// --- map / global facts ---
		Sint32 fruitOnMap;    ///< 1 if any fruit resource exists on the map (Map query).
		Sint32 totalPrestige; ///< game->totalPrestige (all teams; for the explorer-defence heuristic).

		// --- swim / pool decision (v9) ---
		// algaeDiscovered: 1 if any takeable ALGA tile is currently DISCOVERED (an
		// explorer/colony has revealed it; FOW-gated, not unfogged truth). Algae is a
		// swim-only food source, so reachable algae is a direct reason to train SWIM.
		// swimLandReach / swimWaterReach: count of tiles reachable from the colony by a
		// bounded flood-fill WITHOUT swim (water blocks) vs WITH swim (water passes).
		// swimWaterReach >= swimLandReach always; the gap measures how much the map
		// opens up if our units learn to swim. Computed only while no pool exists yet
		// (CortexWater.assessSwim, gated in observe to bound cost); both 0 otherwise.
		// algaeReachable: 1 if a DISCOVERED takeable ALGA tile is 8-adjacent to a tile
		// the colony can reach by a GROUND path (the no-swim flood-fill) — i.e. a worker
		// can stand on the shore and harvest it now, with no swimming pool. Distinct from
		// algaeDiscovered (which counts any seen algae, including deep/walled-off algae a
		// non-swimmer can never reach): this is the "can we actually deliver algae to a
		// site" signal that gates ALGA-consuming builds (the school and its upgrades).
		// Computed every cycle (unlike the reach counts), since school upgrades happen
		// late, when a pool may already exist.
		Sint32 algaeDiscovered;
		Sint32 swimLandReach;
		Sint32 swimWaterReach;
		Sint32 algaeReachable;

		// --- opponents ---
		Sint32 enemyCount;    ///< Number of active slots below.
		EnemySlot enemies[MAX_ENEMY_SLOTS];
	};

	/// Discrete, bounded intents the policy can choose. Hierarchical: a real
	/// decision is (kind, plus a few bounded parameters), never an unbounded
	/// "(x, y)". Phase 1 wires NoOp and Build; later phases add kinds (and a
	/// version bump).
	enum CortexActionKind
	{
		ACTION_NOOP = 0,        ///< Do nothing this decision cycle.
		ACTION_BUILD,           ///< Place buildingType at buildCandidates[buildingType][locationSlot].
		ACTION_SET_PRODUCTION,  ///< Set every finished swarm's production ratio to productionRatio[].
		ACTION_PLACE_WAR_FLAG,  ///< Offense: ensure our single war flag sits on flagTargets[locationSlot] (create or move there), radius=flagRadius, warriors=unitCount.
		ACTION_PLACE_DEFENSE_FLAG,///< Defense: ensure our single war flag sits on defenseTarget (create or move there), radius=flagRadius, warriors=unitCount.
		ACTION_CLEAR_FLAGS,     ///< Remove our war flag (OrderDelete) if one exists — no offense/defense wanted right now.
		ACTION_UPGRADE_BUILDING,///< Upgrade one finished `buildingType` instance to its next level (engine OrderConstruction). The action layer resolves which instance (the bottleneck-eligible one) and the worker counts.
		// (Wheat-forbidden paint is NOT an action kind: it runs every cycle in
		// parallel with the primary action — see CortexPolicy::wantWheatProtection
		// and AICortex::enqueueWheatForbidden.)
		ACTION_TUNE_WORKERS,    ///< Set each tracked swarm/inn/site's maxUnitWorking to swarmWorkers[i]/innWorkers[i]/siteWorkers[i] (indexed in lockstep with obs.trackedSwarms[]/trackedInns[]/trackedSites[]); -1 == leave unchanged. The action layer dedups against the building's current maxUnitWorking and emits one OrderModifyBuilding per real change.
		ACTION_SET_PRIORITY,    ///< Set tracked-swarm engine priority via OrderChangePriority: the FIRST swarm (trackedSwarms[0], the primary/starting swarm) to priorityTarget, every other swarm to priorityRest (-1/0/+1 each). The action layer dedups against each swarm's current Building::priority and emits one order per real change.

		ACTION_KIND_COUNT
	};

	/// The policy's output: an action intent, not an engine Order. The action
	/// layer (AICortex::translateAction) turns it into one or more Orders. POD,
	/// versioned, bounded.
	struct CortexAction
	{
		Uint32 version;      ///< == ACTION_VERSION.
		Sint32 kind;         ///< A CortexActionKind value.
		Sint32 buildingType; ///< For ACTION_BUILD: an IntBuildingType::Number in [0, CORTEX_BUILDING_TYPES). Else -1.
		Sint32 locationSlot; ///< For ACTION_BUILD: index in [0, CORTEX_BUILD_CANDIDATES). For ACTION_PLACE_WAR_FLAG: index in [0, CORTEX_FLAG_TARGETS). Else -1.
		Sint32 productionRatio[CORTEX_UNIT_TYPES]; ///< For ACTION_SET_PRODUCTION: target swarm ratio [WORKER,EXPLORER,WARRIOR], each 0..CORTEX_MAX_RATIO ({0,0,0} = halt). Else all 0.
		Sint32 flagRadius;   ///< For ACTION_PLACE_*_FLAG: war-flag attraction radius (unitStayRange), clamped to [1, CORTEX_MAX_FLAG_RADIUS]. Else -1.
		Sint32 unitCount;    ///< For ACTION_PLACE_*_FLAG: warriors to summon (flag maxUnitWorking), clamped to [0, CORTEX_MAX_FLAG_UNITS]. Else -1.
		Sint32 swarmWorkers[CORTEX_MAX_TRACKED_SWARMS]; ///< For ACTION_TUNE_WORKERS: desired maxUnitWorking for trackedSwarms[i], or -1 to leave unchanged. Else all -1.
		Sint32 innWorkers[CORTEX_MAX_TRACKED_INNS];     ///< For ACTION_TUNE_WORKERS: desired maxUnitWorking for trackedInns[i], or -1 to leave unchanged. Else all -1.
		Sint32 siteWorkers[CORTEX_MAX_TRACKED_SITES];   ///< For ACTION_TUNE_WORKERS: desired maxUnitWorking for trackedSites[i] (pour idle workers into construction), or -1 to leave unchanged. Else all -1.
		Sint32 priorityTarget; ///< For ACTION_SET_PRIORITY: target engine priority (-1/0/+1) for the FIRST swarm (trackedSwarms[0]). Else CORTEX_PRIORITY_NONE.
		Sint32 priorityRest;   ///< For ACTION_SET_PRIORITY: target engine priority (-1/0/+1) for every NON-first swarm (trackedSwarms[1..]). Else CORTEX_PRIORITY_NONE.
	};

	// --- building-histogram accessors -------------------------------------
	// The long-level encoding (see CORTEX_BUILDING_LONG_LEVELS) lives only here.

	/// Finished buildings of `type` at internal level >= minLevel (0-based).
	inline Sint32 cortexFinishedBuildingsMinLevel(const CortexObservation& obs, int type, int minLevel)
	{
		Sint32 count = 0;
		for (int level = minLevel; level <= 2; level++) // three building levels: 0,1,2
			count += obs.buildingCountPerLevel[type][(level << 1) + 1]; // odd long-level == finished
		return count;
	}

	/// All finished buildings of `type`, any level.
	inline Sint32 cortexFinishedBuildings(const CortexObservation& obs, int type)
	{
		return cortexFinishedBuildingsMinLevel(obs, type, 0);
	}

	/// Construction sites of `type`, any level (even long-levels).
	inline Sint32 cortexBuildingSites(const CortexObservation& obs, int type)
	{
		return obs.buildingCountPerLevel[type][0]
		     + obs.buildingCountPerLevel[type][2]
		     + obs.buildingCountPerLevel[type][4];
	}

	/// Highest FINISHED building level (0,1,2) of `type`, or -1 if none finished.
	/// Reads the odd long-level slots (finished) high-to-low.
	inline Sint32 cortexMaxFinishedLevel(const CortexObservation& obs, int type)
	{
		for (int level = 2; level >= 0; level--)
			if (obs.buildingCountPerLevel[type][(level << 1) + 1] > 0)
				return level;
		return -1;
	}

	/// Count of `type` buildings currently mid-UPGRADE: a construction site of an
	/// already-raised level (1 or 2). A fresh level-0 site (even slot 0) is a NEW
	/// build, not an upgrade, so it is excluded. Lets the policy avoid stacking a
	/// second upgrade on a type that is already upgrading.
	inline Sint32 cortexBuildingsUpgrading(const CortexObservation& obs, int type)
	{
		return obs.buildingCountPerLevel[type][2]  // site of level 1 (0->1 upgrade)
		     + obs.buildingCountPerLevel[type][4]; // site of level 2 (1->2 upgrade)
	}

	/// Construct an empty/no-op observation with the current version stamped.
	inline CortexObservation makeEmptyObservation()
	{
		CortexObservation obs;
		obs.version = OBSERVATION_VERSION;
		obs.valid = 0;
		obs.tick = 0;

		obs.totalUnit = 0;
		obs.workers = 0;
		obs.explorers = 0;
		obs.warriors = 0;
		obs.freeWorkers = 0;
		obs.totalFree = 0;
		obs.totalNeeded = 0;

		obs.totalBuilding = 0;
		obs.feedCapacity = 0;
		obs.starvingUnits = 0;
		obs.needFood = 0;
		obs.needFoodCritical = 0;
		obs.needFoodNoInns = 0;
		obs.needHeal = 0;

		obs.prestige = 0;
		obs.swarmsProducing = 0;

		for (int i = 0; i < CORTEX_UNIT_LEVELS; i++)
		{
			obs.buildLevel[i] = 0;
			obs.walkLevel[i] = 0;
			obs.attackSpeedLevel[i] = 0;
			obs.attackStrengthLevel[i] = 0;
			obs.workerSwimLevel[i] = 0;
			obs.explorerMagicGroundLevel[i] = 0;
		}

		obs.maxBuildLevel = 0;

		for (int t = 0; t < CORTEX_BUILDING_TYPES; t++)
		{
			obs.upgradableCount[t] = 0;
			for (int l = 0; l < CORTEX_BUILDING_LONG_LEVELS; l++)
				obs.buildingCountPerLevel[t][l] = 0;
			for (int c = 0; c < CORTEX_BUILD_CANDIDATES; c++)
			{
				obs.buildCandidates[t][c].valid = 0;
				obs.buildCandidates[t][c].x = 0;
				obs.buildCandidates[t][c].y = 0;
				obs.buildCandidates[t][c].score = 0;
				obs.buildCandidates[t][c].wheatDist = -1;
			}
		}

		for (int i = 0; i < CORTEX_FLAG_TARGETS; i++)
		{
			obs.flagTargets[i].valid = 0;
			obs.flagTargets[i].x = 0;
			obs.flagTargets[i].y = 0;
			obs.flagTargets[i].score = 0;
			obs.flagTargets[i].wheatDist = -1;
		}
		obs.defenseTarget.valid = 0;
		obs.defenseTarget.x = 0;
		obs.defenseTarget.y = 0;
		obs.defenseTarget.score = 0;
		obs.defenseTarget.wheatDist = -1;
		obs.warFlagsActive = 0;
		obs.enemyUnitsNearFlag = 0;
		obs.unitsUnderAttack = 0;
		obs.buildingsUnderAttack = 0;

		// Neutral defaults; AICortex overwrites these with its live RAM-only
		// hysteresis state after observe() returns, before policy.decide() (the
		// wheatOpenMargin pattern: observe leaves a placeholder, AICortex injects).
		obs.flagPosture = CORTEX_POSTURE_NONE;
		obs.offenseHoldUntil = 0;

		obs.wheatOpenMargin = 0;
		obs.wheatProtectAddCount = 0;
		obs.wheatProtectDelCount = 0;
		obs.swarmsProducingExplorer = 0;
		obs.swarmsProducingWarrior = 0;
		obs.swarmsProducingWorker = 0;

		obs.swarmCount = 0;
		obs.innCount = 0;
		for (int i = 0; i < CORTEX_MAX_TRACKED_SWARMS; i++)
		{
			obs.trackedSwarms[i].valid = 0;
			obs.trackedSwarms[i].gid = -1;
			obs.trackedSwarms[i].corn = 0;
			obs.trackedSwarms[i].maxCorn = 0;
			obs.trackedSwarms[i].maxUnitWorking = 0;
			obs.trackedSwarms[i].unitsInside = 0;
			obs.trackedSwarms[i].maxUnitInside = 0;
			obs.trackedSwarms[i].nearestWheatDist = -1;
			obs.trackedSwarms[i].harvestableWheatNearby = -1;
			obs.trackedSwarms[i].restockTripsNeeded = -1;
			obs.trackedSwarms[i].priority = 0;
		}
		for (int i = 0; i < CORTEX_MAX_TRACKED_INNS; i++)
		{
			obs.trackedInns[i].valid = 0;
			obs.trackedInns[i].gid = -1;
			obs.trackedInns[i].corn = 0;
			obs.trackedInns[i].maxCorn = 0;
			obs.trackedInns[i].maxUnitWorking = 0;
			obs.trackedInns[i].unitsInside = 0;
			obs.trackedInns[i].maxUnitInside = 0;
			obs.trackedInns[i].nearestWheatDist = -1;
			obs.trackedInns[i].harvestableWheatNearby = -1;
			obs.trackedInns[i].restockTripsNeeded = -1;
			obs.trackedInns[i].priority = 0;
		}
		obs.siteCount = 0;
		for (int i = 0; i < CORTEX_MAX_TRACKED_SITES; i++)
		{
			obs.trackedSites[i].valid = 0;
			obs.trackedSites[i].gid = -1;
			obs.trackedSites[i].maxUnitWorking = 0;
			obs.trackedSites[i].deliveriesLeft = 0;
		}

		obs.fruitOnMap = 0;
		obs.totalPrestige = 0;

		obs.algaeDiscovered = 0;
		obs.swimLandReach = 0;
		obs.swimWaterReach = 0;
		obs.algaeReachable = 0;

		obs.enemyCount = 0;
		for (int i = 0; i < MAX_ENEMY_SLOTS; i++)
		{
			obs.enemies[i].active = 0;
			obs.enemies[i].teamNumber = -1;
			obs.enemies[i].totalUnit = 0;
			obs.enemies[i].totalBuilding = 0;
			obs.enemies[i].prestige = 0;
		}
		return obs;
	}

	/// Construct a no-op action with the current version stamped.
	inline CortexAction makeNoOpAction()
	{
		CortexAction action;
		action.version = ACTION_VERSION;
		action.kind = ACTION_NOOP;
		action.buildingType = -1;
		action.locationSlot = -1;
		for (int i = 0; i < CORTEX_UNIT_TYPES; i++)
			action.productionRatio[i] = 0;
		action.flagRadius = -1;
		action.unitCount = -1;
		for (int i = 0; i < CORTEX_MAX_TRACKED_SWARMS; i++)
			action.swarmWorkers[i] = -1;
		for (int i = 0; i < CORTEX_MAX_TRACKED_INNS; i++)
			action.innWorkers[i] = -1;
		for (int i = 0; i < CORTEX_MAX_TRACKED_SITES; i++)
			action.siteWorkers[i] = -1;
		action.priorityTarget = CORTEX_PRIORITY_NONE;
		action.priorityRest   = CORTEX_PRIORITY_NONE;
		return action;
	}

	/// Construct a build action targeting one candidate slot.
	inline CortexAction makeBuildAction(int buildingType, int locationSlot)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_BUILD;
		action.buildingType = buildingType;
		action.locationSlot = locationSlot;
		return action;
	}

	/// Construct a set-production action: target ratio applied to all swarms.
	/// {0,0,0} halts production. Callers should keep each entry in [0, CORTEX_MAX_RATIO].
	inline CortexAction makeSetProductionAction(int workerRatio, int explorerRatio, int warriorRatio)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_SET_PRODUCTION;
		action.productionRatio[0] = workerRatio;
		action.productionRatio[1] = explorerRatio;
		action.productionRatio[2] = warriorRatio;
		return action;
	}

	/// Offense: recall/commit our single war flag onto flagTargets[locationSlot].
	/// radius/unitCount are clamped to [1,CORTEX_MAX_FLAG_RADIUS]/[0,CORTEX_MAX_FLAG_UNITS]
	/// by the action layer.
	inline CortexAction makeWarFlagAction(int locationSlot, int flagRadius, int unitCount)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_PLACE_WAR_FLAG;
		action.locationSlot = locationSlot;
		action.flagRadius = flagRadius;
		action.unitCount = unitCount;
		return action;
	}

	/// Defense: recall our single war flag onto obs.defenseTarget.
	inline CortexAction makeDefenseFlagAction(int flagRadius, int unitCount)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_PLACE_DEFENSE_FLAG;
		action.flagRadius = flagRadius;
		action.unitCount = unitCount;
		return action;
	}

	/// Remove our war flag if one exists (no offense/defense wanted right now).
	inline CortexAction makeClearFlagsAction()
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_CLEAR_FLAGS;
		return action;
	}

	/// Upgrade one finished instance of `buildingType` to its next level. The
	/// action layer picks the specific eligible (bottleneck) instance and the
	/// engine-default worker counts; the policy only names the type to upgrade.
	inline CortexAction makeUpgradeAction(int buildingType)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_UPGRADE_BUILDING;
		action.buildingType = buildingType;
		return action;
	}

	/// Construct an empty worker-tuning action: kind set, every per-building target
	/// initialised to -1 (leave unchanged). The caller fills swarmWorkers[i] /
	/// innWorkers[i] (indexed in lockstep with obs.trackedSwarms[]/trackedInns[])
	/// for the buildings it wants to retarget. The action layer dedups, so a target
	/// equal to the building's current maxUnitWorking emits no order.
	inline CortexAction makeTuneWorkersAction()
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_TUNE_WORKERS;
		return action;
	}

	/// Set the FIRST swarm's engine priority to `first` and every other swarm's to
	/// `rest` (each -1/0/+1). The action layer dedups against each swarm's current
	/// Building::priority, so re-issuing the same targets every cycle emits no order.
	/// Steady state keeps the primary swarm at CORTEX_PRIORITY_HIGH (first) so it wins
	/// worker/hauler contention while later swarms stay CORTEX_PRIORITY_NORMAL (rest);
	/// the panic defense raises ALL swarms to HIGH (first == rest == HIGH).
	inline CortexAction makeSetPriorityAction(int first, int rest)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_SET_PRIORITY;
		action.priorityTarget = first;
		action.priorityRest   = rest;
		return action;
	}
}
