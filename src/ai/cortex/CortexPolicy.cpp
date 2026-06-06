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

	/// Percent of current inn feeding capacity the population must reach to trigger
	/// the next inn (Priority 2). Building at 100% means waiting until feeding is
	/// already exhausted before the next inn even breaks ground, so the colony spends
	/// the whole inn-build window over capacity. Triggering at 80% gives that lead
	/// time back — feeding stays ahead of population growth.
	static const int INN_BUILD_CAPACITY_PERCENT = 80;

	/// --- worker-surplus production throttle (tunable AI design choice) -----
	// "Available workers" is the engine's own player-facing free-worker figure:
	// idle workers minus open job requests (isFree[WORKER] - totalNeeded; see
	// TeamStat.cpp:290, where it is shown as "free" and, when negative, the units
	// are "seeking a job"). When that surplus climbs above WORKER_SURPLUS_HI the
	// colony has more idle hands than it can place, so the swarm stops minting
	// workers and pours into warriors (plus the usual scout explorer) instead. It
	// resumes worker production only once the surplus drains back below zero — job
	// requests once again outnumber idle workers. The gap between the two
	// thresholds (HI on the way in, 0 on the way out) is deliberate hysteresis so
	// the production mix does not flip every cycle while the figure hovers near the
	// line. Combat-phase only: the throttle needs a warrior slice to fall back on,
	// else dropping workers would leave the swarm at the forbidden {0,0,0} halt.
	static const int WORKER_SURPLUS_HI = 3;

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
	/// Above this many standing warriors the pre-combat panic defense is
	/// suppressed: a colony with a real army of its own can absorb a hit through
	/// the normal defense path (war flags, barracks healing) and does not need to
	/// derail its economy into the emergency 100%-warrior / HIGH-priority / panic-
	/// hospital response. Panic is for the defenceless early colony, not one that
	/// already fields a force.
	static const int PANIC_MAX_WARRIORS = 15;
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

	/// --- expand-vs-upgrade tuning (all tunable AI design choices) ---------
	// A finished level-L training building trains its eligible units to ability
	// level L+1 (the most that level can give). Once most of those units are
	// already at L+1 the building's current level has little left to do, so the
	// lever to keep improving units is to UPGRADE it to L+1 (which then trains to
	// L+2). But an upgrade tears the building fully offline for a resource- and
	// footprint-gated rebuild window (~hundreds to ~2000 ticks — NOT a fixed
	// timer; see docs/AI/cortex/upgrade-expand-mechanics.md section 4), training
	// and feeding nobody meanwhile. So we (a) only upgrade once the current level
	// is mostly "done" (UPGRADE_MAXED_PERCENT), and (b) for buildings whose offline
	// window is a capability blackout (barracks: no warrior training/healing; inn:
	// no feeding) we EXPAND first — build a second instance so one keeps working
	// through the upgrade. School/racetrack upgrades are single-instance: their
	// blackout only pauses a tech ramp, it does not strand the army or starve.

	/// Percent of a training building's eligible units that must already be MAXED at
	/// its current level (trained to level+1) before we spend on upgrading it. Below
	/// this the level still has trainees to serve and upgrading would strand them.
	/// The user's spec put this at ~50-70%; 60 is the midpoint, tunable.
	static const int UPGRADE_MAXED_PERCENT = 60;
	/// Minimum FINISHED barracks before an upgrade is allowed. The upgrade blacks a
	/// barracks out for ~hundreds-to-2000 ticks (measured ~1900) during which it
	/// trains and heals no warriors; with only one barracks that is a total army
	/// blackout. Requiring two means an upgrade always leaves one training, and the
	/// laggard-first findUpgradeTarget (AICortex.cpp) lifts them one at a time.
	static const int BARRACKS_MIN_BEFORE_UPGRADE = 2;
	/// Minimum FINISHED inns before an upgrade is allowed, so feeding never hits
	/// zero during the blackout. Paired with the post-upgrade feed-slack check below.
	static const int INN_MIN_BEFORE_UPGRADE = 2;
	/// Hospital (HEAL) expansion. We scale hospital COUNT with ARMY SIZE rather than
	/// the instantaneous needHeal: a hurt warrior out on the offense flag is fighting
	/// or dying, not queued at a hospital, so needHeal badly understates true demand.
	/// One hospital per HOSPITAL_WARRIORS_PER standing warriors keeps heal throughput
	/// (a hospital heals only 2/5/7 units at once at L0/1/2, slowly) ahead of a
	/// growing army — up to HOSPITAL_MAX instances (footprint/labour bound). Both are
	/// hand-picked AI design choices, tunable against the benchmark.
	static const int HOSPITAL_MAX          = 3;
	static const int HOSPITAL_WARRIORS_PER = 8;

	/// Percent of `total` eligible units already trained to >= `servedLevel` on an
	/// ability, from a per-level histogram slice `dist` (an upgradeState row). A unit
	/// at >= servedLevel cannot be improved further by a building of the level that
	/// produces `servedLevel` (== buildingLevel+1), so this is the "% already maxed at
	/// the current building level" the expand-vs-upgrade gate keys on. Returns 0 for
	/// an empty pool so "no units" never reads as "all maxed".
	static int unitsServedPct(const Sint32 dist[CORTEX_UNIT_LEVELS], Sint32 total, int servedLevel)
	{
		if (total <= 0)
			return 0;
		if (servedLevel < 0)
			servedLevel = 0;
		Sint32 served = 0;
		for (int lvl = servedLevel; lvl < CORTEX_UNIT_LEVELS; lvl++)
			served += dist[lvl];
		if (served > total) // guard a transient count race (dist slightly ahead of total).
			served = total;
		return static_cast<int>(served * 100 / total);
	}

	/// Smallest feeding capacity (type->maxUnitInside) among our finished inns, or a
	/// large sentinel when none are tracked. This is the capacity an inn UPGRADE takes
	/// offline (findUpgradeTarget lifts the lowest-level == smallest inn first), so the
	/// inn-upgrade gate checks feedCapacity-minus-this against population first, to
	/// guarantee the blackout never starves the colony.
	static int smallestFinishedInnCapacity(const CortexObservation& obs)
	{
		int smallest = -1;
		for (int i = 0; i < obs.innCount; i++)
		{
			const TrackedBuilding& t = obs.trackedInns[i];
			if (!t.valid)
				continue;
			if (smallest < 0 || t.maxUnitInside < smallest)
				smallest = t.maxUnitInside;
		}
		return (smallest < 0) ? 9999 : smallest; // none tracked → force the spare-inn path.
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
		const Sint32 race          = cortexFinishedBuildings(obs, CORTEX_BUILD_WALKSPEED);
		const Sint32 raceSites     = cortexBuildingSites(obs, CORTEX_BUILD_WALKSPEED);
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
		//
		// The inn requirement is "feeding is established", not "the first inn has
		// finished": when free workers exist we do NOT make them sit idle waiting for
		// the inn to top out — if an inn is at least UNDERWAY (built or building) spare
		// labour starts the rest of the build-out (and the army ramp) now, in parallel
		// with the inn finishing. Without free workers we hold for the finished inn as
		// before, so nothing is pulled off hauling while the colony is labour-tight.
		const bool innEstablished  = (inns >= COMBAT_ECON_MIN_INNS)
		                          || (obs.freeWorkers > 0
		                           && (inns + innSites) >= COMBAT_ECON_MIN_INNS);
		const bool combatPhase     = (innEstablished
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
		const int growExplorer = (wantEarlyExplorer || (combatPhase && wantScout)) ? 1 : 0;
		const int growWarrior  = combatPhase ? 1 : 0;

		// Worker-surplus throttle (see WORKER_SURPLUS_HI): stop minting workers while
		// idle labour piles up and resume once it is spent. "Available workers" is the
		// engine's own free-worker figure — idle workers minus open job requests
		// (TeamStat.cpp:290). Hysteresis: suppress above the high watermark, resume
		// once it goes negative, and HOLD the current mode in the band between so the
		// mix does not oscillate. The held mode is read back from swarmsProducingWorker
		// (no raw-ratio access). Combat-phase only — outside it growWarrior is 0, so
		// dropping workers would halt the swarm at the forbidden {0,0,0}.
		const int availableWorkers = obs.freeWorkers - obs.totalNeeded;
		const bool suppressingNow  =
		    (swarms > 0 && obs.swarmsProducing > 0 && obs.swarmsProducingWorker == 0);
		bool suppressWorkers;
		if (availableWorkers > WORKER_SURPLUS_HI)
			suppressWorkers = true;            // idle hands to spare -> make warriors
		else if (availableWorkers < 0)
			suppressWorkers = false;           // labour scarce again -> make workers
		else
			suppressWorkers = suppressingNow;  // hold within the hysteresis band
		const int growWorker = (combatPhase && suppressWorkers) ? 0
		                     : (combatPhase ? 2 : 1);

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
		// Suppressed once we field a real army (warriors > PANIC_MAX_WARRIORS): such a
		// colony defends through the normal path and need not derail its economy.
		const bool panic       = (!combatPhase && underAttack
		                          && warriors <= PANIC_MAX_WARRIORS);
		if (panic)
		{
			// (1) 100% warriors — fire until every swarm is warrior-only.
			if (swarms > 0 && obs.swarmsProducingWarrior < swarms)
				return makeSetProductionAction(0, 0, 1);
			// (2) Swarms to HIGH priority — EVERY swarm, not just the primary, so the
			//     whole warrior pump wins worker contention while the base is hit.
			if (anySwarmPriorityNot(obs, CORTEX_PRIORITY_HIGH))
				return makeSetPriorityAction(CORTEX_PRIORITY_HIGH, CORTEX_PRIORITY_HIGH);
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
		else if (swarmPrioritiesNeedSplit(obs, CORTEX_PRIORITY_HIGH, CORTEX_PRIORITY_NORMAL))
		{
			// Steady-state priority split: keep the FIRST/primary swarm at HIGH so it
			// always wins worker/hauler contention and pumps the early worker economy,
			// while any later (second) swarm sits at NORMAL. This also restores the
			// primary swarm to HIGH after a panic ends (the panic raised every swarm).
			return makeSetPriorityAction(CORTEX_PRIORITY_HIGH, CORTEX_PRIORITY_NORMAL);
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
			if (!combatPhase && swarms > 0 && obs.swarmsProducingWarrior > 0)
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
			// (e) Apply / revert the worker-surplus throttle: re-issue the mix
			//     whenever the swarm's worker output disagrees with the desired
			//     growWorker (workers on <-> off). The symmetric peer of (c)/(d) for
			//     the explorer slice; only ever meaningful in the combat phase, where
			//     growWorker can be 0. Self-terminating and action-layer dedup'd, so a
			//     no-op re-issue emits no order.
			if (combatPhase && swarms > 0
			 && (growWorker > 0) != (obs.swarmsProducingWorker > 0))
				return makeSetProductionAction(growWorker, growExplorer, growWarrior);
		}

		// Worker-hauling tuning (swarms + inns + construction sites) no longer lives
		// in this priority ladder — it runs EVERY decision cycle in PARALLEL with
		// whatever single action this ladder returns, so keeping existing buildings
		// fed never preempts nor waits behind a build/upgrade decision (and vice
		// versa). See CortexPolicy::tuneWorkers(), enqueued alongside decide() by the
		// engine binding (AICortex::getOrder) the same way wheat-forbidden upkeep is.

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
		if (innSites == 0)
		{
			const bool noInnYet      = (inns == 0 && obs.totalUnit > 0);
			const bool capacityShort =
				(obs.totalUnit * 100 >= obs.feedCapacity * INN_BUILD_CAPACITY_PERCENT);
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
		//
		// ALGA gate: a school costs ALGA to build at EVERY level (2/12/10), and algae is
		// harvestable only off water. If no algae is reachable from shore (a landlocked
		// map, or algae walled off by resources/deep water with no ground path) a school
		// site can never be supplied and would stall forever — so we hold the school
		// until a harvestable algae tile is found. This must NOT block the rest of the
		// build-out: the racetrack (Priority 4) is decoupled from the school below so it
		// proceeds anyway, and the engine's maxBuildLevel gate naturally holds upgrades
		// (which need a school) until one exists. Once algae is discovered the school
		// builds on the next cycle.
		if (combatPhase && canExpand && obs.algaeReachable
		 && school == 0 && schoolSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SCIENCE);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_SCIENCE, slot);
		}

		// --- Priority 4: racetrack (WALKSPEED) — second tech building. Trains WALK,
		// speeding every unit: shorter hauling round-trips (more economy throughput)
		// and faster army repositioning. Normally held until the school is finished so
		// HARVEST/BUILD land first — BUT the school needs ALGA, so on a map with no
		// reachable algae no school will ever come; we must not let that permanently
		// block the racetrack and everything behind it. Decouple ONLY when the school is
		// genuinely unbuildable (no reachable algae): when algae IS reachable the school
		// is coming, so keep waiting for it (Priority 3 also fires first by ordering, so
		// the racetrack never races ahead of an in-progress school on a normal map).
		if (combatPhase && canExpand && (school > 0 || !obs.algaeReachable)
		 && race == 0 && raceSites == 0)
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

		// --- Priority 5.5: swimming pool (SWIMSPEED) — train SWIM so our workers and
		// warriors can cross water. Built when an explorer has revealed reachable ALGA
		// (a food resource that grows only on water, so harvestable only by swimmers)
		// OR when allowing swim MATERIALLY expands the colony's reachable area — a
		// water-separated wheat patch / stretch of land, or the only-or-much-shorter
		// route to a water-locked enemy. The reach test compares the bounded land-only
		// vs land+water flood-fill counts surfaced by CortexWater (swimWaterReach is
		// always >= swimLandReach; we want a pool when the swim count is more than
		// NUMER/DENOM larger). This mirrors the INTENT of AICastor's computeNeedSwim
		// (the swim-helps predicate). One pool suffices — SWIM is a team-wide trained
		// ability, not per-building capacity. Gated on the established economy + spare
		// labour like the other tech builds, so the build crew comes off idle hands.
		const Sint32 pool      = cortexFinishedBuildings(obs, CORTEX_BUILD_SWIMSPEED);
		const Sint32 poolSites = cortexBuildingSites(obs, CORTEX_BUILD_SWIMSPEED);
		const bool swimExpandsReach = (obs.swimLandReach > 0
		 && obs.swimWaterReach * CORTEX_SWIM_REACH_GAIN_DENOM
		  > obs.swimLandReach * CORTEX_SWIM_REACH_GAIN_NUMER);
		const bool wantPool = (obs.algaeDiscovered != 0 || swimExpandsReach);
		// Prerequisite: hold the swimming pool until the racetrack is actually built
		// (finished). The racetrack's placement is what defines the colony's compact
		// footprint; SWIM is a later convenience that only makes sense once that core
		// layout exists. Without a finished racetrack, hold the pool regardless of the
		// swim signals.
		const bool poolPrereq = (race > 0);
		// Not until the army ramp has actually begun: swimming is not mandatory for
		// workers in the opening, so we hold the pool until warriors are in the swarm
		// mix (warriors > 0, which only happens in the combat phase). This keeps the
		// pool from competing with the early economy/army build-up.
		if (combatPhase && canExpand && warriors > 0 && wantPool && poolPrereq
		 && pool == 0 && poolSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SWIMSPEED);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_SWIMSPEED, slot);
		}

		// --- Priority 6: barracks (ATTACK) — the army pivot. Lets produced warriors
		// train to attack level 1 and get healed between fights. One is enough.
		if (combatPhase && barracks == 0 && barracksSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_ATTACK);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_ATTACK, slot);
		}

		// --- Priorities 6.3-6.8: unified EXPAND-vs-UPGRADE ladder. ----------------
		// For each training/feeding class we decide, from two signals, whether the
		// current building level still has work (keep it / expand for redundancy) or is
		// mostly done and worth UPGRADING:
		//   • "% of eligible units already maxed at the current level" (unitsServedPct
		//     over the matching upgradeState slice, vs the matching unit pool), and
		//   • spare labour (canExpand) to pay for the build/teardown.
		// obs.upgradableCount[T] already encodes the FULL engine Upgradable predicate
		// (finished, full HP, not already upgrading, has a higher level, the larger
		// footprint fits, and crucially maxBuildLevel > level — so a nonzero count means
		// a school has lifted our BUILD skill and a class-T building can actually go up a
		// level). cortexBuildingsUpgrading(T) guards against stacking two upgrades of the
		// same class. The action layer's findUpgradeTarget picks the laggard (lowest
		// level) instance, so with two buildings it lifts them one at a time.

		// --- Priority 6.3 + 6.5: barracks (ATTACK) — the unit-strength lever. ---
		// Warriors train ATTACK_SPEED+ATTACK_STRENGTH (in parallel) to barracksLevel+1.
		// EXPAND first: an upgrade blacks a barracks out for ~hundreds-to-2000 ticks,
		// during which it trains and heals no warriors — with one barracks that is a
		// total army blackout (the measured ~1900-tick defect this ladder fixes). So we
		// require a SECOND barracks before upgrading; the new one keeps training the
		// warrior stream while the laggard upgrades. Gated on canExpand so the build/
		// teardown comes off idle hands, never off hauling or army production.
		const Sint32 barracksLevel = cortexMaxFinishedLevel(obs, CORTEX_BUILD_ATTACK);
		const int attackMaxedPct   = unitsServedPct(obs.attackStrengthLevel, warriors, barracksLevel + 1);
		const bool barracksUpgradeWanted = combatPhase && canExpand
		 && barracks >= 1
		 && obs.upgradableCount[CORTEX_BUILD_ATTACK] > 0
		 && cortexBuildingsUpgrading(obs, CORTEX_BUILD_ATTACK) == 0
		 && attackMaxedPct >= UPGRADE_MAXED_PERCENT;
		if (barracksUpgradeWanted && barracks < BARRACKS_MIN_BEFORE_UPGRADE && barracksSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_ATTACK);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_ATTACK, slot);
		}
		if (barracksUpgradeWanted && barracks >= BARRACKS_MIN_BEFORE_UPGRADE)
			return makeUpgradeAction(CORTEX_BUILD_ATTACK);

		// --- Priority 6.6: school (SCIENCE) upgrade. ---
		// Workers train BUILD+HARVEST (in parallel) to schoolLevel+1; buildLevel[] is
		// the worker BUILD distribution (only workers have BUILD performance), and
		// because the school upgrades both abilities in one visit HARVEST tracks it, so
		// the BUILD slice alone gates both. Single-instance: a school blackout only
		// pauses worker tech (maxBuildLevel, already earned, does NOT drop), it strands
		// no army and starves no one — and the maxed gate means few workers are waiting.
		// A school UPGRADE consumes ALGA too (12 at L1, 10 at L2), so like the build it
		// needs reachable algae — gate on algaeReachable so an upgrade is never started
		// against a site that can no longer be supplied (e.g. the shoreline algae has
		// since been depleted).
		const Sint32 schoolLevel = cortexMaxFinishedLevel(obs, CORTEX_BUILD_SCIENCE);
		const int buildMaxedPct  = unitsServedPct(obs.buildLevel, obs.workers, schoolLevel + 1);
		if (combatPhase && canExpand && school >= 1 && obs.algaeReachable
		 && obs.upgradableCount[CORTEX_BUILD_SCIENCE] > 0
		 && cortexBuildingsUpgrading(obs, CORTEX_BUILD_SCIENCE) == 0
		 && buildMaxedPct >= UPGRADE_MAXED_PERCENT)
			return makeUpgradeAction(CORTEX_BUILD_SCIENCE);

		// --- Priority 6.7: racetrack (WALKSPEED) upgrade. ---
		// Workers AND warriors train WALK to raceLevel+1; walkLevel[] sums both (the
		// racetrack's eligible pool), explorers excluded (zero WALK performance).
		// Single-instance: a racetrack blackout only leaves units at their current
		// speed for the window — no capability loss.
		const Sint32 raceLevel  = cortexMaxFinishedLevel(obs, CORTEX_BUILD_WALKSPEED);
		const int walkMaxedPct  = unitsServedPct(obs.walkLevel, obs.workers + warriors, raceLevel + 1);
		if (combatPhase && canExpand && race >= 1
		 && obs.upgradableCount[CORTEX_BUILD_WALKSPEED] > 0
		 && cortexBuildingsUpgrading(obs, CORTEX_BUILD_WALKSPEED) == 0
		 && walkMaxedPct >= UPGRADE_MAXED_PERCENT)
			return makeUpgradeAction(CORTEX_BUILD_WALKSPEED);

		// --- Priority 6.8: inn (FOOD) upgrade — spare-first, feed-safe. ---
		// An inn is a feeding building, not a trainer, so there is no "% maxed" signal;
		// the gate is purely feed safety. Upgrading raises maxUnitInside (4→7→17) and
		// speeds feeding, but the blackout removes that inn's whole feeding capacity. We
		// upgrade only with (a) a redundant inn so feeding never hits zero, and (b)
		// enough capacity left over (feedCapacity minus the inn we'd take offline) to
		// still feed the population through the blackout. Feed-led growth (Priority 2)
		// keeps feedCapacity ≈ population, so that slack rarely exists — when an upgrade
		// is wanted but unsafe we build ONE spare inn first to create it (the added inn
		// is at the current max level, ≥ the lowest-level inn we'd upgrade, so one spare
		// suffices). Priority 2 remains the backstop if growth erodes the slack mid-blackout.
		const bool innUpgradeWanted = combatPhase && canExpand && inns >= 1
		 && obs.upgradableCount[CORTEX_BUILD_FOOD] > 0
		 && cortexBuildingsUpgrading(obs, CORTEX_BUILD_FOOD) == 0;
		if (innUpgradeWanted)
		{
			const int lostCapacity = smallestFinishedInnCapacity(obs);
			const bool feedSlackOk = (obs.feedCapacity - lostCapacity) >= obs.totalUnit;
			const bool innRedundant = (inns >= INN_MIN_BEFORE_UPGRADE);
			if (innRedundant && feedSlackOk)
				return makeUpgradeAction(CORTEX_BUILD_FOOD);
			// Not safe yet: build one spare inn to create the redundancy / slack.
			if (innSites == 0)
			{
				const int slot = firstValidCandidate(obs, CORTEX_BUILD_FOOD);
				if (slot >= 0)
					return makeBuildAction(CORTEX_BUILD_FOOD, slot);
			}
		}

		// --- Priority 6.9: hospital (HEAL) expand + upgrade. ---
		// More hospitals AND higher-level ones both grow army sustain: a level-L
		// hospital heals maxUnitInside units at once (2/5/7 at L0/1/2) and faster per
		// unit (30/18/6 ticks), so an upgrade is a big jump on both axes.
		//   EXPAND: one hospital per HOSPITAL_WARRIORS_PER standing warriors, up to
		//     HOSPITAL_MAX. The first hospital is Priority 5 / the panic path; this
		//     grows the count as the army grows. (Army-scaled, not needHeal-scaled —
		//     see the constant: wounded warriors out on the flag never queue to heal.)
		//   UPGRADE: only ever upgrade a hospital when a SECOND finished hospital
		//     exists to cover healing (heal >= 2). An upgrade turns the building into
		//     a construction site — a heal blackout for that hospital — so upgrading
		//     the only one would leave the army with nowhere to heal. With a redundant
		//     hospital, the one-at-a-time guard (cortexBuildingsUpgrading == 0) keeps
		//     the other finished and available throughout. This guarantees that once a
		//     hospital is built, at least one stays available to heal units.
		if (combatPhase && canExpand && heal >= 1 && healSites == 0
		 && heal < HOSPITAL_MAX
		 && warriors >= heal * HOSPITAL_WARRIORS_PER)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_HEAL);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_HEAL, slot);
		}
		if (combatPhase && canExpand && heal >= 2
		 && obs.upgradableCount[CORTEX_BUILD_HEAL] > 0
		 && cortexBuildingsUpgrading(obs, CORTEX_BUILD_HEAL) == 0)
			return makeUpgradeAction(CORTEX_BUILD_HEAL);

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
		const bool openingBuildOutDone =
		    inns >= 1 && school >= 1 && race >= 1 && heal >= 1 && barracks >= 1;
		if (combatPhase && canExpand && openingBuildOutDone
		 && anySwarmSupplyStressed(obs)
		 && swarms >= 1 && swarms < CORTEX_MAX_SWARMS && swarmSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SWARM);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_SWARM, slot);
		}

		// --- Priority 7: defense (recall the army to a threatened building). ---
		// War flags are standing buildings: once placed they keep summoning
		// warriors without being re-issued, so this need not fire every cycle —
		// it just (re)positions the single flag onto the current threat. Defense
		// outranks offense: when our base is under attack the lone flag comes home.
		if (combatPhase && obs.buildingsUnderAttack > 0
		 && warriors >= DEFENSE_MIN_WARRIORS && obs.defenseTarget.valid)
			return makeDefenseFlagAction(DEFENSE_FLAG_RADIUS, warriors);

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
		{
			const bool offenseWillClaim =
				combatPhase && warriors >= ATTACK_MIN_WARRIORS && obs.flagTargets[0].valid;
			if (obs.warFlagsActive > 0 && obs.buildingsUnderAttack == 0
			 && !offenseWillClaim && obs.enemyUnitsNearFlag == 0)
				return makeClearFlagsAction();
		}

		// Wheat sustainability (checkerboard forbidden paint) is NO LONGER a rung
		// here: it runs every decision cycle in parallel with whatever primary action
		// this ladder picks, so it can never be starved by build/upgrade/offense work
		// (and conversely never steals a cycle from them). See wantWheatProtection()
		// below and AICortex::enqueueWheatForbidden, called each cycle in getOrder().

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

		// (Stranded-flag teardown moved up to Priority 7.2 so it is reachable above the
		// economy rungs and respects the straggler-grace hold — see above.)

		return makeNoOpAction();
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
			// Wheat-starved override: a swarm whose catchment holds too little
			// HARVESTABLE wheat cannot use more than a single hauler — extra workers
			// find no wheat to harvest and just idle or thrash the depleted patch. Cap
			// it at CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP outright (not the gentle
			// +/-1 step), regardless of the corn buffer. harvestableWheatNearby is -1
			// when unknown (game absent); only act on a real count. Takes precedence
			// over the buffer-driven add/remove below.
			if (t.harvestableWheatNearby >= 0
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
}
