// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

// War-flag lifecycle for Cortex. Cortex runs TWO independent war flags at once —
// an OFFENSE flag pushing on the enemy and a DEFENSE flag recalling the home
// reserve — each tracked by its own gid, sized, and given its own engine priority
// (defense HIGH so it out-recruits offense for the free-warrior pool; offense
// NORMAL). This decoupling replaces the old single-flag model where a "recall" was
// just an OrderMoveFlag that teleported the lone flag's anchor home while its
// committed army stayed forward. Split out of AICortex.cpp to keep both files under
// the source-size cap; the determinism-relevant logic is unchanged from the inline
// single-flag version, only generalised to the offense WAVE PIPELINE (an array of
// offense flags managed by gid) plus the single defense flag.

#include "AICortex.h"
#include "CortexObservation.h"

#include "Order.h"
#include "Player.h"
#include "team/Team.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "BuildingType.h"
#include "building/Building.h"
#include "unit/Unit.h"
#include "unit/UnitConsts.h"
#include "Game.h"
#include "map/Map.h"

using std::shared_ptr;

// Map distance (warp-safe) beyond which an existing war flag is recalled to a new
// target rather than left in place. Small slack so a flag already sitting on (or
// right next to) the target isn't pointlessly re-ordered every cycle.
static const int FLAG_MOVE_THRESHOLD = 3;

Building* AICortex::findFlagByGid(Uint16 gid) const
{
	// War flags are VIRTUAL buildings: they live in team->virtualBuildings, not
	// team->myBuildings (which holds only real, map-footprint buildings). The
	// container is a std::list — iterate it in insertion order (deterministic);
	// never a std::set. Resolve a tracked gid to its live flag, or NULL if unset /
	// already gone.
	if (gid == NOGBID)
		return NULL;
	Team* team = player->team;
	for (Building* b : team->virtualBuildings)
	{
		if (b
		    && b->gid == gid
		    && b->type->shortTypeNum == IntBuildingType::WAR_FLAG
		    && b->buildingState == Building::ALIVE)
			return b;
	}
	return NULL;
}

bool AICortex::isOwnedGid(Uint16 gid) const
{
	if (gid == NOGBID)
		return false;
	if (gid == defenseFlagGid)
		return true;
	for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
		if (offenseWaves[i].gid == gid)
			return true;
	return false;
}

Building* AICortex::rediscoverFlag(Uint16& gid, int tx, int ty)
{
	// An OrderCreate we issued earlier has now (potentially) landed as a new WAR_FLAG
	// with a fresh gid we never saw. Claim it into `gid`: the live WAR_FLAG nearest
	// (tx, ty) whose gid is NOT already owned by another tracked flag (defense or any
	// other offense wave). Targets are far apart, so the nearest unclaimed flag is
	// unambiguously the one this slot just placed even if several created on one cycle.
	Team* team = player->team;
	Building* best = NULL;
	int bestDist = 0;
	for (Building* b : team->virtualBuildings)
	{
		if (!b
		    || b->type->shortTypeNum != IntBuildingType::WAR_FLAG
		    || b->buildingState != Building::ALIVE)
			continue;
		if (isOwnedGid(b->gid))
			continue; // already claimed by another tracked flag.
		const int dist = team->game->map.warpDistMax(b->posX, b->posY, tx, ty);
		if (best == NULL || dist < bestDist)
		{
			best = b;
			bestDist = dist;
		}
	}
	if (best != NULL)
		gid = best->gid;
	return best;
}

void AICortex::ensureFlagAt(Uint16& gid, Sint32& cooldown, int tx, int ty, int radius,
                            int count, int minLevel, int priority,
                            const Cortex::CortexObservation& obs)
{
	// Clamp the discrete params to Cortex's own bounds (the engine clamps nothing on
	// its own). radius == flag unitStayRange, count == warriors summoned.
	if (radius < 1)
		radius = 1;
	else if (radius > Cortex::CORTEX_MAX_FLAG_RADIUS)
		radius = Cortex::CORTEX_MAX_FLAG_RADIUS;
	if (count < 0)
		count = 0;
	else if (count > Cortex::CORTEX_MAX_FLAG_UNITS)
		count = Cortex::CORTEX_MAX_FLAG_UNITS;
	if (minLevel < 0)
		minLevel = 0;
	else if (minLevel > NB_UNIT_LEVELS - 1)
		minLevel = NB_UNIT_LEVELS - 1;

	Game* game = player->team->game;

	// Resolve this flag. A stale gid (flag died or was deleted) drops to NULL — reset it
	// so we re-create. If we never captured the gid of a create we issued, try to claim
	// the newly-landed flag now.
	Building* existing = findFlagByGid(gid);
	if (existing == NULL && gid != NOGBID)
		gid = NOGBID; // tracked flag is gone; forget it.
	if (existing == NULL)
		existing = rediscoverFlag(gid, tx, ty);

	if (existing == NULL)
	{
		// No flag yet: create one. An OrderCreate takes several ticks to execute and
		// register the virtual building; without a cooldown the policy re-issues the
		// create before the flag appears. Per-flag cooldown so a pending create never
		// stalls a different flag's placement.
		if (obs.tick < cooldown)
			return;

		// Resolve WAR_FLAG the VIRTUAL way: flags have no building-site variant, so pass
		// isBuildingSite==false. (The economy path passes true, which returns -1 for
		// flags — that is exactly why the build path skips them.)
		const std::string& name = IntBuildingType::reverseConversionMap[IntBuildingType::WAR_FLAG];
		Sint32 typeNum = globalContainer->buildingsTypes.getTypeNum(name, 0, false);
		if (typeNum < 0)
			return;

		// Virtual-building room gate: for a flag this just checks no other own-flag
		// occupies the tile and ignores fog (checkFow defaulted true is harmless here).
		BuildingType* bt = globalContainer->buildingsTypes.get(typeNum);
		if (!game->checkRoomForBuilding(tx, ty, bt, player->team->teamNumber))
			return;

		// 7-arg OrderCreate: the trailing flagRadius sets the flag's unitStayRange at
		// execution (Game_orders.cpp executeCreate). For a flag, unitWorking ==
		// unitWorkingFuture == warriors summoned, so both are `count`. The new flag's gid
		// is unknown until it registers; rediscoverFlag claims it a later cycle.
		orderQueue.push(shared_ptr<Order>(new OrderCreate(
			player->team->teamNumber, tx, ty, typeNum,
			count, count, radius)));
		cooldown = obs.tick + BUILD_COOLDOWN_TICKS;
		return;
	}

	// Flag already exists: keep it on-target AND in sync with the requested force
	// size, veteran filter, and engine priority. (radius retargeting for an in-place
	// flag is still deferred — only count + minLevel + priority are reconciled here.)
	int dist = game->map.warpDistMax(existing->posX, existing->posY, tx, ty);
	if (dist > FLAG_MOVE_THRESHOLD)
		orderQueue.push(shared_ptr<Order>(new OrderMoveFlag(existing->gid, tx, ty, false)));

	// Scale the standing flag's summon count to the requested size. Dedup against — and
	// locally mirror — the live building field, the executor-mirroring pattern the swarm
	// worker-tuning uses, so a steady-state count emits no order and we do not re-spam
	// the order until the engine applies it.
	if (count != existing->maxUnitWorking)
	{
		orderQueue.push(shared_ptr<Order>(new OrderModifyBuilding(existing->gid, count)));
		existing->maxUnitWorking = count;
	}

	// Apply the veteran filter. A warrior answers a flag only when
	// min(level[ATTACK_SPEED], level[ATTACK_STRENGTH]) >= minLevelToFlag
	// (building/Misc.cpp:106). OrderCreate cannot carry minLevelToFlag (the flag is
	// born at 0, building/Lifecycle.cpp:81), so it is set here once the flag has a gid.
	if (minLevel != existing->minLevelToFlag)
	{
		orderQueue.push(shared_ptr<Order>(new OrderModifyMinLevelToFlag(existing->gid, minLevel)));
		existing->minLevelToFlag = minLevel;
	}

	// Apply the engine priority (-1/0/+1). The DEFENSE flag rides at HIGH so it wins
	// the free-warrior recruitment race (Team::updateAllBuildingTasks walks the
	// priority buckets high-first); the OFFENSE flag stays at NORMAL. Born at 0
	// (building/Lifecycle.cpp:66) like minLevelToFlag, so set here once it has a gid.
	// Mirror the executor (executeChangePriority sets priority then updateCallLists)
	// locally so the dedup won't re-fire before the order executes.
	if (priority != existing->priority)
	{
		existing->priority = priority;
		existing->updateCallLists();
		orderQueue.push(shared_ptr<Order>(new OrderChangePriority(existing->gid, priority)));
	}
}

void AICortex::clearOneFlag(Uint16& gid)
{
	Building* existing = findFlagByGid(gid);
	if (existing)
		orderQueue.push(shared_ptr<Order>(new OrderDelete(existing->gid)));
	gid = NOGBID; // forget it either way: a stale gid must not block a future create.
}

void AICortex::clearAllOffenseFlags()
{
	// Stand the whole offense pipeline down: delete every live wave flag and free its
	// slot. Releases all committed warriors back to the pool (where the defense flag, or
	// a fresh muster, can recruit them). Idempotent — a no-op on already-empty slots.
	for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
	{
		clearOneFlag(offenseWaves[i].gid);
		offenseWaves[i].musterUntil = 0;
		offenseWaves[i].createCooldown = 0;
	}
}

bool AICortex::computeRallyPoint(int& rx, int& ry) const
{
	// The muster point is the colony's heart: its first (lowest-index) finished or
	// in-progress SWARM_BUILDING, which is a single valid colony tile, so no centroid
	// averaging (which would break across the toroidal map's wrap seam) is needed. Fall
	// back to the first alive building if somehow no swarm exists, and report failure
	// only when we have no buildings at all (caller then plants straight on the enemy).
	Team* team = player->team;
	Building* fallback = NULL;
	for (int i = 0; i < Building::MAX_COUNT; i++)
	{
		Building* b = team->myBuildings[i];
		if (b == NULL || b->buildingState == Building::DEAD)
			continue;
		if (fallback == NULL)
			fallback = b;
		if (b->type->shortTypeNum == IntBuildingType::SWARM_BUILDING)
		{
			rx = b->posX;
			ry = b->posY;
			return true;
		}
	}
	if (fallback != NULL)
	{
		rx = fallback->posX;
		ry = fallback->posY;
		return true;
	}
	return false;
}

void AICortex::reconcileStaleDefenseFlag(const Cortex::CortexObservation& obs)
{
	// The assault is over the moment nothing of ours is taking fire — tear the defense
	// flag down so its HIGH-priority pull stops hogging warriors the offense waves want.
	// scoreDefense DECLINES when buildingsUnderAttack == 0, so the action ladder would
	// never place a teardown; this runs unconditionally each cycle instead. Idempotent:
	// clearOneFlag is a no-op once the flag is already gone.
	if (obs.buildingsUnderAttack == 0)
		clearOneFlag(defenseFlagGid);
}

void AICortex::manageOffenseWaves(int targetX, int targetY, int radius, int warriors,
                                  const Cortex::CortexObservation& obs)
{
	// The offense WAVE PIPELINE. Each slot in offenseWaves[] is either empty, MUSTERING
	// at the home rally (musterUntil > 0), or MARCHING on the enemy (musterUntil == 0).
	// Run once per offense decision cycle:
	//   1. Reconcile every live wave: a mustering wave that is "near full" (or timed out)
	//      MARCHES; a marching wave ground down to OFFENSE_WAVE_SPENT_WARRIORS is retired.
	//   2. Keep exactly ONE wave mustering (at NORMAL priority, so fresh warriors flow
	//      into it) whenever a slot is free and we still have warriors to gather.
	// Marching waves ride at LOW priority: they keep their mustered cohort (the engine
	// never poaches a flag-bound warrior) but never pull solo replacements, so a wave
	// stays cohesive and a fresh wave forms behind it — continuous pressure, no downtime.
	int rallyX = 0, rallyY = 0;
	const bool haveRally = computeRallyPoint(rallyX, rallyY);

	// --- 1. reconcile existing waves ---
	bool someoneMustering = false;
	for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
	{
		OffenseWave& w = offenseWaves[i];
		Building* flag = findFlagByGid(w.gid);
		if (flag == NULL && w.gid != NOGBID)
		{
			// flag died/was deleted; free the slot.
			w.gid = NOGBID;
			w.musterUntil = 0;
		}
		if (w.gid == NOGBID && w.musterUntil == 0)
			continue; // empty slot.

		// The flag's assigned-cohort size (unitsWorking) is the wave's true strength: it
		// counts warriors BOUND to the flag whether they have arrived or are still
		// walking to it, and drops only as they die. Spatial proximity would read ~0
		// during the cross-map march and retire the wave before it ever engaged.
		const int cohort = flag ? static_cast<int>(flag->unitsWorking.size()) : 0;

		if (w.musterUntil != 0)
		{
			// MUSTERING. March once the gathered cohort is near full, or on timeout.
			int wantGathered = Cortex::CORTEX_MAX_FLAG_UNITS
			                 * OFFENSE_MUSTER_READY_NUM / OFFENSE_MUSTER_READY_DEN;
			if (wantGathered < 1)
				wantGathered = 1;
			const bool massed   = cohort >= wantGathered;
			const bool timedOut = obs.tick >= w.musterUntil;
			if (massed || timedOut)
				w.musterUntil = 0; // -> MARCHING (handled below).
			else
			{
				someoneMustering = true;
				ensureFlagAt(w.gid, w.createCooldown, rallyX, rallyY, radius,
				             Cortex::CORTEX_MAX_FLAG_UNITS, 0, Cortex::CORTEX_PRIORITY_NORMAL, obs);
				continue;
			}
		}

		// MARCHING. Retire a spent wave (cohort ground down by combat); else hold the
		// assault on the enemy target at LOW priority (keeps its cohort, pulls no fresh
		// recruits). A just-marched wave still has its full cohort (transit-invariant),
		// so it is never retired before it arrives.
		if (flag != NULL && cohort <= OFFENSE_WAVE_SPENT_WARRIORS)
		{
			clearOneFlag(w.gid);
			w.musterUntil = 0;
			w.createCooldown = 0;
			continue;
		}
		ensureFlagAt(w.gid, w.createCooldown, targetX, targetY, radius,
		             Cortex::CORTEX_MAX_FLAG_UNITS, 0, Cortex::CORTEX_PRIORITY_LOW, obs);
	}

	// --- 2. ensure one wave is mustering the next cohort ---
	// Only if no wave is already mustering, a slot is free, we have a rally, and there
	// are warriors to gather (so we do not spin up empty flags with nothing to recruit).
	if (!someoneMustering && haveRally && warriors > 0)
	{
		for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
		{
			OffenseWave& w = offenseWaves[i];
			if (w.gid != NOGBID || w.musterUntil != 0)
				continue; // slot in use.
			w.musterUntil = obs.tick + OFFENSE_MUSTER_TIMEOUT_TICKS;
			ensureFlagAt(w.gid, w.createCooldown, rallyX, rallyY, radius,
			             Cortex::CORTEX_MAX_FLAG_UNITS, 0, Cortex::CORTEX_PRIORITY_NORMAL, obs);
			break; // one new musterer per cycle.
		}
	}
}
