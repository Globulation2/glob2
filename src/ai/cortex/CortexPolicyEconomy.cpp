// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexPolicy.h"
#include "CortexTuning.h"

namespace Cortex
{
	// The tuning defaults must reproduce the committed scores exactly (the
	// zero-behavior-change contract of the params seam).
	static_assert(CortexTuning{}.scoreSecondSwarmBase == SCORE_SECOND_SWARM_BASE
	           && CortexTuning{}.scoreSecondSwarmStep == SCORE_SECOND_SWARM_STEP,
	              "CortexTuning score defaults must stay in lockstep with CortexScore");

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
	/// Early/mid game it is the conservative cap (tuning.swarmWorkerCap, default
	/// 7); once workers have trained BUILD to level
	/// CORTEX_SWARM_CAP_LIFT_BUILDLEVEL AND at least one idle worker exists (so
	/// promoting a hauler doesn't starve construction), the ceiling lifts to the
	/// late-game value (12).
	static int swarmWorkerCap(const CortexObservation& obs)
	{
		if (obs.maxBuildLevel >= CORTEX_SWARM_CAP_LIFT_BUILDLEVEL && obs.freeWorkers > 0)
			return CORTEX_SWARM_WORKER_CAP_LATE;
		return cortexTuning().swarmWorkerCap;
	}

	/// FIELD-DEPLETED face of the wheat bottleneck: the swarm's harvestable wheat has
	/// run out (harvestableWheatNearby below tuning.wheatStarvedTiles), so the
	/// wheat-starved throttle has pinned it to a single hauler — it can no longer be
	/// at the worker cap, yet it produces almost nothing. The catchment is DEAD, not
	/// merely strained. harvestableWheatNearby is -1 when unknown (game absent) —
	/// guarded with >= 0. Shared by the fresh-patch trigger and its severity grade.
	static bool swarmFieldDepleted(const TrackedBuilding& t)
	{
		return t.valid
		    && t.harvestableWheatNearby >= 0
		    && t.harvestableWheatNearby < cortexTuning().wheatStarvedTiles;
	}

	/// CAPPED-DRAINING face of the wheat bottleneck: pinned at the worker cap yet
	/// the CORN buffer is draining below the expansion line — demand outruns supply
	/// even at full haulers. Shared by the fresh-patch trigger and its severity
	/// grade so the two can never disagree. The Muka seed-1 diagnosis
	/// (.tmp/rankgate-diag/FINDINGS.md) showed this face fires on production-cycle
	/// corn noise while the patch still holds abundant wheat, so it takes the
	/// optional wheat-abundance veto: with tuning.expandWheatVeto > 0, a swarm that
	/// can still see that many harvestable tiles is NOT capped-draining, whatever
	/// its buffer does this cycle (0 = veto off, the committed behavior).
	static bool swarmCappedDraining(const TrackedBuilding& t)
	{
		const CortexTuning& tuning = cortexTuning();
		if (t.maxUnitWorking < tuning.swarmWorkerCap || t.corn >= tuning.expandCornLo)
			return false;
		if (tuning.expandWheatVeto > 0
		 && t.harvestableWheatNearby >= tuning.expandWheatVeto)
			return false;
		return true;
	}

	/// A swarm "wants a fresh wheat patch" when its current catchment can no longer
	/// sustain it. Two faces of the same bottleneck:
	///   (1) CAPPED-DRAINING: pinned at the worker cap (CORTEX_SWARM_WORKER_CAP) yet
	///       its CORN buffer is still draining below the add-a-hauler line
	///       (CORTEX_SWARM_CORN_ADD_LO) — demand outruns supply even at full haulers.
	///   (2) FIELD-DEPLETED: see swarmFieldDepleted — (1) can never fire for such a
	///       swarm again (the throttle holds it below the cap).
	/// Either way the fix is the same: expand onto a NEW patch with another swarm
	/// rather than pour more labour (or more inns) onto an exhausted catchment. On a
	/// boxed-in map like Muka the first swarm's field depletes mid-game, so (2) is the
	/// dominant trigger; (1) catches the early "can't keep up at 7 workers" case.
	static bool swarmWantsFreshPatch(const TrackedBuilding& t)
	{
		if (!t.valid)
			return false;
		return swarmCappedDraining(t) || swarmFieldDepleted(t);
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

	// Debounce counter for the fresh-patch desire gate: how many CONSECUTIVE
	// decision cycles (decide() calls, ~25 ticks apart) anySwarmWantsFreshPatch
	// has held, so scoreSecondSwarm can require it to persist
	// tuning.expandDebounceCycles cycles before firing — a corn-buffer dip that
	// self-heals next cycle is production noise, not a spent catchment. Called
	// EXACTLY ONCE per decision cycle, at the top of decide(); decideCombat and
	// extractDecideFeatures never touch it. RAM-only on purpose (same precedent
	// as the inn settle window / hauler kickstart): a save reload restarts the
	// streak at 0 on every client in lockstep, so no desync — the trigger merely
	// re-arms. At the default of 1 cycle, streak >= 1 is exactly
	// anySwarmWantsFreshPatch(obs) this cycle: the committed behavior.
	void CortexPolicy::updateExpandStreak(const CortexObservation& obs)
	{
		if (anySwarmWantsFreshPatch(obs))
			expandWantStreak_++;
		else
			expandWantStreak_ = 0;
	}

	/// Severity of the worst swarm that wants a fresh patch, in [1, tuning.expandCornLo];
	/// 0 when none does. The second-swarm score scales with this. A FIELD-DEPLETED
	/// catchment is the strongest expand signal, so it scores the maximum; a
	/// CAPPED-DRAINING swarm scales with how far its CORN buffer sits below the line.
	static int swarmFreshPatchSeverity(const CortexObservation& obs)
	{
		const CortexTuning& tuning = cortexTuning();
		int worst = 0;
		for (int i = 0; i < obs.swarmCount; i++)
		{
			const TrackedBuilding& t = obs.trackedSwarms[i];
			if (!t.valid)
				continue;
			int sev = 0;
			if (swarmCappedDraining(t))
				sev = tuning.expandCornLo - t.corn; // corn in [0,expandCornLo-1] -> 1..expandCornLo
			if (swarmFieldDepleted(t))
				sev = tuning.expandCornLo; // exhausted catchment: max severity
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
		// EXPERIMENT: the marginal-value discount (drop to SCORE_FEED_BOTTLENECKED
		// when anySwarmWantsFreshPatch, so swarm expansion wins) is REMOVED. Its
		// premise — "an inn beside a depleted field can never be stocked, so the
		// deficit is wheat-SUPPLY not inn-CAPACITY" — is contradicted by the Muka
		// inn trace: during the famine the inns sit at 5-10/10 corn with one hauler
		// (restockReq ~0), full of eaters (inside maxed). They are NOT corn-starved;
		// the shortage is feeding THROUGHPUT (inn eating-slots vs a 120-unit
		// population), which only MORE inns fix. Discounting inns toward swarms then
		// added breeding instead of feeding — the wrong lever. Keep the inn at full
		// SCORE_FEED_CAPACITY so feeding capacity wins the cycle over expansion.
		return { SCORE_FEED_CAPACITY, makeBuildAction(CORTEX_BUILD_FOOD, slot) };
	}

	// --- Priority 2.5: swarm RECOVERY only. We deliberately do NOT build a
	// second swarm for now (may revisit) — a team starts with one swarm, so this
	// fires only if that swarm was destroyed, restoring the ability to produce.
	// A finished first inn is required so the rebuild happens behind a working
	// feed loop — that precondition is GATE_BOOTSTRAP in decide()'s gate table.
	ScoredAction CortexPolicy::scoreSwarmRecovery(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (f.swarms == 0 && f.swarmSites == 0)
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
	// EXPANSION IS DECIDED BY RANKING, NOT LADDER COMPLETION. There is deliberately
	// no "opening build-out finished" precondition serializing the second swarm
	// behind the tech ladder (school/racetrack/hospital/barracks). A hard ladder
	// gate couples expansion to preconditions that have nothing to do with wheat:
	// the school (correctly) hard-waits on reachable algae, so on a map where algae
	// is discovered late the ladder can sit incomplete for tens of thousands of
	// ticks — vetoing expansion the whole time even when fresh wheat is in reach
	// and the catchment is the real constraint, which loses the macro game against
	// a multi-swarm boom. When expansion should win a cycle is already encoded in
	// this scorer's own machinery: anySwarmWantsFreshPatch is the desire gate (no
	// swarm is added until an existing catchment genuinely cannot keep up), the
	// severity grade lifts the score above the tech band exactly when wheat is the
	// binding constraint, and the famine score handles the trap-escape case — so
	// against the tech candidates the second swarm competes on urgency, not on
	// ladder position. Bootstrap protection lives in decide()'s gate table
	// (GATE_BOOTSTRAP: the first inn must actually FINISH before any expansion) and
	// staffing discipline is GATE_LABOR (spare labour, so the new swarm never
	// steals the haulers that keep the buffers full).
	//
	// We deliberately do NOT require zero in-flight sites of the core building
	// types: Cortex's feed-led growth builds inns indefinitely, so an inn site is
	// almost always pending — gating on "no sites" would make this unsatisfiable on
	// a healthy, ever-expanding colony. The swarmSites==0 guard alone prevents
	// queuing two swarms at once.
	// FRESH-PATCH GATE: hold until an existing swarm genuinely cannot keep up —
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
	// spare labour (freeWorkers >= 1, GATE_LABOR) to STAFF it — without it relocation
	// declines rather than steal the haulers that keep the buffers full — and a VALID
	// candidate (slot >= 0) guarantees genuinely fresh wheat to move to, so we never
	// pour labour onto a dead field. Healthy (combatPhase) colonies keep the graded
	// severity score; only the famine slice takes SCORE_SECOND_SWARM_FAMINE.
	ScoredAction CortexPolicy::scoreSecondSwarm(const CortexObservation& obs, const DecideFacts& f) const
	{
		// economyEstablished (⊇ combatPhase) admits the foodSaturated famine slice
		// (a mature colony may relocate while starving). The bootstrap stays
		// protected by GATE_BOOTSTRAP in decide()'s gate table (economyEstablished
		// alone admits an inn merely underway when freeWorkers > 0; the gate
		// requires the FIRST inn actually FINISHED), and spare labour to STAFF the
		// new swarm is GATE_LABOR; canExpand's !hungry is deliberately not required
		// (a wheat bottleneck makes the colony hungry, so requiring !hungry would
		// block the very swarm that cures it).
		// The desire gate is the DEBOUNCED anySwarmWantsFreshPatch: the streak
		// decide() maintains (updateExpandStreak) must have held for
		// tuning.expandDebounceCycles consecutive cycles. At the default of 1 this
		// is exactly anySwarmWantsFreshPatch(obs) this cycle.
		const CortexTuning& tuning = cortexTuning();
		if (f.economyEstablished
		 && expandWantStreak_ >= tuning.expandDebounceCycles
		 && f.swarms >= 1 && f.swarms < CORTEX_MAX_TRACKED_SWARMS && f.swarmSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SWARM);
			if (slot >= 0)
			{
				// Healthy colony: score scales with how spent the worst catchment is
				// (severity 1..expandCornLo), landing above the tech/upgrade band so the
				// fresh wheat patch outranks another upgrade when wheat is the binding
				// constraint. The severity floor (default 1 = any) lets the search
				// demand a genuinely spent catchment before expansion fires at all;
				// it gates the famine branch too (a famine catchment is field-depleted
				// in practice, i.e. already at max severity).
				// Famine (foodSaturated): relocation is the trap escape and must outrank
				// the wheat-blitz — see SCORE_SECOND_SWARM_FAMINE.
				const int severity = swarmFreshPatchSeverity(obs);
				if (severity >= tuning.expandSeverityFloor)
				{
					const int score = f.foodSaturated
						? SCORE_SECOND_SWARM_FAMINE
						: tuning.scoreSecondSwarmBase + severity * tuning.scoreSecondSwarmStep;
					return { score, makeBuildAction(CORTEX_BUILD_SWARM, slot) };
				}
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
			 && t.harvestableWheatNearby < cortexTuning().wheatStarvedTiles)
				desired = CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP;
			// Buffer draining: bring one more hauler in before the swarm stalls.
			// CORTEX_SWARM_CORN_ADD_LO is the stall threshold (swarm stops
			// producing at < 5 corn); catching it early buys a cycle of slack.
			else if (t.corn < CORTEX_SWARM_CORN_ADD_LO && t.maxUnitWorking < sCap)
				desired = t.maxUnitWorking + 1;
			// Buffer saturated: the buffer is full enough that this hauler could
			// do more useful work elsewhere — release one.
			else if (t.corn >= cortexTuning().swarmCornRemHi && t.maxUnitWorking > CORTEX_SWARM_WORKER_MIN)
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
			// Inn hauler ceiling = the inn's CORN-deficit restock demand.
			// CortexObservation fills restockTripsNeeded = (maxCorn - corn) in hauler
			// trips: how empty the corn buffer is. We set maxUnitWorking to that demand,
			// clamped to [MIN, CAP]; the engine self-regulates the actual hauler count
			// below this ceiling each tick. Scales with inn LEVEL (bigger corn cap means
			// more trips, more haulers) instead of the old fixed corn thresholds that
			// collapsed a level-2 inn to one hauler. restockTripsNeeded == -1 means
			// unknown (game absent): leave it untouched.
			int desired = t.maxUnitWorking;
			// Wheat-starvation override: if the inn has no CORN within
			// CORTEX_INN_WHEAT_STARVED_RADIUS tiles (nearestWheatDist < 0 means none
			// within the scan cap), its haulers have nothing to fetch — hold it at the
			// floor regardless of the corn deficit. Mirrors the swarm wheat-starved
			// clamp; without it a corn-deficit inn beside an exhausted or too-distant
			// field would pull a crowd of haulers that just idle.
			if (t.nearestWheatDist < 0
			 || t.nearestWheatDist > CORTEX_INN_WHEAT_STARVED_RADIUS)
				desired = CORTEX_INN_WORKER_MIN;
			else if (t.restockTripsNeeded >= 0)
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
