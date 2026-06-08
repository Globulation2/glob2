// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace Cortex
{
	// Reduce a per-level upgrade-state histogram (attackStrength/walk/build) to one
	// RAW feature for the decision vector: the highest unit level that has any units
	// (the achieved tech tier, 0..CORTEX_UNIT_LEVELS-1). The DECIDE_CONTRACT names
	// these as the *array* fields (obs.attackStrengthLevel/walkLevel/buildLevel) but
	// the 48-wide vector takes one scalar per row, so a reduction is needed. The max
	// occupied level is the threshold-free choice — it bakes in NONE of the teacher's
	// unitsServedPct / target-level judgment (CortexPolicyTech.cpp), matching the
	// contract's stated reason for excluding the derived booleans. See the report
	// for this discrepancy.
	static int cortexLevelSignal(const Sint32 hist[CORTEX_UNIT_LEVELS])
	{
		int top = 0;
		for (int lvl = 0; lvl < CORTEX_UNIT_LEVELS; lvl++)
			if (hist[lvl] > 0) top = lvl;
		return top;
	}

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

	/// --- worker-surplus production throttle (tunable AI design choice) -----
	// "Available workers" is idle workers minus only the FILLABLE open job requests
	// (isFree[WORKER] - fillableNeeded). It is NOT the engine's raw free-worker
	// figure (isFree[WORKER] - totalNeeded; TeamStat.cpp:290): totalNeeded counts
	// open slots at buildings whose type->level exceeds the workforce's HARVEST
	// level, which no current-level worker can ever take (building/Misc.cpp:127). We
	// subtract only the slots the current workforce can actually staff, so those
	// unfillable jobs don't mask an idle-worker surplus. When that surplus climbs
	// above WORKER_SURPLUS_HI the
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
	/// Above this many standing warriors the pre-combat panic defense is
	/// suppressed: a colony with a real army of its own can absorb a hit through
	/// the normal defense path (war flags, barracks healing) and does not need to
	/// derail its economy into the emergency 100%-warrior / HIGH-priority / panic-
	/// hospital response. Panic is for the defenceless early colony, not one that
	/// already fields a force.
	static const int PANIC_MAX_WARRIORS = 15;

	CortexPolicy::CortexPolicy()
		: mlSwarmCaps_(false)
		, mlDecide_(false)
	{
		// Opt into a learned policy (effort B pilots) only when asked AND the net
		// actually loads; otherwise stay on the hand rule. Reading the env + loading
		// the blob happens once here, at AI construction — never during a tick — so
		// the per-tick decision stays a pure, deterministic function of the
		// observation. The two modes are mutually exclusive env values (a user picks
		// one); each loads its own net independently. See docs/AI/cortex/PILOT.md and
		// docs/AI/cortex/DECIDE_PILOT.md.
		const char* mode = getenv("GLOB2_CORTEX_POLICY");
		const std::string modeStr = (mode ? mode : "");
		if (modeStr == "ml")
		{
			const char* netPath = getenv("GLOB2_CORTEX_NET");
			if (netPath && netPath[0] && swarmNet_.load(netPath))
				mlSwarmCaps_ = true;
			else
				std::cerr << "CORTEX_POLICY=ml but the swarm-cap net failed to load"
				             " (GLOB2_CORTEX_NET='" << (netPath ? netPath : "")
				          << "') — falling back to the hand rule.\n";
		}
		else if (modeStr == "ml-decide")
		{
			const char* netPath = getenv("GLOB2_CORTEX_DECISION_NET");
			if (netPath && netPath[0] && decisionNet_.loadDecide(netPath))
				mlDecide_ = true;
			else
				std::cerr << "CORTEX_POLICY=ml-decide but the decision net failed to load"
				             " (GLOB2_CORTEX_DECISION_NET='" << (netPath ? netPath : "")
				          << "') — falling back to the hand rule.\n";
		}
	}

	CortexPolicy::DecideFacts CortexPolicy::computeFacts(const CortexObservation& obs)
	{
		DecideFacts f;

		f.inns          = cortexFinishedBuildings(obs, CORTEX_BUILD_FOOD);
		f.innSites      = cortexBuildingSites(obs, CORTEX_BUILD_FOOD);
		f.swarms        = cortexFinishedBuildings(obs, CORTEX_BUILD_SWARM);
		f.swarmSites    = cortexBuildingSites(obs, CORTEX_BUILD_SWARM);
		f.barracks      = cortexFinishedBuildings(obs, CORTEX_BUILD_ATTACK);
		f.barracksSites = cortexBuildingSites(obs, CORTEX_BUILD_ATTACK);
		f.school        = cortexFinishedBuildings(obs, CORTEX_BUILD_SCIENCE);
		f.schoolSites   = cortexBuildingSites(obs, CORTEX_BUILD_SCIENCE);
		f.heal          = cortexFinishedBuildings(obs, CORTEX_BUILD_HEAL);
		f.healSites     = cortexBuildingSites(obs, CORTEX_BUILD_HEAL);
		f.race          = cortexFinishedBuildings(obs, CORTEX_BUILD_WALKSPEED);
		f.raceSites     = cortexBuildingSites(obs, CORTEX_BUILD_WALKSPEED);
		f.warriors      = obs.warriors;

		const Sint32 starvingPct   = (obs.totalUnit > 0)
			? (obs.starvingUnits * 100 / obs.totalUnit) : 0;
		const Sint32 hungryPct     = (obs.totalUnit > 0)
			? (obs.needFood * 100 / obs.totalUnit) : 0;
		f.starving        = (starvingPct >= STARVE_HALT_PERCENT);
		f.hungry          = (hungryPct >= HUNGRY_HALT_PERCENT);

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
		const bool innEstablished  = (f.inns >= COMBAT_ECON_MIN_INNS)
		                          || (obs.freeWorkers > 0
		                           && (f.inns + f.innSites) >= COMBAT_ECON_MIN_INNS);
		// economyEstablished = a mature colony (inn + swarm + real population),
		// REGARDLESS of food: it stays true through a famine. combatPhase splits off
		// the healthy slice (mature AND not starving) — it gates offense/defense/
		// barracks and the steady-state production mix, which all want a healthy
		// colony. foodSaturated is the complementary famine slice (mature BUT starving):
		// the population has overshot what the wheat catchment can feed. The two are
		// mutually exclusive and partition economyEstablished by f.starving.
		f.economyEstablished = (innEstablished
		                           && f.swarms >= COMBAT_ECON_MIN_SWARMS
		                           && obs.totalUnit >= COMBAT_ECON_MIN_UNITS);
		f.combatPhase   = f.economyEstablished && !f.starving;
		f.foodSaturated = f.economyEstablished &&  f.starving;

		// Spare labour: idle workers exist, so a tech/expansion build can be started
		// without stealing the haulers that keep the swarm + inn CORN buffers full.
		// The economy expands whenever this holds — there is never an idle
		// "surplus, do nothing" state. Feeding (the inn, Priority 2) is exempt: it is
		// built on the capacity trigger regardless of spare labour, because feeding
		// is existential and a starving colony has no spare labour yet.
		f.canExpand       = (obs.freeWorkers > 0 && !f.starving && !f.hungry);

		// Production mix {WORKER, EXPLORER, WARRIOR} — a HARD rule: NEVER {0,0,0}.
		// Bootstrap is worker-biased with one early explorer (reveal our wheat /
		// scout). Once established, stay worker-DOMINANT (2:1 over warriors) so the
		// worker pool keeps growing to staff new buildings and haul corn, while a
		// warrior slice builds the army and one explorer stays out to scout the
		// enemy base (so flagTargets can populate for offense).
		const bool wantEarlyExplorer = (!f.combatPhase && f.swarms >= 1 && obs.explorers == 0);
		const bool wantScout         = (obs.explorers == 0); // keep ≥1 explorer out.
		f.growExplorer = (wantEarlyExplorer || (f.combatPhase && wantScout)) ? 1 : 0;
		// An established colony keeps building/replacing its army even while starving
		// (foodSaturated): the swarm converts the doomed surplus food into SOLDIERS
		// instead of more starving mouths, which is exactly the army the blitz commits.
		f.growWarrior  = f.economyEstablished ? 1 : 0;

		// Split open-job demand by whether the current workforce can fill it. A worker
		// works at a building of level L only if its HARVEST level >= L
		// (building/Misc.cpp:127); maxBuildLevel == the max worker HARVEST level
		// (schools train BUILD and HARVEST in lockstep), so a job at building level L is
		// FILLABLE iff L <= maxBuildLevel. fillableNeeded + unfillableNeeded ==
		// totalNeeded by construction (the two slices partition totalNeededPerLevel[]).
		int cap = obs.maxBuildLevel;
		if (cap < 0) cap = 0;
		if (cap > CORTEX_UNIT_LEVELS - 1) cap = CORTEX_UNIT_LEVELS - 1;
		f.fillableNeeded   = 0;
		f.unfillableNeeded = 0;
		for (int lvl = 0; lvl < CORTEX_UNIT_LEVELS; lvl++)
		{
			if (lvl <= cap)
				f.fillableNeeded   += obs.totalNeededPerLevel[lvl];
			else
				f.unfillableNeeded += obs.totalNeededPerLevel[lvl];
		}

		// Worker-surplus throttle (see WORKER_SURPLUS_HI): stop minting workers while
		// idle labour piles up and resume once it is spent. "Available workers" is idle
		// workers minus only the FILLABLE open jobs — jobs at building levels above the
		// workforce's HARVEST level (maxBuildLevel) cannot be taken by ANY number of
		// current-level workers (building/Misc.cpp:127), so minting more workers cannot
		// satisfy them; only training (a school raising HARVEST/BUILD level) can. Those
		// unfillable jobs must NOT mask the idle-worker surplus, or the swarm would keep
		// producing workers that also can't take the jobs. Hysteresis: suppress above
		// the high watermark, resume once it goes negative, and HOLD the current mode in
		// the band between so the mix does not oscillate. The held mode is read back from
		// swarmsProducingWorker (no raw-ratio access). Combat-phase only — outside it
		// growWarrior is 0, so dropping workers would halt the swarm at the forbidden {0,0,0}.
		const int availableWorkers = obs.freeWorkers - f.fillableNeeded;
		const bool suppressingNow  =
		    (f.swarms > 0 && obs.swarmsProducing > 0 && obs.swarmsProducingWorker == 0);
		bool suppressWorkers;
		if (availableWorkers > WORKER_SURPLUS_HI)
			suppressWorkers = true;            // idle hands to spare -> make warriors
		else if (availableWorkers < 0)
			suppressWorkers = false;           // labour scarce again -> make workers
		else
			suppressWorkers = suppressingNow;  // hold within the hysteresis band
		// FEEDING GOVERNOR: once the colony is at/over what wheat can feed, stop minting
		// workers — adding mouths during a famine only deepens it. The warrior slice
		// (growWarrior, set above for any established colony) keeps the swarm off the
		// forbidden {0,0,0} halt. Two stages: f.hungry is the proactive brake (units
		// waiting for food but not yet losing HP); f.foodSaturated is the hard governor
		// (units actively starving). Both apply only to an established colony — a
		// bootstrapping colony still needs workers to raise its first inn.
		if (f.foodSaturated || (f.economyEstablished && f.hungry))
			f.growWorker = 0;
		else if (f.combatPhase)
			f.growWorker = suppressWorkers ? 0 : 2;
		else
			f.growWorker = 1;

		// Pre-combat panic flag (Priority 0). Shared because Priority 1 keys its whole
		// block on !panic. Suppressed once we field a real army (warriors >
		// PANIC_MAX_WARRIORS): such a colony defends through the normal path and need
		// not derail its economy.
		const bool underAttack = (obs.buildingsUnderAttack > 0 || obs.unitsUnderAttack > 0);
		f.panic       = (!f.combatPhase && underAttack
		                          && f.warriors <= PANIC_MAX_WARRIORS);

		return f;
	}

	CortexAction CortexPolicy::decide(const CortexObservation& obs, DecideTrace* trace)
	{
		if (trace)
		{
			trace->eligibleMask = 0;
			trace->chosen = -1;
		}

		// Reject an observation built against a layout this policy wasn't
		// written for, or one that was never populated. Either way: do nothing.
		if (obs.version != OBSERVATION_VERSION || !obs.valid)
			return makeNoOpAction();

		const DecideFacts f = computeFacts(obs);

		// Utility selection: every decision scores itself from the observation;
		// the highest score wins. Evaluated in a fixed order so equal scores fall
		// to the earlier decision (deterministic). This replaces the old
		// first-match-wins ladder — a low-urgency decision can no longer be
		// permanently starved behind a higher one.
		const ScoredAction candidates[] = {
			scorePanicDefense(obs, f),
			scoreProductionControl(obs, f),
			scoreFeedCapacity(obs, f),
			scoreSwarmRecovery(obs, f),
			scoreSchool(obs, f),
			scoreRacetrack(obs, f),
			scoreHospital(obs, f),
			scoreSwimmingPool(obs, f),
			scoreBarracks(obs, f),
			scoreBarracksUpgrade(obs, f),
			scoreSchoolUpgrade(obs, f),
			scoreRacetrackUpgrade(obs, f),
			scoreInnUpgrade(obs, f),
			scoreHospitalExpandUpgrade(obs, f),
			scoreSecondSwarm(obs, f),
			scoreDefense(obs, f),
			scoreRetireFlag(obs, f),
			scoreOffense(obs, f),
		};
		// Eligibility mask + hand argmax in ONE pass over candidates. The mask is now
		// built unconditionally (not only when tracing) because the ML decision-net
		// path needs it every cycle; the hand argmax (best/bestIndex) is computed
		// alongside so the trace label and the off-path behaviour are unchanged.
		Uint32 eligibleMask = 0;
		ScoredAction best{ SCORE_NONE, makeNoOpAction() };
		int bestIndex = -1;
		const int n = static_cast<int>(sizeof(candidates) / sizeof(candidates[0]));
		for (int k = 0; k < n; k++)
		{
			const ScoredAction& c = candidates[k];
			// Eligibility (the decline gate): a positive score means this candidate
			// did not decline this cycle — the ML mask bit. The candidate array index
			// IS the class index (DECIDE_CONTRACT action map); never reorder it.
			if (c.score > SCORE_NONE)
				eligibleMask |= (Uint32(1) << k);
			if (c.score > best.score) // strict: earlier candidate wins ties
			{
				best = c;
				bestIndex = k;
			}
		}
		if (trace)
		{
			// The trace ALWAYS reflects the HAND rule (the BC training label): mask =
			// hand decline gates, chosen = hand argmax. Even when mlDecide_ is on, the
			// trace records the hand choice (label semantics) while selection below uses
			// the net — these are kept distinct on purpose. In practice we never trace
			// and run-ml at once, but the semantics stay clean either way.
			trace->eligibleMask = eligibleMask;
			trace->chosen = bestIndex; // -1 when nothing eligible (initial NoOp held)
		}

		// ML decision-selection (DECIDE pilot): select among the eligible candidates
		// with the learned net's utility scores instead of the hand SCORE_* argmax.
		// The eligible set (decline gates) is identical to the hand path — only the
		// selection differs (DECIDE_CONTRACT.md §Inference rule). The net is integer/
		// I16F16 and loaded once in the ctor, so the choice stays deterministic in
		// lockstep (no per-tick load, no new save/load state). When mlDecide_ is off
		// this whole block is skipped and behaviour is byte-identical to the hand path.
		if (mlDecide_)
		{
			int features[NUM_DECIDE_FEATURES];
			extractDecideFeatures(obs, features);
			const int k = decisionNet_.scoreDecision(features, eligibleMask);
			if (k >= 0)
				return candidates[k].action;
			return makeNoOpAction(); // nothing eligible — same NoOp as the hand path
		}

		return best.action;
	}

	void CortexPolicy::extractDecideFeatures(const CortexObservation& obs,
	                                         int features[NUM_DECIDE_FEATURES])
	{
		// Reuse the same fact bundle decide() builds: the finished-building / site
		// counts (idx 0-11) and the fillable/unfillable open-job partition (idx
		// 20-21) come straight from computeFacts, so the trace and the live policy
		// can never disagree on those derivations. Everything else is a raw
		// CortexObservation scalar — the net relearns the teacher's thresholds, so
		// the derived judgment booleans are deliberately NOT exposed (DECIDE_CONTRACT).
		const DecideFacts f = computeFacts(obs);

		// idx 40: count of valid offense flag targets (discovered enemy buildings).
		int flagTargetsValid = 0;
		for (int i = 0; i < CORTEX_FLAG_TARGETS; i++)
			if (obs.flagTargets[i].valid) flagTargetsValid++;

		// idx 12: upgradableCount is a per-building-type array; the 48-wide vector
		// takes one scalar, so sum it — total finished buildings that pass the engine
		// Upgradable predicate right now. The raw "how much upgrade work is available"
		// signal, no per-type threshold baked in. (Discrepancy: contract names the
		// array obs.upgradableCount; see the report.)
		int upgradableTotal = 0;
		for (int t = 0; t < CORTEX_BUILDING_TYPES; t++)
			upgradableTotal += obs.upgradableCount[t];

		int i = 0;
		features[i++] = f.swarms;          // 0
		features[i++] = f.swarmSites;      // 1
		features[i++] = f.inns;            // 2
		features[i++] = f.innSites;        // 3
		features[i++] = f.school;          // 4
		features[i++] = f.schoolSites;     // 5
		features[i++] = f.race;            // 6
		features[i++] = f.raceSites;       // 7
		features[i++] = f.heal;            // 8
		features[i++] = f.healSites;       // 9
		features[i++] = f.barracks;        // 10
		features[i++] = f.barracksSites;   // 11
		features[i++] = upgradableTotal;   // 12 upgradableCount (sum over types)
		features[i++] = obs.totalUnit;     // 13
		features[i++] = obs.workers;       // 14
		features[i++] = obs.explorers;     // 15
		features[i++] = obs.warriors;      // 16
		features[i++] = obs.freeWorkers;   // 17
		features[i++] = obs.totalFree;     // 18
		features[i++] = obs.totalNeeded;   // 19
		features[i++] = f.fillableNeeded;  // 20
		features[i++] = f.unfillableNeeded;// 21
		features[i++] = obs.feedCapacity;  // 22
		features[i++] = obs.needFood;      // 23
		features[i++] = obs.starvingUnits; // 24
		features[i++] = obs.maxBuildLevel; // 25
		features[i++] = obs.swarmCount;    // 26
		features[i++] = obs.innCount;      // 27
		features[i++] = obs.swarmsProducing;        // 28
		features[i++] = obs.swarmsProducingWorker;  // 29
		features[i++] = obs.swarmsProducingWarrior; // 30
		features[i++] = obs.swarmsProducingExplorer;// 31
		features[i++] = cortexLevelSignal(obs.attackStrengthLevel); // 32 attackStrengthLevel
		features[i++] = cortexLevelSignal(obs.walkLevel);           // 33 walkLevel
		features[i++] = cortexLevelSignal(obs.buildLevel);          // 34 buildLevel
		features[i++] = obs.buildingsUnderAttack; // 35
		features[i++] = obs.unitsUnderAttack;     // 36
		features[i++] = obs.warFlagsActive;       // 37
		features[i++] = obs.enemyCount;           // 38
		features[i++] = obs.enemyUnitsNearFlag;   // 39
		features[i++] = flagTargetsValid;         // 40 flagTargetsValid
		features[i++] = obs.flagPosture;          // 41
		features[i++] = obs.defenseTarget.valid ? 1 : 0; // 42 haveDefenseTarget
		features[i++] = obs.algaeReachable;       // 43
		features[i++] = obs.algaeDiscovered;      // 44
		features[i++] = obs.swimLandReach;        // 45
		features[i++] = obs.swimWaterReach;       // 46
		features[i++] = obs.tick;                 // 47
	}
}
