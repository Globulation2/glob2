#include "CortexWorld.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "AICortex.h"
#include "CortexObservation.h"
#include "CortexWheat.h"

#include "Player.h"
#include "team/Team.h"
#include "building/Building.h"
#include "unit/UnitConsts.h"
#include "Game.h"
#include "map/Map.h"
#include "Utilities.h"
#include "TeamStat.h"
#include <iostream>
#include <sstream>
#include <string>
#include <cstdio>
#include <cstdlib>

using std::shared_ptr;

void AICortex::dumpAttackState(const Cortex::CortexObservation& obs) const
{
	using namespace Cortex;
	auto& cerr = diagnosticStream;
	Cortex::WorldTeam* team = decisionPlayer->team;
	Cortex::World* game = team->game;
	const int me = team->teamNumber;

	cerr << "CORTEX_DUMP ==== first-under-attack snapshot ====\n";
	cerr << "CORTEX_DUMP team=" << me << " tick=" << obs.tick
	     << " (~" << (obs.tick / 25) << "s)"
	     << " buildingsUnderAttack=" << obs.buildingsUnderAttack
	     << " unitsUnderAttack=" << obs.unitsUnderAttack << "\n";

	// --- economy ---
	cerr << "CORTEX_DUMP ECON totalUnit=" << obs.totalUnit
	     << " workers=" << obs.workers
	     << " explorers=" << obs.explorers
	     << " warriors=" << obs.warriors
	     << " freeWorkers=" << obs.freeWorkers
	     << " feedCapacity=" << obs.feedCapacity
	     << " needFoodCrit=" << obs.needFoodCritical
	     << " starving=" << obs.starvingUnits
	     << " needHeal=" << obs.needHeal << "\n";

	// --- buildings (finished / sites) ---
	cerr << "CORTEX_DUMP BUILD totalBuilding=" << obs.totalBuilding
	     << " swarms=" << cortexFinishedBuildings(obs, CORTEX_BUILD_SWARM)
	     << "(+" << cortexBuildingSites(obs, CORTEX_BUILD_SWARM) << "site)"
	     << " inns=" << cortexFinishedBuildings(obs, CORTEX_BUILD_FOOD)
	     << "(+" << cortexBuildingSites(obs, CORTEX_BUILD_FOOD) << "site)"
	     << " hospital=" << cortexFinishedBuildings(obs, CORTEX_BUILD_HEAL)
	     << "(+" << cortexBuildingSites(obs, CORTEX_BUILD_HEAL) << "site)"
	     << " barracks=" << cortexFinishedBuildings(obs, CORTEX_BUILD_ATTACK)
	     << "(+" << cortexBuildingSites(obs, CORTEX_BUILD_ATTACK) << "site)"
	     << " school=" << cortexFinishedBuildings(obs, CORTEX_BUILD_SCIENCE)
	     << "(+" << cortexBuildingSites(obs, CORTEX_BUILD_SCIENCE) << "site)\n";

	// --- production / combat readiness ---
	cerr << "CORTEX_DUMP PROD swarmCount=" << obs.swarmCount
	     << " innCount=" << obs.innCount
	     << " swarmsProducing=" << obs.swarmsProducing
	     << " producingWarrior=" << obs.swarmsProducingWarrior
	     << " producingExplorer=" << obs.swarmsProducingExplorer
	     << " producingWorker=" << obs.swarmsProducingWorker
	     << " maxBuildLevel=" << obs.maxBuildLevel
	     << " warFlagsActive=" << obs.warFlagsActive << "\n";

	// per-swarm WHEAT buffer / workers — shows whether the economy loop has stalled.
	for (int i = 0; i < obs.swarmCount && i < CORTEX_MAX_TRACKED_SWARMS; i++)
	{
		const TrackedBuilding& s = obs.trackedSwarms[i];
		if (!s.valid) continue;
		cerr << "CORTEX_DUMP   swarm[" << i << "] wheat=" << s.wheat << "/" << s.maxWheat
		     << " maxUnitWorking=" << s.maxUnitWorking
		     << " inside=" << s.unitsInside
		     << " priority=" << s.priority
		     << " nearestWheat=" << s.nearestWheatDist
		     << " harvestable=" << s.harvestableWheatNearby << "\n";
	}

	// per-inn wheat-gate detail (DIAGNOSTIC: feedCap root-cause). feedCapacity sums
	// only inns that pass the gate (harvestable >= CORTEX_WHEAT_MIN_TILES). nearestWheat
	// is forbidden-BLIND; harvestable is the forbidden-AWARE gate count. wheat-present
	// (nearestWheat small) but gate-fail (harvestable < MIN) => wheat is FORBIDDEN (b);
	// nearestWheat large/-1 => wheat DEPLETED/ABSENT (c).
	for (int i = 0; i < obs.innCount && i < CORTEX_MAX_TRACKED_INNS; i++)
	{
		const TrackedBuilding& n = obs.trackedInns[i];
		if (!n.valid) continue;
		const bool feeds = (n.harvestableWheatNearby >= CORTEX_WHEAT_MIN_TILES);
		cerr << "CORTEX_DUMP   inn[" << i << "] wheat=" << n.wheat << "/" << n.maxWheat
		     << " maxUnitWorking=" << n.maxUnitWorking
		     << " inside=" << n.unitsInside << "/" << n.maxUnitInside
		     << " nearestWheat=" << n.nearestWheatDist
		     << " harvestable=" << n.harvestableWheatNearby
		     << " feedsGate=" << (feeds ? 1 : 0) << "\n";
	}

	// --- placement: can the policy even site a new inn / swarm right now? ---
	{
		const int types[2] = { CORTEX_BUILD_FOOD, CORTEX_BUILD_SWARM };
		const char* names[2] = { "inn", "swarm" };
		for (int ti = 0; ti < 2; ti++)
		{
			int valid = 0, bestWheat = -1, bx = -1, by = -1;
			for (int c = 0; c < CORTEX_BUILD_CANDIDATES; c++)
			{
				const BuildCandidate& cand = obs.buildCandidates[types[ti]][c];
				if (!cand.valid) continue;
				if (valid == 0) { bestWheat = cand.wheatDist; bx = cand.x; by = cand.y; }
				valid++;
			}
			cerr << "CORTEX_DUMP PLACE " << names[ti] << " validCandidates=" << valid
			     << " bestWheatDist=" << bestWheat
			     << " at=(" << bx << "," << by << ")\n";
		}
	}

	// --- scouting: what Cortex has actually discovered (FOW-gated) ---
	int flagTargetsValid = 0;
	for (int i = 0; i < CORTEX_FLAG_TARGETS; i++)
		if (obs.flagTargets[i].valid) flagTargetsValid++;
	cerr << "CORTEX_DUMP SCOUT flagTargets(seen enemy buildings)=" << flagTargetsValid
	     << " enemyCount=" << obs.enemyCount << "\n";
	for (int i = 0; i < MAX_ENEMY_SLOTS; i++)
	{
		const EnemySlot& e = obs.enemies[i];
		if (!e.active) continue;
		cerr << "CORTEX_DUMP   enemy team=" << e.teamNumber
		     << " discoveredBuildings=" << e.totalBuilding
		     << " visibleUnits(inOurFOW)=" << e.totalUnit << "\n";
	}

	// --- ground truth (diagnostic only; never fed to the policy) ---
	for (int t = 0; t < game->teamsCount(); t++)
	{
		Cortex::WorldTeam* et = game->teams[t];
		if (!et || et->teamNumber == me) continue;
		const TeamStat* es = et->stats.getLatestStat();
		if (!es) continue;
		int as0 = es->upgradeState[ATTACK_STRENGTH][0];
		int as1 = es->upgradeState[ATTACK_STRENGTH][1];
		int as2 = es->upgradeState[ATTACK_STRENGTH][2];
		int as3 = es->upgradeState[ATTACK_STRENGTH][3];
		int sp0 = es->upgradeState[ATTACK_SPEED][0];
		int sp1 = es->upgradeState[ATTACK_SPEED][1];
		int sp2 = es->upgradeState[ATTACK_SPEED][2];
		int sp3 = es->upgradeState[ATTACK_SPEED][3];
		cerr << "CORTEX_DUMP TRUTH enemy team=" << et->teamNumber
		     << " totalUnit=" << es->totalUnit
		     << " warriors=" << es->numberUnitPerType[WARRIOR]
		     << " workers=" << es->numberUnitPerType[WORKER]
		     << " totalBuilding=" << es->totalBuilding
		     << " atkStrengthLvls=[" << as0 << "," << as1 << "," << as2 << "," << as3 << "]"
		     << " atkSpeedLvls=[" << sp0 << "," << sp1 << "," << sp2 << "," << sp3 << "]\n";
	}
	cerr << "CORTEX_DUMP ==== end snapshot ====" << std::endl;
}

// TRAINING TRACE for the ML worker-tuning pilot (docs/AI/cortex/PILOT.md). Appends
// one CSV row per valid tracked swarm to <prefix>.team<N>.csv, where <prefix> is
// GLOB2_CORTEX_TRACE. Each row is the swarm's observed state this decision cycle
// plus the cap the HAND RULE chose (the BC target). Pure read of obs + the tune
// action already computed for gameplay; buffering output never touches RNG,
// orders, or persisted state, so the lockstep sync stream is unaffected. One file
// per AI instance avoids interleaving and lets each write its own header once.
void AICortex::dumpWorkerTrace(const Cortex::CortexObservation& obs,
                               const Cortex::CortexAction& tune)
{
	using namespace Cortex;
	const int me = decisionPlayer->team->teamNumber;

	const char* prefix = getenv("GLOB2_CORTEX_TRACE");
	if (!prefix || !prefix[0]) return;
	const std::string path = std::string(prefix) + ".team" + std::to_string(me) + ".csv";
	const std::string header = "tick,team,swarm_index,gid,wheat,maxWheat,maxUnitWorking,"
			           "unitsInside,maxUnitInside,nearestWheatDist,harvestableWheatNearby,"
			           "freeWorkers,totalFree,totalNeeded,workers,swarmCount,feedCapacity,"
			           "starvingUnits,needFood,maxBuildLevel,desired\n";

	const bool haveTune = (tune.kind == ACTION_TUNE_WORKERS);
	std::ostringstream row;
	for (int i = 0; i < obs.swarmCount && i < CORTEX_MAX_TRACKED_SWARMS; i++)
	{
		const TrackedBuilding& t = obs.trackedSwarms[i];
		if (!t.valid) continue;
		// The cap the hand rule chose this cycle: tune.swarmWorkers[i] when it set one
		// (>= 0), else the swarm is left unchanged at its current maxUnitWorking.
		const int desired = (haveTune && tune.swarmWorkers[i] >= 0)
		                  ? tune.swarmWorkers[i] : t.maxUnitWorking;
		row << obs.tick << ',' << me << ',' << i << ',' << t.gid << ','
		    << t.wheat << ',' << t.maxWheat << ',' << t.maxUnitWorking << ','
		    << t.unitsInside << ',' << t.maxUnitInside << ','
		    << t.nearestWheatDist << ',' << t.harvestableWheatNearby << ','
		    << obs.freeWorkers << ',' << obs.totalFree << ',' << obs.totalNeeded << ','
		    << obs.workers << ',' << obs.swarmCount << ',' << obs.feedCapacity << ','
		    << obs.starvingUnits << ',' << obs.needFood << ',' << obs.maxBuildLevel << ','
		    << desired << '\n';
	}
	const std::string text = row.str();
	if (!text.empty())
	{
		bufferedDiagnostics.push_back({path, header, text});
	}
}

// DECISION-SELECTION TRACE for the decide() ML pilot (docs/AI/cortex/DECIDE_CONTRACT.md).
// Appends ONE CSV row per decision cycle to <prefix>.team<N>.csv, where <prefix> is
// GLOB2_CORTEX_DECIDE_TRACE. Each row is: tick, team, the 48 decision features (in
// DECIDE_CONTRACT idx order, computed by CortexPolicy::extractDecideFeatures — the
// single source of truth the future inference path reuses), the per-cycle eligibility
// bitmask, the chosen class index, and the cycle's failed feasibility-gate bitmask
// (CortexGate bits — ANDed with a candidate's candidateGates[] mask this shows WHY a
// gated candidate was vetoed). Pure read of obs + the DecideTrace decide()
// already produced for gameplay; buffering output never touches RNG, orders, or
// persisted state, so the lockstep sync stream is unaffected. Separate output path from the worker trace (distinct CSV, distinct schema).
void AICortex::dumpDecideTrace(const Cortex::CortexObservation& obs,
                               const Cortex::DecideTrace& trace)
{
	using namespace Cortex;
	const int me = decisionPlayer->team->teamNumber;

	const char* prefix = getenv("GLOB2_CORTEX_DECIDE_TRACE");
	if (!prefix || !prefix[0]) return;
	const std::string path = std::string(prefix) + ".team" + std::to_string(me) + ".csv";
	const std::string header = "tick,team,"
			           "swarms,swarmSites,inns,innSites,school,schoolSites,race,raceSites,"
			           "heal,healSites,barracks,barracksSites,upgradableCount,totalUnit,"
			           "workers,explorers,warriors,freeWorkers,totalFree,totalNeeded,"
			           "fillableNeeded,unfillableNeeded,feedCapacity,needFood,starvingUnits,"
			           "maxBuildLevel,swarmCount,innCount,swarmsProducing,swarmsProducingWorker,"
			           "swarmsProducingWarrior,swarmsProducingExplorer,attackStrengthLevel,"
			           "walkLevel,buildLevel,buildingsUnderAttack,unitsUnderAttack,"
			           "warFlagsActive,enemyCount,enemyUnitsNearFlag,flagTargetsValid,"
			           "flagPosture,haveDefenseTarget,algaeReachable,algaeDiscovered,"
			           "swimLandReach,swimWaterReach,tick,"
			           "eligible_mask,chosen,failedGates\n";

	int features[CortexPolicy::NUM_DECIDE_FEATURES];
	CortexPolicy::extractDecideFeatures(obs, features);

	std::ostringstream row;
	row << obs.tick << ',' << me;
	for (int k = 0; k < CortexPolicy::NUM_DECIDE_FEATURES; k++)
		row << ',' << features[k];
	row << ',' << trace.eligibleMask << ',' << trace.chosen
	    << ',' << trace.failedGates << '\n';

	const std::string text = row.str();
	bufferedDiagnostics.push_back({path, header, text});
}

// INN DIAGNOSTIC TRACE (docs debugging Cortex-vs-Nicowar worker allocation to inns).
// The inn-side companion to dumpWorkerTrace: appends one CSV row per valid tracked
// inn to <prefix>.team<N>.csv, where <prefix> is GLOB2_CORTEX_INN_TRACE. Each row is
// the inn's observed state this decision cycle (wheat buffer, restock demand, the
// forbidden-blind/aware wheat diagnostics), the worker cap the tune action chose (the
// same `desired` convention as the swarm trace), plus colony-level context and the
// production-mix tier facts. The tiers are recomputed here via the pure
// CortexPolicy::computeFacts because getOrder() has no DecideFacts to pass through
// (decide() builds one internally) — re-deriving it is byte-identical and avoids
// duplicating the tier formula. Pure read of obs + the tune action already computed
// for gameplay; buffering output never touches RNG, orders, or persisted state,
// so the lockstep sync stream is unaffected. Separate output path from the worker/decision traces (distinct CSV, distinct schema).
void AICortex::dumpInnTrace(const Cortex::CortexObservation& obs,
                            const Cortex::CortexAction& tune)
{
	using namespace Cortex;
	const int me = decisionPlayer->team->teamNumber;

	const char* prefix = getenv("GLOB2_CORTEX_INN_TRACE");
	if (!prefix || !prefix[0]) return;
	const std::string path = std::string(prefix) + ".team" + std::to_string(me) + ".csv";
	const std::string header = "tick,team,inn_index,gid,wheat,maxWheat,maxUnitWorking,unitsInside,"
			           "maxUnitInside,nearestWheatDist,harvestableWheatNearby,"
			           "diagBlindWheatNearby,restockTripsNeeded,priority,ticksSinceFinished,"
			           "desired,freeWorkers,workers,warriors,totalUnit,feedCapacity,"
			           "starvingUnits,needFood,growWorker,growWarrior,tierBase,tierMid,"
			           "tierNeeds\n";

	// Recompute the production-mix tier facts (CortexPolicy.cpp:205-269) from the pure
	// computeFacts: tierBase = the hauler floor (Σ swarm+inn maxUnitWorking +
	// WORKER_TARGET_BUFFER, == f.workersNeeded), tierNeeds = full staffable demand
	// (obs.workers + fillableNeeded), tierMid = halfway between. growWorker/growWarrior
	// are the resulting production slices this cycle.
	const CortexPolicy::DecideFacts f = CortexPolicy::computeFacts(obs);
	const int tierBase  = f.workersNeeded;
	const int tierNeeds = obs.workers + f.fillableNeeded;
	const int tierMid   = tierBase + (tierNeeds - tierBase) / 2;

	const bool haveTune = (tune.kind == ACTION_TUNE_WORKERS);
	std::ostringstream row;
	for (int i = 0; i < obs.innCount && i < CORTEX_MAX_TRACKED_INNS; i++)
	{
		const TrackedBuilding& n = obs.trackedInns[i];
		if (!n.valid) continue;
		// The cap the tune chose this cycle: tune.innWorkers[i] when it set one (>= 0),
		// else the inn is left unchanged at its current maxUnitWorking.
		const int desired = (haveTune && tune.innWorkers[i] >= 0)
		                  ? tune.innWorkers[i] : n.maxUnitWorking;
		row << obs.tick << ',' << me << ',' << i << ',' << n.gid << ','
		    << n.wheat << ',' << n.maxWheat << ',' << n.maxUnitWorking << ','
		    << n.unitsInside << ',' << n.maxUnitInside << ','
		    << n.nearestWheatDist << ',' << n.harvestableWheatNearby << ','
		    << n.diagBlindWheatNearby << ',' << n.restockTripsNeeded << ','
		    << n.priority << ',' << n.ticksSinceFinished << ',' << desired << ','
		    << obs.freeWorkers << ',' << obs.workers << ',' << obs.warriors << ','
		    << obs.totalUnit << ',' << obs.feedCapacity << ',' << obs.starvingUnits << ','
		    << obs.needFood << ',' << f.growWorker << ',' << f.growWarrior << ','
		    << tierBase << ',' << tierMid << ',' << tierNeeds << '\n';
	}
	const std::string text = row.str();
	if (!text.empty())
	{
		bufferedDiagnostics.push_back({path, header, text});
	}
}
