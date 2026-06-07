// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexPolicy.h"

namespace Cortex
{
	/// Percent of current inn feeding capacity the population must reach to trigger
	/// the next inn (Priority 2). Building at 100% means waiting until feeding is
	/// already exhausted before the next inn even breaks ground, so the colony spends
	/// the whole inn-build window over capacity. Triggering at 80% gives that lead
	/// time back — feeding stays ahead of population growth.
	static const int INN_BUILD_CAPACITY_PERCENT = 80;

	/// First valid candidate slot for `type` (the placement helper already ranks
	/// them best-first), or -1 if the observation surfaced no legal location.
	static int firstValidCandidate(const CortexObservation& obs, int type)
	{
		for (int slot = 0; slot < CORTEX_BUILD_CANDIDATES; slot++)
			if (obs.buildCandidates[type][slot].valid)
				return slot;
		return -1;
	}

	/// The per-swarm maxUnitWorking ceiling that applies right now.
	/// Early/mid game it is the conservative cap (7); once workers have trained
	/// BUILD to level CORTEX_SWARM_CAP_LIFT_BUILDLEVEL AND at least one idle
	/// worker exists (so promoting a hauler doesn't starve construction), the
	/// ceiling lifts to the late-game value (12).
	static int swarmWorkerCap(const CortexObservation& obs)
	{
		if (obs.maxBuildLevel >= CORTEX_SWARM_CAP_LIFT_BUILDLEVEL && obs.freeWorkers > 0)
			return CORTEX_SWARM_WORKER_CAP_LATE;
		return CORTEX_SWARM_WORKER_CAP;
	}

	/// True if any tracked swarm is "supply-stressed": pinned at the base worker cap
	/// (CORTEX_SWARM_WORKER_CAP — the "7 workers" ceiling) yet its CORN buffer is
	/// still draining below the add-a-hauler line (CORTEX_SWARM_CORN_ADD_LO). Such a
	/// swarm wants more haulers but cannot take any (already capped), so piling labour
	/// on it can no longer keep it fed — its local wheat catchment is the bottleneck.
	/// THAT is the signal to expand onto a fresh wheat patch with a second swarm,
	/// rather than starve the first. (The late-game cap lift to 12 only happens once
	/// BUILD tech has matured; checking the base cap here keeps the trigger at the
	/// user's "can't keep up with 7 workers" threshold.)
	static bool anySwarmSupplyStressed(const CortexObservation& obs)
	{
		for (int i = 0; i < obs.swarmCount; i++)
		{
			const TrackedBuilding& t = obs.trackedSwarms[i];
			if (t.valid
			 && t.maxUnitWorking >= CORTEX_SWARM_WORKER_CAP
			 && t.corn < CORTEX_SWARM_CORN_ADD_LO)
				return true;
		}
		return false;
	}

	// --- Priority 1: production control. HARD RULE: the swarm always produces;
	// the ratio is NEVER {0,0,0}. We only (re)issue a ratio when the swarm's
	// current output does not match the desired mix — detected from the bounded
	// count signals (the pure policy cannot read raw ratios) — so once the mix is
	// applied this whole block falls through and stops preempting the build
	// priorities below. Suppressed while panicking (the panic block owns the
	// ratio then). The action layer dedups, so a stray re-issue of an
	// already-applied ratio emits no order.
	std::optional<CortexAction> CortexPolicy::tryProductionControl(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (!f.panic)
		{
			// (a) (Re)start any swarm producing nothing — freshly built, or a halted
			//     {0,0,0} ratio loaded from an old save.
			if (obs.swarmsProducing < f.swarms)
				return makeSetProductionAction(f.growWorker, f.growExplorer, f.growWarrior);
			// (a2) End-of-panic recovery. The pre-combat panic defense flips swarms to
			//      100%-warrior production (growWarrior is 0 before the combat phase, so
			//      that mix exists ONLY as a panic leftover). When the attack ends the
			//      `panic` flag clears, but nothing here would revert the ratio: rules
			//      (a)/(c)/(d) never fire (the swarm IS producing, just warriors), and
			//      (b) is combat-phase-only — so the swarm would pump warriors forever
			//      and the worker economy never resumes. Detect the leftover via the
			//      swarmsProducingWarrior count and pull the mix back to the no-warrior
			//      economy ratio. Self-terminating: once no swarm makes warriors it
			//      stops firing (and re-fires only if another panic flips them again).
			if (!f.combatPhase && f.swarms > 0 && obs.swarmsProducingWarrior > 0)
				return makeSetProductionAction(f.growWorker, f.growExplorer, f.growWarrior);
			// (b) Establish the warrior mix once the economy is established but no
			//     warriors are being made yet. Self-terminating: stops firing as soon
			//     as the first warrior appears (re-fires if the army is wiped to 0).
			if (f.combatPhase && f.swarms > 0 && f.warriors == 0)
				return makeSetProductionAction(f.growWorker, f.growExplorer, f.growWarrior);
			// (c) Fold an explorer into the mix when we want one out but none is being
			//     produced and we have none yet (reveals our wheat / the enemy base).
			if (f.growExplorer > 0 && f.swarms > 0
			 && obs.swarmsProducingExplorer == 0 && obs.explorers == 0)
				return makeSetProductionAction(f.growWorker, f.growExplorer, f.growWarrior);
			// (d) Drop the explorer slice back out once an explorer exists, so we do
			//     not keep over-producing them. Stops once no swarm carries it.
			if (f.growExplorer == 0 && f.swarms > 0 && obs.swarmsProducingExplorer > 0)
				return makeSetProductionAction(f.growWorker, f.growExplorer, f.growWarrior);
			// (e) Apply / revert the worker-surplus throttle: re-issue the mix
			//     whenever the swarm's worker output disagrees with the desired
			//     growWorker (workers on <-> off). The symmetric peer of (c)/(d) for
			//     the explorer slice; only ever meaningful in the combat phase, where
			//     growWorker can be 0. Self-terminating and action-layer dedup'd, so a
			//     no-op re-issue emits no order.
			if (f.combatPhase && f.swarms > 0
			 && (f.growWorker > 0) != (obs.swarmsProducingWorker > 0))
				return makeSetProductionAction(f.growWorker, f.growExplorer, f.growWarrior);
		}

		// Worker-hauling tuning (swarms + inns + construction sites) no longer lives
		// in this priority ladder — it runs EVERY decision cycle in PARALLEL with
		// whatever single action this ladder returns, so keeping existing buildings
		// fed never preempts nor waits behind a build/upgrade decision (and vice
		// versa). See CortexPolicy::tuneWorkers(), enqueued alongside decide() by the
		// engine binding (AICortex::getOrder) the same way wheat-forbidden upkeep is.
		return std::nullopt;
	}

	// --- Priority 2: feed capacity (inns). FEED-LED, not wheat-led: build an inn
	// whenever the population has reached INN_BUILD_CAPACITY_PERCENT of the inns'
	// feeding capacity (or there is no inn yet) — placing the next inn at 80% full
	// rather than 100% so its build window overlaps the climb to capacity instead
	// of starting after feeding is already exhausted. An existing inn short of
	// WHEAT SUPPLY is the worker-tuning loop's problem (add haulers), NEVER a
	// reason to place another inn here. One site at a time; placement already
	// rejects sites too far from wheat. Ungated by spare labour — feeding is
	// existential, and this is what keeps the swarm from ever needing to halt for
	// overpopulation.
	std::optional<CortexAction> CortexPolicy::tryFeedCapacity(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (f.innSites == 0)
		{
			const bool noInnYet      = (f.inns == 0 && obs.totalUnit > 0);
			const bool capacityShort =
				(obs.totalUnit * 100 >= obs.feedCapacity * INN_BUILD_CAPACITY_PERCENT);
			if (noInnYet || capacityShort)
			{
				const int slot = firstValidCandidate(obs, CORTEX_BUILD_FOOD);
				if (slot >= 0)
					return makeBuildAction(CORTEX_BUILD_FOOD, slot);
			}
		}
		return std::nullopt;
	}

	// --- Priority 2.5: swarm RECOVERY only. We deliberately do NOT build a
	// second swarm for now (may revisit) — a team starts with one swarm, so this
	// fires only if that swarm was destroyed, restoring the ability to produce.
	std::optional<CortexAction> CortexPolicy::trySwarmRecovery(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (f.swarms == 0 && f.swarmSites == 0 && f.inns > 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SWARM);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_SWARM, slot);
		}
		return std::nullopt;
	}

	// --- Priority 6.95: second swarm on a freshly-discovered wheat patch. ------
	// A team starts with one swarm; Priority 2.5 only ever REBUILDS that one if it
	// is destroyed. Here we EXPAND: add swarms one per nearby wheat patch, up to
	// CORTEX_MAX_SWARMS. The placement helper already forces CORTEX_SWARM_MIN_SPACING
	// between swarms AND CORTEX_WHEAT_MAX_DIST to CORN, so a VALID swarm candidate
	// necessarily sits on a DIFFERENT patch within haul range — i.e. this fires
	// exactly when "another wheat patch is found in relative proximity to the base".
	// Placed AFTER the whole opening ladder (inn → school → racetrack → hospital →
	// barracks) and gated on that ladder being finished (openingBuildOutDone), so a
	// second swarm never disrupts the initial build run; gated on canExpand (spare
	// idle workers, not in food trouble) so the new swarm has labour to staff it.
	//
	// "Initial run done" means each core building TYPE has been built at least once
	// (inn, school, racetrack, hospital, barracks all finished). We deliberately do
	// NOT also require zero in-flight sites of those types: Cortex's feed-led growth
	// builds inns indefinitely, so an inn site is almost always pending — gating on
	// "no sites" would make this unsatisfiable on a healthy, ever-expanding colony.
	// The swarmSites==0 guard alone prevents queuing two swarms at once.
	// SUPPLY-STRESS GATE: also hold until the existing swarm(s) genuinely cannot
	// keep up — at least one is pinned at the 7-worker cap with a still-draining
	// CORN buffer (anySwarmSupplyStressed). Until then a single swarm + more
	// haulers is the cheaper answer; only a wheat-catchment bottleneck warrants a
	// whole new swarm on a fresh patch.
	std::optional<CortexAction> CortexPolicy::trySecondSwarm(const CortexObservation& obs, const DecideFacts& f) const
	{
		const bool openingBuildOutDone =
		    f.inns >= 1 && f.school >= 1 && f.race >= 1 && f.heal >= 1 && f.barracks >= 1;
		if (f.combatPhase && f.canExpand && openingBuildOutDone
		 && anySwarmSupplyStressed(obs)
		 && f.swarms >= 1 && f.swarms < CORTEX_MAX_SWARMS && f.swarmSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SWARM);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_SWARM, slot);
		}
		return std::nullopt;
	}

	// Worker-hauling tuning (closed-loop wheat-economy), run EVERY decision cycle in
	// PARALLEL with decide()'s single primary action — see the header doc. Each cycle
	// we nudge each swarm's maxUnitWorking by AT MOST +/-1 based on its corn-buffer
	// level, set each inn's to its collectable restock demand, and raise construction
	// sites toward the free-worker pool. This self-damps: when a building's corn level
	// sits in the deadband (ADD_LO <= corn < REM_HI) no adjustment fires; only when it
	// crosses a threshold does the count move, and the +/-1 step rate prevents the
	// chunky 5-CORN-per-unit production schedule from driving oscillation (a single
	// step per cycle is slower than the buffer responds, so it converges rather than
	// hunting). In steady state (buffers in the deadband) this returns ACTION_NOOP and
	// emits no order; the action layer also dedups per-building, so a re-issued
	// already-applied count is free.
	//
	// Note: makeTuneWorkersAction() returns an action with all
	// swarmWorkers[]/innWorkers[]/siteWorkers[] preset to -1 (leave unchanged); we
	// only overwrite entries for buildings that actually need adjustment.
	CortexAction CortexPolicy::tuneWorkers(const CortexObservation& obs) const
	{
		if (obs.version != OBSERVATION_VERSION || !obs.valid)
			return makeNoOpAction();

		CortexAction tune = makeTuneWorkersAction();
		bool anyChange = false;
		const int sCap = swarmWorkerCap(obs);
		for (int i = 0; i < obs.swarmCount; i++) {
			const TrackedBuilding& t = obs.trackedSwarms[i];
			if (!t.valid) continue;
			int desired = t.maxUnitWorking;
			if (mlSwarmCaps_)
			{
				// Effort-B pilot: the learned net picks the absolute cap. It applies
				// the SAME wheat-starved clamp + [WORKER_MIN..swarmWorkerCap] mask +
				// argmax internally (ML_CONTRACT.md), so we hand it the 16 features in
				// the contract's exact order and use its choice directly. Integer/
				// I16F16 → deterministic. Inn/site caps below stay hand-coded.
				const int features[CortexNet::NUM_FEATURES] = {
					t.corn, t.maxCorn, t.maxUnitWorking, t.unitsInside, t.maxUnitInside,
					t.nearestWheatDist, t.harvestableWheatNearby,
					obs.freeWorkers, obs.totalFree, obs.totalNeeded, obs.workers,
					obs.swarmCount, obs.feedCapacity, obs.starvingUnits, obs.needFood,
					obs.maxBuildLevel };
				desired = swarmNet_.chooseSwarmWorkers(features, obs.maxBuildLevel,
				                                       obs.freeWorkers, t.harvestableWheatNearby);
			}
			// Wheat-starved override: a swarm whose catchment holds too little
			// HARVESTABLE wheat cannot use more than a single hauler — extra workers
			// find no wheat to harvest and just idle or thrash the depleted patch. Cap
			// it at CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP outright (not the gentle
			// +/-1 step), regardless of the corn buffer. harvestableWheatNearby is -1
			// when unknown (game absent); only act on a real count. Takes precedence
			// over the buffer-driven add/remove below.
			else if (t.harvestableWheatNearby >= 0
			 && t.harvestableWheatNearby < CORTEX_SWARM_WHEAT_STARVED_TILES)
				desired = CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP;
			// Buffer draining: bring one more hauler in before the swarm stalls.
			// CORTEX_SWARM_CORN_ADD_LO is the stall threshold (swarm stops
			// producing at < 5 corn); catching it early buys a cycle of slack.
			else if (t.corn < CORTEX_SWARM_CORN_ADD_LO && t.maxUnitWorking < sCap)
				desired = t.maxUnitWorking + 1;
			// Buffer saturated: the buffer is full enough that this hauler could
			// do more useful work elsewhere — release one.
			else if (t.corn >= CORTEX_SWARM_CORN_REM_HI && t.maxUnitWorking > CORTEX_SWARM_WORKER_MIN)
				desired = t.maxUnitWorking - 1;
			if (desired != t.maxUnitWorking) { tune.swarmWorkers[i] = desired; anyChange = true; }
		}
		for (int i = 0; i < obs.innCount; i++) {
			const TrackedBuilding& t = obs.trackedInns[i];
			if (!t.valid) continue;
			// Post-build settle window: a freshly finished inn starts with an empty
			// buffer (a large restock deficit) and its as-built worker count. Hold that
			// count for CORTEX_INN_TUNE_DELAY_TICKS before applying the demand ceiling,
			// so a brand-new inn does not immediately pull a crowd of haulers off the
			// rest of the economy — it fills gradually meanwhile.
			if (t.ticksSinceFinished >= 0
			 && t.ticksSinceFinished < CORTEX_INN_TUNE_DELAY_TICKS)
				continue;
			// Inn hauler ceiling = the COLLECTABLE restock demand. CortexObservation
			// fills restockTripsNeeded = the inn's corn + IN-SIGHT fruit deficit in
			// hauler trips (fogged/unreachable resources excluded — fruit is
			// visibleToBeCollected, so it can't be hauled under fog, and the engine's
			// own desiredNumberOfWorkers counts the RAW deficit and would over-request
			// haulers that then idle). We set maxUnitWorking to that demand, clamped to
			// [MIN, CAP]; the engine self-regulates the actual hauler count below this
			// ceiling each tick. Scales with inn LEVEL (bigger corn/fruit caps → more
			// trips → more haulers) instead of the old fixed corn thresholds that
			// collapsed a level-2 inn to one hauler, and it does not forget fruit.
			// restockTripsNeeded == -1 means unknown (game absent): leave it untouched.
			int desired = t.maxUnitWorking;
			if (t.restockTripsNeeded >= 0)
			{
				int target = t.restockTripsNeeded;
				if (target < CORTEX_INN_WORKER_MIN) target = CORTEX_INN_WORKER_MIN;
				if (target > CORTEX_INN_WORKER_CAP) target = CORTEX_INN_WORKER_CAP;
				desired = target;
			}
			if (desired != t.maxUnitWorking) { tune.innWorkers[i] = desired; anyChange = true; }
		}
		// Construction sites: pour idle workers into in-progress builds. A site's
		// worker cap may rise to match the number of FREE workers, bounded by the
		// resource hauler-trips it still needs (more workers than that find no
		// delivery job). We only RAISE the cap (never lower it — that would slow a
		// build already underway), and draw down a running free-worker budget
		// across sites in index order so the caps we raise do not collectively
		// over-subscribe the idle pool. maxUnitWorking is a ceiling, not a
		// reservation: the engine still only assigns units that are actually idle,
		// and the HIGH-priority swarm keeps its own haulers regardless.
		int freeBudget = obs.freeWorkers;
		for (int i = 0; i < obs.siteCount && freeBudget > 0; i++) {
			const TrackedSite& s = obs.trackedSites[i];
			if (!s.valid || s.deliveriesLeft <= 0) continue;
			int want = s.deliveriesLeft < freeBudget ? s.deliveriesLeft : freeBudget;
			// The engine asserts any worker request <= CORTEX_MAX_BUILDING_WORKERS
			// (Game_orders.cpp:206); a big site can need far more deliveries.
			if (want > CORTEX_MAX_BUILDING_WORKERS) want = CORTEX_MAX_BUILDING_WORKERS;
			if (want > s.maxUnitWorking) {
				tune.siteWorkers[i] = want;
				anyChange = true;
				freeBudget -= (want - s.maxUnitWorking); // extra idle hands this claims.
			}
		}
		return anyChange ? tune : makeNoOpAction();
	}
}
