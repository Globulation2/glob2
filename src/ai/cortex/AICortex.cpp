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

// Map distance (warp-safe) beyond which an existing war flag is recalled to a
// new target rather than left in place. Small slack so a flag already sitting on
// (or right next to) the target isn't pointlessly re-ordered every cycle.
static const int FLAG_MOVE_THRESHOLD = 3;

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
}

void AICortex::init(Player* player)
{
	this->player = player;
	timer = 0;
	for (int t = 0; t < Cortex::CORTEX_BUILDING_TYPES; t++)
		buildCooldownUntil[t] = 0; // per-type build cooldown; none pending at start.
	pendingUpgradeType = -1; // no upgrade in flight.
	pendingUpgradeUntil = 0;
	flagCooldownUntil = 0;
	flagPosture = POSTURE_NONE;
	offenseHoldUntil = 0;
	wheatOpenMargin = -1; // sentinel: drawn lazily on the first decision cycle.
	attackDumped = false; // diagnostic one-shot; never serialized.
	traceFile = nullptr; // gated ML training trace; lazily opened, never serialized.
	traceOpenAttempted = false; // open the trace at most once, even if it fails.
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
	flagCooldownUntil = stream->readSint32("flagCooldownUntil");
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
	stream->writeSint32(flagCooldownUntil, "flagCooldownUntil");
	stream->writeSint32(flagPosture, "flagPosture");
	stream->writeSint32(offenseHoldUntil, "offenseHoldUntil");
	stream->writeSint32(wheatOpenMargin, "wheatOpenMargin");
	stream->writeLeaveSection();
}

Building* AICortex::findOwnWarFlag() const
{
	// War flags are VIRTUAL buildings: they live in team->virtualBuildings, not
	// team->myBuildings (which holds only real, map-footprint buildings). The
	// container is a std::list — iterate it in insertion order (deterministic);
	// never a std::set. Cortex keeps at most one war flag, so return the first.
	Team* team = player->team;
	for (Building* b : team->virtualBuildings)
	{
		if (b
		    && b->type->shortTypeNum == IntBuildingType::WAR_FLAG
		    && b->buildingState == Building::ALIVE)
			return b;
	}
	return NULL;
}

void AICortex::ensureWarFlagAt(int tx, int ty, const Cortex::CortexAction& action, const Cortex::CortexObservation& obs)
{
	// Clamp the discrete action params to Cortex's own bounds (the engine clamps
	// nothing on its own). radius == flag unitStayRange, count == warriors summoned.
	int radius = action.flagRadius;
	if (radius < 1)
		radius = 1;
	else if (radius > Cortex::CORTEX_MAX_FLAG_RADIUS)
		radius = Cortex::CORTEX_MAX_FLAG_RADIUS;
	int count = action.unitCount;
	if (count < 0)
		count = 0;
	else if (count > Cortex::CORTEX_MAX_FLAG_UNITS)
		count = Cortex::CORTEX_MAX_FLAG_UNITS;

	Game* game = player->team->game;

	Building* existing = findOwnWarFlag();
	if (existing == NULL)
	{
		// No flag yet: create one. An OrderCreate takes several ticks to execute
		// and register the virtual building; without a cooldown the policy re-issues
		// the create before the flag appears. This is the flag's OWN cooldown, NOT
		// the build cooldown — a queued economy build must never delay a flag (and
		// especially not a defensive recall) by up to BUILD_COOLDOWN_TICKS.
		if (obs.tick < flagCooldownUntil)
			return;

		// Resolve WAR_FLAG the VIRTUAL way: flags have no building-site variant, so
		// pass isBuildingSite==false. (The economy path passes true, which returns
		// -1 for flags — that is exactly why the build path skips them.)
		const std::string& name = IntBuildingType::reverseConversionMap[IntBuildingType::WAR_FLAG];
		Sint32 typeNum = globalContainer->buildingsTypes.getTypeNum(name, 0, false);
		if (typeNum < 0)
			return;

		// Virtual-building room gate: for a flag this just checks no other own-flag
		// occupies the tile and ignores fog (checkFow defaulted true is harmless here).
		BuildingType* bt = globalContainer->buildingsTypes.get(typeNum);
		if (!game->checkRoomForBuilding(tx, ty, bt, player->team->teamNumber))
			return;

		// 7-arg OrderCreate: the trailing flagRadius sets the flag's unitStayRange at
		// execution (Game_orders.cpp executeCreate). For a flag, unitWorking ==
		// unitWorkingFuture == warriors summoned, so both are `count`.
		orderQueue.push(shared_ptr<Order>(new OrderCreate(
			player->team->teamNumber, tx, ty, typeNum,
			count, count, radius)));
		flagCooldownUntil = obs.tick + BUILD_COOLDOWN_TICKS;
		return;
	}

	// Flag already exists: move it only if it is far from the target; otherwise
	// leave it where it is. We intentionally do NOT re-issue radius/count changes
	// here for v1 — keeping the per-cycle work minimal (retargeting radius/count
	// for an in-place flag is deferred).
	int dist = game->map.warpDistMax(existing->posX, existing->posY, tx, ty);
	if (dist > FLAG_MOVE_THRESHOLD)
		orderQueue.push(shared_ptr<Order>(new OrderMoveFlag(existing->gid, tx, ty, false)));
}

void AICortex::clearOwnWarFlag()
{
	Building* existing = findOwnWarFlag();
	if (existing)
		orderQueue.push(shared_ptr<Order>(new OrderDelete(existing->gid)));
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

		Cortex::CortexObservation obs = Cortex::observe(player, wheatOpenMargin);

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
			          << " prod=" << obs.swarmsProducing << "/" << obs.swarmCount
			          << " maxBuildLvl=" << obs.maxBuildLevel
			          << " brkLvl=" << cortexMaxFinishedLevel(obs, CORTEX_BUILD_ATTACK)
			          << " schLvl=" << cortexMaxFinishedLevel(obs, CORTEX_BUILD_SCIENCE)
			          << " innLvl=" << cortexMaxFinishedLevel(obs, CORTEX_BUILD_FOOD)
			          << " upgBrk=" << obs.upgradableCount[CORTEX_BUILD_ATTACK]
			          << " brkUpgrading=" << cortexBuildingsUpgrading(obs, CORTEX_BUILD_ATTACK)
			          << " underAtk=" << (obs.buildingsUnderAttack + obs.unitsUnderAttack)
			          << "\n";
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

		Cortex::CortexAction action = policy.decide(obs);
		translateAction(action, obs);

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

		// Wheat-forbidden upkeep runs EVERY decision cycle, in PARALLEL with the
		// primary action above — it is not an ACTION_* the build/upgrade/offense
		// ladder could starve, nor does it consume the cycle's single action slot.
		// The policy still owns whether to paint (starving gate + real diff); when it
		// says yes we enqueue the full ADD/DEL paint here, alongside whatever orders
		// translateAction queued. They drain one-per-tick over the many ticks until
		// the next decision cycle, so both go out — they no longer compete for a turn.
		if (policy.wantWheatProtection(obs))
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
