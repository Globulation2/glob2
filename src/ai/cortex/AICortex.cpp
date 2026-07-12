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
#include "Ressource.h"
#include "unit/Unit.h"
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

AICortex::AICortex(Player* player)
{
	init(player);
}

AICortex::AICortex(GAGCore::InputStream* stream, Player* player, Sint32 versionMinor)
{
	init(player);
	load(stream, player, versionMinor);
}

AICortex::~AICortex()
{
	if (traceFile) // flush + release the gated training trace (RAM-only handle).
		std::fclose(traceFile);
	if (decideTraceFile) // flush + release the gated decision trace (RAM-only handle).
		std::fclose(decideTraceFile);
	if (innTraceFile) // flush + release the gated inn-diagnostic trace (RAM-only handle).
		std::fclose(innTraceFile);
}

void AICortex::init(Player* player)
{
	this->player = player;
	timer = 0;
	for (int t = 0; t < Cortex::CORTEX_BUILDING_TYPES; t++)
		buildCooldownUntil[t] = 0; // per-type build cooldown; none pending at start.
	pendingUpgradeType = -1; // no upgrade in flight.
	pendingUpgradeUntil = 0;
	for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
	{
		offenseWaves[i].gid = NOGBID;       // no offense waves yet.
		offenseWaves[i].musterUntil = 0;
		offenseWaves[i].createCooldown = 0;
	}
	for (int i = 0; i < Cortex::CORTEX_MAX_DEFENSE_FLAGS; i++)
	{
		defenseFlags[i].gid = NOGBID;       // no defense flags yet.
		defenseFlags[i].createCooldown = 0; // no defense create pending at start.
	}
	enemyWarriorLevelSeen = 0; // no enemy warrior sighted yet (monotone intel latch).
	forwardInnX = -1; // no forward inn ordered yet (position-tracked underway latch).
	forwardInnY = -1;
	forwardHealX = -1; // no forward hospital ordered yet.
	forwardHealY = -1;
	rangeGateBindingSince = 0; // the attack-range gate is not binding at start.
	flagPosture = POSTURE_NONE;
	offenseHoldUntil = 0;
	wheatOpenMargin = -1; // sentinel: drawn lazily on the first decision cycle.
	attackDumped = false; // diagnostic one-shot; never serialized.
	traceFile = nullptr; // gated ML training trace; lazily opened, never serialized.
	traceOpenAttempted = false; // open the trace at most once, even if it fails.
	decideTraceFile = nullptr; // gated ML decision trace; lazily opened, never serialized.
	decideTraceOpenAttempted = false; // open the decision trace at most once, even if it fails.
	innTraceFile = nullptr; // gated inn-diagnostic trace; lazily opened, never serialized.
	innTraceOpenAttempted = false; // open the inn trace at most once, even if it fails.
	innFinishedTick.clear(); // RAM-only inn settle clock; rebuilt as inns are seen.
	swarmKickstarted = false; // start-of-game swarm worker kickstart not yet done.
}

bool AICortex::load(GAGCore::InputStream* stream, Player* player, Sint32 versionMinor)
{
	this->player = player;
	stream->readEnterSection("AICortex");
	timer = stream->readUint32("timer");
	stream->readEnterSection("buildCooldownUntil");
	for (int t = 0; t < Cortex::CORTEX_BUILDING_TYPES; t++)
	{
		stream->readEnterSection(t);
		buildCooldownUntil[t] = stream->readSint32("buildCooldownUntil");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	pendingUpgradeType = stream->readSint32("pendingUpgradeType");
	pendingUpgradeUntil = stream->readSint32("pendingUpgradeUntil");
	stream->readEnterSection("offenseWaves");
	for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
	{
		stream->readEnterSection(i);
		offenseWaves[i].gid = static_cast<Uint16>(stream->readUint32("gid"));
		offenseWaves[i].musterUntil = stream->readSint32("musterUntil");
		offenseWaves[i].createCooldown = stream->readSint32("createCooldown");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readEnterSection("defenseFlags");
	for (int i = 0; i < Cortex::CORTEX_MAX_DEFENSE_FLAGS; i++)
	{
		stream->readEnterSection(i);
		defenseFlags[i].gid = static_cast<Uint16>(stream->readUint32("gid"));
		defenseFlags[i].createCooldown = stream->readSint32("createCooldown");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	enemyWarriorLevelSeen = stream->readSint32("enemyWarriorLevelSeen");
	forwardInnX = stream->readSint32("forwardInnX");
	forwardInnY = stream->readSint32("forwardInnY");
	forwardHealX = stream->readSint32("forwardHealX");
	forwardHealY = stream->readSint32("forwardHealY");
	rangeGateBindingSince = stream->readSint32("rangeGateBindingSince");
	flagPosture = stream->readSint32("flagPosture");
	offenseHoldUntil = stream->readSint32("offenseHoldUntil");
	// Persisted, NOT redrawn on load: re-drawing would consume a fresh syncRand on
	// every load and desync replays. -1 means a pre-wheat save (or a game that has
	// not reached its first decision cycle yet) — getOrder draws it next cycle.
	wheatOpenMargin = stream->readSint32("wheatOpenMargin");
	stream->readLeaveSection();
	// orderQueue is transient working state, not persisted; it refills on the
	// next decision cycle after load.
	return true;
}

void AICortex::save(GAGCore::OutputStream* stream)
{
	stream->writeEnterSection("AICortex");
	stream->writeUint32(timer, "timer");
	stream->writeEnterSection("buildCooldownUntil");
	for (int t = 0; t < Cortex::CORTEX_BUILDING_TYPES; t++)
	{
		stream->writeEnterSection(t);
		stream->writeSint32(buildCooldownUntil[t], "buildCooldownUntil");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeSint32(pendingUpgradeType, "pendingUpgradeType");
	stream->writeSint32(pendingUpgradeUntil, "pendingUpgradeUntil");
	stream->writeEnterSection("offenseWaves");
	for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(offenseWaves[i].gid, "gid");
		stream->writeSint32(offenseWaves[i].musterUntil, "musterUntil");
		stream->writeSint32(offenseWaves[i].createCooldown, "createCooldown");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("defenseFlags");
	for (int i = 0; i < Cortex::CORTEX_MAX_DEFENSE_FLAGS; i++)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(defenseFlags[i].gid, "gid");
		stream->writeSint32(defenseFlags[i].createCooldown, "createCooldown");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeSint32(enemyWarriorLevelSeen, "enemyWarriorLevelSeen");
	stream->writeSint32(forwardInnX, "forwardInnX");
	stream->writeSint32(forwardInnY, "forwardInnY");
	stream->writeSint32(forwardHealX, "forwardHealX");
	stream->writeSint32(forwardHealY, "forwardHealY");
	stream->writeSint32(rangeGateBindingSince, "rangeGateBindingSince");
	stream->writeSint32(flagPosture, "flagPosture");
	stream->writeSint32(offenseHoldUntil, "offenseHoldUntil");
	stream->writeSint32(wheatOpenMargin, "wheatOpenMargin");
	stream->writeLeaveSection();
}

Building* AICortex::findUpgradeTarget(int buildingType) const
{
	// Scan our real buildings by ARRAY INDEX (myBuildings, never team->upgrade
	// or any std::set) so the selection is lockstep-deterministic. We keep the
	// single best instance whose b->type->shortTypeNum == buildingType and that
	// passes the FULL engine Upgradable predicate — the same seven conditions the
	// observation's upgradableCount uses (CortexTypes.h:182-189), which are in
	// turn exactly what Building::launchConstruction's UPGRADE branch and the GUI
	// upgrade gate require:
	//   - buildingState == ALIVE                          (C++: Construction.cpp:95)
	//   - !type->isBuildingSite                           (C++: Construction.cpp:95)
	//   - hp == type->hpMax  (else launchConstruction REPAIRS, not upgrades)
	//                                                      (C++: Construction.cpp:97-108)
	//   - constructionResultState == NO_CONSTRUCTION (not already up/repairing)
	//   - type->nextLevel != BUILDING_LEVEL_NONE (not already at max level)
	//                                                      (C++: Construction.cpp:105, GameGUIInput.cpp:424)
	//   - team->maxBuildLevel() > type->level             (C++: GameGUIInput.cpp:426)
	//   - isHardSpaceForBuildingSite(UPGRADE) (larger next-level footprint fits)
	//                                                      (C++: Construction.cpp:105, GameGUIInput.cpp:425)
	// If ANY condition fails the OrderConstruction would be silently dropped, so
	// only a fully-eligible instance is worth targeting.
	Team* team = player->team;
	const int maxBuildLevel = team->maxBuildLevel(); // C++: team/TeamRouting.cpp:245-259

	Building* best = NULL;
	int bestLevel = 0;
	std::size_t bestDemand = 0;
	for (int i = 0; i < Building::MAX_COUNT; i++)
	{
		Building* b = team->myBuildings[i];
		if (b == NULL)
			continue;
		if (b->type->shortTypeNum != buildingType)
			continue;
		// C++: Building::launchConstruction, building/Construction.cpp:93-108.
		if (b->buildingState != Building::ALIVE)
			continue;
		if (b->type->isBuildingSite)
			continue;
		if (b->type->nextLevel == BUILDING_LEVEL_NONE)
			continue;
		if (b->hp != b->type->hpMax)
			continue; // hp < hpMax would launch a REPAIR; > can't happen.
		if (b->constructionResultState != Building::NO_CONSTRUCTION)
			continue;
		if (maxBuildLevel <= b->type->level) // C++: GameGUIInput.cpp:426 (> level)
			continue;
		if (!b->isHardSpaceForBuildingSite(Building::UPGRADE)) // C++: building/Building.h:200
			continue;

		// Bottleneck ranking (deterministic — we deliberately do NOT mimic
		// Nicowar's `syncRand() % buildings.size()` random pick from
		// ai/nicowar/Upgrade.cpp:141). Pick, in order:
		//   (1) LOWEST type->level — lift the most-behind building first, so the
		//       colony's weakest variant catches up before already-strong ones.
		//   (2) tie -> HIGHEST demand == unitsInside.size(): a building at capacity
		//       has units queued inside it (training/healing/feeding), so it is the
		//       real production bottleneck whose upgrade pays off most. unitsInside
		//       is a std::list, so we read .size().
		//   (3) tie -> first scan order (lowest array index, ~lowest gid) as the
		//       final fully-deterministic tie-break. No rand()/syncRand() is needed
		//       since (1)+(2)+(3) totally order the candidates; if a future tie-break
		//       beyond index were ever wanted it must use syncRand(), never rand().
		const std::size_t demand = b->unitsInside.size();
		bool better;
		if (best == NULL)
			better = true;
		else if (b->type->level != bestLevel)
			better = (b->type->level < bestLevel);
		else if (demand != bestDemand)
			better = (demand > bestDemand);
		else
			better = false; // equal rank -> keep the earlier (lower-index) instance.
		if (better)
		{
			best = b;
			bestLevel = b->type->level;
			bestDemand = demand;
		}
	}
	return best;
}

shared_ptr<Order> AICortex::getOrder(void)
{
	// Drain any Orders queued by a prior decision cycle, one per tick.
	if (!orderQueue.empty())
	{
		shared_ptr<Order> order = orderQueue.front();
		orderQueue.pop();
		return order;
	}

	// Run the observation -> policy -> action pipeline on a slow cadence.
	timer++;
	if ((timer % OBSERVE_INTERVAL) == 0)
	{
		// Draw the per-game wheat open-margin N exactly once, lazily, on the first
		// decision cycle — not in the constructor — so the sync RNG is live and the
		// draw lands at the same point in the shared stream on every client (all
		// clients run getOrder in lockstep). syncRand(), NEVER rand(): this value
		// must be identical across machines. The draw shifts the shared RNG stream,
		// so it is replay-relevant (validated against the deterministic harness).
		if (wheatOpenMargin < 0)
		{
			const int span = Cortex::WHEAT_OPEN_MARGIN_MAX - Cortex::WHEAT_OPEN_MARGIN_MIN + 1;
			wheatOpenMargin = Cortex::WHEAT_OPEN_MARGIN_MIN
			                + static_cast<int>(syncRand() % span);
		}

		// Pass the FIRST offense wave's flag gid: the observation captures that flag's
		// footprint for enemyUnitsNearFlag (scoreRetireFlag's straggler grace on the
		// primary push). The per-wave warrior counts the pipeline needs are computed
		// directly in the action layer (countWarriorsNear), not via the observation.
		Cortex::CortexObservation obs = Cortex::observe(player, wheatOpenMargin, offenseWaves[0].gid);

		// Stamp each tracked inn's post-build settle clock. The first cycle we see an
		// inn finished we record obs.tick; thereafter ticksSinceFinished is the age,
		// which the policy uses to suppress worker-tuning during the settle window.
		// Prune gids no longer present so a long game's map stays bounded (an inn that
		// died, or upgraded into a site and dropped out of the observation, is forgotten
		// — if it reappears finished it settles afresh, which is the intended behaviour).
		{
			std::map<Uint16, Sint32> stillAlive;
			for (int i = 0; i < obs.innCount; i++)
			{
				Cortex::TrackedBuilding& t = obs.trackedInns[i];
				if (!t.valid || t.gid < 0)
					continue;
				const Uint16 gid = static_cast<Uint16>(t.gid);
				std::map<Uint16, Sint32>::iterator it = innFinishedTick.find(gid);
				const Sint32 firstSeen = (it != innFinishedTick.end()) ? it->second
				                                                       : obs.tick;
				stillAlive[gid] = firstSeen;
				t.ticksSinceFinished = obs.tick - firstSeen;
			}
			innFinishedTick.swap(stillAlive);
		}

		// Start-of-game swarm kickstart: jump the pre-placed starting swarm straight
		// to SWARM_START_WORKERS haulers the first cycle we see it, so the early
		// worker economy ramps immediately instead of crawling up one hauler per
		// cycle from the map's arbitrary initial maxUnitWorking. One-shot. We mirror
		// the change into obs as well (and the engine executor pattern: local write +
		// Order) so the policy's worker-tuning loop tunes FROM this baseline this same
		// cycle rather than fighting it. trackedSwarms[0] is the primary/starting
		// swarm (observe fills by array index, lowest first).
		if (!swarmKickstarted && obs.swarmCount > 0 && obs.trackedSwarms[0].valid
		 && obs.trackedSwarms[0].maxUnitWorking != SWARM_START_WORKERS)
		{
			Cortex::TrackedBuilding& t0 = obs.trackedSwarms[0];
			const int bid = Building::GIDtoID(static_cast<Uint16>(t0.gid));
			Building* b = player->team->myBuildings[bid];
			if (b && b->buildingState == Building::ALIVE && !b->type->isBuildingSite
			 && b->type->shortTypeNum == IntBuildingType::SWARM_BUILDING)
			{
				b->maxUnitWorking = SWARM_START_WORKERS;
				b->update();
				orderQueue.push(shared_ptr<Order>(
					new OrderModifyBuilding(b->gid, SWARM_START_WORKERS)));
				t0.maxUnitWorking = SWARM_START_WORKERS;
				swarmKickstarted = true;
			}
		}

		// DIAGNOSTIC (gated): compact per-decision-cycle econ trace, to watch the
		// economy ramp. Pure read → stderr; no RNG/order/state touched.
		if (getenv("CORTEX_DUMP_PERIODIC"))
		{
			using namespace Cortex;
			std::cerr << "CORTEX_TRACE t=" << obs.tick
			          << " u=" << obs.totalUnit
			          << " W=" << obs.workers << " E=" << obs.explorers << " A=" << obs.warriors
			          << " freeW=" << obs.freeWorkers
			          << " swarm=" << cortexFinishedBuildings(obs, CORTEX_BUILD_SWARM)
			          << "/" << cortexBuildingSites(obs, CORTEX_BUILD_SWARM) << "s"
			          << " pool=" << cortexFinishedBuildings(obs, CORTEX_BUILD_SWIMSPEED)
			          << "/" << cortexBuildingSites(obs, CORTEX_BUILD_SWIMSPEED) << "s"
			          << " algae=" << obs.algaeDiscovered
			          << " reach=" << obs.swimLandReach << "/" << obs.swimWaterReach
			          << " race=" << cortexFinishedBuildings(obs, CORTEX_BUILD_WALKSPEED)
			          << " swarmCand=" << (obs.buildCandidates[CORTEX_BUILD_SWARM][0].valid ? 1 : 0)
			          << " inn=" << cortexFinishedBuildings(obs, CORTEX_BUILD_FOOD)
			          << "/" << cortexBuildingSites(obs, CORTEX_BUILD_FOOD) << "s"
			          << " brk=" << cortexFinishedBuildings(obs, CORTEX_BUILD_ATTACK)
			          << "/" << cortexBuildingSites(obs, CORTEX_BUILD_ATTACK) << "s"
			          << " sch=" << cortexFinishedBuildings(obs, CORTEX_BUILD_SCIENCE)
			          << "/" << cortexBuildingSites(obs, CORTEX_BUILD_SCIENCE) << "s"
			          << " hosp=" << cortexFinishedBuildings(obs, CORTEX_BUILD_HEAL)
			          << "/" << cortexBuildingSites(obs, CORTEX_BUILD_HEAL) << "s"
			          << " hospUpg=" << cortexBuildingsUpgrading(obs, CORTEX_BUILD_HEAL)
			          << " needHeal=" << obs.needHeal
			          << " feedCap=" << obs.feedCapacity
			          << " need=" << obs.totalNeeded << " totFree=" << obs.totalFree
			          << " prodW=" << obs.swarmsProducingWorker
			          << " prod=" << obs.swarmsProducing << "/" << obs.swarmCount
			          << " maxBuildLvl=" << obs.maxBuildLevel
			          << " brkLvl=" << cortexMaxFinishedLevel(obs, CORTEX_BUILD_ATTACK)
			          << " schLvl=" << cortexMaxFinishedLevel(obs, CORTEX_BUILD_SCIENCE)
			          << " innLvl=" << cortexMaxFinishedLevel(obs, CORTEX_BUILD_FOOD)
			          << " upgBrk=" << obs.upgradableCount[CORTEX_BUILD_ATTACK]
			          << " brkUpgrading=" << cortexBuildingsUpgrading(obs, CORTEX_BUILD_ATTACK)
			          << " underAtk=" << (obs.buildingsUnderAttack + obs.unitsUnderAttack)
			          << " starv=" << obs.starvingUnits
			          << "\n";
			// Per-inn wheat-gate detail (feedCap root-cause). feedCapacity sums only
			// inns that pass the gate (harvestable >= CORTEX_WHEAT_MIN_TILES=5).
			// nearestWheat is forbidden-BLIND; harvestable is forbidden-AWARE. When
			// feedCap==0: corn-present (nearestWheat small) + gate-fail => FORBIDDEN (b);
			// nearestWheat large/-1 => DEPLETED/ABSENT (c).
			for (int i = 0; i < obs.innCount && i < CORTEX_MAX_TRACKED_INNS; i++)
			{
				const Cortex::TrackedBuilding& n = obs.trackedInns[i];
				if (!n.valid) continue;
				std::cerr << "CORTEX_INN t=" << obs.tick << " inn=" << i
				          << " corn=" << n.corn << "/" << n.maxCorn
				          << " haulers=" << n.maxUnitWorking
				          << " restockReq=" << n.restockTripsNeeded
				          << " inside=" << n.unitsInside << "/" << n.maxUnitInside
				          << " nearestWheat=" << n.nearestWheatDist
				          << " blindCorn=" << n.diagBlindCornNearby
				          << " harvestable=" << n.harvestableWheatNearby
				          << " feedsGate=" << (n.harvestableWheatNearby >= CORTEX_WHEAT_MIN_TILES ? 1 : 0)
				          << "\n";
			}
			// Per-swarm corn buffer + assigned haulers: contrast against the inns above to
			// see whether the scarce haulers are feeding PRODUCTION (swarm corn full) while
			// the inns (FEEDING) sit empty.
			for (int i = 0; i < obs.swarmCount && i < CORTEX_MAX_TRACKED_SWARMS; i++)
			{
				const Cortex::TrackedBuilding& s = obs.trackedSwarms[i];
				if (!s.valid) continue;
				std::cerr << "CORTEX_SWARM t=" << obs.tick << " swarm=" << i
				          << " corn=" << s.corn << "/" << s.maxCorn
				          << " haulers=" << s.maxUnitWorking
				          << " prio=" << s.priority
				          << " harvestable=" << s.harvestableWheatNearby
				          << "\n";
			}
			// Direct engine-gradient probe per real inn: is COLLECTABLE (ripe, reachable)
			// corn actually available at the inn? cornAvail=0 with corn tiles nearby ⇒ the
			// local wheat is unripe/over-harvested, not merely fogged — that is why
			// restockTripsNeeded computes 0 and the inn never refills.
			{
				Game* g = player->team->game;
				Team* tm = player->team;
				int innIdx = 0;
				for (int b = 0; b < Building::MAX_COUNT; b++)
				{
					Building* bb = tm->myBuildings[b];
					if (bb == NULL || bb->buildingState == Building::DEAD)
						continue;
					if (bb->type->shortTypeNum != IntBuildingType::FOOD_BUILDING)
						continue;
					std::cerr << "CORTEX_INNGRAD t=" << obs.tick << " inn=" << innIdx++
					          << " at=" << bb->posX << "," << bb->posY
					          << " corn=" << bb->ressources[CORN] << "/" << bb->type->maxRessource[CORN]
					          << " cornAvail=" << (g->map.ressourceAvailable(tm->teamNumber, CORN, false, bb->posX, bb->posY) ? 1 : 0)
					          << " cornGrad=" << (int)g->map.getGradient(tm->teamNumber, CORN, false, bb->posX, bb->posY)
					          << "\n";
				}
			}
		}

		// DIAGNOSTIC (gated): per-offense-wave cohort medical/HP state. Answers "are the
		// DEPLOYED warriors starving in the field?" — walk each live offense flag's bound
		// cohort (unitsWorking) and tally medical state, HP, hunger, and arrival, plus the
		// flag's distance to the nearest own inn (food source). Pure read → stderr.
		if (getenv("CORTEX_DUMP_OFFENSE"))
		{
			Game* game = player->team->game;
			Team* team = player->team;
			for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
			{
				Building* flag = findFlagByGid(offenseWaves[i].gid);
				if (flag == NULL)
					continue;
				const bool mustering = offenseWaves[i].musterUntil != 0;
				int n = 0, hungry = 0, damaged = 0, free = 0, arrived = 0;
				long hpSum = 0, hungrySum = 0;
				int minHp = 1 << 30, minHungry = 1 << 30;
				for (Unit* u : flag->unitsWorking)
				{
					if (u == NULL)
						continue;
					n++;
					hpSum += u->hp;
					hungrySum += u->hungry;
					if (u->hp < minHp) minHp = u->hp;
					if (u->hungry < minHungry) minHungry = u->hungry;
					if (u->medical == Unit::MED_HUNGRY) hungry++;
					else if (u->medical == Unit::MED_DAMAGED) damaged++;
					else free++;
					if (game->map.warpDistMax(u->posX, u->posY, flag->posX, flag->posY)
					    <= flag->unitStayRange)
						arrived++;
				}
				// Distance from the flag (the front) to the nearest own inn (food).
				int innDist = -1;
				for (int b = 0; b < Building::MAX_COUNT; b++)
				{
					Building* bb = team->myBuildings[b];
					if (bb == NULL || bb->buildingState == Building::DEAD)
						continue;
					if (bb->type->shortTypeNum != IntBuildingType::FOOD_BUILDING)
						continue;
					int d = game->map.warpDistMax(flag->posX, flag->posY, bb->posX, bb->posY);
					if (innDist < 0 || d < innDist)
						innDist = d;
				}
				std::cerr << "CORTEX_OFF t=" << obs.tick << " wave=" << i
				          << " state=" << (mustering ? "muster" : "march")
				          << " at=" << flag->posX << "," << flag->posY
				          << " cohort=" << n << " arrived=" << arrived
				          << " free=" << free << " hungry=" << hungry << " damaged=" << damaged
				          << " avgHp=" << (n ? hpSum / n : 0) << " minHp=" << (n ? minHp : 0)
				          << " avgHungry=" << (n ? hungrySum / n : 0)
				          << " minHungry=" << (n ? minHungry : 0)
				          << " innDist=" << innDist
				          << "\n";
			}
		}

		// DIAGNOSTIC (gated, one-shot): characterize the game state the first cycle
		// the colony is under attack. Pure read → stderr; no RNG, no order, no
		// persisted state touched, so the sync stream is unaffected.
		if (!attackDumped && getenv("CORTEX_DUMP_ATTACK")
		    && (obs.buildingsUnderAttack > 0 || obs.unitsUnderAttack > 0))
		{
			dumpAttackState(obs);
			attackDumped = true;
		}

		// Release the one-upgrade-in-flight guard once the issued upgrade is visible
		// as a construction site (the policy's own cortexBuildingsUpgrading /
		// finished-count gates take over from here) or the safety timeout lapses.
		if (pendingUpgradeType >= 0
		 && (Cortex::cortexBuildingsUpgrading(obs, pendingUpgradeType) > 0
		     || obs.tick >= pendingUpgradeUntil))
		{
			pendingUpgradeType = -1;
			pendingUpgradeUntil = 0;
		}

		// Echo the action layer's RAM-only offense-hold hysteresis state into the
		// observation so the PURE policy can make the hold-vs-recall (thrash-damper)
		// decision itself. AICortex still OWNS and mutates these members when it
		// actually places a flag (translateActionPlaceWarFlag re-arms offenseHoldUntil);
		// decide() only READS this mirror. Injected here, after observe() and before
		// decide(), exactly like wheatOpenMargin's per-game value is echoed in.
		obs.flagPosture = flagPosture;
		obs.offenseHoldUntil = offenseHoldUntil;

		// Monotone latch of the highest enemy-warrior ATTACK_STRENGTH level ever seen.
		// enemyWarriorLevelVisible is FOW-gated (-1 when no enemy warrior is in view), so
		// we only ever RAISE the persisted latch — a lull in visibility must not reset the
		// war-preparation level-match gate. AICortex OWNS and serializes enemyWarriorLevelSeen;
		// we echo the current value into the observation each cycle for the PURE policy,
		// the same flagPosture echo pattern used just above (injected after observe(),
		// before decide()).
		if (obs.enemyWarriorLevelVisible > enemyWarriorLevelSeen)
			enemyWarriorLevelSeen = obs.enemyWarriorLevelVisible;
		obs.enemyWarriorLevelLatched = enemyWarriorLevelSeen;

		// FORWARD-SITE UNDERWAY LATCH (position-tracked): AICortex OWNS and serializes
		// the position of the forward inn / hospital it last ORDERED (forwardInnX/Y,
		// forwardHealX/Y, set in translateActionBuildForward). Reconcile each tracked
		// pair against the live buildings and echo obs.forwardInnUnderway/
		// forwardHealUnderway for the PURE policy — the flagPosture/latch echo pattern,
		// and the replacement for the old proximity scan (a tracked position never
		// false-positives on an unrelated economy food/heal site). A tracked site that
		// is a construction site marks underway; one that has FINISHED clears the pair
		// (the finished inn now opens the envelope via supportDist); one that never
		// appears holds underway while its build cooldown is still in flight, else
		// clears (order rejected or site destroyed).
		{
			Sint32* trackX[2]      = { &forwardInnX,  &forwardHealX };
			Sint32* trackY[2]      = { &forwardInnY,  &forwardHealY };
			const int types[2]     = { Cortex::CORTEX_BUILD_FOOD, Cortex::CORTEX_BUILD_HEAL };
			const int shortTypes[2] = { IntBuildingType::FOOD_BUILDING, IntBuildingType::HEAL_BUILDING };
			Sint32* underway[2]    = { &obs.forwardInnUnderway, &obs.forwardHealUnderway };
			for (int p = 0; p < 2; p++)
			{
				if (*trackX[p] < 0)
					continue; // nothing ordered for this slot.
				Building* found = NULL;
				for (int i = 0; i < Building::MAX_COUNT; i++)
				{
					Building* b = player->team->myBuildings[i];
					if (b == NULL || b->buildingState != Building::ALIVE)
						continue;
					if (b->type->shortTypeNum != shortTypes[p])
						continue;
					if (b->posX == *trackX[p] && b->posY == *trackY[p])
					{
						found = b;
						break;
					}
				}
				if (found != NULL)
				{
					if (found->type->isBuildingSite)
						*underway[p] = 1; // still building: don't order a second one.
					else
					{
						*trackX[p] = -1; // finished: the inn now opens the envelope.
						*trackY[p] = -1;
					}
				}
				else if (obs.tick < buildCooldownUntil[types[p]])
					*underway[p] = 1; // order still in flight (not yet a visible site).
				else
				{
					*trackX[p] = -1; // site destroyed or order rejected: stop tracking.
					*trackY[p] = -1;
				}
			}
		}

		// RANGE-GATE GRACE WAIVER: the offense range gate binds while the army wants to
		// attack but every known target sits outside the support envelope. AICortex OWNS
		// and serializes rangeGateBindingSince (the tick the bind began; 0 == not
		// binding) and echoes obs.rangeGateWaived — 1 once the bind has outlived the
		// grace window — into the observation for the PURE policy (the flagPosture echo
		// pattern). Past the grace, computeOffenseCommit attacks out-of-envelope anyway
		// while the forward base keeps building, so a never-ordered "possible" forward
		// base cannot hold the gate shut forever.
		{
			const bool binding = obs.flagTargets[0].valid && obs.warriors > 0
			                  && Cortex::cortexInRangeTargetSlot(obs) < 0;
			if (binding)
			{
				if (rangeGateBindingSince == 0)
					rangeGateBindingSince = obs.tick;
			}
			else
				rangeGateBindingSince = 0;
			const int grace = Cortex::cortexTuning().attackRangeGraceTicks;
			obs.rangeGateWaived = (grace > 0 && rangeGateBindingSince > 0
			                    && obs.tick - rangeGateBindingSince >= grace) ? 1 : 0;
			// FIRST-CONTACT WAIVER: flag targets ARE discovered enemy buildings
			// (placeFlagTargets gates on the same seenByMask predicate as the
			// totalBuilding intel), so the gate first binds the moment the FIRST
			// enemy building is discovered — and when that lone data point is out
			// of the envelope, holding fire for the full grace forfeits the first
			// strike against a still-unscouted colony (the diagnosed Muka residual:
			// the ungated build attacks at first contact and wins). Waive while at
			// most ONE enemy building is discovered; the normal bind-then-grace
			// behavior governs once the enemy base is actually mapped. (totalBuilding
			// counts LIVE discovered buildings, so razing them can re-arm the waiver
			// — acceptable: an enemy reduced to one known standing building is as
			// good as unscouted again.)
			if (binding && obs.rangeGateWaived == 0
			 && Cortex::cortexTuning().attackRangeUnscoutedWaiver != 0)
			{
				Sint32 discovered = 0;
				for (int i = 0; i < obs.enemyCount; i++)
					if (obs.enemies[i].active)
						discovered += obs.enemies[i].totalBuilding;
				if (discovered <= 1)
					obs.rangeGateWaived = 1;
			}
		}

		// DIAGNOSTIC (gated): per-cycle inputs of the v18 offense-commit gates (attack
		// range + war-prep level match), which decideCombat() consumes invisibly — the
		// decide trace only covers the economy argmax. Pure read → stderr; no RNG, no
		// order, no persisted state touched, so the sync stream is unaffected.
		if (getenv("CORTEX_DUMP_GATES"))
		{
			std::cerr << "CORTEX_GATES t=" << obs.tick
			          << " latch=" << obs.enemyWarriorLevelLatched
			          << " visible=" << obs.enemyWarriorLevelVisible
			          << " ownStr=[" << obs.attackStrengthLevel[0] << "," << obs.attackStrengthLevel[1]
			          << "," << obs.attackStrengthLevel[2] << "," << obs.attackStrengthLevel[3] << "]"
			          << " range=" << Cortex::cortexAttackRange(obs)
			          << " inRangeSlot=" << Cortex::cortexInRangeTargetSlot(obs)
			          << " fwdInn=" << obs.forwardInn.valid << "/" << obs.forwardInnUnderway
			          << " fwdHeal=" << obs.forwardHeal.valid << "/" << obs.forwardHealUnderway
			          << " waived=" << obs.rangeGateWaived
			          << " freeWarriors=" << obs.freeWarriors;
			for (int i = 0; i < Cortex::CORTEX_FLAG_TARGETS; i++)
				if (obs.flagTargets[i].valid)
					std::cerr << " tgt" << i << "=(" << obs.flagTargets[i].x << ","
					          << obs.flagTargets[i].y << ")d" << obs.flagTargetSupportDist[i];
			std::cerr << "\n";
		}

		// DECISION-SELECTION TRACE (gated): when GLOB2_CORTEX_DECIDE_TRACE is set,
		// ask decide() to fill the per-cycle eligibility mask + chosen class index
		// and record one CSV row. The trace is a pure read-out of the decision
		// decide() makes anyway — passing &trace does not change the action — so it
		// touches no RNG/order/sync state, like the worker trace. See DECIDE_CONTRACT.md.
		Cortex::CortexAction action;
		if (getenv("GLOB2_CORTEX_DECIDE_TRACE"))
		{
			Cortex::DecideTrace decideTrace;
			action = policy.decide(obs, &decideTrace);
			dumpDecideTrace(obs, decideTrace);
		}
		else
			action = policy.decide(obs);
		translateAction(action, obs);

		// War-flag management runs EVERY decision cycle, in PARALLEL with decide()'s
		// economy action above — NOT as a competing candidate in the same argmax.
		// Previously the three war-flag scorers (Defense / RetireFlag / Offense) shared
		// decide()'s single action slot with the whole economy/tech ladder, so a busy
		// economy could starve a flag move (and a flag move could steal the economy's
		// only slot). decideCombat() now owns the war-flag argmax and we emit its action
		// here, alongside the economy action: both sets of Orders queue and drain
		// one-per-tick over the ticks until the next decision cycle, so economy and
		// combat each get one decision per second. The combat-INTERNAL priority is
		// unchanged (serious-defense > blitz > defense > retire > offense — the SCORE_*
		// bands did not move); only the economy-vs-combat single-slot contention is
		// gone. ACTION_NOOP when no flag wants to move this cycle enqueues nothing.
		Cortex::CortexAction combat = policy.decideCombat(obs);
		translateAction(combat, obs);

		// Defense-flag teardown runs EVERY decision cycle, in PARALLEL with the action
		// ladder (not gated on winning it) — the assault is over the instant nothing of
		// ours is taking fire, but scoreDefense declines then so the ladder can't place
		// the teardown itself. Idempotent once the flag is gone.
		reconcileStaleDefenseFlag(obs);

		// Worker-hauling tuning (swarms / inns / construction sites) runs EVERY
		// decision cycle, in PARALLEL with the primary action above — it emits
		// OrderModifyBuilding worker-count changes, not an OrderCreate competing for
		// the build/upgrade ladder's single action slot, so keeping existing
		// buildings fed never preempts nor waits behind a build decision (and vice
		// versa). translateAction queues its OrderModifyBuildings alongside whatever
		// the primary action queued; they drain one-per-tick over the ticks until the
		// next decision cycle, so both go out. ACTION_NOOP (steady state, buffers in
		// the deadband) enqueues nothing.
		Cortex::CortexAction tune = policy.tuneWorkers(obs);
		translateAction(tune, obs);

		// TRAINING TRACE (gated): record this cycle's per-swarm (state, hand-action)
		// pairs for the ML worker-tuning pilot. Pure read of obs + the tune action we
		// just computed; writing a file touches no RNG/order/sync state. See
		// docs/AI/cortex/PILOT.md.
		if (getenv("GLOB2_CORTEX_TRACE"))
			dumpWorkerTrace(obs, tune);

		// INN DIAGNOSTIC TRACE (gated): the inn-side companion to the worker trace,
		// for debugging worker allocation to inns. Pure read of obs + the same tune
		// action; writing a file touches no RNG/order/sync state. See dumpInnTrace.
		if (getenv("GLOB2_CORTEX_INN_TRACE"))
			dumpInnTrace(obs, tune);

		// Wheat-forbidden upkeep runs EVERY decision cycle, in PARALLEL with the
		// primary action above — it is not an ACTION_* the build/upgrade/offense
		// ladder could starve, nor does it consume the cycle's single action slot.
		// The policy still owns whether to paint (starving gate + real diff); when it
		// says yes we enqueue the full ADD/DEL paint here, alongside whatever orders
		// translateAction queued. They drain one-per-tick over the many ticks until
		// the next decision cycle, so both go out — they no longer compete for a turn.
		// WHEAT-BLITZ takes precedence: during a famine (foodSaturated with a
		// committable army and a target) we LIFT all wheat protection for a one-time
		// food burst to fuel the attack. wantWheatProtection returns false while
		// starving, so the two gates are mutually exclusive and the executor never
		// double-emits; blitz-lift wins when both could apply.
		if (policy.wantWheatBlitzLift(obs))
			enqueueWheatForbidden(obs, /*liftAll=*/true);
		else if (policy.wantWheatProtection(obs))
			enqueueWheatForbidden(obs);

		if (!orderQueue.empty())
		{
			shared_ptr<Order> order = orderQueue.front();
			orderQueue.pop();
			return order;
		}
	}

	return shared_ptr<Order>(new NullOrder());
}
