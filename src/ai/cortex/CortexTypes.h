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
	/// signals: wheatOpenMargin (the seeded open-margin N echoed into the action),
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
	static const Uint32 OBSERVATION_VERSION = 7;
	/// Layout version of CortexAction. Bump on any field add/remove/resize.
	/// v2 (2026-06-02) added ACTION_SET_PRODUCTION + productionRatio[].
	/// v3 (2026-06-03) added the war-flag action kinds (ACTION_PLACE_WAR_FLAG,
	/// ACTION_PLACE_DEFENSE_FLAG, ACTION_CLEAR_FLAGS) + flagRadius/unitCount.
	/// v4 (2026-06-04) added ACTION_UPGRADE_BUILDING (upgrade an existing finished
	/// building to its next level via OrderConstruction; reuses buildingType).
	/// v5 (2026-06-04) added ACTION_PROTECT_WHEAT + wheatOpenMargin (paint the
	/// checkerboard forbidden pattern over our wheat for sustainability).
	/// v6 (2026-06-05) added ACTION_TUNE_WORKERS + swarmWorkers[]/innWorkers[]:
	/// the per-tracked-building desired maxUnitWorking (-1 == leave unchanged),
	/// applied via OrderModifyBuilding with action-layer dedup. Arrays are indexed
	/// in lockstep with obs.trackedSwarms[]/trackedInns[].
	/// v7 (2026-06-05) added ACTION_SET_PRIORITY + priorityTarget: set every tracked
	/// swarm's engine priority to priorityTarget (-1/0/+1) via OrderChangePriority,
	/// action-layer dedup'd against each building's current priority. The panic
	/// defense raises swarms to +1 under attack and drops them back to 0 after.
	static const Uint32 ACTION_VERSION = 7;

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
	static const int CORTEX_BUILD_SWARM   = 0; ///< IntBuildingType::SWARM_BUILDING
	static const int CORTEX_BUILD_FOOD    = 1; ///< IntBuildingType::FOOD_BUILDING (inn)
	static const int CORTEX_BUILD_HEAL    = 2; ///< IntBuildingType::HEAL_BUILDING (hospital)
	static const int CORTEX_BUILD_ATTACK  = 5; ///< IntBuildingType::ATTACK_BUILDING (barracks)
	static const int CORTEX_BUILD_SCIENCE = 6; ///< IntBuildingType::SCIENCE_BUILDING (school)

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

	// --- wheat-protection tuning (all tunable AI design choices) -----------
	// Cortex paints a checkerboard `forbidden` pattern over its wheat (CORN) so
	// workers harvest one half while the protected half stays full and reseeds it
	// (forbidden blocks harvest, MapGradientGlobal.cpp:135, but NOT growth,
	// MapStep.cpp:80). See docs/AI/cortex/wheat-protection-plan.md and the
	// geometry core in CortexWheat.h/.cpp.

	/// Open margin N range: the first N rows of wheat nearest the harvest source
	/// stay fully open (unpainted); the checkerboard begins at depth N+1. N is
	/// drawn once per game via syncRand (per-game variety + the ML-learnable knob).
	/// Capped LOW because real starter fields are only ~5-7 tiles deep — an open
	/// margin of 3-4 would leave nothing to checkerboard.
	static const int WHEAT_OPEN_MARGIN_MIN = 0;
	static const int WHEAT_OPEN_MARGIN_MAX = 2;
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

	/// Inn buffer control. Hungrier than a swarm, so a higher worker ceiling. Add a
	/// worker below ADD_LO, drop one near the 10-CORN cap (REM_HI).
	static const int CORTEX_INN_CORN_ADD_LO = 5;
	static const int CORTEX_INN_CORN_REM_HI = 8;
	static const int CORTEX_INN_WORKER_MIN  = 1;
	static const int CORTEX_INN_WORKER_CAP  = 6;

	/// Placement geometry for wheat-fed buildings. A new swarm must sit at least
	/// MIN_SPACING from any existing swarm (so two swarms don't share one wheat
	/// catchment) and no farther than WHEAT_MAX_DIST from a CORN tile (beyond that
	/// the 5-CORN/150-tick haul can't keep the buffer above the stall line). Inns
	/// honour WHEAT_MAX_DIST too (same haul logic, hungrier). Chebyshev tiles.
	static const int CORTEX_SWARM_MIN_SPACING = 6;
	static const int CORTEX_WHEAT_MAX_DIST    = 5;
	/// Bound on the nearest-CORN radial scan (CortexPlacement::nearestCornDist):
	/// tiles beyond this report "no wheat in reach" (-1). A few past WHEAT_MAX_DIST
	/// so the supply-distance expansion trigger can still measure "just out of range".
	static const int CORTEX_WHEAT_SCAN_CAP = 12;
	/// Supply-distance expansion trigger: a swarm pinned at its worker cap whose
	/// nearest CORN is beyond this has outrun its local wheat — warrant a NEW swarm
	/// near fresh wheat rather than piling more haulers onto the starved one.
	static const int CORTEX_SWARM_SUPPLY_RADIUS = 5;

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
		Sint32 priority;       ///< Building::priority (-1/0/+1) — lets the policy raise/restore swarm priority for the panic defense.
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
		Sint32 buildLevel[CORTEX_UNIT_LEVELS];               ///< stat->upgradeState[BUILD][lvl] (any unit type).
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
		// Defense triggers: how many of our own units / buildings are currently under
		// attack (underAttackTimer > 0). Nonzero => recall the army to defend.
		Sint32 unitsUnderAttack;
		Sint32 buildingsUnderAttack;

		// --- wheat sustainability (v5) ---
		// The open margin N drawn once per game (AICortex, via syncRand) and echoed
		// through the observation so the pure policy reads it like any other feature
		// and passes it into ACTION_PROTECT_WHEAT — the ML seam (a learned policy
		// later OUTPUTS N here instead of echoing the seeded value).
		Sint32 wheatOpenMargin;
		// Reconcile diff between the checkerboard we WANT over our wheat right now and
		// the team's CURRENT forbidden paint (footprints excluded). Counts-only summary
		// from a bounded colony-region scan; the full tile masks are rebuilt in the
		// action layer. The policy emits ACTION_PROTECT_WHEAT only when either is > 0.
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

		// --- map / global facts ---
		Sint32 fruitOnMap;    ///< 1 if any fruit resource exists on the map (Map query).
		Sint32 totalPrestige; ///< game->totalPrestige (all teams; for the explorer-defence heuristic).

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
		ACTION_PROTECT_WHEAT,   ///< Reconcile the checkerboard forbidden paint over our wheat at open-margin wheatOpenMargin. The action layer builds the ADD/DEL tile masks and emits OrderAlterateForbidden(MODE_ADD/DEL).
		ACTION_TUNE_WORKERS,    ///< Set each tracked swarm/inn's maxUnitWorking to swarmWorkers[i]/innWorkers[i] (indexed in lockstep with obs.trackedSwarms[]/trackedInns[]); -1 == leave unchanged. The action layer dedups against the building's current maxUnitWorking and emits one OrderModifyBuilding per real change.
		ACTION_SET_PRIORITY,    ///< Set every tracked swarm's engine priority to priorityTarget (-1/0/+1) via OrderChangePriority. The action layer dedups against each swarm's current Building::priority and emits one order per real change.

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
		Sint32 wheatOpenMargin; ///< For ACTION_PROTECT_WHEAT: the open-margin N (depth <= N stays unpainted), echoed from the seeded obs.wheatOpenMargin. Else -1.
		Sint32 swarmWorkers[CORTEX_MAX_TRACKED_SWARMS]; ///< For ACTION_TUNE_WORKERS: desired maxUnitWorking for trackedSwarms[i], or -1 to leave unchanged. Else all -1.
		Sint32 innWorkers[CORTEX_MAX_TRACKED_INNS];     ///< For ACTION_TUNE_WORKERS: desired maxUnitWorking for trackedInns[i], or -1 to leave unchanged. Else all -1.
		Sint32 priorityTarget; ///< For ACTION_SET_PRIORITY: target engine priority (-1/0/+1) for every tracked swarm. Else CORTEX_PRIORITY_NONE.
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
		obs.unitsUnderAttack = 0;
		obs.buildingsUnderAttack = 0;

		obs.wheatOpenMargin = 0;
		obs.wheatProtectAddCount = 0;
		obs.wheatProtectDelCount = 0;
		obs.swarmsProducingExplorer = 0;
		obs.swarmsProducingWarrior = 0;

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
			obs.trackedInns[i].priority = 0;
		}

		obs.fruitOnMap = 0;
		obs.totalPrestige = 0;

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
		action.wheatOpenMargin = -1;
		for (int i = 0; i < CORTEX_MAX_TRACKED_SWARMS; i++)
			action.swarmWorkers[i] = -1;
		for (int i = 0; i < CORTEX_MAX_TRACKED_INNS; i++)
			action.innWorkers[i] = -1;
		action.priorityTarget = CORTEX_PRIORITY_NONE;
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

	/// Reconcile the checkerboard forbidden paint over our wheat at open-margin
	/// `openMargin`. The action layer builds the ADD/DEL tile masks (the bounded
	/// colony-region scan) and emits the OrderAlterateForbidden orders.
	inline CortexAction makeProtectWheatAction(int openMargin)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_PROTECT_WHEAT;
		action.wheatOpenMargin = openMargin;
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

	/// Set every tracked swarm's engine priority to `priority` (-1/0/+1). The action
	/// layer dedups against each swarm's current Building::priority, so re-issuing the
	/// same target every cycle emits no order. Used by the panic defense to raise
	/// swarms to CORTEX_PRIORITY_HIGH under attack and restore CORTEX_PRIORITY_NORMAL.
	inline CortexAction makeSetPriorityAction(int priority)
	{
		CortexAction action = makeNoOpAction();
		action.kind = ACTION_SET_PRIORITY;
		action.priorityTarget = priority;
		return action;
	}
}
