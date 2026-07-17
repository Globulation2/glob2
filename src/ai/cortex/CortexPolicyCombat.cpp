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

	/// Warriors available for the NORMAL offense commit under the war-preparation
	/// level match: with the gate on (warPrepLevelMatch), only warriors whose
	/// ATTACK_STRENGTH level >= the highest enemy-warrior level ever OBSERVED
	/// (FOW-gated, latched by AICortex) count toward the commit bar — sending
	/// level-0 warriors into a level-2 army just feeds it kills. The required
	/// level is capped by what our barracks can currently train (finished level
	/// + 1, the engine training rule — unit/UnitDisplacement.cpp:347), so the
	/// gate converts into training/upgrade pressure and can never deadlock the
	/// offense behind a level we cannot reach yet.
	static int matchedWarriors(const CortexObservation& obs)
	{
		int required = (cortexTuning().warPrepLevelMatch != 0)
			? obs.enemyWarriorLevelLatched : 0;
		const int brkLevel  = cortexMaxFinishedLevel(obs, CORTEX_BUILD_ATTACK);
		const int trainable = (brkLevel < 0) ? 0 : brkLevel + 1;
		if (required > trainable)
			required = trainable;
		if (required > CORTEX_UNIT_LEVELS - 1)
			required = CORTEX_UNIT_LEVELS - 1;
		if (required < 0)
			required = 0;
		int count = 0;
		for (int lvl = required; lvl < CORTEX_UNIT_LEVELS; lvl++)
			count += obs.attackStrengthLevel[lvl];
		return count;
	}

	/// Warriors that count toward an offense commit against `rawCount`. For an
	/// AMPHIBIOUS campaign only swim-capable warriors (obs.swimWarriors) can cross the
	/// water to the target, so the count is capped there — sending land-bound warriors
	/// at a water-locked target just strands them on the near shore. A land campaign
	/// (campaignAmphibious == 0) returns rawCount unchanged, so non-water games are
	/// bit-identical to before. Capping (rather than replacing) keeps the commit
	/// monotone in rawCount, so the sustain path stays strictly weaker-to-fail than the
	/// start path (swimWarriors <= warriors and matchedWarriors <= warriors both hold),
	/// preserving the no-retire/re-commit-thrash invariant.
	static int commitWarriors(const CortexObservation& obs, int rawCount)
	{
		if (obs.campaignAmphibious == 0)
			return rawCount;
		return (rawCount < obs.swimWarriors) ? rawCount : obs.swimWarriors;
	}

	/// The offense commit decision, shared by scoreOffense and scoreRetireFlag
	/// (whose "offense will claim the standing flags" test must mirror the commit
	/// exactly or the flags thrash between the two).
	struct OffenseCommit
	{
		bool normal; ///< patient turtle-then-commit fires this cycle.
		bool blitz;  ///< famine desperation fires this cycle.
		int  slot;   ///< flag-target slot to assault; -1 when neither fires.
	};
	static OffenseCommit computeOffenseCommit(const CortexObservation& obs,
	                                          bool combatPhase, bool foodSaturated,
	                                          int warriors, bool sustain)
	{
		OffenseCommit c = { false, false, -1 };
		if (!obs.flagTargets[0].valid)
			return c; // nothing scouted: nothing to commit to.
		if (sustain)
		{
			// SUSTAIN path (scoreRetireFlag): a war is already standing. Gates govern
			// STARTING an offense, never sustaining one — so skip BOTH the attack-range
			// gate and the war-prep level match. slot 0 (the nearest target) with the
			// RAW warrior count is a strictly WEAKER-to-fail condition than the gated
			// start, so whenever the gated START commit fires the ungated SUSTAIN commit
			// fires too: the flag is abandoned only when even the ungated commit would
			// not claim it, and there is no retire/re-commit thrash.
			c.normal = combatPhase && commitWarriors(obs, warriors) >= ATTACK_MIN_WARRIORS;
			c.blitz  = foodSaturated && commitWarriors(obs, warriors) >= BLITZ_MIN_WARRIORS;
			if (c.normal || c.blitz)
				c.slot = 0; // flagTargets[0] is valid (guarded above).
			return c;
		}
		// START path (scoreOffense). ATTACK-RANGE gate: assault the nearest target
		// inside the support envelope (see cortexInRangeTargetSlot). When every target
		// is out of range the gate binds ONLY while a forward base could cure it AND
		// the cure is younger than the grace window (obs.rangeGateWaived == 0) — no
		// legal forward spot/none underway, or a grace-expired one, means attack anyway
		// (pre-v18 behavior) rather than turtling forever while the forward base keeps
		// building.
		int slot = cortexInRangeTargetSlot(obs);
		const bool forwardPossible = obs.forwardInn.valid || obs.forwardHeal.valid
		                          || obs.forwardInnUnderway || obs.forwardHealUnderway;
		if (slot < 0 && (!forwardPossible || obs.rangeGateWaived != 0))
			slot = 0;
		c.normal = combatPhase && slot >= 0
		        && commitWarriors(obs, matchedWarriors(obs)) >= ATTACK_MIN_WARRIORS;
		// BLITZ ignores both gates: the colony is starving in place, so it spends
		// whatever army it has NOW, on the nearest known target — but an amphibious
		// target still needs swimmers to reach it, so the swim cap applies here too.
		c.blitz = foodSaturated && commitWarriors(obs, warriors) >= BLITZ_MIN_WARRIORS;
		if (c.normal || c.blitz)
			c.slot = (slot >= 0) ? slot : 0;
		return c;
	}

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
		// Gate on economyEstablished, NOT combatPhase: combatPhase == economyEstablished &&
		// !starving, so the old gate DISABLED the recall during a famine — exactly when the
		// foodSaturated blitz throws the army forward and the base most needs defending. A
		// mature colony defends regardless of food. (Pre-economy emergencies still route
		// through scorePanicDefense.)
		if (f.economyEstablished && obs.buildingsUnderAttack > 0
		 && f.warriors >= DEFENSE_MIN_WARRIORS && obs.defenseTargets[0].valid)
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
			// A base assault is "serious" — earns the recall even mid-offense-hold —
			// when it caves multiple buildings AT ONCE *or* is butchering our units en
			// masse. The units trigger is the one the Muka collapse needed: the harasser
			// picks off our standing units (unitsUnderAttack in the teens) long before it
			// flattens a second building, so a buildings-only gate let the colony bleed
			// out before the army ever came home.
			const bool seriousThreat =
				(obs.buildingsUnderAttack >= CORTEX_DEFENSE_SERIOUS_BUILDINGS)
			 || (obs.unitsUnderAttack    >= CORTEX_DEFENSE_SERIOUS_UNITS);
			if (obs.flagPosture == CORTEX_POSTURE_OFFENSE
			 && obs.tick < obs.offenseHoldUntil
			 && !seriousThreat)
				return { SCORE_DEFENSE, makeNoOpAction() }; // hold the offense; ignore the minor-harassment recall.

			// minLevel 0: a base assault recalls every warrior regardless of level.
			// A serious assault must beat the famine-blitz (SCORE_OFFENSE_BLITZ); minor
			// harassment keeps the lower SCORE_DEFENSE urgency.
			const int score = seriousThreat ? SCORE_DEFENSE_SERIOUS : SCORE_DEFENSE;
			return { score, makeDefenseFlagAction(DEFENSE_FLAG_RADIUS, f.warriors, 0) };
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
		// "Offense will claim the standing flags" — this reuses computeOffenseCommit
		// (compute the same commit, read its firing bits) so the retire and re-commit
		// decisions cannot disagree by hand-inlining divergent conditions. It passes
		// sustain=true: gates govern STARTING an offense (scoreOffense, sustain=false),
		// never SUSTAINING one, so the sustain commit skips the attack-range gate and
		// the war-prep level match and tests only the raw army against a target. That is
		// strictly WEAKER-to-fail than the gated start, so whenever the gated START
		// commit fires the SUSTAIN commit fires too — no retire/re-commit thrash — and
		// an already-standing war is abandoned only when even the ungated commit would
		// not claim it.
		const OffenseCommit commit =
			computeOffenseCommit(obs, f.combatPhase, f.foodSaturated, f.warriors, true);
		const bool offenseWillClaim = (commit.normal || commit.blitz);
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
		// The commit decision lives in computeOffenseCommit (shared with scoreRetireFlag
		// so the START and SUSTAIN decisions cannot disagree). This is the START path
		// (sustain=false): it applies the ATTACK-RANGE gate (with the grace-timeout
		// waiver) and the war-prep level match (matchedWarriors) to the NORMAL turtle-
		// then-commit path, waives both for the famine BLITZ, and hands back the target
		// slot to assault. scoreRetireFlag calls the SAME function with sustain=true —
		// a strictly WEAKER-to-fail condition — so the retire test never fires while
		// this start commit does. commit.slot < 0 means neither the normal nor the blitz
		// commit fires this cycle. The action layer turns a firing commit into the WAVE
		// PIPELINE — mustering and marching successive cohesive waves — so this scorer
		// just expresses "we want to be attacking (this target)"; it need not (and must
		// not) micro-manage individual flags.
		const OffenseCommit commit = computeOffenseCommit(obs, f.combatPhase, f.foodSaturated, f.warriors, false);
		if (commit.slot < 0)
			return cortexDecline();
		const int count = (f.warriors < CORTEX_MAX_FLAG_UNITS) ? f.warriors : CORTEX_MAX_FLAG_UNITS;
		// minLevel 0 (every warrior) for BOTH commit kinds: muster-then-march (the
		// action layer) gathers and marches the WHOLE wave as one mass, so the old
		// veteran filter — which kept low-level warriors home and marched only the
		// trained cohort — is gone (it would shrink the wave and, worse, peel low-
		// level warriors off the flag mid-march, breaking cohesion). The action layer
		// owns the flag's level filter now and pins it to 0.
		const bool blitzOnly = commit.blitz && !commit.normal;
		const int score = blitzOnly ? SCORE_OFFENSE_BLITZ : SCORE_OFFENSE;
		return { score, makeWarFlagAction(commit.slot, OFFENSE_FLAG_RADIUS, count, 0) };
	}

	// --- Priority 4.5: forward base (extend the attack-range support envelope). ---
	// This fires in exactly ONE state: we WANT to attack but can't reach — a
	// war-ready colony (combatPhase) with a scouted target, yet EVERY known target
	// sits outside the attack range (cortexInRangeTargetSlot < 0). The cure is to
	// push our food/heal support toward the front so the target falls inside the
	// envelope. The INN leads: food is the binding support (an army with no forward
	// inn starves on the march; a hospital only speeds recovery), so we plant the
	// forward inn first and the forward hospital only once the inn is placed.
	//
	// NOT gated on matchedWarriors: the war-preparation level match delays STARTING
	// the offense (scoreOffense), but it must never delay the envelope cure. Building
	// an inn is workers' business — a colony that will want to attack once its
	// warriors mature should already be projecting its support forward, not waiting
	// for the army to level up before it lays the foundation. The observation surfaces
	// forwardInn/forwardHeal ONLY in this out-of-range state (valid==0 otherwise), so
	// no separate "should we?" guard is needed beyond the gate here; the *Underway
	// flags (AICortex's position-tracked latch) stop us from ordering a second forward
	// site while one is building. Re-issue pacing (the per-type build cooldown) lives
	// in the action layer, so this may fire every cycle until a site actually appears.
	//
	// Gated GATE_BOOTSTRAP | GATE_LABOR in decide() (a build crew off idle hands,
	// behind the first inn) and canExpand-checked here, exactly like the other builds.
	ScoredAction CortexPolicy::scoreForwardBase(const CortexObservation& obs, const DecideFacts& f) const
	{
		if (!f.combatPhase || !f.canExpand || !obs.flagTargets[0].valid
		 || cortexInRangeTargetSlot(obs) >= 0)
			return cortexDecline();
		if (obs.forwardInn.valid && !obs.forwardInnUnderway)
			return { SCORE_FORWARD_BASE, makeBuildForwardAction(CORTEX_BUILD_FOOD) };
		if (obs.forwardHeal.valid && !obs.forwardHealUnderway)
			return { SCORE_FORWARD_BASE, makeBuildForwardAction(CORTEX_BUILD_HEAL) };
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
