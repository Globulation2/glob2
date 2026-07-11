// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexPolicy.h"
#include "CortexTuning.h"

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

	/// Buffer added to the swarm+inn hauler demand to form the worker target's lower
	/// bound `base` (see computeFacts). A couple of spare workers above what the
	/// hauling jobs strictly require absorbs construction deliveries and hauler
	/// replacement without tipping the colony into a worker shortage every time a
	/// building finishes.
	static const int WORKER_TARGET_BUFFER = 2;
	/// Swarm production-mix worker ratios for the three-tier worker-target rule
	/// (computeFacts). Below the hauler floor the swarm makes workers (TIER1, vs the
	/// single scout explorer); in the middle band (floor..mid) it stays
	/// worker-DOMINANT — tuning.workerRatioTier2 (default half the bar) workers to
	/// one warrior — while it grows the worker base toward full staffing; above mid
	/// it drops to the army. The tier-2 ratio and the mid divisor are tuning knobs
	/// (CortexTuning); TIER1 stays fixed.
	static const int WORKER_RATIO_TIER1 = 2;

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
		: expandWantStreak_(0)
		, mlSwarmCaps_(false)
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

		// Explorer slice of the production mix. Bootstrap puts one early explorer
		// out (reveal our wheat / scout); once established we keep ≥1 explorer out
		// at all times (so flagTargets can populate for offense). The WORKER and
		// WARRIOR slices are decided by the worker-target rule below.
		const bool wantEarlyExplorer = (!f.combatPhase && f.swarms >= 1 && obs.explorers == 0);
		const bool wantScout         = (obs.explorers == 0); // keep ≥1 explorer out.
		f.growExplorer = (wantEarlyExplorer || (f.combatPhase && wantScout)) ? 1 : 0;

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

		// --- Three-tier worker-target production mix --------------------------
		// Two worker-COUNT boundaries gate the {worker, warrior} slices:
		//
		//   base  = Σ(swarm + inn hauler requests) + WORKER_TARGET_BUFFER — the hauler
		//           floor: enough workers to staff every swarm + inn hauling job plus a
		//           small buffer. Each building's CURRENT maxUnitWorking is its live
		//           hauler request (tuneWorkers converges it to the level the corn
		//           buffer / restock deficit calls for), so summing them is the live
		//           "how many haulers does the economy want" figure.
		//   needs = obs.workers + fillableNeeded — the full STAFFABLE worker demand:
		//           the count at which every job the current workforce can take is
		//           filled. Jobs above the workers' HARVEST level are excluded
		//           (only a school clears those, not more workers).
		//   mid   = base + (needs - base)/2 — halfway across the gap between the bare
		//           hauler floor and full staffing.
		//
		// The swarm grows its worker base all the way to `mid` before it commits the
		// army, instead of flipping to warriors the instant the bare hauler floor is
		// met. (That early flip pinned workers at the floor while jobs went unstaffed
		// and the colony starved — the famine-spiral the Muka trace exposed.) Reading
		// our OWN buildings is not a fog cheat.
		int base = WORKER_TARGET_BUFFER;
		for (int i = 0; i < obs.swarmCount; i++)
			if (obs.trackedSwarms[i].valid)
				base += obs.trackedSwarms[i].maxUnitWorking;
		for (int i = 0; i < obs.innCount; i++)
			if (obs.trackedInns[i].valid)
				base += obs.trackedInns[i].maxUnitWorking;
		f.workersNeeded = base;
		const int needs = obs.workers + f.fillableNeeded;
		// mid <= base when demand is already met by the floor. The divisor
		// (default 2 = halfway) is a tuning knob: construction-site jobs inflate
		// `needs`, so it also sets how hard an in-flight expansion delays the
		// warrior ramp (.tmp/rankgate-diag/FINDINGS.md).
		const int mid   = base + (needs - base) / cortexTuning().tierMidDiv;

		// Warriors are only ever folded in once the economy is established (below that
		// there are no warriors to make, and the colony still needs every worker to
		// raise its first inn). The three tiers, all keyed on the worker COUNT:
		//   workers < base       -> workers only: grow the hauler base, no army yet.
		//   base <= workers < mid -> worker-DOMINANT mix (half-bar workers : 1 warrior):
		//        keep growing workers toward full staffing while the army starts.
		//   workers >= mid       -> warriors: the worker base has covered half the gap
		//        to full staffing (and since `needs` tracks live demand, in practice
		//        nearly all of it), so spare population goes to the army. During a
		//        famine the population has overshot what wheat can feed, so few jobs
		//        are open (needs low, mid ~ base) and the swarm converts the doomed
		//        surplus food into SOLDIERS rather than more starving mouths.
		if (obs.workers < base)
		{
			f.growWorker  = WORKER_RATIO_TIER1;
			f.growWarrior = 0;
		}
		else if (obs.workers < mid)
		{
			f.growWorker  = cortexTuning().workerRatioTier2;
			f.growWarrior = f.economyEstablished ? 1 : 0;
		}
		else
		{
			f.growWorker  = 0;
			f.growWarrior = f.economyEstablished ? 1 : 0;
		}

		// HARD rule: the production mix is NEVER {0,0,0} (a halted swarm). If the
		// worker target is met but nothing else is being produced (not yet
		// established, scout already out), keep one worker going.
		if (f.growWorker == 0 && f.growWarrior == 0 && f.growExplorer == 0)
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

	// DECIDE_CONTRACT action-map class indices for the three war-flag decisions.
	// They are still EVALUATED inside decide() (for the 18-class eligibility mask +
	// trace), but their SELECTION moved to decideCombat() so a busy economy can never
	// starve a war-flag move (and vice versa). Identified by explicit class index
	// rather than "the last three" so a future appended ECONOMY candidate stays
	// selectable in decide(). Keep in lockstep with CortexPolicy::decide()'s
	// candidates[] order and docs/AI/cortex/DECIDE_CONTRACT.md.
	enum {
		DECIDE_CLASS_DEFENSE     = 15,
		DECIDE_CLASS_RETIRE_FLAG = 16,
		DECIDE_CLASS_OFFENSE     = 17
	};
	static bool isCombatDecideClass(int k)
	{
		return k == DECIDE_CLASS_DEFENSE
		    || k == DECIDE_CLASS_RETIRE_FLAG
		    || k == DECIDE_CLASS_OFFENSE;
	}

	CortexAction CortexPolicy::decide(const CortexObservation& obs, DecideTrace* trace)
	{
		if (trace)
		{
			trace->eligibleMask = 0;
			trace->chosen = -1;
			trace->failedGates = 0;
		}

		// Reject an observation built against a layout this policy wasn't
		// written for, or one that was never populated. Either way: do nothing.
		if (obs.version != OBSERVATION_VERSION || !obs.valid)
			return makeNoOpAction();

		// The one per-cycle mutation: advance the fresh-patch debounce streak
		// scoreSecondSwarm reads (see CortexPolicyEconomy.cpp).
		updateExpandStreak(obs);

		const DecideFacts f = computeFacts(obs);

		// Feasibility gates (see CortexGate): each bit evaluated ONCE per cycle
		// from the shared facts, then applied declaratively per candidate via
		// candidateGates[] below. A set bit means the gate FAILED this cycle.
		Uint32 failedGates = 0;
		if (f.inns < 1)
			failedGates |= GATE_BOOTSTRAP;
		if (obs.freeWorkers < 1)
			failedGates |= GATE_LABOR;

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
		// Per-candidate required feasibility gates (CortexGate bits), in EXACT
		// lockstep with candidates[] above — the shared array index IS the
		// DECIDE_CONTRACT class index; never reorder either, append only. A
		// candidate whose required gate failed this cycle is treated exactly as
		// if its scorer had declined. Mask 0 = never gated: the candidate either
		// has no shared feasibility precondition, or its precondition is
		// entangled in a composite fact (canExpand / economyEstablished /
		// combatPhase) that stays inside the scorer.
		static const Uint32 candidateGates[] = {
			0,                           // PanicDefense — the emergency must never be vetoed.
			0,                           // ProductionControl — the swarm always produces.
			0,                           // FeedCapacity — feeding is existential, deliberately ungated.
			GATE_BOOTSTRAP,              // SwarmRecovery — rebuild only behind a finished inn.
			0,                           // School — spare labour entangled in canExpand.
			0,                           // Racetrack — spare labour entangled in canExpand.
			GATE_LABOR,                  // Hospital — build crew comes off idle hands.
			0,                           // SwimmingPool — spare labour entangled in canExpand.
			0,                           // Barracks — combatPhase-gated in the scorer.
			0,                           // BarracksUpgrade — spare labour entangled in canExpand.
			0,                           // SchoolUpgrade — canExpand relaxed by unfillable-jobs override.
			0,                           // RacetrackUpgrade — spare labour entangled in canExpand.
			GATE_BOOTSTRAP,              // InnUpgrade — needs a first inn to upgrade.
			GATE_LABOR,                  // HospitalExpandUpgrade — build crew off idle hands.
			GATE_BOOTSTRAP | GATE_LABOR, // SecondSwarm — bootstrap protected, staffable.
			0,                           // Defense — war flags are never feasibility-gated.
			0,                           // RetireFlag — war flags are never feasibility-gated.
			0,                           // Offense — war flags are never feasibility-gated.
		};
		static_assert(sizeof(candidateGates) / sizeof(candidateGates[0])
		           == sizeof(candidates) / sizeof(candidates[0]),
		              "candidateGates[] must stay in lockstep with candidates[]");
		// Eligibility mask + hand argmax in ONE pass over candidates. The mask is now
		// built unconditionally (not only when tracing) because the ML decision-net
		// path needs it every cycle; the hand argmax (best/bestIndex) is computed
		// alongside so the trace label and the off-path behaviour are unchanged.
		//
		// SELECTION is restricted to the ECONOMY candidates: the war-flag decisions
		// (Defense/RetireFlag/Offense) split out to decideCombat(), which
		// runs them on its own parallel pass in getOrder(). decide() must NOT also
		// select them or it would double-emit the same flag action this cycle. They
		// are still EVALUATED here so the full 18-class eligibleMask + trace stay
		// intact for the DECIDE_CONTRACT pilot (training continuity); only the
		// economyMask feeds selection. Combat classes are excluded by explicit class
		// index (DECIDE_CONTRACT action map), so a future appended ECONOMY candidate
		// stays selectable here.
		Uint32 eligibleMask = 0; // full 18-class mask: trace + ML contract.
		Uint32 economyMask = 0;  // economy-only subset that decide() may select.
		ScoredAction best{ SCORE_NONE, makeNoOpAction() };
		int bestIndex = -1;
		const int n = static_cast<int>(sizeof(candidates) / sizeof(candidates[0]));
		for (int k = 0; k < n; k++)
		{
			const ScoredAction& c = candidates[k];
			// Feasibility veto: a candidate whose required gate failed this cycle
			// is exactly a decline — excluded from selection AND from the mask,
			// identical to its scorer returning cortexDecline().
			if (candidateGates[k] & failedGates)
				continue;
			// Eligibility (the decline gate): a positive score means this candidate
			// did not decline this cycle — the ML mask bit. The candidate array index
			// IS the class index (DECIDE_CONTRACT action map); never reorder it.
			if (c.score > SCORE_NONE)
			{
				eligibleMask |= (Uint32(1) << k);
				if (!isCombatDecideClass(k))
				{
					economyMask |= (Uint32(1) << k);
					if (c.score > best.score) // strict: earlier candidate wins ties
					{
						best = c;
						bestIndex = k;
					}
				}
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
			trace->failedGates = failedGates; // why a gated candidate was vetoed (CortexGate bits)
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
			// Select over the ECONOMY subset only: the combat classes are owned by
			// decideCombat()'s parallel pass, so the net must not pick them here either
			// (else the flag action double-emits with the combat pass this cycle).
			const int k = decisionNet_.scoreDecision(features, economyMask);
			if (k >= 0)
				return candidates[k].action;
			return makeNoOpAction(); // nothing eligible — same NoOp as the hand path
		}

		return best.action;
	}

	CortexAction CortexPolicy::decideCombat(const CortexObservation& obs) const
	{
		// Same guard decide() uses: reject an unpopulated / wrong-layout observation.
		if (obs.version != OBSERVATION_VERSION || !obs.valid)
			return makeNoOpAction();

		const DecideFacts f = computeFacts(obs);

		// Utility-argmax over ONLY the three war-flag scorers, in the same fixed order
		// and with the same strict earlier-wins-ties rule as decide(). Their SCORE_*
		// bands give a fixed combat-internal priority:
		// serious-defense (SCORE_DEFENSE_SERIOUS) > blitz-offense (SCORE_OFFENSE_BLITZ)
		// > defense (SCORE_DEFENSE) > retire (SCORE_RETIRE_FLAG) > offense (SCORE_OFFENSE).
		// decide() evaluates these same scorers for the trace/ML mask but no longer
		// selects them; this is where a war-flag move is actually chosen.
		const ScoredAction combat[] = {
			scoreDefense(obs, f),
			scoreRetireFlag(obs, f),
			scoreOffense(obs, f),
		};
		ScoredAction best{ SCORE_NONE, makeNoOpAction() };
		const int n = static_cast<int>(sizeof(combat) / sizeof(combat[0]));
		for (int k = 0; k < n; k++)
			if (combat[k].score > best.score) // strict: earlier candidate wins ties
				best = combat[k];
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
