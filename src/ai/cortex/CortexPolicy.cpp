// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexPolicy.h"

namespace Cortex
{
	// --- Phase-1 economy tuning -------------------------------------------
	// Hand-picked thresholds for the v0 rules. These are AI design choices for
	// a brand-new AI (not ported engine mechanics), so they are tunable; later
	// phases / an ML policy replace this whole function. The goal of Phase 1 is
	// a colony that feeds itself, GROWS to a target size, and then HOLDS there
	// instead of overpopulating into starvation.

	/// Target colony size BEFORE the combat phase. The policy halts unit
	/// production at this population while it is still a pure economy.
	static const int GROWTH_UNIT_MAX = 24;
	/// Target colony size once the combat phase is active. A standing army needs
	/// a bigger population; the real ceiling is still feeding capacity
	/// (overCapacity below), this is just the upper clamp.
	static const int COMBAT_UNIT_MAX = 48;
	/// Sustainable population per unit of feeding capacity. AICastor's foodLock
	/// uses `unitSum >= foodSum << 1` (2x), but Castor also actively manages wheat
	/// supply (clearing flags); Cortex does not yet, so its effective carrying
	/// capacity is lower — be conservative until farming management lands.
	static const int FEED_SUSTAIN_MULT = 2;
	/// Halt production if at least this percent of the colony is actively
	/// starving (losing HP) — the reactive safety net for when feedCapacity
	/// overestimates (e.g. an inn exists but its wheat is exhausted).
	static const int STARVE_HALT_PERCENT = 6;
	/// Halt production earlier, while units are merely hungry (not yet losing
	/// HP), to catch a food shortfall before it becomes a death spiral. Mirrors
	/// Nicowar halving production on its hungry-fraction trigger.
	static const int HUNGRY_HALT_PERCENT = 20;
	/// Cap on swarms during the pure-economy phase (more = faster repopulation).
	static const int MAX_SWARMS = 3;
	/// Cap on swarms once in the combat phase (more production = faster army).
	static const int COMBAT_MAX_SWARMS = 5;

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
	// CRITICAL TEMPO LESSON (measured): on SmallForTwo this tech investment costs
	// more tempo than the extra warrior level returns if it competes with growth or
	// the army — eagerly building a school + tearing the only barracks down to a
	// site dropped us from 55% to 42% vs Castor (with new timeout draws). So the
	// whole Phase-2 tech layer is gated SURPLUS-ONLY: it fires only once the colony
	// has hit its population ceiling and is healthy (economySurplus below), where
	// idle workers exist and growth/army production is not being starved. Worst case
	// it never fires (== Phase-3 behaviour); best case it strengthens a mature army.

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

	/// True if any valid tracked swarm is already running at (or above) its
	/// worker cap — meaning the only remaining lever to increase wheat throughput
	/// for that swarm is a second swarm, not more workers at this one.
	static bool anySwarmAtWorkerCap(const CortexObservation& obs)
	{
		const int cap = swarmWorkerCap(obs);
		for (int i = 0; i < obs.swarmCount; i++)
			if (obs.trackedSwarms[i].valid && obs.trackedSwarms[i].maxUnitWorking >= cap)
				return true;
		return false;
	}

	/// True if any valid tracked inn is "getting busy": already at its worker
	/// cap AND still wheat-starved (corn < CORTEX_INN_CORN_ADD_LO), meaning it
	/// cannot haul any faster even with more workers.  This is the signal to
	/// build a second inn before the first one falls over entirely.
	static bool anyInnSaturated(const CortexObservation& obs)
	{
		for (int i = 0; i < obs.innCount; i++) {
			const TrackedBuilding& t = obs.trackedInns[i];
			if (t.valid
			 && t.maxUnitWorking >= CORTEX_INN_WORKER_CAP
			 && t.corn < CORTEX_INN_CORN_ADD_LO)
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

		// How many units the colony can actually sustain right now.
		const Sint32 sustainable   = obs.feedCapacity * FEED_SUSTAIN_MULT;
		const Sint32 starvingPct   = (obs.totalUnit > 0)
			? (obs.starvingUnits * 100 / obs.totalUnit) : 0;
		const Sint32 hungryPct     = (obs.totalUnit > 0)
			? (obs.needFood * 100 / obs.totalUnit) : 0;

		const bool starving        = (starvingPct >= STARVE_HALT_PERCENT);
		const bool hungry          = (hungryPct >= HUNGRY_HALT_PERCENT);

		// Combat phase gate: a self-feeding colony with real production capacity,
		// not currently starving. Below this we are still a pure economy and play
		// exactly like Phase 1 (workers only, halt at GROWTH_UNIT_MAX).
		const bool combatPhase     = (inns   >= COMBAT_ECON_MIN_INNS
		                           && swarms >= COMBAT_ECON_MIN_SWARMS
		                           && obs.totalUnit >= COMBAT_ECON_MIN_UNITS
		                           && !starving);

		// Phase-dependent population ceiling and swarm cap.
		const Sint32 popCap        = combatPhase ? COMBAT_UNIT_MAX : GROWTH_UNIT_MAX;
		const Sint32 swarmCap      = combatPhase ? COMBAT_MAX_SWARMS : MAX_SWARMS;

		const bool atPopGoal       = (obs.totalUnit >= popCap);
		// feedCapacity == 0 before the first inn: don't treat the bootstrap colony
		// as "over capacity" or it would halt the workers needed to build that inn.
		const bool overCapacity    = (obs.feedCapacity > 0 && obs.totalUnit >= sustainable);
		const bool shouldGrow      = !atPopGoal && !overCapacity && !starving && !hungry;

		// Surplus state: the colony has hit its population ceiling (or feeding cap)
		// and is healthy. This is the ONLY time we spend on Phase-2 tech (school +
		// barracks upgrade) — at that point growth has stopped and idle workers are
		// available, so the school build and the barracks teardown-to-upgrade no
		// longer steal tempo from growth or army production. (Spending eagerly here
		// is what regressed the benchmark; see the Phase-2 tempo lesson above.)
		const bool economySurplus  = (atPopGoal || overCapacity) && !starving && !hungry;

		// Combat-phase production mixes in explorers (to scout enemy buildings so
		// flagTargets can populate) and warriors (the army). Warrior-weighted so a
		// defensible force builds up fast; one explorer is enough to reveal the
		// enemy base on these maps. Pure-economy phase is workers only — EXCEPT one
		// early explorer (below) to reveal our own wheat fast (the wheat paint is
		// FOW-gated, so coverage only reaches wheat we can currently see).
		// {WORKER, EXPLORER, WARRIOR}.
		const bool wantEarlyExplorer = (!combatPhase && swarms >= 1 && obs.explorers == 0);
		const int growWorker   = 1;
		const int growExplorer = (combatPhase || wantEarlyExplorer) ? 1 : 0;
		const int growWarrior  = combatPhase ? 2 : 0;

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

		// --- Priority 1: production control (this is what bounds population). ---
		// Suppressed entirely while panicking: the panic block owns production then
		// (it has flipped the swarms to all-warrior), so these halt/resume/explorer
		// rules must not fight it by reverting the ratio back toward workers.
		// The policy is pure, so it can't read swarm ratios directly — it uses
		// obs.swarmsProducing (count of finished swarms currently producing) to
		// tell whether a halt/resume order is actually needed, and the action
		// layer dedups per-swarm, so re-deciding the same intent each cycle is free.
		if (!panic && !shouldGrow && obs.swarmsProducing > 0)
			return makeSetProductionAction(0, 0, 0); // halt: hold population steady.
		if (!panic && shouldGrow && obs.swarmsProducing < swarms)
			return makeSetProductionAction(growWorker, growExplorer, growWarrior); // resume.
		// One-shot economy->combat ratio flip: all swarms are producing but still
		// worker-only (warriors == 0), so retarget them to the combat mix. Once
		// warriors start appearing this stops firing, freeing cycles for the flag
		// actions below (the action layer dedups, so a stray re-issue is harmless).
		if (!panic && combatPhase && shouldGrow && swarms > 0
		 && obs.swarmsProducing == swarms && warriors == 0)
			return makeSetProductionAction(growWorker, growExplorer, growWarrior);
		// One-shot early-explorer flip (economy phase): all swarms producing but
		// none set to explorers and we have none — retarget once to fold one in.
		// Guarded by swarmsProducingExplorer == 0 so it fires exactly once.
		if (!panic && wantEarlyExplorer && shouldGrow && swarms > 0
		 && obs.swarmsProducing == swarms && obs.swarmsProducingExplorer == 0)
			return makeSetProductionAction(growWorker, growExplorer, growWarrior);
		// Revert: once an explorer exists and a swarm is still set to produce them,
		// drop the economy swarms back to workers-only so we don't keep over-
		// producing explorers. Stops as soon as no swarm carries an explorer ratio.
		if (!panic && !combatPhase && shouldGrow && swarms > 0
		 && obs.swarmsProducing == swarms
		 && obs.swarmsProducingExplorer > 0 && obs.explorers >= 1)
			return makeSetProductionAction(1, 0, 0);

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

		// --- Priority 2: food capacity (build inns to raise the sustainable cap). ---
		// One inn at a time. Build when there is no inn, when units are hungry with
		// nowhere to eat, when current capacity can't yet sustain the pop goal, OR
		// when an existing inn is saturated — at its worker cap AND still corn-starved
		// (anyInnSaturated), meaning it cannot haul any faster even with more workers.
		// That last trigger fires early, while the first inn is "getting busy", so
		// the second inn is underway before the first falls over entirely.
		// CortexPlacement already rejects sites that are too far from wheat or too
		// close to another inn, so any valid candidate slot is geographically sound —
		// we do not re-check wheat distance here.
		if (innSites == 0)
		{
			const bool noInnYet      = (inns == 0 && obs.totalUnit > 0);
			const bool hungryNoInn   = (obs.needFoodNoInns > 0);
			const bool capacityShort = (obs.totalUnit > 0 && sustainable < popCap);
			const bool innSaturated  = anyInnSaturated(obs);
			if (noInnYet || hungryNoInn || capacityShort || innSaturated)
			{
				const int slot = firstValidCandidate(obs, CORTEX_BUILD_FOOD);
				if (slot >= 0)
					return makeBuildAction(CORTEX_BUILD_FOOD, slot);
			}
		}

		// --- Priority 3: barracks (train/heal warriors; combat phase only). ---
		// One barracks is enough for the first increment — it lets produced
		// warriors level up and get healed between fights.
		if (combatPhase && barracks == 0 && barracksSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_ATTACK);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_ATTACK, slot);
		}

		// --- Priority 4: school (SCIENCE_BUILDING) — the upgrade prerequisite. ---
		// A school trains our workers' BUILD skill, which raises team maxBuildLevel,
		// the engine gate that lets us upgrade the barracks (and so train level-2
		// warriors). Build one only when the economy is in SURPLUS (at the population
		// ceiling, healthy) so the ~5-worker build crew comes off idle hands, never
		// off growth or army production — building it eagerly regressed the benchmark.
		// One school suffices for this increment; upgrading the school itself (toward
		// level-3 warriors) is deferred to the next increment.
		if (combatPhase && economySurplus && barracks > 0
		 && school == 0 && schoolSites == 0)
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SCIENCE);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_SCIENCE, slot);
		}

		// --- Priority 5: swarms (production capacity, only while growing). ---
		// Needs an inn first (so new units have somewhere to eat), one site at a
		// time, capped. A fresh swarm defaults to producing workers; the
		// production-control step above retargets/halts it as needed.
		//
		// Expansion trigger: build a new swarm ONLY when we genuinely need one —
		// either this is the bootstrap (swarms == 0, first swarm ever) OR some
		// existing swarm is already pinned at its worker cap. The "at worker cap"
		// signal matters because swarm production is timeout-gated (one unit per
		// ~150 ticks regardless of worker count beyond the minimum needed to keep
		// the corn buffer from stalling), so more workers at the SAME swarm can't
		// speed production — only an additional swarm can. The tuning loop
		// (Priority 1.5) tries workers first; once it saturates at the cap this
		// block correctly concludes that a new swarm is the only remaining lever.
		//
		// swarms < swarmCap remains the hard backstop ceiling so we never over-build.
		// CortexPlacement rejects any candidate site that is too far from wheat or
		// too close to an existing swarm; the valid candidate slots are already
		// geographically sound — we do not re-check those predicates here.
		if (shouldGrow && swarmSites == 0 && inns > 0 && swarms < swarmCap
		 && (swarms == 0 || anySwarmAtWorkerCap(obs)))
		{
			const int slot = firstValidCandidate(obs, CORTEX_BUILD_SWARM);
			if (slot >= 0)
				return makeBuildAction(CORTEX_BUILD_SWARM, slot);
		}

		// --- Priority 6: upgrade the barracks (the unit-strength lever). ---
		// obs.upgradableCount already encodes the FULL engine Upgradable predicate
		// (finished, full HP, not already upgrading, has a higher level, the larger
		// footprint fits, and crucially maxBuildLevel > level) — so a nonzero count
		// means "a school has lifted our BUILD skill and a barracks is ready to go to
		// the next level." We upgrade one at a time (cortexBuildingsUpgrading guards
		// against stacking a second upgrade on a type already in progress) and let
		// the action layer choose the bottleneck instance — beating Nicowar's
		// uniform syncRand()%buildings.size() target pick (ai/nicowar/Upgrade.cpp:141).
		// Gated on economySurplus so we only tear a working barracks down to an
		// upgrade site when growth has stopped and idle workers exist — never while
		// still growing the army (that teardown gap, where the lone barracks can't
		// train or heal, is what made the eager version lose tempo).
		if (combatPhase && economySurplus
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
