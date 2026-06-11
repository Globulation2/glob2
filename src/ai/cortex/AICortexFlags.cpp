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
// single-flag version, only generalised over FlagRole.

#include "AICortex.h"
#include "CortexObservation.h"

#include "Order.h"
#include "Player.h"
#include "team/Team.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "BuildingType.h"
#include "building/Building.h"
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

Building* AICortex::rediscoverFlag(FlagRole role, int tx, int ty)
{
	// An OrderCreate we issued earlier has now (potentially) landed as a new WAR_FLAG
	// with a fresh gid we never saw. Claim it for `role`: the live WAR_FLAG nearest
	// (tx, ty) whose gid is NOT already owned by the other role. The two roles' targets
	// (an enemy base vs. one of our buildings under fire) are far apart, so the nearest
	// unclaimed flag is unambiguously the one this role just placed even in the rare
	// case both roles created on the same cycle.
	const Uint16 otherGid = (role == FLAG_OFFENSE) ? defenseFlagGid : offenseFlagGid;
	Team* team = player->team;
	Building* best = NULL;
	int bestDist = 0;
	for (Building* b : team->virtualBuildings)
	{
		if (!b
		    || b->type->shortTypeNum != IntBuildingType::WAR_FLAG
		    || b->buildingState != Building::ALIVE)
			continue;
		if (b->gid == otherGid)
			continue; // owned by the other role.
		const int dist = team->game->map.warpDistMax(b->posX, b->posY, tx, ty);
		if (best == NULL || dist < bestDist)
		{
			best = b;
			bestDist = dist;
		}
	}
	if (best != NULL)
	{
		if (role == FLAG_OFFENSE)
			offenseFlagGid = best->gid;
		else
			defenseFlagGid = best->gid;
	}
	return best;
}

void AICortex::ensureFlagAt(FlagRole role, int tx, int ty, int radius, int count,
                            int minLevel, int priority, const Cortex::CortexObservation& obs)
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
	Uint16& gid = (role == FLAG_OFFENSE) ? offenseFlagGid : defenseFlagGid;
	int& cooldown = flagCooldownUntil[role];

	// Resolve the role's flag. A stale gid (flag died or was deleted) drops to NULL —
	// reset it so we re-create. If we never captured the gid of a create we issued,
	// try to claim the newly-landed flag now.
	Building* existing = findFlagByGid(gid);
	if (existing == NULL && gid != NOGBID)
		gid = NOGBID; // tracked flag is gone; forget it.
	if (existing == NULL)
		existing = rediscoverFlag(role, tx, ty);

	if (existing == NULL)
	{
		// No flag yet: create one. An OrderCreate takes several ticks to execute and
		// register the virtual building; without a cooldown the policy re-issues the
		// create before the flag appears. Per-role cooldown so a pending offense create
		// never stalls a defensive recall (and vice versa).
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

void AICortex::clearFlag(FlagRole role)
{
	Uint16& gid = (role == FLAG_OFFENSE) ? offenseFlagGid : defenseFlagGid;
	Building* existing = findFlagByGid(gid);
	if (existing)
		orderQueue.push(shared_ptr<Order>(new OrderDelete(existing->gid)));
	gid = NOGBID; // forget it either way: a stale gid must not block a future create.
}

void AICortex::reconcileStaleDefenseFlag(const Cortex::CortexObservation& obs)
{
	// The assault is over the moment nothing of ours is taking fire — tear the defense
	// flag down so its HIGH-priority pull stops hogging warriors the offense flag wants.
	// scoreDefense DECLINES when buildingsUnderAttack == 0, so the action ladder would
	// never place a teardown; this runs unconditionally each cycle instead. Idempotent:
	// clearFlag is a no-op once the flag is already gone.
	if (obs.buildingsUnderAttack == 0)
		clearFlag(FLAG_DEFENSE);
}
