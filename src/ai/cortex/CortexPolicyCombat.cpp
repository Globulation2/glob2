// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexPolicy.h"

namespace Cortex
{
	/// Reactive threshold that suppresses *expansion spending* (never swarm
	/// production) while the colony is in food trouble: percent of units actively
	/// starving (losing HP) above which we stop starting new tech/expansion builds
	/// until feeding recovers.
	static const int STARVE_HALT_PERCENT = 6;

	/// Standing warriors required before committing an offensive war flag. We
	/// turtle (build the army at home, defending) until we have a real force, then
	/// commit. The barracks auto-trains idle warriors to attack level 1 during this
	/// turtle build-up, so a raw-count commit already sends level-1 warriors; gating
	/// on a *trained* count instead only delayed the attack and made us turtle to a
	/// draw against a stronger economy (measured: it cost ~13 pts vs Castor and added
	/// timeout draws), so we commit on raw numbers.
	///
	/// COMMIT-SIZE LESSON (measured, SmallForTwo --swap-sides, with the offense-hold
	/// hysteresis in the action layer): RAISING this delays the attack and turtles us
	/// into a stronger enemy economy — 18 dropped Castor from ~51% to ~24% (and added
	/// timeout draws) with no gain vs Nicowar/Warrush. LOWERING it to commit earlier,
	/// now that the hysteresis lets the early push actually advance instead of
	/// oscillating home, was strictly better: 8 beat 10 and 12 vs Nicowar (~2.5% vs
	/// ~1.2%/~0%) and lifted Castor to ~51% with only sporadic draws. Warrush is
	/// insensitive to this knob (its all-in rush wins on tempo regardless). So we
	/// commit early at 8 — a smaller but EARLIER force that harasses before the enemy
	/// army matures, paired with the hysteresis so the push holds instead of melting.
	static const int ATTACK_MIN_WARRIORS = 8;
	/// Blitz commit size: when the colony is past wheat capacity and starving
	/// (foodSaturated), it will die in place if it sits still — so we spend whatever
	/// army we have on the enemy NOW, at a lower bar than the patient turtle-then-
	/// commit ATTACK_MIN_WARRIORS. Tunable.
	static const int BLITZ_MIN_WARRIORS = 4;
	/// Standing warriors required to bother recalling for defense — defend with
	/// whatever we have the moment the base is touched.
	static const int DEFENSE_MIN_WARRIORS = 1;
	/// War-flag attraction radii (the flag's unitStayRange). Offense reaches a
	/// little wider to engage units around the target building; defense is tight
	/// around the threatened building. Clamped to CORTEX_MAX_FLAG_RADIUS by the
	/// action layer.
	static const int OFFENSE_FLAG_RADIUS = 8;
	static const int DEFENSE_FLAG_RADIUS = 5;

	/// First valid candidate slot for `type` (the placement helper already ranks
	/// them best-first), or -1 if the observation surfaced no legal location.
	static int firstValidCandidate(const CortexObservation& obs, int type)
	{
		for (int slot = 0; slot < CORTEX_BUILD_CANDIDATES; slot++)
			if (obs.buildCandidates[type][slot].valid)
				return slot;
		return -1;
	}

	/// True if any valid tracked swarm's engine priority differs from `target`.
	/// Drives the panic defense's raise-to-HIGH and the restore-to-NORMAL steps:
	/// each fires only while a swarm is not yet at the wanted priority, and the
	/// action layer dedups, so the orders stop as soon as every swarm matches.
	static bool anySwarmPriorityNot(const CortexObservation& obs, int target)
	{
		for (int i = 0; i < obs.swarmCount; i++)
			if (obs.trackedSwarms[i].valid && obs.trackedSwarms[i].priority != target)
				return true;
		return false;
	}

	/// True if the swarm priorities do not yet match the steady-state split: the
	/// FIRST/primary swarm (the first valid tracked swarm) at `firstTarget` and every
	/// other swarm at `restTarget`. Drives the always-keep-the-primary-swarm-HIGH
	/// posture; the action layer dedups, so it stops firing once the split holds.
	static bool swarmPrioritiesNeedSplit(const CortexObservation& obs,
	                                     int firstTarget, int restTarget)
	{
		bool seenFirst = false;
		for (int i = 0; i < obs.swarmCount; i++)
		{
			const TrackedBuilding& t = obs.trackedSwarms[i];
			if (!t.valid)
				continue;
			const int want = seenFirst ? restTarget : firstTarget;
			seenFirst = true;
			if (t.priority != want)
				return true;
		}
		return false;
	}

	// --- Priority 0: pre-combat panic defense. ---
	// Before the combat phase unlocks (it needs COMBAT_ECON_MIN_UNITS units + an
	// inn + a swarm), Cortex has no army and the combat-gated defense flag /
	// barracks are unreachable — so an early all-in rush kills a defenceless
	// colony (the measured Warrush failure mode). This is the economy-phase
	// emergency response. It PREEMPTS the economy priorities while it still has
	// setup work, firing only when we are NOT yet in combat phase AND something
	// of ours is under attack. Response, in order (each step dedups, so once
	// satisfied it falls through to the next):
	//   (1) flip every swarm to 100%-warrior production — start making fighters
	//       now (raw level-0/1 warriors still buy time and bodies),
	//   (2) raise the swarms to HIGH engine priority so they win worker
	//       contention and keep the warrior pump fed,
	//   (3) panic-build a hospital (HEAL_BUILDING) to heal the defenders.
	// Once all three are set the block falls through entirely to the normal
	// economy beneath, which keeps the swarms fed (Priority 1.5) while they pump
	// warriors. The trigger is reactive (underAttackTimer-based), so when the
	// attack ends the panic clears and the economy resumes on its own; the
	// restore branch then drops the swarms back to NORMAL priority.
	ScoredAction CortexPolicy::scorePanicDefense(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (f.panic)
		{
			// (1) 100% warriors — fire until every swarm is warrior-only.
			if (f.swarms > 0 && obs.swarmsProducingWarrior < f.swarms)
				return { SCORE_PANIC_DEFENSE, makeSetProductionAction(0, 0, 1) };
			// (2) Swarms to HIGH priority — EVERY swarm, not just the primary, so the
			//     whole warrior pump wins worker contention while the base is hit.
			if (anySwarmPriorityNot(obs, CORTEX_PRIORITY_HIGH))
				return { SCORE_PANIC_DEFENSE, makeSetPriorityAction(CORTEX_PRIORITY_HIGH, CORTEX_PRIORITY_HIGH) };
			// (3) Panic-build one hospital if none is up or already building.
			if (f.heal == 0 && f.healSites == 0)
			{
				const int slot = firstValidCandidate(obs, CORTEX_BUILD_HEAL);
				if (slot >= 0)
					return { SCORE_PANIC_DEFENSE, makeBuildAction(CORTEX_BUILD_HEAL, slot) };
			}
			// Panic setup complete — fall through to the normal economy, which keeps
			// the swarms fed while they produce the defending army.
		}
		else if (swarmPrioritiesNeedSplit(obs, CORTEX_PRIORITY_HIGH, CORTEX_PRIORITY_NORMAL))
		{
			// Steady-state priority split: keep the FIRST/primary swarm at HIGH so it
			// always wins worker/hauler contention and pumps the early worker economy,
			// while any later (second) swarm sits at NORMAL. This also restores the
			// primary swarm to HIGH after a panic ends (the panic raised every swarm).
			return { SCORE_PANIC_DEFENSE, makeSetPriorityAction(CORTEX_PRIORITY_HIGH, CORTEX_PRIORITY_NORMAL) };
		}
		return cortexDecline();
	}

	// --- Priority 7: defense (recall the army to a threatened building). ---
	// War flags are standing buildings: once placed they keep summoning
	// warriors without being re-issued, so this need not fire every cycle —
	// it just (re)positions the single flag onto the current threat. Defense
	// outranks offense: when our base is under attack the lone flag comes home.
	ScoredAction CortexPolicy::scoreDefense(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (f.combatPhase && obs.buildingsUnderAttack > 0
		 && f.warriors >= DEFENSE_MIN_WARRIORS && obs.defenseTarget.valid)
		{
			// THRASH HYSTERESIS (relocated from the action layer): if we are mid-
			// offense-push (CORTEX_POSTURE_OFFENSE and still inside the hold window)
			// and the base threat is merely harassment — fewer than
			// CORTEX_DEFENSE_SERIOUS_BUILDINGS of our buildings taking fire at once —
			// do NOT recall. Holding the offense flag where it stands (a war flag is a
			// standing building, so it keeps summoning) lets the army actually reach and
			// break the enemy line instead of oscillating home every decision cycle. A
			// real base assault (>= CORTEX_DEFENSE_SERIOUS_BUILDINGS under fire) still
			// earns the recall. We HOLD by returning NoOp — NOT std::nullopt — so the
			// ladder stops here exactly as it did when this was Priority 7's match: the
			// action layer then changes nothing (no flag re-task, no posture mutation),
			// leaving the existing offense flag in place. Falling through instead would
			// let tryRetireFlag/tryOffense run, which they never did before — a behavior
			// change. The hold-window re-arm and posture mutation stay an EXECUTION
			// side-effect in AICortex::translateActionPlaceWarFlag (state ownership did
			// not move; only this DECISION did).
			const bool seriousThreat =
				(obs.buildingsUnderAttack >= CORTEX_DEFENSE_SERIOUS_BUILDINGS);
			if (obs.flagPosture == CORTEX_POSTURE_OFFENSE
			 && obs.tick < obs.offenseHoldUntil
			 && !seriousThreat)
				return { SCORE_DEFENSE, makeNoOpAction() }; // hold the offense; ignore the minor-harassment recall.

			return { SCORE_DEFENSE, makeDefenseFlagAction(DEFENSE_FLAG_RADIUS, f.warriors) };
		}
		return cortexDecline();
	}

	// --- Priority 7.2: retire a purposeless war flag. ---
	// A flag (defense flags plant with as few as DEFENSE_MIN_WARRIORS == 1) is
	// stranded the moment the threat clears yet the army is too thin to attack:
	// defense (above) no longer fires, and offense (below) needs ATTACK_MIN_WARRIORS
	// plus a seen target. Without this rung the flag sat forever in that dead band —
	// neither moved nor removed — pinning its warriors to an empty spot. We retire it
	// whenever it has no combat purpose THIS cycle (offense will not claim it) and we
	// are not defending. RETIRE-AND-RETURN: deleting the flag frees the warriors back
	// to the pool; once they rebuild to ATTACK_MIN_WARRIORS with a seen target, the
	// offense rung re-commits a fresh flag (the create cooldown damps threshold thrash).
	//
	// Hoisted ABOVE wheat/economy on purpose: flag teardown is a cheap one-shot and
	// must not be starvable by per-cycle farm upkeep or build/upgrade work.
	//
	// HOLD-ONLY straggler grace: while visible enemy units still loiter inside the
	// flag's stay-range (enemyUnitsNearFlag > 0) we hold position so the army finishes
	// them off; the flag is retired only once the area is genuinely clear.
	ScoredAction CortexPolicy::scoreRetireFlag(const CortexObservation& obs, const DecideFacts& f) const
	{
		const bool offenseWillClaim =
			f.combatPhase && f.warriors >= ATTACK_MIN_WARRIORS && obs.flagTargets[0].valid;
		if (obs.warFlagsActive > 0 && obs.buildingsUnderAttack == 0
		 && !offenseWillClaim && obs.enemyUnitsNearFlag == 0)
			return { SCORE_RETIRE_FLAG, makeClearFlagsAction() };
		return cortexDecline();
	}

	// --- Priority 8: offense (plant the war flag on the nearest known enemy). ---
	// Once we have an army (turtle-then-commit; the warriors have been training to
	// attack level 1 at the barracks during the build-up) and have actually
	// scouted an enemy building (flagTargets[0] is the nearest discovered one).
	// Sticky by design: this is the default "keep attacking" action, and it sits
	// last so every economy / build / upgrade action above preempts it whenever
	// real work exists. The action layer moves the existing flag, not stacks it.
	//
	// Wheat sustainability (checkerboard forbidden paint) is NOT a rung here: it
	// runs every decision cycle in parallel with whatever primary action this ladder
	// picks, so it can never be starved by build/upgrade/offense work (and conversely
	// never steals a cycle from them). See wantWheatProtection() below and
	// AICortex::enqueueWheatForbidden, called each cycle in getOrder().
	ScoredAction CortexPolicy::scoreOffense(const CortexObservation& obs, const DecideFacts& f) const
	{
		// Normal offense: a healthy colony (combatPhase) with a real army and a
		// scouted target — the patient turtle-then-commit.
		const bool normalCommit = f.combatPhase && f.warriors >= ATTACK_MIN_WARRIORS;
		// BLITZ: the colony is past wheat capacity and starving (foodSaturated). Sitting
		// still means starving in place, so spend whatever army we have NOW — a lower
		// bar (BLITZ_MIN_WARRIORS) than the patient commit. Scores ABOVE the economy/
		// tech band (more economy can't be fed anyway) but below the existential rungs.
		const bool blitzCommit = f.foodSaturated && f.warriors >= BLITZ_MIN_WARRIORS;
		if ((normalCommit || blitzCommit) && obs.flagTargets[0].valid)
		{
			const int count = (f.warriors < CORTEX_MAX_FLAG_UNITS) ? f.warriors : CORTEX_MAX_FLAG_UNITS;
			const int score = (blitzCommit && !normalCommit) ? SCORE_OFFENSE_BLITZ : SCORE_OFFENSE;
			return { score, makeWarFlagAction(0, OFFENSE_FLAG_RADIUS, count) };
		}
		return cortexDecline();
	}

	bool CortexPolicy::wantWheatProtection(const CortexObservation& obs) const
	{
		// Reject an observation built against a layout this policy wasn't written
		// for, or one that was never populated — same guard decide() uses.
		if (obs.version != OBSERVATION_VERSION || !obs.valid)
			return false;

		// Same starving gate as decide()'s economy rungs: never wall off wheat
		// while the colony is dying. Computed here independently because this runs
		// in parallel with (not inside) the primary-action ladder.
		const Sint32 starvingPct = (obs.totalUnit > 0)
			? (obs.starvingUnits * 100 / obs.totalUnit) : 0;
		const bool starving = (starvingPct >= STARVE_HALT_PERCENT);

		// Emit only when the reconcile has real work: ADD newly-revealed wheat tiles
		// or DEL tiles where the wheat is gone / out of view. An empty diff means the
		// paint already matches what we want, so there is nothing to order this cycle.
		return !starving
		    && (obs.wheatProtectAddCount > 0 || obs.wheatProtectDelCount > 0);
	}

	bool CortexPolicy::wantWheatBlitzLift(const CortexObservation& obs) const
	{
		// Reject an unpopulated / wrong-layout observation — same guard decide() uses.
		if (obs.version != OBSERVATION_VERSION || !obs.valid)
			return false;

		// The blitz is the foodSaturated regime with a committable army and a scouted
		// target — exactly scoreOffense's blitzCommit branch. Source the famine
		// condition from computeFacts (a private STATIC member, callable from this
		// const method) so foodSaturated stays single-sourced with decide(), rather
		// than recomputing the starving percentage here.
		const DecideFacts f = computeFacts(obs);
		return f.foodSaturated
		    && obs.warriors >= BLITZ_MIN_WARRIORS
		    && obs.flagTargets[0].valid;
	}
}
