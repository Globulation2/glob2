// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "AI.h"
#include "ai/engine/AIOrderScheduler.h"
#include "ai/engine/AIReceiptSerialization.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include "AIRuleOrders.h"
#include <cstdlib>
#include "AIJavaScript.h"
#include "AIMaxima.h"
#include "Player.h"
#include "Utilities.h"
#include "Game.h"
#include "Order.h"
#include "TeamStat.h"
#include <assert.h>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <Stream.h>
#include "FileFormatVersions.h"


#include "AINull.h"
#include "AINumbi.h"
#include "AICastor.h"
#include "AIWarrush.h"
#include "AINicowar.h"
#include "shared_runtime/Runtime.h"
#include "cortex/AICortex.h"
#include "AICabino.h"

using std::shared_ptr;

AI::AI(ImplementationID implementationID, Player *player)
{
	aiImplementation=NULL;
	
	switch (implementationID)
	{
		case NONE:
			aiImplementation=new AINull();
		break;
		case NUMBI:
			aiImplementation=new AINumbi(player);
		break;
		case CASTOR:
			aiImplementation=new AICastor(player);
		break;
		case NICOWAR:
			aiImplementation=new AISharedRuntime::Runtime(new NewNicowar, player);
		break;
		case WARRUSH:
			aiImplementation=new AIWarrush(player);
		break;
		case ECONO:
			aiImplementation=new AISharedRuntime::Runtime(new AISharedRuntime::Econo, player);
		break;
		case MAXIMA:
			aiImplementation=new AIMaxima::Maxima(player);
		break;
		case CORTEX:
			aiImplementation=new AICortex(player);
		break;
		case CABINO:
			aiImplementation=new Cabino::AICabino(player);
		break;
		case JAVASCRIPT:
			aiImplementation=new AIJavaScript(player);
		break;
		default:
			throw std::runtime_error("Unknown AI implementation");
	}

	this->implementationID=implementationID;
	this->player=player;
	assert(aiImplementation);
	aiImplementation->setRandomEngine(randomEngine);
}

AI::AI(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	aiImplementation=NULL;
	implementationID=NONE;
	this->player=player;
	try {
		if (!load(stream, versionMinor)) throw std::runtime_error("Invalid saved AI");
	} catch (...) { delete aiImplementation; aiImplementation=nullptr; throw; }
}

AI::~AI()
{
	if (aiImplementation)
		delete aiImplementation;
	aiImplementation=NULL;
}

namespace
{
// The opt-in test guard is read once; decisions run on worker threads.
bool ruleAuditEnabled()
{
	static const bool enabled = std::getenv("GLOB2_TEST_AI_RULE_AUDIT") != nullptr;
	return enabled;
}
}

std::shared_ptr<Order> AI::getOrder(bool paused)
{
	assert(player);
	if (paused || !player->team->isAlive)
		return shared_ptr<Order>(new NullOrder());
	assert(aiImplementation);
	initializeRandom();
	SyncRandScope randomScope(randomEngine);
	bindTelemetry();
	aiImplementation->telemetry.tick = player->game->stepCounter;
	aiImplementation->telemetry.count(AITelemetry::Polls);
	PerformanceTelemetry::Scope aiTime(
		PerformanceTelemetry::Id::AI,
		PerformanceTelemetry::collector().actor(player->number, player->team->teamNumber,
												implementationID, telemetrySeries->generation));
	// Loaded colonies can retain workers hauling training supplies even after
	// the strategy gates remove that investment. Release those assignments for
	// native controllers before planning; hospitals and barracks can still heal.
	std::shared_ptr<Order> order;
	if (implementationID!=JAVASCRIPT && player->game->gameHeader.isUnitUpgradesDisabled())
		for (int i=0;i<Building::MAX_COUNT;++i)
			if (auto* b=player->team->myBuildings[i]; b && !b->type->isBuildingSite
				&& AIRules::trainingBuilding(*b->type) && b->maxUnitWorking>0
				&& !AIRules::usefulWithoutTraining(*player->game,b->typeNum))
			{ order=std::make_shared<OrderModifyBuilding>(b->gid,0); break; }
	if (!order) order = aiImplementation->getOrder();
	// Qualification audits planning at selection time. A replay sees orders
	// after the network queue, when a repair may already have finished, and
	// cannot reliably distinguish that repair from an unavailable upgrade.
	// This opt-in test guard reports violations; it never filters an order.
	if (ruleAuditEnabled() &&
		!AIRules::permittedQueuedOrder(*player->game, *order))
		throw std::runtime_error("AI selected work unavailable under the match rules");
	aiTime.stop();
	const auto type = order->getOrderType();
	aiImplementation->telemetry.count(AITelemetry::OrderTypes + type);
	aiImplementation->telemetry.count(type == ORDER_NULL ? AITelemetry::NullOrders
														 : AITelemetry::Orders);
	telemetrySeries->current.tick = player->game->stepCounter;
	telemetrySeries->current.available = true;
	return order;
}

SimulationSnapshot::Requirements AI::observationRequirements() const
{
 auto required=aiImplementation->observationRequirements();
 if(implementationID!=JAVASCRIPT && player->game->gameHeader.isUnitUpgradesDisabled())
  required|=SimulationSnapshot::bit(SimulationSnapshot::Component::Catalogs)
   |SimulationSnapshot::bit(SimulationSnapshot::Component::Entities)
   |SimulationSnapshot::bit(SimulationSnapshot::Component::Rules);
 return required;
}
void AI::prepareDecision()
{
	if (!aiImplementation->supportsObservation()) throw std::logic_error("AI decision boundary audit incomplete");
	initializeRandom();
	bindTelemetry();
	if (!decisionTelemetry) { decisionTelemetry = std::make_unique<AITelemetry::Series>(*telemetrySeries); decisionTelemetry->history.clear(); }
}
AIEngine::Command AI::decide(const AIEngine::DecisionContext& context)
{
	if (!decisionTelemetry) throw std::logic_error("AI decision dispatch was not prepared by owner");
	SyncRandScope randomScope(randomEngine);
	auto& sink = aiImplementation->telemetry;
	sink.series = decisionTelemetry.get(); sink.tick = context.world.tick;
	sink.count(AITelemetry::Polls);
    // Wrapper commands have their own feedback lane. Controller receipts must
    // survive polls preempted by the training-release policy.
    if(wrapperGeneration!=context.controllerGeneration) {
        wrapperReleases.clear();deferredControllerReceipts.clear();
        wrapperGeneration=context.controllerGeneration;
    }
    for(const auto& receipt:context.receipts) {
        if(receipt.request.generation!=wrapperGeneration) continue;
        auto wrapper=std::find_if(wrapperReleases.begin(),wrapperReleases.end(),
            [&](const auto& release){return release.pollSequence==receipt.request.pollSequence;});
        if(wrapper!=wrapperReleases.end()) {
            if(receipt.selectedTarget!=wrapper->target) throw std::logic_error("AI wrapper receipt identity mismatch");
            wrapperReleases.erase(wrapper);
        } else if(!receipt.command.empty() && receipt.command.front()!=ORDER_NULL)
            deferredControllerReceipts.push_back(receipt);
    }
    if(deferredControllerReceipts.size()>9) throw std::logic_error("AI deferred receipt bound exceeded");
	std::shared_ptr<Order> order;
	// Preserve the existing native training-release policy using captured config
	// and catalog capabilities. No live rules helper is reachable here.
	if (implementationID != JAVASCRIPT && context.world.rules.upgradesDisabled)
		for (const auto& b : context.world.buildings)
		{
			if (b.team != int(context.team) || b.maxUnitWorking <= 0) continue;
			const auto& kind = context.world.catalog->at(b.typeNum);
			if (kind.site || !AIRules::trainingBuilding(kind.resolvedType)) continue;
			bool useful = false;
			for (unsigned i = 0; i < unsigned(AIPlanning::BuildingIntent::Count); ++i) {
				const auto intent = AIPlanning::BuildingIntent(i);
				if (AIPlanning::BuildingCapabilityIndex::trainingAbility(intent) < 0 && intent != AIPlanning::BuildingIntent::TrainConstruction)
					useful = useful || (kind.capabilityMask & (Uint64(1) << i));
			}
            if(!useful && std::none_of(wrapperReleases.begin(),wrapperReleases.end(),
                    [&](const auto& release){return release.target==b.identity;})) {
                order=std::make_shared<OrderModifyBuilding>(b.identity.gid,0);
                order->aiSelectedTarget=b.identity;
                wrapperReleases.push_back({context.pollSequence,b.identity});
                if(wrapperReleases.size()>9) throw std::logic_error("AI wrapper release bound exceeded");
                break;
            }
		}
    if(!order) {
        // DecisionContext holds a reference, so construct the merged batch view.
        AIEngine::DecisionContext merged{context.world,context.player,context.team,deferredControllerReceipts,
            context.observation,context.scheduledTick,context.pollSequence,context.resourceEnrollments,context.fieldDiagnostics,context.controllerGeneration};
        order=aiImplementation->getOrder(merged);
        deferredControllerReceipts.clear();
    }
	if (!order) throw std::runtime_error("AI returned no order object");
    if (ruleAuditEnabled() &&
        !AIRules::permittedQueuedOrder(context.world,*order))
        throw std::runtime_error("AI selected work unavailable under the match rules");
	sink.count(AITelemetry::OrderTypes + order->getOrderType());
	sink.count(order->getOrderType() == ORDER_NULL ? AITelemetry::NullOrders : AITelemetry::Orders);
	aiImplementation->captureTelemetry();
	decisionTelemetry->current.tick = context.world.tick; decisionTelemetry->current.available = true;
	auto result = AIEngine::Command::capture(*order, context.world);
	result.fieldDiagnostics = context.fieldDiagnostics;
	result.retainedQueryVectorBytes = aiImplementation->retainedQueryVectorBytes();
	result.telemetry = decisionTelemetry->current;
	result.namedTelemetry = decisionTelemetry->named;
	result.diagnostics = std::move(aiImplementation->bufferedDiagnostics);
	aiImplementation->bufferedDiagnostics.clear();
	return result;
}
void AI::publishDecision(const AIEngine::Command& output)
{
	// The private telemetry sink stays bound to the worker stream. Publishing
	// copies completed values and never visits mutable controller state.
	if (output.telemetry && telemetrySeries) {
		telemetrySeries->current = *output.telemetry; telemetrySeries->named = output.namedTelemetry;
	}
	for (const auto& diagnostic : output.diagnostics)
	{
		if (diagnostic.path.empty()) {
			if (diagnostic.standardOutput) std::cout << diagnostic.text;
			else std::cerr << diagnostic.text;
		}
		else {
			std::ofstream file(diagnostic.path, std::ios::app);
			if (file && file.tellp() == 0) file << diagnostic.header;
			if (file) file << diagnostic.text;
		}
	}
}

void AI::initializeRandom()
{
	if (randomInitialized) return;
	// Mix a stable player identity into the saved game seed. Do not use team
	// number: multiple controllers can act for the same team.
	Uint32 seed = player->game->gameHeader.getRandomSeed() ^
		(0x9e3779b9u * (static_cast<Uint32>(player->number) + 1u));
	seed ^= seed >> 16;
	seed *= 0x7feb352du;
	seed ^= seed >> 15;
	seed *= 0x846ca68bu;
	seed ^= seed >> 16;
	randomEngine.seed(seed);
	randomInitialized = true;
}

bool AI::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	randomInitialized = false;
	decisionTelemetry.reset();
	resumeTelemetry = true;
	telemetrySeries.reset();
	telemetryTeam = nullptr;
	assert(player);
	
	if (aiImplementation)
		delete aiImplementation;
	aiImplementation=NULL;
    wrapperReleases.clear();deferredControllerReceipts.clear();wrapperGeneration=0;

	char signature[4];
	
	stream->readEnterSection("AI");
	stream->read(signature, 4, "signatureStart");
	if (memcmp(signature,"AI b", 4)!=0)
	{
		fprintf(stderr, "AI::bad begining signature\n");
		stream->readLeaveSection();
		return false;
	}

	const Uint32 savedImplementation = stream->readUint32("implementitionID");
	if (savedImplementation > JAVASCRIPT) return false;
	implementationID=static_cast<ImplementationID>(savedImplementation);

	switch (implementationID)
	{
		case NONE:
			aiImplementation=new AINull();
		break;
		case NUMBI:
			aiImplementation=new AINumbi(player);
			if (!aiImplementation->load(stream, player, versionMinor))
			{
				stream->readLeaveSection();
				return false;
			}
		break;
		case CASTOR:
			aiImplementation=new AICastor(player);
			if (!aiImplementation->load(stream, player, versionMinor))
			{
				fprintf(stderr, "AI::load: AICastor load failed\n");
				stream->readLeaveSection();
				return false;
			}
		break;
		case NICOWAR:
			aiImplementation=new AISharedRuntime::Runtime(new NewNicowar, player);
			if (!aiImplementation->load(stream, player, versionMinor)) return false;
		break;
		case ECONO:
			aiImplementation=new AISharedRuntime::Runtime(new AISharedRuntime::Econo, player);
			if (!aiImplementation->load(stream, player, versionMinor)) return false;
		break;
		case WARRUSH:
			aiImplementation=new AIWarrush(player);
			if (!aiImplementation->load(stream, player, versionMinor)) return false;
		break;
		case MAXIMA:
			if(versionMinor<115)
				throw std::runtime_error("This Maxima save uses a retired strategy format");
			aiImplementation=new AIMaxima::Maxima(stream,player,versionMinor);
		break;
		case CORTEX:
			aiImplementation=new AICortex(stream, player, versionMinor);
		break;
		case CABINO:
			aiImplementation=new Cabino::AICabino(stream, player, versionMinor);
		break;
		case JAVASCRIPT:
			aiImplementation=new AIJavaScript(player);
			if(!aiImplementation->load(stream,player,versionMinor)) return false;
		break;
		default:
			fprintf(stderr, "AI id %d does not exist, you probably try to load a map from a more recent version of glob2.\n", implementationID);
			return false;
	}
	assert(aiImplementation);
	aiImplementation->setRandomEngine(randomEngine);
	if (versionMinor >= 121)
	{
		std::ostringstream state;
		state.imbue(std::locale::classic());
		stream->readEnterSection("randomState");
		for (unsigned i = 0; i < MersenneTwister::state_size; ++i)
		{
			stream->readEnterSection(i);
			state << stream->readUint32("word") << ' ';
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		std::istringstream input(state.str());
		input.imbue(std::locale::classic());
		if (!(input >> randomEngine)) return false;
		randomInitialized = true;
	}

	if (versionMinor >= FILE_FORMAT_VERSION_AI_PIPELINE) {
		std::vector<std::shared_ptr<AITelemetry::Series>> saved;
		AITelemetry::load(stream, saved, versionMinor);
		if (saved.size() > 1) return false;
		if (!saved.empty()) decisionTelemetry = std::make_unique<AITelemetry::Series>(*saved.front());
        loadWrapperFeedback(stream);
	}
	stream->read(signature, 4, "signatureEnd");
	stream->readLeaveSection();
	if (memcmp(signature,"AI e", 4)!=0)
	{
		fprintf(stderr, "AI::bad end signature\n");
		return false;
	}
	
	return true;
}

void AI::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AI");
	stream->write("AI b", 4, "signatureStart");
	
	stream->writeUint32(static_cast<Uint32>(implementationID), "implementitionID");
	
	assert(aiImplementation);
	aiImplementation->save(stream);
	initializeRandom();
	std::ostringstream randomState;
	randomState.imbue(std::locale::classic());
	randomState << randomEngine;
	std::istringstream state(randomState.str());
	state.imbue(std::locale::classic());
	stream->writeEnterSection("randomState");
	for (unsigned i = 0; i < MersenneTwister::state_size; ++i)
	{
		Uint32 word;
		if (!(state >> word)) throw std::runtime_error("Invalid AI RNG state while saving");
		stream->writeEnterSection(i);
		stream->writeUint32(word, "word");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	
	std::vector<std::shared_ptr<AITelemetry::Series>> saved;
	if (decisionTelemetry) saved.push_back(std::make_shared<AITelemetry::Series>(*decisionTelemetry));
	AITelemetry::save(stream, saved);
    saveWrapperFeedback(stream);
	stream->write( "AI e",  4, "signatureEnd");
	stream->writeLeaveSection();
}

void AI::bindTelemetry()
{
	if (telemetrySeries && telemetryTeam == player->team)
		return;
	Uint32 generation = 0;
	for (int t = 0; t < player->game->mapHeader.getNumberOfTeams(); ++t)
		if (player->game->teams[t])
			for (auto &record : player->game->teams[t]->stats.aiTelemetry)
				if (record->player == player->number)
				{
					generation = std::max(generation, record->generation + 1);
					if (resumeTelemetry && record->active && t == player->team->teamNumber &&
						record->implementation == implementationID &&
						record->schemaVersion == aiImplementation->telemetrySchemaVersion() &&
						record->fields == aiImplementation->telemetrySchema())
						telemetrySeries = record;
					else
						record->active = false;
				}
	if (!telemetrySeries || telemetryTeam)
	{
		telemetrySeries = std::make_shared<AITelemetry::Series>();
		auto &r = *telemetrySeries;
		r.player = player->number;
		r.implementation = implementationID;
		r.generation = generation;
		r.coverage = player->game->stepCounter;
		r.playerName = player->name;
		r.schemaVersion = aiImplementation->telemetrySchemaVersion();
		r.fields = aiImplementation->telemetrySchema();
		r.current.tick = r.coverage;
		r.current.values.resize(r.fields.size());
		for (unsigned i = 0; i < r.fields.size(); ++i)
			if (r.fields[i].kind == AITelemetry::Counter)
				r.current.values[i] = {0, r.coverage, true};
		player->team->stats.aiTelemetry.push_back(telemetrySeries);
	}
	telemetryTeam = player->team;
	resumeTelemetry = false;
	// Once decisions run on the engine's lane the controller's sink belongs to
	// that lane (decide() binds decisionTelemetry); a rebind on the owner must
	// not race a decision still in flight.
	if (decisionTelemetry) return;
	aiImplementation->telemetry.series = telemetrySeries.get();
	aiImplementation->telemetry.tick = player->game->stepCounter;
}
void AI::captureTelemetry()
{
	// Async controller state belongs to its lane; the owner samples only published output.
	if (decisionTelemetry) return;
	bindTelemetry();
	aiImplementation->telemetry.tick = player->game->stepCounter;
	// Initial/dormant AIs may have lazily initialized state. Never inspect it.
	if (telemetrySeries->current.available)
		aiImplementation->captureTelemetry();
	telemetrySeries->current.tick = player->game->stepCounter;
}

void AI::saveWrapperFeedback(GAGCore::OutputStream* stream) const
{
    stream->writeEnterSection("wrapperFeedback");
    stream->writeUint32(wrapperGeneration,"generation");
    stream->writeUint32(wrapperReleases.size(),"releases");
    for(unsigned i=0;i<wrapperReleases.size();++i){
        stream->writeEnterSection(i);const auto& value=wrapperReleases[i];
        stream->writeUint32(Uint32(value.pollSequence),"sequenceLow");stream->writeUint32(Uint32(value.pollSequence>>32),"sequenceHigh");
        stream->writeUint16(value.target.gid,"gid");stream->writeUint32(value.target.generation,"targetGeneration");stream->writeLeaveSection();
    }
    stream->writeUint32(deferredControllerReceipts.size(),"receipts");
    for(unsigned i=0;i<deferredControllerReceipts.size();++i){stream->writeEnterSection(i);AIEngine::saveExecutionReceipt(*stream,deferredControllerReceipts[i]);stream->writeLeaveSection();}
    stream->writeLeaveSection();
}
void AI::loadWrapperFeedback(GAGCore::InputStream* stream)
{
    stream->readEnterSection("wrapperFeedback");
    wrapperGeneration=stream->readUint32("generation");
    const auto releases=stream->readCount("releases",9);
    for(unsigned i=0;i<releases;++i){
        stream->readEnterSection(i);const auto low=stream->readUint32("sequenceLow"),high=stream->readUint32("sequenceHigh");
        WrapperRelease release{Uint64(low)|(Uint64(high)<<32),{stream->readUint16("gid"),stream->readUint32("targetGeneration")}};
        if(!release.target.generation || release.target.gid==NOGBID ||
            (!wrapperReleases.empty() && wrapperReleases.back().pollSequence>=release.pollSequence))
            throw std::runtime_error("Invalid saved AI wrapper release");
        wrapperReleases.push_back(release);stream->readLeaveSection();
    }
    const auto receipts=stream->readCount("receipts",9);
    for(unsigned i=0;i<receipts;++i){
        stream->readEnterSection(i);auto receipt=AIEngine::loadExecutionReceipt(*stream);
        if(receipt.request.player!=unsigned(player->number) || receipt.request.generation!=wrapperGeneration ||
            (!deferredControllerReceipts.empty() && deferredControllerReceipts.back().request.pollSequence>=receipt.request.pollSequence) ||
            std::any_of(wrapperReleases.begin(),wrapperReleases.end(),[&](const auto& release){return release.pollSequence==receipt.request.pollSequence;}))
            throw std::runtime_error("Invalid saved deferred AI receipt");
        deferredControllerReceipts.push_back(std::move(receipt));stream->readLeaveSection();
    }
    stream->readLeaveSection();
}
