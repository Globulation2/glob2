// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

// War-flag lifecycle for Cortex. Cortex runs two independent war-flag systems at once
// — the OFFENSE WAVE PIPELINE pushing on the enemy and the DEFENSE flag SET (up to
// CORTEX_MAX_DEFENSE_FLAGS flags) recalling the home reserve to each point under fire —
// each flag tracked by its own gid, sized, and given its own engine priority (defense
// HIGH so it out-recruits offense for the free-warrior pool; offense NORMAL). This
// decoupling replaces the old single-flag model where a "recall" was just an
// OrderMoveFlag that teleported the lone flag's anchor home while its committed army
// stayed forward. Split out of AICortex.cpp to keep both files under the source-size
// cap; the determinism-relevant logic is unchanged from the inline single-flag version,
// only generalised to the offense WAVE PIPELINE (an array of offense flags managed by
// gid) plus the multi-point DEFENSE flag set (an array of defense flags managed by gid).

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
	for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
		if (offenseWaves[i].gid == gid)
			return true;
	for (int i = 0; i < Cortex::CORTEX_MAX_DEFENSE_FLAGS; i++)
		if (defenseFlags[i].gid == gid)
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
		offenseWaves[i].phase = WAVE_NONE;
		offenseWaves[i].phaseDeadline = 0;
		offenseWaves[i].landingX = offenseWaves[i].landingY = -1;
		offenseWaves[i].musterBestArrived = 0;
		offenseWaves[i].createCooldown = 0;
	}
}

int AICortex::countArrivedAtFlag(Building* flag) const
{
	// Warriors of the flag's BOUND cohort (unitsWorking) that have actually reached it —
	// within its unitStayRange (the same warp-safe Chebyshev metric the diagnostic and
	// the flag recruiter use). Reads deterministic simulation state, so driving phase
	// transitions from it is lockstep-safe (every client runs the identical AI over the
	// identical state). unitsWorking is a std::list — iterated in order, never a set.
	if (flag == NULL)
		return 0;
	Game* game = player->team->game;
	int arrived = 0;
	for (Unit* u : flag->unitsWorking)
	{
		if (u == NULL)
			continue;
		if (game->map.warpDistMax(u->posX, u->posY, flag->posX, flag->posY)
		    <= flag->unitStayRange)
			arrived++;
	}
	return arrived;
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
	// The assault is over the moment nothing of ours is taking fire — tear the WHOLE
	// defense flag set down so its HIGH-priority pull stops hogging warriors the offense
	// waves want. scoreDefense DECLINES when buildingsUnderAttack == 0, so the action
	// ladder would never place a teardown; this runs unconditionally each cycle instead.
	// Idempotent: clearOneFlag is a no-op on any slot already gone.
	if (obs.buildingsUnderAttack == 0)
		for (int i = 0; i < Cortex::CORTEX_MAX_DEFENSE_FLAGS; i++)
			clearOneFlag(defenseFlags[i].gid);
}

void AICortex::sweepOrphanWarFlags(const Cortex::CortexObservation& obs)
{
	// Delete own war flags that no tracker owns and never will. ensureFlagAt fires
	// OrderCreate blind: the new flag's gid is claimed only by rediscoverFlag on a LATER
	// cycle, so a teardown in that window (retire / defense-deficit release / no-target
	// stand-down / reconcileStaleDefenseFlag) leaves the slot NOGBID while the flag lands
	// UNTRACKED. Nothing else deletes it and it summons warriors forever, draining the
	// free pool the offense pipeline needs. This sweep runs LAST in the decision cycle,
	// after every slot's management pass — so any live slot that will claim its pending
	// create already has (rediscoverFlag runs each cycle the slot is managed). What is
	// still unowned here is either a flag that just registered this cycle (a pending
	// create we may yet latch) or a genuine orphan. We separate the two with a two-cycle
	// settle window: a flag is swept only once it has been unowned across two consecutive
	// decision cycles, by which point no live slot is still waiting for it.
	Team* team = player->team;

	// Record this cycle's unowned flags, carrying each gid's ORIGINAL first-seen tick
	// forward. A flag can appear more than once when walking virtualBuildings; keying by
	// gid dedups the bookkeeping so a duplicate entry cannot reset (or advance) the window.
	std::map<Uint16, Sint32> seenNow;
	for (Building* b : team->virtualBuildings)
	{
		if (!b
		    || b->type->shortTypeNum != IntBuildingType::WAR_FLAG
		    || b->buildingState != Building::ALIVE
		    || isOwnedGid(b->gid))
			continue;
		std::map<Uint16, Sint32>::const_iterator prev = unownedFlagSeen.find(b->gid);
		seenNow[b->gid] = (prev != unownedFlagSeen.end()) ? prev->second : obs.tick;
	}

	// Sweep the flags that survived a full decision cycle unowned (first seen before this
	// cycle). Iterate virtualBuildings in insertion order (deterministic — never a set) to
	// emit the OrderDeletes; erase each swept gid from seenNow so a duplicate list entry
	// neither re-deletes it nor keeps it armed for next cycle.
	for (Building* b : team->virtualBuildings)
	{
		if (!b
		    || b->type->shortTypeNum != IntBuildingType::WAR_FLAG
		    || b->buildingState != Building::ALIVE
		    || isOwnedGid(b->gid))
			continue;
		std::map<Uint16, Sint32>::iterator it = seenNow.find(b->gid);
		if (it != seenNow.end() && it->second < obs.tick)
		{
			orderQueue.push(shared_ptr<Order>(new OrderDelete(b->gid)));
			seenNow.erase(it);
		}
	}

	unownedFlagSeen.swap(seenNow);
}

void AICortex::manageOffenseWaves(int targetX, int targetY, int radius, int warriors,
                                  bool staged, int stagingX, int stagingY, bool amphibious,
                                  const Cortex::CortexObservation& obs)
{
	// The offense WAVE PIPELINE. Each slot in offenseWaves[] is empty (WAVE_NONE),
	// MUSTERING at the home rally, CROSSING to the campaign's staging point, or
	// ASSAULTING the enemy target. Run once per offense decision cycle:
	//   1. Reconcile every live wave: advance its phase (MUSTER -> [CROSS ->] ASSAULT)
	//      when it is "massed enough" or times out; retire an ASSAULT wave ground down to
	//      OFFENSE_WAVE_SPENT_WARRIORS.
	//   2. Keep exactly ONE wave mustering (NORMAL priority, so fresh warriors flow into
	//      it) whenever a slot is free and we still have warriors to gather — subject to
	//      the amphibious swim-staging hold (below).
	// CROSS/ASSAULT waves ride at LOW priority: they keep their mustered cohort (the
	// engine never poaches a flag-bound warrior) but pull no solo replacements, so a wave
	// stays cohesive and a fresh wave forms behind it — continuous pressure, no downtime.
	//
	// A STAGED campaign (amphibious landing zone, or a long land march's forward rally)
	// routes each wave through CROSS at the staging point (stagingX/stagingY) so the
	// army masses forward and releases as one fleet; the CROSS anchor is no longer
	// water-specific. A short land campaign (staged false) runs MUSTER -> ASSAULT
	// exactly as before. The swim-staging muster hold below stays gated on `amphibious`
	// — a forward-rally campaign's marchers need no SWIM training.
	int rallyX = 0, rallyY = 0;
	const bool haveRally = computeRallyPoint(rallyX, rallyY);

	const int crossTimeout = Cortex::cortexTuning().crossTimeoutTicks;
	const int fleetRelease = Cortex::cortexTuning().fleetReleaseArrived;

	// Muster "massed" bar: a fraction (num/den) of the flag ceiling, on ARRIVED warriors.
	int musterReady = Cortex::CORTEX_MAX_FLAG_UNITS
	                * OFFENSE_MUSTER_READY_NUM / OFFENSE_MUSTER_READY_DEN;
	if (musterReady < 1)
		musterReady = 1;

	// --- pass A: per-wave flag / arrived / cohort, and the CROSS fleet aggregate ---
	// arrived drives phase transitions (warriors actually present); cohort (BOUND count,
	// unitsWorking.size()) is the wave's true strength for the spent-retire test — spatial
	// proximity reads ~0 mid-march and would retire a wave before it ever engaged.
	Building* flags[MAX_OFFENSE_FLAGS] = { NULL };
	int arrived[MAX_OFFENSE_FLAGS] = { 0 };
	int cohort[MAX_OFFENSE_FLAGS] = { 0 };
	int crossArrivedAtLanding = 0;
	for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
	{
		OffenseWave& w = offenseWaves[i];
		Building* flag = findFlagByGid(w.gid);
		if (flag == NULL && w.gid != NOGBID)
		{
			// flag died/was deleted; free the slot entirely.
			w.gid = NOGBID;
			w.phase = WAVE_NONE;
			w.phaseDeadline = 0;
			w.landingX = w.landingY = -1;
			w.musterBestArrived = 0;
		}
		flags[i] = flag;
		if (w.phase == WAVE_NONE)
			continue;
		cohort[i]  = flag ? static_cast<int>(flag->unitsWorking.size()) : 0;
		arrived[i] = countArrivedAtFlag(flag);
		// Fleet aggregate: warriors arrived across every wave CROSSing to THIS cycle's
		// staging point, so a large combined force releases together as one assault.
		if (w.phase == WAVE_CROSS && staged
		 && w.landingX == stagingX && w.landingY == stagingY)
			crossArrivedAtLanding += arrived[i];
	}
	const bool fleetReleaseNow = crossArrivedAtLanding >= fleetRelease;

	// --- pass B: advance phases and (re)place each live wave's flag ---
	bool someoneMustering = false;
	for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
	{
		OffenseWave& w = offenseWaves[i];
		if (w.phase == WAVE_NONE)
			continue;
		Building* flag = flags[i];

		if (w.phase == WAVE_MUSTER)
		{
			// TIMEOUT RESTART while the muster is still FILLING: each new arrival
			// high-water mark re-arms the deadline. The fixed timeout alone released
			// the home muster at 2-3 of 20 arrived (measured, Mazury seed 3): warriors
			// pop from ~6 scattered swarms and are still commuting to the rally when
			// OFFENSE_MUSTER_TIMEOUT_TICKS lapses. Restarting on a net increase keeps
			// the muster alive exactly while warriors are still gathering, and the
			// moment arrivals stall (deaths matching births, or nothing inbound) the
			// unmoved deadline releases the wave as before — so a stalled muster still
			// cannot hang the offense. High-water (not last-seen) so an oscillating
			// arrived count cannot re-arm forever. No new knob: the per-increase window
			// IS the existing muster timeout.
			if (arrived[i] > w.musterBestArrived)
			{
				w.musterBestArrived = arrived[i];
				w.phaseDeadline = obs.tick + OFFENSE_MUSTER_TIMEOUT_TICKS;
			}
			// March once the ARRIVED cohort is near full, or on the muster timeout.
			const bool massed   = arrived[i] >= musterReady;
			const bool timedOut = obs.tick >= w.phaseDeadline;
			if (massed || timedOut)
			{
				if (staged)
				{
					// -> CROSS: move the flag to the staging point (the amphibious
					// landing zone or the long-march forward rally); the bound cohort
					// marches/crosses to it. Do NOT evaluate the cross-release this
					// cycle — arrived is still measured at the RALLY, so re-checking now
					// would instantly re-release and skip the staging. The move happens
					// here; the release test runs from next cycle, once arrived reflects
					// the staging point.
					w.phase = WAVE_CROSS;
					w.landingX = stagingX;
					w.landingY = stagingY;
					w.phaseDeadline = obs.tick + crossTimeout;
					ensureFlagAt(w.gid, w.createCooldown, w.landingX, w.landingY, radius,
					             Cortex::CORTEX_MAX_FLAG_UNITS, 0, Cortex::CORTEX_PRIORITY_LOW, obs);
					continue;
				}
				// -> ASSAULT (land campaign): fall through to place on the target below.
				w.phase = WAVE_ASSAULT;
				w.phaseDeadline = 0;
				w.landingX = w.landingY = -1;
			}
			else
			{
				someoneMustering = true;
				ensureFlagAt(w.gid, w.createCooldown, rallyX, rallyY, radius,
				             Cortex::CORTEX_MAX_FLAG_UNITS, 0, Cortex::CORTEX_PRIORITY_NORMAL, obs);
				continue;
			}
		}

		if (w.phase == WAVE_CROSS)
		{
			// Release to the ASSAULT when this wave has massed at the staging point,
			// when the whole staged fleet has (fleetReleaseNow), or on the cross
			// timeout — or immediately if the campaign is no longer staged (target
			// became close/land-reachable, or the staging point vanished), so a
			// stranded CROSS wave never hangs.
			int crossReady = cohort[i] * OFFENSE_MUSTER_READY_NUM / OFFENSE_MUSTER_READY_DEN;
			if (crossReady < 1)
				crossReady = 1;
			const bool massed   = arrived[i] >= crossReady;
			const bool timedOut = obs.tick >= w.phaseDeadline;
			if (!staged || fleetReleaseNow || massed || timedOut)
			{
				// -> ASSAULT: fall through to place on the target below.
				w.phase = WAVE_ASSAULT;
				w.phaseDeadline = 0;
				w.landingX = w.landingY = -1;
			}
			else
			{
				ensureFlagAt(w.gid, w.createCooldown, w.landingX, w.landingY, radius,
				             Cortex::CORTEX_MAX_FLAG_UNITS, 0, Cortex::CORTEX_PRIORITY_LOW, obs);
				continue;
			}
		}

		// WAVE_ASSAULT. Retire a spent wave (cohort ground down by combat); else hold the
		// assault on the enemy target at LOW priority (keeps its cohort, pulls no fresh
		// recruits). cohort (BOUND count), not arrived — a just-released wave still has
		// its full cohort in transit, so it is never retired before it engages.
		if (flag != NULL && cohort[i] <= OFFENSE_WAVE_SPENT_WARRIORS)
		{
			clearOneFlag(w.gid);
			w.phase = WAVE_NONE;
			w.phaseDeadline = 0;
			w.landingX = w.landingY = -1;
			w.musterBestArrived = 0;
			w.createCooldown = 0;
			continue;
		}
		ensureFlagAt(w.gid, w.createCooldown, targetX, targetY, radius,
		             Cortex::CORTEX_MAX_FLAG_UNITS, 0, Cortex::CORTEX_PRIORITY_LOW, obs);
	}

	// --- pass C: ensure one wave is mustering the next cohort ---
	// AMPHIBIOUS SWIM-STAGING HOLD: with no muster flag recruiting, fresh warriors stay
	// ACT_RANDOM and walk to the swimming pool to learn SWIM (a flag-bound warrior is
	// ACT_FLAG and never trains). A musterer would bind every fresh warrior from birth and
	// freeze the swim-capable count at zero — the measured "bound from birth so never
	// learns to swim" loop on water maps. So on an amphibious campaign we do NOT spawn a
	// new musterer until enough warriors can already swim; existing waves stand.
	if (amphibious && obs.swimWarriors < Cortex::cortexTuning().amphibiousMinSwimWarriors)
		return;
	// Only if no wave is already mustering, a slot is free, we have a rally, and there
	// are warriors to gather (so we do not spin up empty flags with nothing to recruit).
	if (!someoneMustering && haveRally && warriors > 0)
	{
		for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
		{
			OffenseWave& w = offenseWaves[i];
			if (w.phase != WAVE_NONE || w.gid != NOGBID)
				continue; // slot in use.
			w.phase = WAVE_MUSTER;
			w.phaseDeadline = obs.tick + OFFENSE_MUSTER_TIMEOUT_TICKS;
			w.landingX = w.landingY = -1;
			w.musterBestArrived = 0; // fresh muster: arrival high-water starts empty.
			ensureFlagAt(w.gid, w.createCooldown, rallyX, rallyY, radius,
			             Cortex::CORTEX_MAX_FLAG_UNITS, 0, Cortex::CORTEX_PRIORITY_NORMAL, obs);
			break; // one new musterer per cycle.
		}
	}
}
