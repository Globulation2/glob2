// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <SDL_rwops.h>

#include <memory>
#include "AITelemetry.h"
namespace GAGCore
{
	class InputStream;
	class OutputStream;
}
class Player;
class Team;
class Order;
class AIImplementation;
/*
 * AI is the base class for the AI-implementations
 */
class AI
{
public:
	///TODO: Explain
	enum ImplementationID
	{
		///Reference to AINull
		NONE=0,
		///Reference to AINumbi
		NUMBI=1,
		///Reference to AICastor
		CASTOR=2,
		///Reference to AIWarrush
		WARRUSH=3,
		///Reference to the AIEcho based AIEcono
		ECONO=4,
		///Reference to the AIEcho based AINicowar
		NICOWAR=5,
		///Reference to AICortex (direct AIImplementation binding)
		CORTEX=6,
		///Standalone Maxima strategy AI.
		MAXIMA=7,
		///Reference to AICabino, a resurrected port of the original (2005-2007)
		///Nicowar: a set of independent specialist modules (defense, attack,
		///construction, upgrades, unit/swarm management) that each act on
		///their own but cooperate toward one game plan, direct AIImplementation
		///binding, no AIEcho involved.
		CABINO=8,
		///Reference to AIAtlas, a declarative spatial AI: its policy emits the
		///map configuration it wants and a reconciler issues the orders that
		///close the gap. See src/ai/atlas/AtlasDesiredState.h.
		ATLAS=9,

		SIZE
	};
	static const ImplementationID toggleAI=CASTOR;

public:
	AI(ImplementationID implementationID, Player *player);
	AI(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	~AI();

	void bindTelemetry();
	void captureTelemetry();
	std::shared_ptr<AITelemetry::Series> telemetrySeries;
	Team *telemetryTeam = nullptr;
	bool resumeTelemetry = false;
	AIImplementation *aiImplementation;
	ImplementationID implementationID;

	Player *player;

	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);

	std::shared_ptr<Order> getOrder(bool paused);

};

