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

	/// A swarm "wants a fresh wheat patch" when its current catchment can no longer
	/// sustain it. Two faces of the same bottleneck:
	///   (1) CAPPED-DRAINING: pinned at the worker cap (CORTEX_SWARM_WORKER_CAP) yet
	///       its CORN buffer is still draining below the add-a-hauler line
	///       (CORTEX_SWARM_CORN_ADD_LO) — demand outruns supply even at full haulers.
	///   (2) FIELD-DEPLETED: its harvestable wheat has run out (harvestableWheatNearby
	///       below CORTEX_SWARM_WHEAT_STARVED_TILES), so the wheat-starved throttle has
	///       pinned it to a single hauler — it can no longer be at the worker cap, so
	///       (1) can never fire for it again, yet it produces almost nothing.
	/// Either way the fix is the same: expand onto a NEW patch with another swarm
	/// rather than pour more labour (or more inns) onto an exhausted catchment. On a
	/// boxed-in map like Muka the first swarm's field depletes mid-game, so (2) is the
	/// dominant trigger; (1) catches the early "can't keep up at 7 workers" case.
	/// harvestableWheatNearby is -1 when unknown (game absent) — guarded with >= 0.
	static bool swarmWantsFreshPatch(const TrackedBuilding& t)
	{
		if (!t.valid)
			return false;
		const bool cappedDraining =
			(t.maxUnitWorking >= CORTEX_SWARM_WORKER_CAP && t.corn < CORTEX_SWARM_CORN_ADD_LO);
		const bool fieldDepleted =
			(t.harvestableWheatNearby >= 0
			 && t.harvestableWheatNearby < CORTEX_SWARM_WHEAT_STARVED_TILES);
		return cappedDraining || fieldDepleted;
	}

	/// True if ANY tracked swarm wants a fresh wheat patch (see swarmWantsFreshPatch).
	/// The trigger for the second swarm AND the marginal-value discount on another inn
	/// (more inns beside an exhausted catchment cannot be stocked either).
	static bool anySwarmWantsFreshPatch(const CortexObservation& obs)
	{
		for (int i = 0; i < obs.swarmCount; i++)
			if (swarmWantsFreshPatch(obs.trackedSwarms[i]))
				return true;
		return false;
	}

	/// Severity of the worst swarm that wants a fresh patch, in [1, CORTEX_SWARM_CORN_ADD_LO];
	/// 0 when none does. The second-swarm score scales with this. A FIELD-DEPLETED
	/// catchment is the strongest expand signal, so it scores the maximum; a
	/// CAPPED-DRAINING swarm scales with how far its CORN buffer sits below the line.
	static int swarmFreshPatchSeverity(const CortexObservation& obs)
	{
		int worst = 0;
		for (int i = 0; i < obs.swarmCount; i++)
		{
			const TrackedBuilding& t = obs.trackedSwarms[i];
			if (!t.valid)
				continue;
			int sev = 0;
			if (t.maxUnitWorking >= CORTEX_SWARM_WORKER_CAP && t.corn < CORTEX_SWARM_CORN_ADD_LO)
				sev = CORTEX_SWARM_CORN_ADD_LO - t.corn; // corn in [0,4] -> 1..5
			if (t.harvestableWheatNearby >= 0
			 && t.harvestableWheatNearby < CORTEX_SWARM_WHEAT_STARVED_TILES)
				sev = CORTEX_SWARM_CORN_ADD_LO; // exhausted catchment: max severity
			if (sev > worst)
				worst = sev;
		}
		return worst;
	}

	// --- Priority 1: production control. HARD RULE: the swarm always produces;
	// the ratio is NEVER {0,0,0}. We only (re)issue a ratio when the swarm's
	// current output does not match the desired mix — detected from the bounded
	// count signals (the pure policy cannot read raw ratios) — so once the mix is
	// applied this whole block falls through and stops preempting the build
	// priorities below. Suppressed while panicking (the panic block owns the
	// ratio then). The action layer dedups, so a stray re-issue of an
	// already-applied ratio emits no order.
	ScoredAction CortexPolicy::scoreProductionControl(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (!f.panic)
		{
			// (a) (Re)start any swarm producing nothing — freshly built, or a halted
			//     {0,0,0} ratio loaded from an old save.
			if (obs.swarmsProducing < f.swarms)
				return { SCORE_PRODUCTION_CONTROL, makeSetProductionAction(f.growWorker, f.growExplorer, f.growWarrior) };
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
			//      Gated on !economyEstablished (NOT !combatPhase): this reverts the
			//      PRE-ESTABLISHMENT panic-leftover warrior mix only. An established-but-
			//      starving (foodSaturated) colony legitimately KEEPS its warriors now
			//      (growWarrior is set for any established colony), so it must not be
			//      reverted here — otherwise the famine blitz would lose its army.
			if (!f.economyEstablished && f.swarms > 0 && obs.swarmsProducingWarrior > 0)
				return { SCORE_PRODUCTION_CONTROL, makeSetProductionAction(f.growWorker, f.growExplorer, f.growWarrior) };
			// (b) Establish the warrior mix once the economy is established but no
			//     warriors are being made yet. Self-terminating: stops firing as soon
			//     as the first warrior appears (re-fires if the army is wiped to 0).
			if (f.economyEstablished && f.swarms > 0 && f.warriors == 0)
				return { SCORE_PRODUCTION_CONTROL, makeSetProductionAction(f.growWorker, f.growExplorer, f.growWarrior) };
			// (c) Fold an explorer into the mix when we want one out but none is being
			//     produced and we have none yet (reveals our wheat / the enemy base).
			if (f.growExplorer > 0 && f.swarms > 0
			 && obs.swarmsProducingExplorer == 0 && obs.explorers == 0)
				return { SCORE_PRODUCTION_CONTROL, makeSetProductionAction(f.growWorker, f.growExplorer, f.growWarrior) };
			// (d) Drop the explorer slice back out once an explorer exists, so we do
			//     not keep over-producing them. Stops once no swarm carries it.
			if (f.growExplorer == 0 && f.swarms > 0 && obs.swarmsProducingExplorer > 0)
				return { SCORE_PRODUCTION_CONTROL, makeSetProductionAction(f.growWorker, f.growExplorer, f.growWarrior) };
			// (e) Apply / revert the worker-surplus throttle AND the famine governor:
			//     re-issue the mix whenever the swarm's actual output disagrees with the
			//     desired one on EITHER the worker slice (workers on <-> off, the
			//     surplus throttle / feeding governor) OR the warrior slice (so a famine
			//     colony that wants warriors but isn't making them re-issues the mix).
			//     The symmetric peer of (c)/(d) for those slices. Gated on
			//     economyEstablished (NOT combatPhase) so it also fires in the
			//     foodSaturated famine regime, where growWorker is forced to 0 and
			//     growWarrior to 1 — without this the governor's mix would be computed
			//     but never issued as an order. Since economyEstablished ⊇ combatPhase,
			//     the only NEW firings are in foodSaturated; healthy combat-phase
			//     colonies behave identically. Self-terminating and action-layer dedup'd.
			if (f.economyEstablished && f.swarms > 0
			 && ((f.growWorker  > 0) != (obs.swarmsProducingWorker  > 0)
			  || (f.growWarrior > 0) != (obs.swarmsProducingWarrior > 0)))
				return { SCORE_PRODUCTION_CONTROL, makeSetProductionAction(f.growWorker, f.growExplorer, f.growWarrior) };
		}

		// Worker-hauling tuning (swarms + inns + construction sites) no longer lives
		// in this priority ladder — it runs EVERY decision cycle in PARALLEL with
		// whatever single action this ladder returns, so keeping existing buildings
		// fed never preempts nor waits behind a build/upgrade decision (and vice
		// versa). See CortexPolicy::tuneWorkers(), enqueued alongside decide() by the
		// engine binding (AICortex::getOrder) the same way wheat-forbidden upkeep is.
		return cortexDecline();
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
	ScoredAction CortexPolicy::scoreFeedCapacity(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (f.innSites != 0)
			return cortexDecline(); // one inn site at a time
		const bool noInnYet      = (f.inns == 0 && obs.totalUnit > 0);
		const bool capacityShort =
			(obs.totalUnit * 100 >= obs.feedCapacity * INN_BUILD_CAPACITY_PERCENT);
		if (!noInnYet && !capacityShort)
			return cortexDecline();
		const int slot = firstValidCandidate(obs, CORTEX_BUILD_FOOD);
		if (slot < 0)
			return cortexDecline();
		// MARGINAL VALUE: when a swarm's wheat catchment is exhausted (it wants a
		// fresh patch — pinned at the worker cap with a draining CORN buffer, or its
		// harvestable wheat run out), the feeding deficit is a wheat-SUPPLY problem,
		// not an inn-CAPACITY one — another inn beside the same depleted field can
		// never be stocked, so its marginal value collapses. Discount it below the
		// second-swarm score (the real fix: a fresh swarm on a new patch) so expansion
		// can win the cycle. The FIRST inn is existential and is never discounted.
		int score = SCORE_FEED_CAPACITY;
		if (!noInnYet && anySwarmWantsFreshPatch(obs))
			score = SCORE_FEED_BOTTLENECKED;
		return { score, makeBuildAction(CORTEX_BUILD_FOOD, slot) };
	}

	// --- Priority 2.5: swarm RECOVERY only. We deliberately do NOT build a
	// second swarm for now (may revisit) — a team starts with one swarm, so this
	// fires only if that swarm was destroyed, restoring the ability to produce.
	ScoredAction CortexPolicy::scoreSwarmRecovery(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (f.swarms == 0 && f.swarmSites == 0 && f.inns > 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SWARM);
			if (slot >= 0)
				return { SCORE_SWARM_RECOVERY, makeBuildAction(CORTEX_BUILD_SWARM, slot) };
		}
		return cortexDecline();
	}

	// --- Priority 6.95: second swarm on a freshly-discovered wheat patch. ------
	// A team starts with one swarm; Priority 2.5 only ever REBUILDS that one if it
	// is destroyed. Here we EXPAND: add swarms one per nearby wheat patch, up to the
	// tracking wall (CORTEX_MAX_TRACKED_SWARMS — a swarm past it would be untracked,
	// invisible to the fresh-patch trigger and hauler tuning). There is no arbitrary
	// count cap below that: WHEN/WHERE to add a swarm is governed by the placement gate
	// below, not a fixed number. The placement helper already forces CORTEX_SWARM_MIN_SPACING
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
	// FRESH-PATCH GATE: also hold until an existing swarm genuinely cannot keep up —
	// its wheat catchment is the bottleneck, either pinned at the worker cap with a
	// draining CORN buffer OR its harvestable wheat exhausted (anySwarmWantsFreshPatch).
	// Until then a single swarm + more haulers is the cheaper answer; only a spent
	// catchment warrants a whole new swarm on a fresh patch.
	//
	// FAMINE RELOCATION (the depletion-trap escape): gated on economyEstablished, NOT
	// combatPhase. combatPhase = established && !starving, so the old gate locked this
	// rung out exactly when a depleted catchment had already pushed the colony into
	// starvation (foodSaturated) — feedCapacity recedes to zero, the inn rung discounts
	// itself (anySwarmWantsFreshPatch → SCORE_FEED_BOTTLENECKED) but the swarm it was
	// clearing room for could never fire, so the colony stacked inns on the dead field
	// and starved in place. Admitting foodSaturated lets the established-but-starving
	// colony relocate onto the fresh patch this candidate sits on. We still require
	// spare labour (freeWorkers > 0) to STAFF it — without it relocation declines rather
	// than steal the haulers that keep the buffers full — and a VALID candidate (slot >= 0)
	// guarantees genuinely fresh wheat to move to, so we never pour labour onto a dead
	// field. Healthy (combatPhase) colonies are unchanged: same graded score as before,
	// so non-famine behaviour (and its replays) is byte-identical.
	ScoredAction CortexPolicy::scoreSecondSwarm(const CortexObservation& obs, const DecideFacts& f) const
	{
		const bool openingBuildOutDone =
		    f.inns >= 1 && f.school >= 1 && f.race >= 1 && f.heal >= 1 && f.barracks >= 1;
		// economyEstablished (⊇ combatPhase) admits the foodSaturated famine slice; the
		// only NEW firings vs the old combatPhase gate are established-AND-starving. We
		// require only spare labour to STAFF the new swarm (freeWorkers > 0); the old
		// canExpand's !hungry was already dropped (a wheat bottleneck makes the colony
		// hungry, so requiring !hungry blocked the very swarm that would cure it).
		if (f.economyEstablished && obs.freeWorkers > 0 && openingBuildOutDone
		 && anySwarmWantsFreshPatch(obs)
		 && f.swarms >= 1 && f.swarms < CORTEX_MAX_TRACKED_SWARMS && f.swarmSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SWARM);
			if (slot >= 0)
			{
				// Healthy colony: score scales with how spent the worst catchment is
				// (severity 1..5), landing above the tech/upgrade band so the fresh wheat
				// patch outranks another upgrade when wheat is the binding constraint.
				// Famine (foodSaturated): relocation is the trap escape and must outrank
				// the wheat-blitz — see SCORE_SECOND_SWARM_FAMINE.
				const int severity = swarmFreshPatchSeverity(obs);
				const int score = f.foodSaturated
					? SCORE_SECOND_SWARM_FAMINE
					: SCORE_SECOND_SWARM_BASE + severity * SCORE_SECOND_SWARM_STEP;
				return { score, makeBuildAction(CORTEX_BUILD_SWARM, slot) };
			}
		}
		return cortexDecline();
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
			// Restore a finished inn to NORMAL engine priority. An inn spends its
			// construction-site phase pinned to LOW (see the site loop below) so it
			// does not out-recruit feeding/production while building; the engine
			// carries that LOW onto the finished building (Update.cpp
			// updateBuildingSite does not reset priority), which would then make the
			// inn LOSE feeding contention — the opposite of what we want. Lift it
			// back to NORMAL the first cycle we see it finished. Independent of the
			// settle window below (priority is a contention bucket, not a hauler
			// count); dedup against the observed priority so a steady-state NORMAL
			// inn emits no order.
			if (t.priority != CORTEX_PRIORITY_NORMAL) {
				tune.innPriority[i] = CORTEX_PRIORITY_NORMAL; anyChange = true;
			}
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
		// Construction priority: pin EVERY construction site (new build or
		// in-progress upgrade) to LOW engine priority. The engine serves
		// worker-assignment buckets highest-priority-first
		// (Team::prioritize_building, TeamStep.cpp), so a LOW site only draws
		// workers once every NORMAL/HIGH building (feeding inns, producing swarms)
		// is satisfied — construction can never out-recruit feeding or production.
		// This is orthogonal to the worker-cap pour below (which only RAISES the
		// ceiling): idle hands still flow into the site, just after the economy is
		// fed. The LOW carries onto the finished building (the engine does not reset
		// it); finished inns are lifted back to NORMAL in the inn loop above, and
		// swarms by the HIGH/NORMAL priority split (CortexPolicyCombat). Dedup
		// against the observed priority so an already-LOW site emits no order.
		for (int i = 0; i < obs.siteCount; i++) {
			const TrackedSite& s = obs.trackedSites[i];
			if (!s.valid) continue;
			if (s.priority != CORTEX_PRIORITY_LOW) {
				tune.sitePriority[i] = CORTEX_PRIORITY_LOW; anyChange = true;
			}
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
