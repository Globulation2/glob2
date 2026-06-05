// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexPolicy.h"

namespace Cortex
{
	// --- economy tuning ---------------------------------------------------
	// Hand-picked thresholds for the v0 rules. These are AI design choices for
	// a brand-new AI (not ported engine mechanics), so they are tunable; later
	// phases / an ML policy replace this whole function. The colony ALWAYS keeps
	// producing and ALWAYS keeps expanding in some form: the swarm's ratio is
	// never zeroed, and idle labour is continuously turned into capacity (more
	// inns to stay ahead of population) and tech (school → racetrack → hospital)
	// rather than parked at an artificial population ceiling. The only real size
	// governor is physical: a swarm stalls when its CORN buffer runs below 5
	// (engine), and feeding is kept ahead of the population by inn-led growth.

	/// Reactive thresholds that suppress *expansion spending* (never swarm
	/// production) while the colony is in food trouble: percent of units actively
	/// starving (losing HP) / merely hungry (not yet losing HP) above which we
	/// stop starting new tech/expansion builds until feeding recovers.
	static const int STARVE_HALT_PERCENT = 6;
	static const int HUNGRY_HALT_PERCENT = 20;

	// --- Phase-3 combat tuning --------------------------------------------
	// All AI design choices, tunable against the benchmark.

	/// Economy preconditions to enter the combat phase: a self-feeding colony
	/// with real production capacity that is not currently starving. Below this
	/// the AI behaves exactly like the Phase-1 economy. Kept low so the warrior
	/// ramp starts early — Nicowar rushes a trained army by ~9-10k ticks, so a
	/// late army arrives to a dead colony.
	static const int COMBAT_ECON_MIN_UNITS = 10;
	static const int COMBAT_ECON_MIN_INNS = 1;
	static const int COMBAT_ECON_MIN_SWARMS = 1;
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
	/// Standing warriors required to bother recalling for defense — defend with
	/// whatever we have the moment the base is touched.
	static const int DEFENSE_MIN_WARRIORS = 1;
	/// War-flag attraction radii (the flag's unitStayRange). Offense reaches a
	/// little wider to engage units around the target building; defense is tight
	/// around the threatened building. Clamped to CORTEX_MAX_FLAG_RADIUS by the
	/// action layer.
	static const int OFFENSE_FLAG_RADIUS = 8;
	static const int DEFENSE_FLAG_RADIUS = 5;

	// --- Phase-2 upgrade tuning -------------------------------------------
	// The unit-strength lever: a finished level-L building trains units to ability
	// level L+1, so a level-0 barracks only ever makes attack-level-1 warriors. To
	// field level-2 warriors (parity with Nicowar's trained army) we must upgrade
	// the barracks to level 1 — which the engine gates on team maxBuildLevel > the
	// barracks level, and maxBuildLevel only rises once a SCHOOL has trained our
	// workers' BUILD skill. So the chain is: build a school -> workers train BUILD
	// (maxBuildLevel -> 1) -> upgrade the barracks -> warriors train to attack
	// level 2.
	//
	// TEMPO NOTE (historical, measured): an earlier design parked the colony at a
	// population ceiling and only spent on tech once "surplus", because eagerly
	// teching while the economy was frozen at a tiny size cost more tempo than it
	// returned. The economy is no longer frozen — it always produces and always
	// expands — so the school/racetrack/barracks-upgrade builds are now gated on
	// `canExpand` (spare idle workers + not in food trouble) instead: the build crew
	// still comes off idle hands, never off hauling or army production, but tech is
	// no longer withheld behind an artificial ceiling that the colony never reached.

	/// First valid candidate slot for `type` (the placement helper already ranks
	/// them best-first), or -1 if the observation surfaced no legal location.
	static int firstValidCandidate(const CortexObservation& obs, int type)
	{
		for (int slot = 0; slot < CORTEX_BUILD_CANDIDATES; slot++)
			if (obs.buildCandidates[type][slot].valid)
				return slot;
		return -1;
	}

	// --- Worker-hauling capacity helpers ------------------------------------
	// These are pure functions of the observation used in two places: the
	// worker-tuning loop (Priority 1.5) and the swarm-expansion gate
	// (Priority 5). Centralising them avoids the two sites drifting apart.

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

	CortexPolicy::CortexPolicy()
	{
	}

	CortexAction CortexPolicy::decide(const CortexObservation& obs)
	{
		// Reject an observation built against a layout this policy wasn't
		// written for, or one that was never populated. Either way: do nothing.
		if (obs.version != OBSERVATION_VERSION || !obs.valid)
			return makeNoOpAction();

		const Sint32 inns          = cortexFinishedBuildings(obs, CORTEX_BUILD_FOOD);
		const Sint32 innSites      = cortexBuildingSites(obs, CORTEX_BUILD_FOOD);
		const Sint32 swarms        = cortexFinishedBuildings(obs, CORTEX_BUILD_SWARM);
		const Sint32 swarmSites    = cortexBuildingSites(obs, CORTEX_BUILD_SWARM);
		const Sint32 barracks      = cortexFinishedBuildings(obs, CORTEX_BUILD_ATTACK);
		const Sint32 barracksSites = cortexBuildingSites(obs, CORTEX_BUILD_ATTACK);
		const Sint32 school        = cortexFinishedBuildings(obs, CORTEX_BUILD_SCIENCE);
		const Sint32 schoolSites   = cortexBuildingSites(obs, CORTEX_BUILD_SCIENCE);
		const Sint32 heal          = cortexFinishedBuildings(obs, CORTEX_BUILD_HEAL);
		const Sint32 healSites     = cortexBuildingSites(obs, CORTEX_BUILD_HEAL);
		const Sint32 warriors      = obs.warriors;

		const Sint32 starvingPct   = (obs.totalUnit > 0)
			? (obs.starvingUnits * 100 / obs.totalUnit) : 0;
		const Sint32 hungryPct     = (obs.totalUnit > 0)
			? (obs.needFood * 100 / obs.totalUnit) : 0;
		const bool starving        = (starvingPct >= STARVE_HALT_PERCENT);
		const bool hungry          = (hungryPct >= HUNGRY_HALT_PERCENT);

		// "Established economy" gate, and also the army-pivot trigger: a self-feeding
		// colony with a swarm + an inn and a real population, not starving. Below it
		// Cortex is still bootstrapping (workers only); at or above it the colony both
		// techs up AND folds warriors into the production mix. There is NO population
		// ceiling and NO production halt — feeding is kept ahead of population by
		// inn-led expansion (Priority 2), and the engine's CORN-buffer stall is the
		// real supply governor.
		const bool combatPhase     = (inns   >= COMBAT_ECON_MIN_INNS
		                           && swarms >= COMBAT_ECON_MIN_SWARMS
		                           && obs.totalUnit >= COMBAT_ECON_MIN_UNITS
		                           && !starving);

		// Spare labour: idle workers exist, so a tech/expansion build can be started
		// without stealing the haulers that keep the swarm + inn CORN buffers full.
		// The economy expands whenever this holds — there is never an idle
		// "surplus, do nothing" state. Feeding (the inn, Priority 2) is exempt: it is
		// built on the capacity trigger regardless of spare labour, because feeding
		// is existential and a starving colony has no spare labour yet.
		const bool canExpand       = (obs.freeWorkers > 0 && !starving && !hungry);

		// Production mix {WORKER, EXPLORER, WARRIOR} — a HARD rule: NEVER {0,0,0}.
		// Bootstrap is worker-biased with one early explorer (reveal our wheat /
		// scout). Once established, stay worker-DOMINANT (2:1 over warriors) so the
		// worker pool keeps growing to staff new buildings and haul corn, while a
		// warrior slice builds the army and one explorer stays out to scout the
		// enemy base (so flagTargets can populate for offense).
		const bool wantEarlyExplorer = (!combatPhase && swarms >= 1 && obs.explorers == 0);
		const bool wantScout         = (obs.explorers == 0); // keep ≥1 explorer out.
		const int growWorker   = combatPhase ? 2 : 1;
		const int growExplorer = (wantEarlyExplorer || (combatPhase && wantScout)) ? 1 : 0;
		const int growWarrior  = combatPhase ? 1 : 0;

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
		const bool underAttack = (obs.buildingsUnderAttack > 0 || obs.unitsUnderAttack > 0);
		const bool panic       = (!combatPhase && underAttack);
		if (panic)
		{
			// (1) 100% warriors — fire until every swarm is warrior-only.
			if (swarms > 0 && obs.swarmsProducingWarrior < swarms)
				return makeSetProductionAction(0, 0, 1);
			// (2) Swarms to HIGH priority.
			if (anySwarmPriorityNot(obs, CORTEX_PRIORITY_HIGH))
				return makeSetPriorityAction(CORTEX_PRIORITY_HIGH);
			// (3) Panic-build one hospital if none is up or already building.
			if (heal == 0 && healSites == 0)
			{
				const int slot = firstValidCandidate(obs, CORTEX_BUILD_HEAL);
				if (slot >= 0)
					return makeBuildAction(CORTEX_BUILD_HEAL, slot);
			}
			// Panic setup complete — fall through to the normal economy, which keeps
			// the swarms fed while they produce the defending army.
		}
		else if (anySwarmPriorityNot(obs, CORTEX_PRIORITY_NORMAL))
		{
			// Not under attack, but a prior panic left swarms at HIGH priority —
			// restore NORMAL so they stop over-pulling workers during ordinary play.
			return makeSetPriorityAction(CORTEX_PRIORITY_NORMAL);
		}

		// --- Priority 1: production control. HARD RULE: the swarm always produces;
		// the ratio is NEVER {0,0,0}. We only (re)issue a ratio when the swarm's
		// current output does not match the desired mix — detected from the bounded
		// count signals (the pure policy cannot read raw ratios) — so once the mix is
		// applied this whole block falls through and stops preempting the build
		// priorities below. Suppressed while panicking (the panic block owns the
		// ratio then). The action layer dedups per-swarm, so a stray re-issue of an
		// already-applied ratio emits no order.
		if (!panic)
		{
			// (a) (Re)start any swarm producing nothing — freshly built, or a halted
			//     {0,0,0} ratio loaded from an old save.
			if (obs.swarmsProducing < swarms)
				return makeSetProductionAction(growWorker, growExplorer, growWarrior);
			// (b) Establish the warrior mix once the economy is established but no
			//     warriors are being made yet. Self-terminating: stops firing as soon
			//     as the first warrior appears (re-fires if the army is wiped to 0).
			if (combatPhase && swarms > 0 && warriors == 0)
				return makeSetProductionAction(growWorker, growExplorer, growWarrior);
			// (c) Fold an explorer into the mix when we want one out but none is being
			//     produced and we have none yet (reveals our wheat / the enemy base).
			if (growExplorer > 0 && swarms > 0
			 && obs.swarmsProducingExplorer == 0 && obs.explorers == 0)
				return makeSetProductionAction(growWorker, growExplorer, growWarrior);
			// (d) Drop the explorer slice back out once an explorer exists, so we do
			//     not keep over-producing them. Stops once no swarm carries it.
			if (growExplorer == 0 && swarms > 0 && obs.swarmsProducingExplorer > 0)
				return makeSetProductionAction(growWorker, growExplorer, growWarrior);
		}

		// --- Priority 1.5: worker-hauling tuning (closed-loop wheat-economy). ---
		// Each cycle we nudge each swarm's and inn's maxUnitWorking by AT MOST +/-1
		// based on its corn-buffer level, then return the tune action if anything
		// actually changed. This self-damps naturally: when a building's corn level
		// sits in the deadband (ADD_LO <= corn < REM_HI) no adjustment fires; only
		// when it crosses a threshold does the count move, and the +/-1 step rate
		// prevents the chunky 5-CORN-per-unit production schedule from driving
		// oscillation (a single step per cycle is slower than the buffer responds,
		// so it converges rather than hunting).
		//
		// Rationale for sitting here — ABOVE the build priorities but BELOW the
		// halt/resume logic:
		//   • Keeping existing buildings fed always outranks starting new ones.
		//     If a swarm is draining because it has too few haulers, the right
		//     first response is to add a hauler, not to immediately build another
		//     swarm.
		//   • However, we fire only when a building actually crosses a threshold,
		//     so in steady state (buffers in the deadband) this block falls through
		//     completely, leaving every cycle available for the build priorities.
		//   • The action layer deduplicates tune targets: if the desired value
		//     equals the current value it emits no order, so re-deciding the same
		//     intent is free.
		//
		// Note: makeTuneWorkersAction() returns an action with all
		// swarmWorkers[]/innWorkers[] preset to -1 (leave unchanged); we only
		// overwrite entries for buildings that actually need adjustment.
		{
			CortexAction tune = makeTuneWorkersAction();
			bool anyChange = false;
			const int sCap = swarmWorkerCap(obs);
			for (int i = 0; i < obs.swarmCount; i++) {
				const TrackedBuilding& t = obs.trackedSwarms[i];
				if (!t.valid) continue;
				int desired = t.maxUnitWorking;
				// Buffer draining: bring one more hauler in before the swarm stalls.
				// CORTEX_SWARM_CORN_ADD_LO is the stall threshold (swarm stops
				// producing at < 5 corn); catching it early buys a cycle of slack.
				if (t.corn < CORTEX_SWARM_CORN_ADD_LO && t.maxUnitWorking < sCap)
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
				int desired = t.maxUnitWorking;
				// Inns feed ~5× faster than swarms consume, so they exhaust their
				// buffer quicker; the same add/remove logic applies with the inn-
				// specific thresholds and ceiling.
				if (t.corn < CORTEX_INN_CORN_ADD_LO && t.maxUnitWorking < CORTEX_INN_WORKER_CAP)
					desired = t.maxUnitWorking + 1;
				else if (t.corn >= CORTEX_INN_CORN_REM_HI && t.maxUnitWorking > CORTEX_INN_WORKER_MIN)
					desired = t.maxUnitWorking - 1;
				if (desired != t.maxUnitWorking) { tune.innWorkers[i] = desired; anyChange = true; }
			}
			if (anyChange) return tune;
		}

		// --- Priority 2: feed capacity (inns). FEED-LED, not wheat-led: build an inn
		// whenever the inns' feeding capacity has fallen behind the current
		// population (or there is no inn yet). An existing inn short of WHEAT SUPPLY
		// is the worker-tuning loop's problem (add haulers), NEVER a reason to place
		// another inn here. One site at a time; placement already rejects sites too
		// far from wheat. Ungated by spare labour — feeding is existential, and this
		// is what keeps the swarm from ever needing to halt for overpopulation.
		if (innSites == 0)
		{
			const bool noInnYet      = (inns == 0 && obs.totalUnit > 0);
			const bool capacityShort = (obs.feedCapacity < obs.totalUnit);
			if (noInnYet || capacityShort)
			{
				const int slot = firstValidCandidate(obs, CORTEX_BUILD_FOOD);
				if (slot >= 0)
					return makeBuildAction(CORTEX_BUILD_FOOD, slot);
			}
		}

		// --- Priority 2.5: swarm RECOVERY only. We deliberately do NOT build a
		// second swarm for now (may revisit) — a team starts with one swarm, so this
		// fires only if that swarm was destroyed, restoring the ability to produce.
		if (swarms == 0 && swarmSites == 0 && inns > 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SWARM);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_SWARM, slot);
		}

		// --- Priority 3: school (SCIENCE) — the first tech building. Trains workers'
		// HARVEST (more CORN carried per haul → fuller swarm/inn buffers, easing the
		// very supply pressure the economy lives on) and BUILD (faster construction +
		// raises team maxBuildLevel, the engine gate that unlocks every building
		// upgrade). Built once the economy is established and spare labour exists, so
		// the build crew comes off idle hands rather than off hauling.
		if (combatPhase && canExpand && school == 0 && schoolSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SCIENCE);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_SCIENCE, slot);
		}

		// --- Priority 4: racetrack (WALKSPEED) — second tech building. Trains WALK,
		// speeding every unit: shorter hauling round-trips (more economy throughput)
		// and faster army repositioning. After the school so HARVEST/BUILD land first.
		if (combatPhase && canExpand && school > 0
		 && cortexFinishedBuildings(obs, CORTEX_BUILD_WALKSPEED) == 0
		 && cortexBuildingSites(obs, CORTEX_BUILD_WALKSPEED) == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_WALKSPEED);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_WALKSPEED, slot);
		}

		// --- Priority 5: hospital (HEAL) — survivability for the standing army.
		// The panic path also builds one reactively under attack; this is the
		// planned, non-emergency one once the economy can spare the labour.
		if (combatPhase && canExpand && heal == 0 && healSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_HEAL);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_HEAL, slot);
		}

		// --- Priority 6: barracks (ATTACK) — the army pivot. Lets produced warriors
		// train to attack level 1 and get healed between fights. One is enough.
		if (combatPhase && barracks == 0 && barracksSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_ATTACK);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_ATTACK, slot);
		}

		// --- Priority 6.5: upgrade the barracks (the unit-strength lever). ---
		// obs.upgradableCount already encodes the FULL engine Upgradable predicate
		// (finished, full HP, not already upgrading, has a higher level, the larger
		// footprint fits, and crucially maxBuildLevel > level) — so a nonzero count
		// means "a school has lifted our BUILD skill and a barracks is ready to go to
		// the next level." One at a time (cortexBuildingsUpgrading guards against
		// stacking a second upgrade); the action layer picks the bottleneck instance.
		// Gated on spare labour so the teardown-to-upgrade comes off idle hands.
		if (combatPhase && canExpand
		 && obs.upgradableCount[CORTEX_BUILD_ATTACK] > 0
		 && cortexBuildingsUpgrading(obs, CORTEX_BUILD_ATTACK) == 0)
			return makeUpgradeAction(CORTEX_BUILD_ATTACK);

		// --- Priority 7: defense (recall the army to a threatened building). ---
		// War flags are standing buildings: once placed they keep summoning
		// warriors without being re-issued, so this need not fire every cycle —
		// it just (re)positions the single flag onto the current threat. Defense
		// outranks offense: when our base is under attack the lone flag comes home.
		if (combatPhase && obs.buildingsUnderAttack > 0
		 && warriors >= DEFENSE_MIN_WARRIORS && obs.defenseTarget.valid)
			return makeDefenseFlagAction(DEFENSE_FLAG_RADIUS, warriors);

		// --- Priority 7.5: wheat sustainability (checkerboard forbidden paint). ---
		// Ungated by combat phase — this is early-economy field maintenance that
		// runs from the first inn. Placed below defense (reacting to an active
		// attack outranks farm upkeep) but above the sticky offense default (upkeep
		// outranks "keep attacking"). Gated on !starving so we never wall off wheat
		// while the colony is dying, and emits only when the reconcile has real work
		// (ADD newly-revealed tiles or DEL ones where the wheat is gone).
		if (!starving
		 && (obs.wheatProtectAddCount > 0 || obs.wheatProtectDelCount > 0))
			return makeProtectWheatAction(obs.wheatOpenMargin);

		// --- Priority 8: offense (plant the war flag on the nearest known enemy). ---
		// Once we have an army (turtle-then-commit; the warriors have been training to
		// attack level 1 at the barracks during the build-up) and have actually
		// scouted an enemy building (flagTargets[0] is the nearest discovered one).
		// Sticky by design: this is the default "keep attacking" action, and it sits
		// last so every economy / build / upgrade action above preempts it whenever
		// real work exists. The action layer moves the existing flag, not stacks it.
		if (combatPhase && warriors >= ATTACK_MIN_WARRIORS && obs.flagTargets[0].valid)
		{
			const int count = (warriors < CORTEX_MAX_FLAG_UNITS) ? warriors : CORTEX_MAX_FLAG_UNITS;
			return makeWarFlagAction(0, OFFENSE_FLAG_RADIUS, count);
		}

		// --- Priority 9: clear a stranded flag. ---
		// We have a flag up but nothing to point it at (target lost, not under
		// attack): remove it so warriors are not pinned to an empty spot.
		if (obs.warFlagsActive > 0 && obs.buildingsUnderAttack == 0
		 && !obs.flagTargets[0].valid)
			return makeClearFlagsAction();

		return makeNoOpAction();
	}
}
