// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "Material.h"
#include "AITelemetryFields.h"
#include "AIStateSerialization.h"
#include "AICortex.h"
#include "CortexSnapshotQueries.h"
#include "ai/observation/WorldQueries.h"
#include "CortexPlacement.h"
#include "ai/engine/AIDecision.h"
#include "ai/engine/AIOrderScheduler.h"
#include "CortexObservation.h"
#include "CortexFoodSources.h"

#include "Order.h"
#include "Version.h"
#include "AIRuleOrders.h"
#include "OrderMessages.h"
#include "Player.h"
#include "team/Team.h"
#include "CortexBuildings.h"
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
#include <string>
#include <cstdlib>
#include <stdexcept>

using std::shared_ptr;

AICortex::AICortex(Player* player)
{
	init(player);
}

AICortex::AICortex(GAGCore::InputStream* stream, Player* player, Sint32 versionMinor)
{
	init(player);
	if (!load(stream, player, versionMinor)) throw std::runtime_error("invalid Cortex execution state");
}

AICortex::~AICortex() = default;

void AICortex::init(Player* player)
{
	this->player = player;
	const auto& saved = player->game->gameHeader.getAIConfig(player->number);
	runtimeTuning = saved.empty() ? Cortex::cortexTuning() : Cortex::CortexTuning();
	std::string error;
	if (!saved.empty() && !Cortex::applyTuning(runtimeTuning, saved, error))
		throw std::runtime_error(error);
	player->game->gameHeader.setAIConfig(player->number, Cortex::tuningValues(runtimeTuning));
	timer = 0;
	for (int t = 0; t < Cortex::CORTEX_BUILDING_TYPES; t++)
		buildCooldownUntil[t] = 0; // per-type build cooldown; none pending at start.
	pendingUpgradeType = -1; // no upgrade in flight.
	pendingUpgradeUntil = 0;
	for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
	{
		offenseWaves[i].gid = NOGBID;       // no offense waves yet.
		offenseWaves[i].phase = WAVE_NONE;
		offenseWaves[i].phaseDeadline = 0;
		offenseWaves[i].landingX = -1;
		offenseWaves[i].landingY = -1;
		offenseWaves[i].musterBestArrived = 0;
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
	issuedCommands.clear();queuedCommands.clear();
	innFinishedTick.clear(); // Persisted settle clock, initially empty.
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
		offenseWaves[i].phase = stream->readSint32("phase");
		offenseWaves[i].phaseDeadline = stream->readSint32("phaseDeadline");
		offenseWaves[i].landingX = stream->readSint32("landingX");
		offenseWaves[i].landingY = stream->readSint32("landingY");
		offenseWaves[i].musterBestArrived = stream->readSint32("musterBestArrived");
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
	// Persisted, NOT redrawn on load: re-drawing would consume a fresh controller draw on
	// every load and desync replays. -1 means a pre-food save (or a game that has
	// not reached its first decision cycle yet) — getOrder draws it next cycle.
	wheatOpenMargin = stream->readSint32("wheatOpenMargin");
	if (versionMinor >= 101)
	{
		swarmKickstarted = stream->readUint8("swarmKickstarted");
		policy.expandWantStreak_ = stream->readSint32("expandWantStreak");
		stream->readEnterSection("unownedFlagSeen");
		const Uint32 unownedCount = stream->readCount("count");
		if (unownedCount > Building::MAX_COUNT) return false;
		unownedFlagSeen.clear();
		for (Uint32 i=0; i<unownedCount; ++i)
		{
			stream->readEnterSection(i);
			const Uint16 gid = stream->readUint16("gid");
			unownedFlagSeen[gid] = stream->readSint32("tick");
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		stream->readEnterSection("innFinishedTick");
		const Uint32 count = stream->readCount("count");
		if (count > Building::MAX_COUNT) return false;
		innFinishedTick.clear();
		for (Uint32 i=0; i<count; ++i)
		{
			stream->readEnterSection(i);
			const Uint16 gid = stream->readUint16("gid");
			innFinishedTick[gid] = stream->readSint32("tick");
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		stream->readEnterSection("orderQueue");
		const Uint32 orders = stream->readCount("count");
		if (orders > 65536) return false;
		while (!orderQueue.empty()) orderQueue.pop();
		for (Uint32 i=0; i<orders; ++i)
		{
			stream->readEnterSection(i);
			NetSendOrder envelope;
			envelope.setDecodeVersionMinor(versionMinor);
			envelope.decodeData(stream);
			if (!envelope.getOrder()) return false;
			AIStateSerialization::normalizeLegacyOrderStaffing(*player->game,*envelope.getOrder(),versionMinor);
			orderQueue.push(envelope.getOrder());
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}
	if (versionMinor >= AI_CORTEX_SAVE_FORMAT_POLICY_STATE)
	{
		const int mode=AIStateSerialization::readSint32(stream,"policyMode");
		if (mode < 0 || mode > 2) return false;
		policy.mlSwarmCaps_ = mode == 1;
		policy.mlDecide_ = mode == 2;
		if (mode) {
			GAGCore::BinaryInputStream::CheckedReads checked(stream);
			const auto size=stream->readUint32("policyBlobSize");
			if (size == 0 || size > 1024*1024) return false;
			std::vector<Uint8> blob(size);
			stream->read(blob.data(),size,"policyBlob");
			auto& net=mode == 1 ? policy.swarmNet_ : policy.decisionNet_;
			if (!net.loadFromMemory(blob.data(),blob.size(),
				mode == 1 ? Cortex::CortexNet::NUM_FEATURES : Cortex::CortexNet::NUM_DECIDE_FEATURES,
				mode == 1 ? Cortex::CortexNet::NUM_LOGITS : Cortex::CortexNet::NUM_DECIDE_LOGITS)) return false;
		}
	}
    issuedCommands.clear();queuedCommands.clear();
    if(versionMinor>=FILE_FORMAT_VERSION_AI_PIPELINE &&
        (!loadCommands(stream,issuedCommands,"issuedCommands",128) ||
         !loadCommands(stream,queuedCommands,"queuedCommands",65536))) return false;
	stream->readLeaveSection();
	return stream->isValid();
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
		stream->writeSint32(offenseWaves[i].phase, "phase");
		stream->writeSint32(offenseWaves[i].phaseDeadline, "phaseDeadline");
		stream->writeSint32(offenseWaves[i].landingX, "landingX");
		stream->writeSint32(offenseWaves[i].landingY, "landingY");
		stream->writeSint32(offenseWaves[i].musterBestArrived, "musterBestArrived");
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
	stream->writeUint8(swarmKickstarted, "swarmKickstarted");
	stream->writeSint32(policy.expandWantStreak_, "expandWantStreak");
	stream->writeEnterSection("unownedFlagSeen");
	stream->writeUint32(unownedFlagSeen.size(), "count");
	int flagIndex=0;
	for (const auto &entry : unownedFlagSeen)
	{
		stream->writeEnterSection(flagIndex++);
		stream->writeUint16(entry.first, "gid");
		stream->writeSint32(entry.second, "tick");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("innFinishedTick");
	stream->writeUint32(innFinishedTick.size(), "count");
	int index=0;
	for (const auto &entry : innFinishedTick)
	{
		stream->writeEnterSection(index++);
		stream->writeUint16(entry.first, "gid");
		stream->writeSint32(entry.second, "tick");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("orderQueue");
	stream->writeUint32(orderQueue.size(), "count");
	auto orders=orderQueue;
	index=0;
	while (!orders.empty())
	{
		stream->writeEnterSection(index++);
		NetSendOrder(orders.front()).encodeData(stream);
		orders.pop();
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	const int mode=policy.mlSwarmCaps_ ? 1 : policy.mlDecide_ ? 2 : 0;
	stream->writeSint32(mode,"policyMode");
	if (mode) {
		const auto blob=(mode == 1 ? policy.swarmNet_ : policy.decisionNet_).snapshotBlob();
		stream->writeUint32(blob.size(),"policyBlobSize");
		stream->write(blob.data(),blob.size(),"policyBlob");
	}
    saveCommands(stream,issuedCommands,"issuedCommands");
    saveCommands(stream,queuedCommands,"queuedCommands");
	stream->writeLeaveSection();
}

const AIEngine::BuildingView* AICortex::findUpgradeTarget(int buildingType) const
{
	// Scan our real buildings by ARRAY INDEX (myBuildings, never team->upgrade
	// or any std::set) so the selection is lockstep-deterministic. We keep the
	// single best instance whose building serves the requested role and that
	// passes the FULL engine Upgradable predicate — the same seven conditions the
	// observation's upgradableCount uses (CortexTypes.h:182-189), which are in
	// turn exactly what Building::launchConstruction's UPGRADE branch and the GUI
	// upgrade gate require:
	//   - buildingState == ALIVE                          (C++: Construction.cpp:95)
	//   - !type->isBuildingSite                           (C++: Construction.cpp:95)
	//   - hp == getEffectiveMaxHp()  (else launchConstruction REPAIRS, not upgrades)
	//                                                      (C++: Construction.cpp:97-108)
	//   - constructionResultState == NO_CONSTRUCTION (not already up/repairing)
	//   - type->nextLevel != BUILDING_LEVEL_NONE (not already at max level)
	//                                                      (C++: Construction.cpp:105, GameGUIInput.cpp:424)
	//   - worker construction qualification meets the target requiredWorkerLevel
	//   - isHardSpaceForBuildingSite(UPGRADE) (larger next-level footprint fits)
	//                                                      (C++: Construction.cpp:105, GameGUIInput.cpp:425)
	// If ANY condition fails the OrderConstruction would be silently dropped, so
	// only a fully-eligible instance is worth targeting.
	const AIEngine::TeamView* team = observedTeam;
	const int maxBuildLevel = Cortex::maxBuildLevel(*observedWorld,*team); // C++: team/TeamRouting.cpp:245-259

	const AIEngine::BuildingView* best = NULL;
	int bestLevel = 0;
	std::size_t bestDemand = 0;
	for (int i = 0; i < ::Building::MAX_COUNT; i++)
	{
		const AIEngine::BuildingView* b = observedWorld->buildingSlots(team->number)[i];
		if (b == NULL)
			continue;
		if (!Cortex::servesRole(*observedWorld, *Cortex::buildingType(*observedWorld,*b), buildingType))
			continue;
		// C++: Building::launchConstruction, building/Construction.cpp:93-108.
		if (b->buildingState != ::Building::ALIVE)
			continue;
		if (Cortex::buildingType(*observedWorld,*b)->isBuildingSite)
			continue;
		if (!observedWorld->isUpgradeAvailable(*b))
			continue;
		if (b->hp != b->maxHp)
			continue; // hp < hpMax would launch a REPAIR; > can't happen.
		if (b->constructionResultState != ::Building::NO_CONSTRUCTION)
			continue;
		if (maxBuildLevel < Cortex::catalogType(*observedWorld,Cortex::buildingType(*observedWorld,*b)->nextLevel)->semantics.requiredWorkerLevel)
			continue;
		if (!observedWorld->isHardSpaceForBuildingSite(*b, true)) // C++: building/Building.h:200
			continue;

		// Bottleneck ranking (deterministic — we deliberately do NOT mimic
		// Nicowar's random building pick from
		// ai/nicowar/Upgrade.cpp:141). Pick, in order:
		//   (1) LOWEST type->level — lift the most-behind building first, so the
		//       colony's weakest variant catches up before already-strong ones.
		//   (2) tie -> HIGHEST demand == unitsInside.size(): a building at capacity
		//       has units queued inside it (training/healing/feeding), so it is the
		//       real production bottleneck whose upgrade pays off most. unitsInside
		//       is a std::list, so we read .size().
		//   (3) tie -> first scan order (lowest array index, ~lowest gid) as the
		//       final deterministic tie-break. No random draw is needed
		//       since (1)+(2)+(3) totally order the candidates; if a future tie-break
		//       beyond index were ever wanted it must use the controller stream.
		const std::size_t demand = observedWorld->occupants(*b).size();
		bool better;
		if (best == NULL)
			better = true;
		else if (Cortex::buildingType(*observedWorld,*b)->level != bestLevel)
			better = (Cortex::buildingType(*observedWorld,*b)->level < bestLevel);
		else if (demand != bestDemand)
			better = (demand > bestDemand);
		else
			better = false; // equal rank -> keep the earlier (lower-index) instance.
		if (better)
		{
			best = b;
			bestLevel = Cortex::buildingType(*observedWorld,*b)->level;
			bestDemand = demand;
		}
	}
	return best;
}

shared_ptr<Order> AICortex::getOrder(void)
{
    const auto view = AIEngine::AIWorldView::capture(*player->game, AIEngine::AIWorldView::captureCatalog(*player->game));
    const std::vector<AIEngine::ExecutionReceipt> receipts;
    return runObservation(AIEngine::DecisionContext{*view, unsigned(player->number), unsigned(player->teamNumber), receipts}, false);
}
shared_ptr<Order> AICortex::getOrder(const AIEngine::DecisionContext& context)
{
    return runObservation(context, true);
}
shared_ptr<Order> AICortex::runObservation(const AIEngine::DecisionContext& context, bool worker)
{
    if(worker) applyReceipts(context);
    std::vector<std::vector<Uint8>> queuedBytes;
    std::queue<std::shared_ptr<Order>> validQueue;
    while(!orderQueue.empty()) {
        auto order=orderQueue.front();orderQueue.pop();
        auto bytes=AIEngine::Command::capture(*order,context.world).bytes;
        const auto intent=std::find_if(queuedCommands.begin(),queuedCommands.end(),[&](const auto& item){return item.bytes==bytes;});
        if(intent!=queuedCommands.end() && !intent->target.empty() && !context.world.building(intent->target)) {
            rejectIntent(*intent);queuedCommands.erase(intent);continue;
        }
        queuedBytes.push_back(std::move(bytes));validQueue.push(std::move(order));
    }
    orderQueue=std::move(validQueue);
    std::erase_if(queuedCommands,[&](const auto& item) {
        return std::find(queuedBytes.begin(),queuedBytes.end(),item.bytes)==queuedBytes.end();
    });
    // Idle polls advance the same cadence without preparing placement queries.
    // Widen only the predicate: the actual timer increment remains the one used
    // by decide(), including signed values restored from older saves.
    if(orderQueue.empty() && ((Sint64(timer)+1) % OBSERVE_INTERVAL)!=0) {
        policy.telemetry=telemetry;
        diagnosticStream.str({}); diagnosticStream.clear();
        ++timer;
        return std::make_shared<NullOrder>();
    }
    const auto& world=context.world;
    // Unwinding restores only the borrowed binding. Diagnostic allocation must
    // happen in ordinary control flow so failures reach the scheduler.
    struct Reset { AICortex& ai; ~Reset() noexcept { ai.observedWorld=nullptr; ai.observedTeam=nullptr; ai.intents.clear(); } } reset{*this};
    intents.clear();
    applyQueuedIntent(world);
    diagnosticStream.str({}); diagnosticStream.clear();
    observedWorld=&world; observedTeam=&world.teams[context.team]; observedPlayer=context.player;
    auto result=decide();
    if(result->getOrderType()!=ORDER_NULL) {
        if(worker) rememberIssued(*result,context,world);
        else {
            const auto bytes=AIEngine::Command::capture(*result,context.world).bytes;
            const auto emitted=std::find_if(queuedCommands.begin(),queuedCommands.end(),[&](const auto& item){return item.bytes==bytes;});
            if(emitted!=queuedCommands.end()) queuedCommands.erase(emitted);
        }
    }
    auto text = diagnosticStream.str();
    if (!text.empty()) bufferedDiagnostics.push_back({{}, {}, std::move(text)});
    diagnosticStream.str({}); diagnosticStream.clear();
    return result;
}
shared_ptr<Order> AICortex::decide()
{
	Cortex::TuningScope tuningScope(runtimeTuning);
	policy.telemetry = telemetry;
	// Drain any Orders queued by a prior decision cycle, one per tick.
	while (!orderQueue.empty() && !Cortex::permittedQueuedOrder(*observedWorld, *orderQueue.front())) orderQueue.pop();
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
		// Draw the food open-margin once from this controller's stream on its
		// first decision cycle. Saves retain both the margin and stream progress.
		if (wheatOpenMargin < 0)
		{
			const int span = Cortex::WHEAT_OPEN_MARGIN_MAX - Cortex::WHEAT_OPEN_MARGIN_MIN + 1;
			wheatOpenMargin = Cortex::WHEAT_OPEN_MARGIN_MIN
			                + static_cast<int>(random() % span);
		}

		// Pass the FIRST offense wave's flag gid: the observation captures that flag's
		// footprint for enemyUnitsNearFlag (scoreRetireFlag's straggler grace on the
		// primary push). The per-wave warrior counts the pipeline needs are computed
		// directly in the action layer (countWarriorsNear), not via the observation.
		Cortex::CortexObservation obs = Cortex::observeWorld(privateRandomEngine(), observedWorld,observedTeam,queryScratch,intents,&diagnosticStream, wheatOpenMargin, offenseWaves[0].gid);

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
			const int bid = ::Building::GIDtoID(static_cast<Uint16>(t0.gid));
			const AIEngine::BuildingView* b = observedWorld->buildingSlots(observedTeam->number)[bid];
			if (b && b->buildingState == ::Building::ALIVE && !Cortex::buildingType(*observedWorld,*b)->isBuildingSite
			 && Cortex::servesRole(*observedWorld, *Cortex::buildingType(*observedWorld,*b), Cortex::CORTEX_BUILD_SWARM))
			{
				Cortex::intentFor(intents,*b).workers = std::min(SWARM_START_WORKERS, int(Cortex::buildingType(*observedWorld,*b)->semantics.assignmentLimit));

				enqueueOrder(shared_ptr<Order>(
					new OrderModifyBuilding(b->gid, Cortex::plannedWorkers(intents,*b))));
				t0.maxUnitWorking = Cortex::plannedWorkers(intents,*b);
				swarmKickstarted = true;
			}
		}

		// DIAGNOSTIC (gated): compact per-decision-cycle econ trace, to watch the
		// economy ramp. Pure read → stderr; no RNG/order/state touched.
		if (getenv("CORTEX_DUMP_PERIODIC"))
		{
			using namespace Cortex;
			diagnosticStream << "CORTEX_TRACE t=" << obs.tick
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
			// Per-inn food-gate detail (feedCap root-cause). feedCapacity sums only
			// inns that pass the gate (harvestable >= CORTEX_WHEAT_MIN_TILES=5).
			// nearestWheat is forbidden-BLIND; harvestable is forbidden-AWARE. When
			// feedCap==0: food-present (nearestWheat small) + gate-fail => FORBIDDEN (b);
			// nearestWheat large/-1 => DEPLETED/ABSENT (c).
			for (int i = 0; i < obs.innCount && i < CORTEX_MAX_TRACKED_INNS; i++)
			{
				const Cortex::TrackedBuilding& n = obs.trackedInns[i];
				if (!n.valid) continue;
				diagnosticStream << "CORTEX_INN t=" << obs.tick << " inn=" << i
				          << " wheat=" << n.supplyStock << "/" << n.supplyCapacity
				          << " haulers=" << n.maxUnitWorking
				          << " restockReq=" << n.restockTripsNeeded
				          << " inside=" << n.unitsInside << "/" << n.maxUnitInside
				          << " nearestWheat=" << n.nearestFoodSourceDistance
				          << " blindWheat=" << n.unrestrictedFoodSourcesNearby
				          << " harvestable=" << n.harvestableFoodSourcesNearby
				          << " feedsGate=" << (n.harvestableFoodSourcesNearby >= CORTEX_WHEAT_MIN_TILES ? 1 : 0)
				          << "\n";
			}
			// Per-swarm food buffer + assigned haulers: contrast against the inns above to
			// see whether the scarce haulers are feeding PRODUCTION (swarm food full) while
			// the inns (FEEDING) sit empty.
			for (int i = 0; i < obs.swarmCount && i < CORTEX_MAX_TRACKED_SWARMS; i++)
			{
				const Cortex::TrackedBuilding& s = obs.trackedSwarms[i];
				if (!s.valid) continue;
				diagnosticStream << "CORTEX_SWARM t=" << obs.tick << " swarm=" << i
				          << " wheat=" << s.supplyStock << "/" << s.supplyCapacity
				          << " haulers=" << s.maxUnitWorking
				          << " prio=" << s.priority
				          << " harvestable=" << s.harvestableFoodSourcesNearby
				          << "\n";
			}
			// Direct engine-gradient probe per real inn: is COLLECTABLE (ripe, reachable)
			// food actually available at the inn? wheatAvail=0 with food tiles nearby ⇒ the
			// local food is unripe/over-harvested, not merely fogged — that is why
			// restockTripsNeeded computes 0 and the inn never refills.
			{
				const AIEngine::AIWorldView* g = observedWorld;
				const AIEngine::TeamView* tm = observedTeam;
				int innIdx = 0;
                // Diagnostics use published fields, or private initialization only.
                // They never enroll a new simulation gradient.
                AIEngine::ResourceInitializations diagnosticFields;
                AIEngine::WorldQueries diagnosticQueries(*g, tm->number, diagnosticFields);
				for (int b = 0; b < ::Building::MAX_COUNT; b++)
				{
					const AIEngine::BuildingView* bb = g->buildingSlots(tm->number)[b];
					if (bb == NULL || bb->buildingState == ::Building::DEAD)
						continue;
					if (!Cortex::servesRole(*observedWorld, *Cortex::buildingType(*observedWorld,*bb), Cortex::CORTEX_BUILD_FOOD))
						continue;
                    int rx=0,ry=0,distance=0;
                    const bool available=diagnosticQueries.resourceAvailableUpdate(tm->number,materialIndex(MaterialId::Food),0,Cortex::plannedX(intents,*bb),Cortex::plannedY(intents,*bb),&rx,&ry,&distance);
                    auto field=g->resourceGradient(tm->number,materialIndex(MaterialId::Food),0);
                    if(field.empty()) field=*diagnosticFields.at((tm->number*MaterialSlotCount+materialIndex(MaterialId::Food))*7).values;
                    const auto gradient=field[g->tileIndex(Cortex::plannedX(intents,*bb),Cortex::plannedY(intents,*bb))];
					diagnosticStream << "CORTEX_INNGRAD t=" << obs.tick << " inn=" << innIdx++
					          << " at=" << Cortex::plannedX(intents,*bb) << "," << Cortex::plannedY(intents,*bb)
					          << " wheat=" << observedWorld->buildingResources(*bb)[materialIndex(MaterialId::Food)] << "/" << Cortex::buildingType(*observedWorld,*bb)->maxMaterial[materialIndex(MaterialId::Food)]
					          << " wheatAvail=" << (available ? 1 : 0)
                              << " wheatGrad=" << int(gradient)
					          << "\n";
				}
			}
			// Ground-truth per-team snapshot (diagnostic only; never fed to the policy).
			// One line per team every decision cycle so training pace, food health, and
			// army size can be compared side-by-side against the opponent.
			{
				const AIEngine::AIWorldView* g = observedWorld;
				for (int t = 0; t < g->teams.size(); t++)
				{
					const AIEngine::TeamView* et = &g->teams[t];
					if (!et) continue;
					const TeamStat* es = &et->statistics;
					if (!es) continue;
					diagnosticStream << "CORTEX_TRUTH t=" << obs.tick
					          << " team=" << et->number
					          << (et->number == observedTeam->number ? " self" : " enemy")
					          << " u=" << es->totalUnit
					          << " W=" << es->numberUnitPerType[WORKER]
					          << " E=" << es->numberUnitPerType[EXPLORER]
					          << " A=" << es->numberUnitPerType[WARRIOR]
					          << " bld=" << es->totalBuilding
					          << " hp=" << es->totalHP
					          << " atkPow=" << es->totalAttackPower
					          << " food=" << es->totalFood << "/" << es->totalFoodCapacity
					          << " hungryNoInn=" << es->needFoodNoInns
					          << " starving=" << es->needFoodCritical
					          << " atkLvls=[" << es->upgradeState[ATTACK_STRENGTH][0]
					          << "," << es->upgradeState[ATTACK_STRENGTH][1]
					          << "," << es->upgradeState[ATTACK_STRENGTH][2]
					          << "," << es->upgradeState[ATTACK_STRENGTH][3] << "]"
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
			const AIEngine::AIWorldView* game = observedWorld;
			const AIEngine::TeamView* team = observedTeam;
			for (int i = 0; i < MAX_OFFENSE_FLAGS; i++)
			{
				const AIEngine::BuildingView* flag = findFlagByGid(offenseWaves[i].gid);
				if (flag == NULL)
					continue;
				const int phase = offenseWaves[i].phase;
				const char* phaseName = (phase == WAVE_MUSTER) ? "muster"
				                      : (phase == WAVE_CROSS)  ? "cross"
				                      : (phase == WAVE_ASSAULT) ? "assault" : "none";
				const int arrived = countArrivedAtFlag(flag);
				int n = 0, hungry = 0, damaged = 0, free = 0;
				long hpSum = 0, hungrySum = 0;
				int minHp = 1 << 30, minHungry = 1 << 30;
				for (const auto ref : observedWorld->workers(*flag))
				{
                    const auto* u=observedWorld->unit(ref);if(!u)continue;
					if (u == NULL)
						continue;
					n++;
					hpSum += u->hp;
					hungrySum += u->hungry;
					if (u->hp < minHp) minHp = u->hp;
					if (u->hungry < minHungry) minHungry = u->hungry;
					if (u->medical == ::Unit::MED_HUNGRY) hungry++;
					else if (u->medical == ::Unit::MED_DAMAGED) damaged++;
					else free++;
				}
				// Distance from the flag (the front) to the nearest own inn (food).
				int innDist = -1;
				for (int b = 0; b < ::Building::MAX_COUNT; b++)
				{
					const AIEngine::BuildingView* bb = observedWorld->buildingSlots(team->number)[b];
					if (bb == NULL || bb->buildingState == ::Building::DEAD)
						continue;
					if (!Cortex::servesRole(*observedWorld, *Cortex::buildingType(*observedWorld,*bb), Cortex::CORTEX_BUILD_FOOD))
						continue;
					int d = Cortex::warpDistMax(*game,Cortex::plannedX(intents,*flag), Cortex::plannedY(intents,*flag), Cortex::plannedX(intents,*bb), Cortex::plannedY(intents,*bb));
					if (innDist < 0 || d < innDist)
						innDist = d;
				}
				diagnosticStream << "CORTEX_OFF t=" << obs.tick << " wave=" << i
				          << " phase=" << phaseName
				          << " at=" << Cortex::plannedX(intents,*flag) << "," << Cortex::plannedY(intents,*flag);
				if (phase == WAVE_CROSS)
					diagnosticStream << " landing=" << offenseWaves[i].landingX << ","
					          << offenseWaves[i].landingY;
				diagnosticStream << " cohort=" << n << " arrived=" << arrived
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

		// Runtime the action layer's RAM-only offense-hold hysteresis state into the
		// observation so the PURE policy can make the hold-vs-recall (thrash-damper)
		// decision itself. AICortex still OWNS and mutates these members when it
		// actually places a flag (translateActionPlaceWarFlag re-arms offenseHoldUntil);
		// decide() only READS this mirror. Injected here, after observe() and before
		// decide(), exactly like wheatOpenMargin's per-game value is runtimeed in.
		obs.flagPosture = flagPosture;
		obs.offenseHoldUntil = offenseHoldUntil;

		// Monotone latch of the highest enemy-warrior ATTACK_STRENGTH level ever seen.
		// enemyWarriorLevelVisible is FOW-gated (-1 when no enemy warrior is in view), so
		// we only ever RAISE the persisted latch — a lull in visibility must not reset the
		// war-preparation level-match gate. AICortex OWNS and serializes enemyWarriorLevelSeen;
		// we runtime the current value into the observation each cycle for the PURE policy,
		// the same flagPosture runtime pattern used just above (injected after observe(),
		// before decide()).
		if (obs.enemyWarriorLevelVisible > enemyWarriorLevelSeen)
			enemyWarriorLevelSeen = obs.enemyWarriorLevelVisible;
		obs.enemyWarriorLevelLatched = enemyWarriorLevelSeen;

		// FORWARD-SITE UNDERWAY LATCH (position-tracked): AICortex OWNS and serializes
		// the position of the forward inn / hospital it last ORDERED (forwardInnX/Y,
		// forwardHealX/Y, set in translateActionBuildForward). Reconcile each tracked
		// pair against the live buildings and runtime obs.forwardInnUnderway/
		// forwardHealUnderway for the PURE policy — the flagPosture/latch runtime pattern,
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
			const int shortTypes[2] = { Cortex::CORTEX_BUILD_FOOD, Cortex::CORTEX_BUILD_HEAL };
			Sint32* underway[2]    = { &obs.forwardInnUnderway, &obs.forwardHealUnderway };
			for (int p = 0; p < 2; p++)
			{
				if (*trackX[p] < 0)
					continue; // nothing ordered for this slot.
				const AIEngine::BuildingView* found = NULL;
				for (int i = 0; i < ::Building::MAX_COUNT; i++)
				{
					const AIEngine::BuildingView* b = observedWorld->buildingSlots(observedTeam->number)[i];
					if (b == NULL || b->buildingState != ::Building::ALIVE)
						continue;
					if (!Cortex::servesRole(*observedWorld, *Cortex::buildingType(*observedWorld,*b), shortTypes[p]))
						continue;
					if (Cortex::plannedX(intents,*b) == *trackX[p] && Cortex::plannedY(intents,*b) == *trackY[p])
					{
						found = b;
						break;
					}
				}
				if (found != NULL)
				{
					if (Cortex::buildingType(*observedWorld,*found)->isBuildingSite)
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
		// binding) and runtimees obs.rangeGateWaived — 1 once the bind has outlived the
		// grace window — into the observation for the PURE policy (the flagPosture runtime
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
			diagnosticStream << "CORTEX_GATES t=" << obs.tick
			          << " latch=" << obs.enemyWarriorLevelLatched
			          << " visible=" << obs.enemyWarriorLevelVisible
			          << " ownStr=[" << obs.attackStrengthLevel[0] << "," << obs.attackStrengthLevel[1]
			          << "," << obs.attackStrengthLevel[2] << "," << obs.attackStrengthLevel[3] << "]"
			          << " range=" << Cortex::cortexAttackRange(obs)
			          << " inRangeSlot=" << Cortex::cortexInRangeTargetSlot(obs)
			          << " fwdInn=" << obs.forwardInn.valid << "/" << obs.forwardInnUnderway
			          << " fwdHeal=" << obs.forwardHeal.valid << "/" << obs.forwardHealUnderway
			          << " waived=" << obs.rangeGateWaived
			          << " freeWarriors=" << obs.freeWarriors
			          << " amphibious=" << obs.campaignAmphibious
			          << " landDist=" << obs.campaignLandDist
			          << " swimDist=" << obs.campaignSwimDist
			          << " swimWarriors=" << obs.swimWarriors
			          << " landing=" << (obs.landingZoneValid
			               ? (std::to_string(obs.landingZoneX) + "," + std::to_string(obs.landingZoneY))
			               : std::string("none"))
			          << " fwdRally=" << (obs.forwardRallyValid
			               ? (std::to_string(obs.forwardRallyX) + "," + std::to_string(obs.forwardRallyY))
			               : std::string("none"));
			for (int i = 0; i < Cortex::CORTEX_FLAG_TARGETS; i++)
				if (obs.flagTargets[i].valid)
					diagnosticStream << " tgt" << i << "=(" << obs.flagTargets[i].x << ","
					          << obs.flagTargets[i].y << ")d" << obs.flagTargetSupportDist[i];
			diagnosticStream << "\n";
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
		telemetry.set(AITrace::AI6::economy_selected_action, action.kind);
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
		telemetry.set(AITrace::AI6::combat_selected_action, combat.kind);
		translateAction(combat, obs);

		// Defense-flag teardown runs EVERY decision cycle, in PARALLEL with the action
		// ladder (not gated on winning it) — the assault is over the instant nothing of
		// ours is taking fire, but scoreDefense declines then so the ladder can't place
		// the teardown itself. Idempotent once the flag is gone.
		reconcileStaleDefenseFlag(obs);

		// Orphan-flag sweep runs LAST — after every flag-management pass above, so any
		// live slot that will latch its pending create already has. Deletes any own war
		// flag that no tracker owns and has stayed unowned across two decision cycles: a
		// flag whose slot was torn down (retire / defense-deficit release / no-target
		// stand-down / reconcileStaleDefenseFlag) while its OrderCreate was in flight,
		// which otherwise summons warriors forever with nothing to delete it.
		sweepOrphanWarFlags(obs);

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
		// Food-BLITZ takes precedence: during a famine (foodSaturated with a
		// committable army and a target) we LIFT all food protection for a one-time
		// food burst to fuel the attack. wantFoodSourceProtection returns false while
		// starving, so the two gates are mutually exclusive and the executor never
		// double-emits; blitz-lift wins when both could apply.
		if (policy.wantFoodBurstLift(obs))
			enqueueFoodSourcesForbidden(obs, /*liftAll=*/true);
		else if (policy.wantFoodSourceProtection(obs))
			enqueueFoodSourcesForbidden(obs);

		while (!orderQueue.empty() && !Cortex::permittedQueuedOrder(*observedWorld, *orderQueue.front())) orderQueue.pop();
		if (!orderQueue.empty())
		{
			shared_ptr<Order> order = orderQueue.front();
			orderQueue.pop();
			return order;
		}
	}

	return shared_ptr<Order>(new NullOrder());
}

void AICortex::applyQueuedIntent(const AIEngine::AIWorldView& world)
{
    const auto apply=[&](const std::shared_ptr<Order>& order,BuildingRef identity) {
        Uint16 gid=0xffff;
        switch(order->getOrderType())
        {
        case ORDER_MODIFY_BUILDING: gid=static_cast<OrderModifyBuilding&>(*order).gid;break;
        case ORDER_MODIFY_SWARM: gid=static_cast<OrderModifySwarm&>(*order).gid;break;
        case ORDER_CHANGE_PRIORITY: gid=static_cast<OrderChangePriority&>(*order).gid;break;
        case ORDER_MODIFY_MIN_LEVEL_TO_FLAG: gid=static_cast<OrderModifyMinLevelToFlag&>(*order).gid;break;
        case ORDER_MOVE_FLAG: gid=static_cast<OrderMoveFlag&>(*order).gid;break;
        default:return;
        }
        if(gid>=Building::MAX_COUNT*Team::MAX_COUNT || Building::GIDtoTeam(gid)>=world.teams.size())return;
        const auto* building=world.buildingAtSlot(gid);
        if(!building || (!identity.empty() && building->identity!=identity))return;
        auto& intent=Cortex::intentFor(intents,*building);
        switch(order->getOrderType())
        {
        case ORDER_MODIFY_BUILDING: intent.workers=static_cast<OrderModifyBuilding&>(*order).numberRequested;break;
        case ORDER_MODIFY_SWARM: {
            const auto& ratios=static_cast<OrderModifySwarm&>(*order).ratio;
            intent.ratios.emplace();std::copy_n(ratios,NB_UNIT_TYPE,intent.ratios->begin());break;
        }
        case ORDER_CHANGE_PRIORITY: intent.priority=static_cast<OrderChangePriority&>(*order).priority;break;
        case ORDER_MODIFY_MIN_LEVEL_TO_FLAG: intent.minLevel=static_cast<OrderModifyMinLevelToFlag&>(*order).minLevelToFlag;break;
        case ORDER_MOVE_FLAG: {
            const auto& move=static_cast<OrderMoveFlag&>(*order);
            intent.x=world.normalizeX(move.x);intent.y=world.normalizeY(move.y);break;
        }
        }
    };
    for(const auto& pending:issuedCommands) {
        if(pending.target.empty()) continue;
        const auto order=Order::getOrder(pending.bytes.data(),pending.bytes.size(),VERSION_MINOR);
        if(order) apply(order,pending.target);
    }
    auto queued=orderQueue;
    while(!queued.empty()) {apply(queued.front(),{});queued.pop();}
}

namespace {
std::vector<Uint8> cortexCommandBytes(Order& order)
{
    std::vector<Uint8> bytes{order.getOrderType()};
    if(order.getDataLength()) {
        const auto* data=order.getData();bytes.insert(bytes.end(),data,data+order.getDataLength());
    }
    return bytes;
}
}
void AICortex::enqueueOrder(std::shared_ptr<Order> order)
{
    if(observedWorld) {
        PendingCommand pending;const auto command=AIEngine::Command::capture(*order,*observedWorld);
        pending.bytes=command.bytes;if(command.target)pending.target=*command.target;
        queuedCommands.push_back(std::move(pending));
    }
    orderQueue.push(std::move(order));
}
void AICortex::rememberFlagCreation(Order& order,Sint32& cooldown)
{
    PendingCommand pending;pending.bytes=cortexCommandBytes(order);pending.flagCooldown=cooldown;
    for(int i=0;i<MAX_OFFENSE_FLAGS;++i)if(&cooldown==&offenseWaves[i].createCooldown) {
        pending.flagKind=1;pending.flagSlot=i;
    }
    for(int i=0;i<Cortex::CORTEX_MAX_DEFENSE_FLAGS;++i)if(&cooldown==&defenseFlags[i].createCooldown) {
        pending.flagKind=2;pending.flagSlot=i;
    }
    if(pending.flagKind) {
        const auto existing=std::find_if(queuedCommands.rbegin(),queuedCommands.rend(),[&](const auto& item){return item.bytes==pending.bytes;});
        if(existing!=queuedCommands.rend()) {
            existing->flagKind=pending.flagKind;existing->flagSlot=pending.flagSlot;existing->flagCooldown=pending.flagCooldown;
        } else queuedCommands.push_back(std::move(pending));
    }
}
void AICortex::rememberQueuedBuild(Order& order,int role)
{
    PendingCommand pending;pending.bytes=cortexCommandBytes(order);
    pending.buildClass=role;pending.buildCooldown=buildCooldownUntil[role];
    if(order.getOrderType()==ORDER_CONSTRUCTION) {
        pending.upgradeType=role;pending.upgradeUntil=pendingUpgradeUntil;
    }
    const auto existing=std::find_if(queuedCommands.rbegin(),queuedCommands.rend(),[&](const auto& item){return item.bytes==pending.bytes;});
    if(existing!=queuedCommands.rend()) {
        existing->buildClass=pending.buildClass;existing->buildCooldown=pending.buildCooldown;
        existing->upgradeType=pending.upgradeType;existing->upgradeUntil=pending.upgradeUntil;
    } else queuedCommands.push_back(std::move(pending));
}
void AICortex::rememberIssued(Order& order,const AIEngine::DecisionContext& context,const AIEngine::AIWorldView& world)
{
    if(issuedCommands.size()>=128) throw std::runtime_error("Cortex pending command limit exceeded");
    PendingCommand pending;pending.bytes=cortexCommandBytes(order);
    pending.observedTick=context.world.tick;pending.scheduledTick=context.scheduledTick;
    pending.sequence=context.pollSequence;
    const auto command=AIEngine::Command::capture(order,context.world);
    if(command.target) pending.target=*command.target;
    const auto flag=std::find_if(queuedCommands.begin(),queuedCommands.end(),[&](const auto& item) {
        return item.bytes==pending.bytes;
    });
    if(flag!=queuedCommands.end()) {
        if(!flag->target.empty()) pending.target=flag->target;
        pending.flagKind=flag->flagKind;pending.flagSlot=flag->flagSlot;pending.flagCooldown=flag->flagCooldown;
        pending.buildClass=flag->buildClass;pending.buildCooldown=flag->buildCooldown;
        pending.upgradeType=flag->upgradeType;pending.upgradeUntil=flag->upgradeUntil;
        queuedCommands.erase(flag);
    }
    const BuildingType* type=nullptr;
    if(dynamic_cast<const OrderConstruction*>(&order)) {
        const auto* building=context.world.building(pending.target);
        if(building) type=Cortex::catalogType(world,building->typeNum);
        if(pending.upgradeType<0 && type && pendingUpgradeType>=0 && Cortex::servesRole(world,*type,pendingUpgradeType)) {
            pending.upgradeType=pendingUpgradeType;pending.upgradeUntil=pendingUpgradeUntil;
        }
    }
    if(pending.buildClass<0 && pending.upgradeType>=0) {
        pending.buildClass=pending.upgradeType;pending.buildCooldown=buildCooldownUntil[pending.upgradeType];
    }
    issuedCommands.push_back(std::move(pending));
}
void AICortex::rejectIntent(const PendingCommand& command)
{
    // The request's stamp identifies this intent, even after a newer action has
    // reused its building gid or flag slot. Rejection never clears newer state.
    if(command.upgradeType>=0 && pendingUpgradeType==command.upgradeType && pendingUpgradeUntil==command.upgradeUntil) {
        pendingUpgradeType=-1;pendingUpgradeUntil=0;
    }
    if(command.buildClass>=0 && buildCooldownUntil[command.buildClass]==command.buildCooldown)
        buildCooldownUntil[command.buildClass]=0;
    if(command.flagKind==1 && offenseWaves[command.flagSlot].createCooldown==command.flagCooldown)
        offenseWaves[command.flagSlot].createCooldown=0;
    if(command.flagKind==2 && defenseFlags[command.flagSlot].createCooldown==command.flagCooldown)
        defenseFlags[command.flagSlot].createCooldown=0;
}
void AICortex::applyReceipts(const AIEngine::DecisionContext& context)
{
    for(const auto& receipt:context.receipts) {
        const auto found=std::find_if(issuedCommands.begin(),issuedCommands.end(),[&](const auto& pending) {
            return pending.observedTick==receipt.request.observedTick && pending.sequence==receipt.request.pollSequence
                && pending.scheduledTick==receipt.scheduledTick && pending.bytes==receipt.command;
        });
        if(found==issuedCommands.end()) continue;
        if(receipt.status!=AIEngine::ExecutionStatus::Accepted) {
            rejectIntent(*found);
        }
        issuedCommands.erase(found);
    }
}
void AICortex::saveCommands(GAGCore::OutputStream* stream,const std::vector<PendingCommand>& commands,const char* name) const
{
    stream->writeEnterSection(name);stream->writeUint32(commands.size(),"count");
    for(unsigned i=0;i<commands.size();++i) {
        stream->writeEnterSection(i);const auto& command=commands[i];
        stream->writeUint32(command.bytes.size(),"size");stream->write(command.bytes.data(),command.bytes.size(),"bytes");
        stream->writeUint16(command.target.gid,"gid");stream->writeUint32(command.target.generation,"generation");
        stream->writeUint32(command.observedTick,"observedTick");stream->writeUint32(command.scheduledTick,"scheduledTick");
        stream->writeUint32(Uint32(command.sequence),"sequenceLow");stream->writeUint32(Uint32(command.sequence>>32),"sequenceHigh");
        stream->writeSint32(command.upgradeType,"upgradeType");stream->writeSint32(command.upgradeUntil,"upgradeUntil");
        stream->writeSint32(command.buildClass,"buildClass");stream->writeSint32(command.buildCooldown,"buildCooldown");
        stream->writeSint32(command.flagKind,"flagKind");stream->writeSint32(command.flagSlot,"flagSlot");stream->writeSint32(command.flagCooldown,"flagCooldown");
        stream->writeLeaveSection();
    }
    stream->writeLeaveSection();
}
bool AICortex::loadCommands(GAGCore::InputStream* stream,std::vector<PendingCommand>& commands,const char* name,unsigned limit)
{
    GAGCore::BinaryInputStream::CheckedReads checked(stream);
    stream->readEnterSection(name);
    const auto count=stream->readCount("count",limit);
    std::vector<PendingCommand> restored;restored.reserve(count);
    size_t totalBytes=0;
    for(unsigned i=0;i<count;++i) {
        stream->readEnterSection(i);PendingCommand command;
        const auto size=stream->readCount("size",4*1024*1024);
        if(!size || size>16*1024*1024-totalBytes)return false;
        totalBytes+=size;
        command.bytes.resize(size);stream->read(command.bytes.data(),size,"bytes");
        if(!Order::getOrder(command.bytes.data(),command.bytes.size(),VERSION_MINOR))return false;
        command.target.gid=stream->readUint16("gid");command.target.generation=stream->readUint32("generation");
        if(!command.target.empty() && Building::GIDtoTeam(command.target.gid)!=player->teamNumber)return false;
        command.observedTick=stream->readUint32("observedTick");command.scheduledTick=stream->readUint32("scheduledTick");
        const auto low=stream->readUint32("sequenceLow"),high=stream->readUint32("sequenceHigh");
        command.sequence=Uint64(low)|(Uint64(high)<<32);
        command.upgradeType=AIStateSerialization::readSint32(stream,"upgradeType");command.upgradeUntil=AIStateSerialization::readSint32(stream,"upgradeUntil");
        command.buildClass=AIStateSerialization::readSint32(stream,"buildClass");command.buildCooldown=AIStateSerialization::readSint32(stream,"buildCooldown");
        command.flagKind=AIStateSerialization::readSint32(stream,"flagKind");command.flagSlot=AIStateSerialization::readSint32(stream,"flagSlot");command.flagCooldown=AIStateSerialization::readSint32(stream,"flagCooldown");
        if(command.upgradeType < -1 || command.upgradeType>=Cortex::CORTEX_BUILDING_TYPES || command.buildClass < -1 || command.buildClass>=Cortex::CORTEX_BUILDING_TYPES
            || command.flagKind<0 || command.flagKind>2 || command.flagSlot < -1
            || (command.flagKind==1 && (command.flagSlot<0 || command.flagSlot>=MAX_OFFENSE_FLAGS))
            || (command.flagKind==2 && (command.flagSlot<0 || command.flagSlot>=Cortex::CORTEX_MAX_DEFENSE_FLAGS)))return false;
        restored.push_back(std::move(command));stream->readLeaveSection();
    }
    stream->readLeaveSection();commands=std::move(restored);return stream->isValid();
}

std::optional<Uint64> AICortex::retainedQueryVectorBytes() const
{
    // Query scratch is controller-owned; the remaining retained vectors
    // are bounded delayed-intent ledgers (model weights are excluded).
    Uint64 bytes=queryScratch.retainedVectorBytes()+Uint64(intents.capacity())*sizeof(Cortex::BuildingIntent)
        +(issuedCommands.capacity()+queuedCommands.capacity())*sizeof(PendingCommand);
    for(const auto* commands:{&issuedCommands,&queuedCommands})
        for(const auto& command:*commands) bytes+=command.bytes.capacity()*sizeof(Uint8);
    return bytes;
}
