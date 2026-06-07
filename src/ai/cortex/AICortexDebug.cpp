// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "AICortex.h"
#include "CortexObservation.h"
#include "CortexWheat.h"

#include "Order.h"
#include "Player.h"
#include "team/Team.h"
#include "GlobalContainer.h"
#include "Settings.h"
#include "IntBuildingType.h"
#include "BuildingType.h"
#include "building/Building.h"
#include "unit/UnitConsts.h"
#include "Game.h"
#include "map/Map.h"
#include "Brush.h"
#include "Utilities.h"
#include "TeamStat.h"
#include <Stream.h>
#include <iostream>
#include <cstdlib>

using std::shared_ptr;

void AICortex::dumpAttackState(const Cortex::CortexObservation& obs) const
{
	using namespace Cortex;
	using std::cerr;
	Team* team = player->team;
	Game* game = team->game;
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

	// per-swarm CORN buffer / workers — shows whether the economy loop has stalled.
	for (int i = 0; i < obs.swarmCount && i < CORTEX_MAX_TRACKED_SWARMS; i++)
	{
		const TrackedBuilding& s = obs.trackedSwarms[i];
		if (!s.valid) continue;
		cerr << "CORTEX_DUMP   swarm[" << i << "] corn=" << s.corn << "/" << s.maxCorn
		     << " maxUnitWorking=" << s.maxUnitWorking
		     << " inside=" << s.unitsInside
		     << " priority=" << s.priority
		     << " nearestWheat=" << s.nearestWheatDist << "\n";
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
		Team* et = game->teams[t];
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
