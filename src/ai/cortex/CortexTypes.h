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
//
// CortexTypes.h is the umbrella public header for the AICortex shared types:
// every includer does #include "CortexTypes.h" and sees the full surface. The
// tunable constants + enums live in CortexConstants.h (included near the top,
// before the structs, since the structs use the size constants for their
// fixed-shape arrays); the inline accessor/factory helpers live in CortexQuery.h
// (included at the bottom, after the structs they reference). This file itself
// holds the POD structs, the *_VERSION constants, and the version changelog.

#include "CortexConstants.h"

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
	/// v16 (2026-06-07) added totalNeededPerLevel[]: open-job slots split by building level, so the worker-surplus throttle can ignore jobs the current (under-leveled) workforce cannot fill.
	static const Uint32 OBSERVATION_VERSION = 16;
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

	// --- tunable constants + enums: see CortexConstants.h (included above) ---

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
		Sint32 totalNeededPerLevel[CORTEX_UNIT_LEVELS]; ///< stat->totalNeededPerLevel[lvl]: the totalNeeded open-job slots split by the building's type->level. Lets the policy separate jobs the current workforce can staff (building level <= max worker HARVEST level) from jobs only higher-trained workers can take. Index = building level 0..CORTEX_UNIT_LEVELS-1.

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
}

// Inline accessor/factory helpers depend on the struct definitions above, so
// they live in CortexQuery.h and are included last (after the structs).
#include "CortexQuery.h"
