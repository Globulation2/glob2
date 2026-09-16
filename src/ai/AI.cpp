// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "AI.h"
#include "AIMaxima.h"
#include "Player.h"
#include "Utilities.h"
#include "Game.h"
#include "Order.h"
#include "TeamStat.h"
#include <assert.h>
#include <stdexcept>
#include <Stream.h>


#include "AINull.h"
#include "AINumbi.h"
#include "AICastor.h"
#include "AIWarrush.h"
#include "AINicowar.h"
#include "echo/Echo.h"
#include "cortex/AICortex.h"
#include "AICabino.h"
#include "neurotica/AINeurotica.h"

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
			aiImplementation=new AIEcho::Echo(new NewNicowar, player);
		break;
		case WARRUSH:
			aiImplementation=new AIWarrush(player);
		break;
		case ECONO:
			aiImplementation=new AIEcho::Echo(new AIEcho::Econo, player);
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
		case NEUROTICA:
			aiImplementation=new AINeurotica(player);
		break;
		default:
			assert(false);
		break;
	}

	this->implementationID=implementationID;
	this->player=player;
}

AI::AI(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	aiImplementation=NULL;
	implementationID=NONE;
	this->player=player;
	bool goodLoad=load(stream, versionMinor);
	assert(goodLoad);
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
	bindTelemetry();
	aiImplementation->telemetry.tick = player->game->stepCounter;
	aiImplementation->telemetry.count(AITelemetry::Polls);
	PerformanceTelemetry::Scope aiTime(
		PerformanceTelemetry::Id::AI,
		PerformanceTelemetry::collector().actor(player->number, player->team->teamNumber,
												implementationID, telemetrySeries->generation));
	auto order = aiImplementation->getOrder();
	aiTime.stop();
	const auto type = order->getOrderType();
	aiImplementation->telemetry.count(AITelemetry::OrderTypes + type);
	aiImplementation->telemetry.count(type == ORDER_NULL ? AITelemetry::NullOrders
														 : AITelemetry::Orders);
	telemetrySeries->current.tick = player->game->stepCounter;
	telemetrySeries->current.available = true;
	return order;
}

bool AI::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
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

	implementationID=(ImplementationID)stream->readUint32("implementitionID");

	switch (implementationID)
	{
		case NONE:
			aiImplementation=new AINull();
		break;
		case NUMBI:
			aiImplementation=new AINumbi(stream, player, versionMinor);
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
			aiImplementation=new AIEcho::Echo(new NewNicowar, player);
			aiImplementation->load(stream, player, versionMinor);
		break;
		case ECONO:
			aiImplementation=new AIEcho::Echo(new AIEcho::Econo, player);
			aiImplementation->load(stream, player, versionMinor);
		break;
		case WARRUSH:
			aiImplementation=new AIWarrush(stream, player, versionMinor);
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
		case NEUROTICA:
			aiImplementation=new AINeurotica(player);
			if (!aiImplementation->load(stream, player, versionMinor))
			{
				fprintf(stderr, "AI::load: AINeurotica load failed\n");
				stream->readLeaveSection();
				return false;
			}
		break;
		default:
			fprintf(stderr, "AI id %d does not exist, you probably try to load a map from a more recent version of glob2.\n", implementationID);
			assert(false);
		break;
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
