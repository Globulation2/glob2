// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

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
	{
		// Opt into the learned swarm worker-cap policy (effort B pilot) only when
		// asked AND the net actually loads; otherwise stay on the hand rule. Reading
		// the env + loading the blob happens once here, at AI construction — never
		// during a tick — so the per-tick decision stays a pure, deterministic
		// function of the observation. See docs/AI/cortex/PILOT.md.
		const char* mode = getenv("GLOB2_CORTEX_POLICY");
		if (mode && std::string(mode) == "ml")
		{
			const char* netPath = getenv("GLOB2_CORTEX_NET");
			if (netPath && netPath[0] && swarmNet_.load(netPath))
				mlSwarmCaps_ = true;
			else
				std::cerr << "CORTEX_POLICY=ml but the swarm-cap net failed to load"
				             " (GLOB2_CORTEX_NET='" << (netPath ? netPath : "")
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
		f.combatPhase     = (innEstablished
		                           && f.swarms >= COMBAT_ECON_MIN_SWARMS
		                           && obs.totalUnit >= COMBAT_ECON_MIN_UNITS
		                           && !f.starving);

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
		f.growWarrior  = f.combatPhase ? 1 : 0;

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
		f.growWorker = (f.combatPhase && suppressWorkers) ? 0
		                     : (f.combatPhase ? 2 : 1);

		// Pre-combat panic flag (Priority 0). Shared because Priority 1 keys its whole
		// block on !panic. Suppressed once we field a real army (warriors >
		// PANIC_MAX_WARRIORS): such a colony defends through the normal path and need
		// not derail its economy.
		const bool underAttack = (obs.buildingsUnderAttack > 0 || obs.unitsUnderAttack > 0);
		f.panic       = (!f.combatPhase && underAttack
		                          && f.warriors <= PANIC_MAX_WARRIORS);

		return f;
	}

	CortexAction CortexPolicy::decide(const CortexObservation& obs)
	{
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
		ScoredAction best{ SCORE_NONE, makeNoOpAction() };
		for (const ScoredAction& c : candidates)
			if (c.score > best.score) // strict: earlier candidate wins ties
				best = c;
		return best.action;
	}
}
