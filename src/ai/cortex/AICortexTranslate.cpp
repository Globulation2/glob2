// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "AICortex.h"
#include "CortexObservation.h"
#include "CortexWheat.h"

#include "Order.h"
#include "Player.h"
#include "team/Team.h"
#include "GlobalContainer.h"
#include "Settings.h"
#include "IntBuildingType.h"
#include "BuildingType.h"
#include "building/Building.h"
#include "unit/UnitConsts.h"
#include "Game.h"
#include "map/Map.h"
#include "Brush.h"
#include "Utilities.h"
#include "TeamStat.h"
#include <Stream.h>
#include <iostream>
#include <cstdlib>

using std::shared_ptr;

void AICortex::translateAction(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs)
{
	switch (action.kind)
	{
		case Cortex::ACTION_NOOP:
			// Nothing to enqueue.
			break;
		case Cortex::ACTION_BUILD:
			translateActionBuild(action, obs);
			break;
		case Cortex::ACTION_BUILD_FORWARD:
			translateActionBuildForward(action, obs);
			break;
		case Cortex::ACTION_SET_PRODUCTION:
			translateActionSetProduction(action, obs);
			break;
		case Cortex::ACTION_PLACE_WAR_FLAG:
			translateActionPlaceWarFlag(action, obs);
			break;
		case Cortex::ACTION_PLACE_DEFENSE_FLAG:
			translateActionPlaceDefenseFlag(action, obs);
			break;
		case Cortex::ACTION_CLEAR_FLAGS:
			translateActionClearFlags();
			break;
		case Cortex::ACTION_UPGRADE_BUILDING:
			translateActionUpgradeBuilding(action, obs);
			break;
		case Cortex::ACTION_TUNE_WORKERS:
			translateActionTuneWorkers(action, obs);
			break;
		case Cortex::ACTION_SET_PRIORITY:
			translateActionSetPriority(action, obs);
			break;
		default:
			// Unknown intent: ignore rather than emit a bogus Order.
			break;
	}
}

void AICortex::translateActionBuild(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs)
{
	const int type = action.buildingType;
	const int slot = action.locationSlot;
	if (type < 0 || type >= Cortex::CORTEX_BUILDING_TYPES)
		return;
	if (slot < 0 || slot >= Cortex::CORTEX_BUILD_CANDIDATES)
		return;

	// An issued OrderCreate takes several ticks to execute and register
	// as a building site, which is longer than one decision cycle. Without
	// a cooldown the policy re-issues the same build on the next cycle
	// (the site isn't visible yet), stacking duplicate orders the engine
	// then rejects. Suppress new builds OF THIS TYPE until the in-flight one
	// can land; a different type is free to be placed this cycle.
	if (obs.tick < buildCooldownUntil[type])
		return;

	const Cortex::BuildCandidate& cand = obs.buildCandidates[type][slot];
	if (!cand.valid)
		return; // policy chose a stale/empty slot; drop rather than misbuild.

	emitBuildOrder(type, cand.x, cand.y, obs.tick);
}

void AICortex::translateActionBuildForward(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs)
{
	// Forward base: build a FOOD inn or HEAL hospital at the observation's forward-base
	// candidate to extend the attack-range support envelope toward the front (the cure
	// when every enemy target sits beyond the army's food/heal support radius). Unlike
	// ACTION_BUILD the candidate does NOT come from buildCandidates[type][slot]; it is the
	// observation's precomputed forward spot: obs.forwardInn for CORTEX_BUILD_FOOD or
	// obs.forwardHeal for CORTEX_BUILD_HEAL. Any other building type is ignored — no
	// forward variant is defined for it.
	const int type = action.buildingType;
	const Cortex::BuildCandidate* cand;
	if (type == Cortex::CORTEX_BUILD_FOOD)
		cand = &obs.forwardInn;
	else if (type == Cortex::CORTEX_BUILD_HEAL)
		cand = &obs.forwardHeal;
	else
		return;

	// Same per-type cooldown discipline as translateActionBuild: suppress a re-issue of
	// this type until the in-flight OrderCreate lands as a visible site.
	if (obs.tick < buildCooldownUntil[type])
		return;

	if (!cand->valid)
		return; // no legal forward spot this cycle; drop rather than misbuild.

	// Record the ordered position by POSITION (not proximity) ONLY when the
	// OrderCreate is actually emitted, so getOrder()'s reconcile can echo the
	// underway guard and later detect the site finishing / vanishing (FIX 3).
	if (emitBuildOrder(type, cand->x, cand->y, obs.tick))
	{
		if (type == Cortex::CORTEX_BUILD_FOOD)
		{
			forwardInnX = cand->x;
			forwardInnY = cand->y;
		}
		else
		{
			forwardHealX = cand->x;
			forwardHealY = cand->y;
		}
	}
}

bool AICortex::emitBuildOrder(int type, int x, int y, int tick)
{
	// Resolve the long building-site type id for a fresh (level 0)
	// building, exactly as the GUI/Echo build path does.
	const std::string& name = IntBuildingType::reverseConversionMap[type];
	Sint32 typeNum = globalContainer->buildingsTypes.getTypeNum(name, 0, true);
	if (typeNum < 0)
		return false; // no buildable site type (e.g. a virtual/flag type) — skip.

	// Worker counts from the engine's canonical defaults: column 0 is the
	// construction-site assignment, column 1 the finished-building one.
	const int unitWorking       = globalContainer->settings.defaultUnitsAssigned[type][0];
	const int unitWorkingFuture = globalContainer->settings.defaultUnitsAssigned[type][1];

	orderQueue.push(shared_ptr<Order>(new OrderCreate(
		player->team->teamNumber, x, y, typeNum,
		unitWorking, unitWorkingFuture)));
	buildCooldownUntil[type] = tick + BUILD_COOLDOWN_TICKS;
	return true;
}

void AICortex::translateActionSetProduction(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs)
{
	(void)obs;
	// Clamp the target defensively to [0, CORTEX_MAX_RATIO]; the engine
	// writes ratios verbatim, so it's the action layer's job to bound them.
	Sint32 target[NB_UNIT_TYPE];
	for (int t = 0; t < NB_UNIT_TYPE; t++)
	{
		int r = action.productionRatio[t];
		target[t] = (r < 0) ? 0 : (r > Cortex::CORTEX_MAX_RATIO ? Cortex::CORTEX_MAX_RATIO : r);
	}

	// Iterate buildings by array index (never std::set) for lockstep
	// determinism. Enqueue one OrderModifySwarm per finished swarm whose
	// current ratio differs from the target; getOrder() drains orderQueue
	// one order per tick, so this naturally retargets one swarm per tick.
	Team* team = player->team;
	for (int i = 0; i < Building::MAX_COUNT; i++)
	{
		Building* b = team->myBuildings[i];
		if (!b)
			continue;
		if (b->type->shortTypeNum != IntBuildingType::SWARM_BUILDING)
			continue;
		if (b->buildingState != Building::ALIVE || b->type->isBuildingSite)
			continue; // finished swarm only — not a site, a swarm under upgrade, or dead.
		                  // (mirrors executeModifySwarm's unitProductionTime guard.)

		// Dedup: only emit when this swarm's current ratio differs from
		// the target, so a steady-state policy that re-issues the same
		// ACTION_SET_PRODUCTION every cycle doesn't spam redundant orders.
		bool differs = false;
		for (int t = 0; t < NB_UNIT_TYPE; t++)
			if (b->ratio[t] != target[t])
			{
				differs = true;
				break;
			}
		if (!differs)
			continue;

		orderQueue.push(shared_ptr<Order>(new OrderModifySwarm(b->gid, target)));
	}
}

void AICortex::translateActionPlaceWarFlag(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs)
{
	// Offense: drive the WAVE PIPELINE toward the chosen enemy-building target. An
	// out-of-range or invalid slot means "no target" — stand the whole offense down (the
	// defense flag is managed independently).
	const int slot = action.locationSlot;
	if (slot < 0 || slot >= Cortex::CORTEX_FLAG_TARGETS
	    || !obs.flagTargets[slot].valid)
	{
		clearAllOffenseFlags();
		flagPosture = POSTURE_NONE;
		offenseHoldUntil = 0;
		return;
	}
	const Cortex::BuildCandidate& target = obs.flagTargets[slot];

	// Arm the hold window on a FRESH commit (a posture transition INTO offense), so a
	// minor-harassment defensive recall is ignored while the first wave forms and
	// marches; it lapses after OFFENSE_HOLD_TICKS so real and post-engagement threats
	// can still pull the army home. The pipeline itself (muster->march, retire spent
	// waves, always-muster-the-next) is what keeps pressure continuous; see
	// manageOffenseWaves. Count/minLevel are owned by the pipeline (full waves, minLevel
	// 0 so the whole army marches as one); we pass it the target, flag radius, and our
	// warrior count (whether there is anything left to muster).
	if (flagPosture != POSTURE_OFFENSE)
		offenseHoldUntil = obs.tick + OFFENSE_HOLD_TICKS;
	flagPosture = POSTURE_OFFENSE;
	manageOffenseWaves(target.x, target.y, action.flagRadius, obs.warriors, obs);
}

void AICortex::translateActionPlaceDefenseFlag(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs)
{
	// PURE TRANSLATION: execute the multi-point recall the policy decided. The thrash-
	// hysteresis (hold an in-progress offense push through minor harassment) lives in the
	// policy (CortexPolicy::scoreDefense) — when the hold should win, the policy returns
	// NoOp and this helper is never reached. This manages only the DEFENSE flag SET (up to
	// CORTEX_MAX_DEFENSE_FLAGS flags, one per distinct building under fire in
	// obs.defenseTargets[], worst-first); the offense waves are managed independently.

	// Reconcile the SET slot-by-slot. An invalid target slot has its flag torn down; a
	// valid one is sized to its OWN local assault — 3x the visible enemy units near THAT
	// building (defenseThreatCount[i]), floored at 1 and capped at the flag ceiling. A
	// bigger local threat pulls more warriors home to that point; a lone harasser few.
	int needed[Cortex::CORTEX_MAX_DEFENSE_FLAGS];
	int totalDeficit = 0;
	bool anyValid = false;
	for (int i = 0; i < Cortex::CORTEX_MAX_DEFENSE_FLAGS; i++)
	{
		needed[i] = 0;
		if (!obs.defenseTargets[i].valid)
		{
			clearOneFlag(defenseFlags[i].gid); // target gone: tear this slot's flag down.
			continue;
		}
		anyValid = true;
		int n = Cortex::CORTEX_DEFENSE_THREAT_MULTIPLE * obs.defenseThreatCount[i];
		if (n < 1)
			n = 1;
		else if (n > Cortex::CORTEX_MAX_FLAG_UNITS)
			n = Cortex::CORTEX_MAX_FLAG_UNITS;
		needed[i] = n;

		// Free-pool-first accounting, summed ACROSS the whole set: each valid slot's
		// shortfall over its CURRENT cohort (0 when the flag is absent). The HIGH-priority
		// defense flags out-recruit everything for FREE warriors, so each fills from the
		// idle reserve without disturbing the forward army; we release the committed army
		// only when the free pool cannot cover the combined recall (below).
		const Building* cur = findFlagByGid(defenseFlags[i].gid);
		const int curUnits = cur ? static_cast<int>(cur->unitsWorking.size()) : 0;
		if (n > curUnits)
			totalDeficit += n - curUnits;
	}

	// No slot has a valid target — nothing is under attack. Every defense flag was already
	// cleared in the loop above; drop the posture and hold.
	if (!anyValid)
	{
		flagPosture = POSTURE_NONE;
		offenseHoldUntil = 0;
		return;
	}

	// Only when the free pool cannot cover the combined deficit do we release the committed
	// army — tearing down ALL offense waves frees their warriors, which the HIGH-priority
	// defense flags then claim. (Priority alone never poaches a flagged warrior; the army
	// comes home only by clearing the flag it is bound to.)
	if (totalDeficit > 0 && obs.freeWarriors < totalDeficit)
		clearAllOffenseFlags();

	// Commit the defensive recall: POSTURE_DEFENSE, hold cleared. Each defense flag rides
	// at HIGH priority, minLevel 0 (recall every warrior regardless of level), radius from
	// the action (DEFENSE_FLAG_RADIUS), each on its own per-slot createCooldown.
	flagPosture = POSTURE_DEFENSE;
	offenseHoldUntil = 0;
	for (int i = 0; i < Cortex::CORTEX_MAX_DEFENSE_FLAGS; i++)
	{
		if (!obs.defenseTargets[i].valid)
			continue;
		const Cortex::BuildCandidate& target = obs.defenseTargets[i];
		ensureFlagAt(defenseFlags[i].gid, defenseFlags[i].createCooldown, target.x, target.y,
		             action.flagRadius, needed[i], 0, Cortex::CORTEX_PRIORITY_HIGH, obs);
	}
}

void AICortex::translateActionClearFlags()
{
	// Stand the offense down (scoreRetireFlag's job: the threat has cleared and the army
	// is too thin to attack). Tear down every offense wave. The DEFENSE flag is torn
	// down separately by reconcileStaleDefenseFlag when nothing is under attack.
	clearAllOffenseFlags();
	flagPosture = POSTURE_NONE;
	offenseHoldUntil = 0;
}

void AICortex::translateActionUpgradeBuilding(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs)
{
	// Upgrade ONE finished instance of buildingType to its next level via
	// the engine upgrade order (OrderConstruction, Order.h:148-169 / body
	// OrderBuilding.cpp:138-144 — it carries the building's gid plus the two
	// worker counts; the engine derives the target level from type->nextLevel
	// in Building::launchConstruction, building/Construction.cpp:93-148).
	const int type = action.buildingType;
	if (type < 0 || type >= Cortex::CORTEX_BUILDING_TYPES)
		return;

	// One upgrade of a given class in flight at a time. A prior upgrade of
	// this type is still converting (issued but not yet a visible site), so
	// the observation still shows it finished — issuing a second now would
	// black out the whole class (e.g. both barracks offline at once). The
	// guard is released (in getOrder) the cycle the site becomes visible.
	if (pendingUpgradeType == type)
		return;

	// Same cooldown discipline as ACTION_BUILD: an OrderConstruction takes
	// ticks to convert the building into a site, and the observation's
	// upgradableCount won't drop until then. Without the cooldown the policy
	// would re-issue the upgrade on the next decision cycle.
	if (obs.tick < buildCooldownUntil[type])
		return;

	Building* b = findUpgradeTarget(type);
	if (!b)
		return; // no instance currently passes the full Upgradable predicate.

	// Worker counts from the engine's canonical defaults, mirroring the GUI
	// upgrade path (gui/GameGUIInput.cpp:409-427). There typeNum =
	// building->typeNum + 1 (the level-(L+1) construction SITE variant) and
	// unitWorking = getDefaultAssignedUnits(typeNum) /
	// unitWorkingFuture = getDefaultAssignedUnits(typeNum + 1) (the level-(L+1)
	// FINISHED variant). Per GameGUIDefaultAssignManager.cpp:14-29, a
	// (shortType, level, site) variant maps to defaultUnitsAssigned column
	// level*2 and a finished variant to column level*2 + 1
	// (Settings.h:73, defaultUnitsAssigned[NB_BUILDING][6]). With
	// targetLevel = type->level + 1 the GUI's two lookups are therefore
	// exactly columns [targetLevel*2] (site assign) and [targetLevel*2 + 1]
	// (finished assign) — equivalent, so we read them directly.
	const int targetLevel = b->type->level + 1;
	const int siteCol     = targetLevel * 2;
	const int finishedCol = targetLevel * 2 + 1;
	// Guard the column indices defensively (defaultUnitsAssigned has 6 cols,
	// levels 0..2; an upgradable building always has nextLevel so targetLevel
	// is 1 or 2 and both columns are in range, but clamp rather than trust it).
	if (siteCol < 0 || finishedCol < 0
	 || siteCol >= 6 || finishedCol >= 6)
		return;
	const int unitWorking       = globalContainer->settings.defaultUnitsAssigned[type][siteCol];
	// NOTE: for some types (e.g. ATTACK_BUILDING) the finished-building
	// columns (1,3,5) are 0 in Settings::resetDefaultUnitsAssigned — that is
	// the engine default, NOT a missing value. The GUI upgrade path passes the
	// same 0 here; launchConstruction uses the site column (unitWorking) as the
	// meaningful crew. Do not "fix" this to a nonzero default.
	const int unitWorkingFuture = globalContainer->settings.defaultUnitsAssigned[type][finishedCol];

	orderQueue.push(shared_ptr<Order>(new OrderConstruction(b->gid, unitWorking, unitWorkingFuture)));
	buildCooldownUntil[type] = obs.tick + BUILD_COOLDOWN_TICKS;
	// Mark this class's upgrade in flight until it shows up as a site (or
	// the safety timeout), so the policy can't stack a second one meanwhile.
	pendingUpgradeType = type;
	pendingUpgradeUntil = obs.tick + UPGRADE_PENDING_TIMEOUT_TICKS;
}

template <typename Tracked, typename Accept>
void AICortex::applyWorkerCounts(const Tracked* tracked, int count, const Sint32* desiredArr,
                                 int maxClamp, Accept accept)
{
	Team* team = player->team;
	for (int i = 0; i < count; i++)
	{
		const Tracked& tb = tracked[i];
		int desired = desiredArr[i];

		// Skip: invalid slot, "leave unchanged" sentinel, or already at target.
		if (!tb.valid)
			continue;
		if (desired < 0)
			continue;
		// Engine ceiling clamp (sites only; maxClamp < 0 means no clamp). Applied
		// BEFORE the dedup so a clamped request that already matches emits nothing.
		if (maxClamp >= 0 && desired > maxClamp)
			desired = maxClamp;
		if (desired == tb.maxUnitWorking)
			continue; // DEDUP: current state already matches; don't re-emit.

		// Resolve gid → Building* via the canonical decode
		// (Building::GIDtoID, inherited from BuildingUtils). This is an O(1)
		// array-index lookup — not a linear scan — so it is both deterministic
		// and cheap. GIDtoID returns the per-team array slot; GIDtoTeam is not
		// needed here because the tracked sets only contain our own buildings
		// (filled from team->myBuildings in CortexTypes.h observe()).
		const int bid = Building::GIDtoID(static_cast<Uint16>(tb.gid));
		Building* b = team->myBuildings[bid];
		if (!b)
			continue;
		// Per-set guard: confirm the decoded building is still the kind we
		// observed (it could have been destroyed, replaced, finished, or started
		// an upgrade between the observation and now). Only an accepted building
		// takes OrderModifyBuilding (mirrors executeModifyBuilding's guard).
		if (!accept(b))
			continue;

		// Update the AI's local view immediately (AICastor pattern) so the
		// dedup won't re-trigger on the next cycle before the order executes.
		b->maxUnitWorking = desired;
		b->update();
		orderQueue.push(shared_ptr<Order>(new OrderModifyBuilding(b->gid, desired)));
	}
}

template <typename Tracked, typename Accept>
void AICortex::applyPriorities(const Tracked* tracked, int count, const Sint32* desiredArr,
                               Accept accept)
{
	Team* team = player->team;
	for (int i = 0; i < count; i++)
	{
		const Tracked& tb = tracked[i];
		const int desired = desiredArr[i];

		// Skip: invalid slot, "leave unchanged" sentinel (CORTEX_PRIORITY_NONE) or
		// any value outside the valid -1/0/+1 range, or already at target.
		if (!tb.valid)
			continue;
		if (desired < Cortex::CORTEX_PRIORITY_LOW || desired > Cortex::CORTEX_PRIORITY_HIGH)
			continue;
		if (desired == tb.priority)
			continue; // DEDUP: current state already matches; don't re-emit.

		const int bid = Building::GIDtoID(static_cast<Uint16>(tb.gid));
		Building* b = team->myBuildings[bid];
		if (!b)
			continue;
		if (!accept(b))
			continue;

		// Mirror the engine executor (Game_orders.cpp:476-484 executeChangePriority:
		// set b->priority then b->updateCallLists()) locally so the AI's view updates
		// immediately and the dedup won't re-fire before the order executes.
		b->priority = desired;
		b->updateCallLists();
		orderQueue.push(shared_ptr<Order>(new OrderChangePriority(b->gid, desired)));
	}
}

void AICortex::translateActionTuneWorkers(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs)
{
	// Apply the per-building desired worker counts (maxUnitWorking) to our
	// tracked swarms and inns. The policy nudges each building +/-1 per cycle
	// inside a deadband — these are small, frequent adjustments. We dedup
	// against the building's current maxUnitWorking so a steady-state policy
	// that re-decides the same target each cycle doesn't flood the order queue.
	// Mirror AICastor's pattern (Control.cpp:229-271): set b->maxUnitWorking
	// locally AND emit the order so the AI's own view updates immediately and
	// the dedup won't re-fire next cycle. This is deterministic — every client
	// runs the identical AI and queues the identical order in the same tick.
	//
	// Iteration order (swarms, then inns, then sites; each by ascending index)
	// is preserved exactly — it is lockstep-relevant.

	// --- swarms: finished, alive SWARM_BUILDING only ---
	applyWorkerCounts(obs.trackedSwarms, obs.swarmCount, action.swarmWorkers, /*maxClamp=*/-1,
		[](const Building* b) {
			return b->buildingState == Building::ALIVE && !b->type->isBuildingSite
			    && b->type->shortTypeNum == IntBuildingType::SWARM_BUILDING;
		});

	// --- inns (FOOD_BUILDING): finished, alive FOOD_BUILDING only ---
	applyWorkerCounts(obs.trackedInns, obs.innCount, action.innWorkers, /*maxClamp=*/-1,
		[](const Building* b) {
			return b->buildingState == Building::ALIVE && !b->type->isBuildingSite
			    && b->type->shortTypeNum == IntBuildingType::FOOD_BUILDING;
		});

	// --- construction sites (pour idle workers into in-progress builds) ---
	// Clamp to the engine ceiling (executeModifyBuilding asserts the request is
	// <= MAX_BUILDING_WORKER_REQUEST). Must still be a LIVE construction site (a
	// finished building is no longer isBuildingSite, so this naturally stops us
	// writing a site cap onto a just-completed building).
	applyWorkerCounts(obs.trackedSites, obs.siteCount, action.siteWorkers,
		/*maxClamp=*/Cortex::CORTEX_MAX_BUILDING_WORKERS,
		[](const Building* b) {
			return b->buildingState == Building::ALIVE && b->type->isBuildingSite;
		});

	// --- inn priority: restore finished inns to NORMAL (undo the LOW inherited
	//     from their construction-site phase, which the engine carries over).
	//     Finished, alive FOOD_BUILDING only. ---
	applyPriorities(obs.trackedInns, obs.innCount, action.innPriority,
		[](const Building* b) {
			return b->buildingState == Building::ALIVE && !b->type->isBuildingSite
			    && b->type->shortTypeNum == IntBuildingType::FOOD_BUILDING;
		});

	// --- site priority: pin construction sites to LOW so construction never
	//     out-recruits feeding/production. Must still be a live building site
	//     (a just-finished building is no longer isBuildingSite, so we naturally
	//     stop writing a site priority onto a completed building). ---
	applyPriorities(obs.trackedSites, obs.siteCount, action.sitePriority,
		[](const Building* b) {
			return b->buildingState == Building::ALIVE && b->type->isBuildingSite;
		});
}

void AICortex::translateActionSetPriority(const Cortex::CortexAction& action, const Cortex::CortexObservation& obs)
{
	// Set tracked-swarm engine priority: the FIRST/primary swarm to
	// priorityTarget, every other swarm to priorityRest (-1/0/+1 each). Steady
	// state keeps the primary swarm HIGH so it wins worker contention; the
	// panic defense raises every swarm to HIGH (target == rest). Dedup against
	// each swarm's current Building::priority, and mirror the engine executor
	// (Game_orders.cpp:476-484 executeChangePriority sets b->priority then
	// b->updateCallLists()) locally so the AI's view updates immediately and
	// the dedup won't re-fire before the order executes. Deterministic:
	// identical AI + identical queued order on every client.
	Team* team = player->team;
	bool seenFirstSwarm = false;
	for (int i = 0; i < obs.swarmCount; i++)
	{
		const Cortex::TrackedBuilding& tb = obs.trackedSwarms[i];
		if (!tb.valid)
			continue;
		// First valid tracked swarm gets priorityTarget; the rest priorityRest.
		const int target = seenFirstSwarm ? action.priorityRest
		                                  : action.priorityTarget;
		seenFirstSwarm = true;
		if (target < Cortex::CORTEX_PRIORITY_LOW || target > Cortex::CORTEX_PRIORITY_HIGH)
			continue; // not a valid priority (e.g. CORTEX_PRIORITY_NONE).
		if (tb.priority == target)
			continue; // DEDUP: already at the target priority.

		const int bid = Building::GIDtoID(static_cast<Uint16>(tb.gid));
		Building* b = team->myBuildings[bid];
		if (!b)
			continue;
		if (b->buildingState != Building::ALIVE || b->type->isBuildingSite)
			continue;
		if (b->type->shortTypeNum != IntBuildingType::SWARM_BUILDING)
			continue;

		b->priority = target;
		b->updateCallLists();
		orderQueue.push(shared_ptr<Order>(new OrderChangePriority(b->gid, target)));
	}
}

void AICortex::enqueueWheatForbidden(const Cortex::CortexObservation& obs, bool liftAll)
{
	(void)obs; // reserved: the gate already ran in CortexPolicy::wantWheatProtection /
	           // wantWheatBlitzLift; liftAll selects the wheat-blitz full-lift mode.

	// Rebuild the ADD/DEL checkerboard masks for our wheat (the bounded colony-region
	// scan, RNG-free) at the per-game open-margin and emit one OrderAlterateForbidden
	// per non-empty diff. No build cooldown: these are area-paint orders, not
	// OrderCreates, and the reconcile is self-correcting — a paint already in place
	// yields an empty diff next cycle, so re-running it every cycle is free. The
	// margin is the AICortex member (the seeded per-game N), == obs.wheatOpenMargin.
	// In liftAll (wheat-blitz) mode `desired` is forced empty, so only the DEL mask is
	// non-empty — the whole field is un-forbidden for a one-time food burst.
	Cortex::WheatReconcile wr =
		Cortex::reconcileWheatForbidden(player, wheatOpenMargin, /*buildMasks=*/true, liftAll);
	const Map* map = &player->team->game->map;
	const Uint8 teamNumber = static_cast<Uint8>(player->team->teamNumber);
	// DEL first so freeing dead tiles never races the ADD of fresh ones.
	if (wr.del.getApplicationCount() > 0)
		orderQueue.push(shared_ptr<Order>(new OrderAlterateForbidden(
			teamNumber, BrushTool::MODE_DEL, &wr.del, map)));
	if (wr.add.getApplicationCount() > 0)
		orderQueue.push(shared_ptr<Order>(new OrderAlterateForbidden(
			teamNumber, BrushTool::MODE_ADD, &wr.add, map)));
}
