// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "AI.h"
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
				&& AIRules::trainingBuilding(b->type->shortTypeNum) && b->maxUnitWorking>0)
			{ order=std::make_shared<OrderModifyBuilding>(b->gid,0); break; }
	if (!order) order = aiImplementation->getOrder();
	// Qualification audits planning at selection time. A replay sees orders
	// after the network queue, when a repair may already have finished, and
	// cannot reliably distinguish that repair from an unavailable upgrade.
	// This opt-in test guard reports violations; it never filters an order.
	if (std::getenv("GLOB2_TEST_AI_RULE_AUDIT") &&
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
	resumeTelemetry = true;
	telemetrySeries.reset();
	telemetryTeam = nullptr;
	assert(player);
	
	if (aiImplementation)
		delete aiImplementation;
	aiImplementation=NULL;

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
	aiImplementation->telemetry.series = telemetrySeries.get();
	aiImplementation->telemetry.tick = player->game->stepCounter;
}
void AI::captureTelemetry()
{
	bindTelemetry();
	aiImplementation->telemetry.tick = player->game->stepCounter;
	// Initial/dormant AIs may have lazily initialized state. Never inspect it.
	if (telemetrySeries->current.available)
		aiImplementation->captureTelemetry();
	telemetrySeries->current.tick = player->game->stepCounter;
}
