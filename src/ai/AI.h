// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <SDL3/SDL_iostream.h>

#include <memory>
#include "MersenneTwister.h"
#include "AITelemetry.h"
#include "sim/snapshot/Requirements.h"
#include "sim/EntityRef.h"
#include <vector>
namespace GAGCore
{
	class InputStream;
	class OutputStream;
}
class Player;
class Team;
class Order;
class AIImplementation;
namespace AIEngine { struct DecisionContext; struct Command; struct ExecutionReceipt; }
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
		///Reference to the AISharedRuntime based AIEcono
		ECONO=4,
		///Reference to the AISharedRuntime based AINicowar
		NICOWAR=5,
		///Reference to AICortex (direct AIImplementation binding)
		CORTEX=6,
		///Standalone Maxima strategy AI.
		MAXIMA=7,
		///Reference to AICabino, a resurrected port of the original (2005-2007)
		///Nicowar: a set of independent specialist modules (defense, attack,
		///construction, upgrades, unit/swarm management) that each act on
		///their own but cooperate toward one game plan, direct AIImplementation
		///binding, no AISharedRuntime involved.
		CABINO=8,

		JAVASCRIPT=9,
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

private:
	MersenneTwister randomEngine;
	bool randomInitialized = false;
	std::unique_ptr<AITelemetry::Series> decisionTelemetry;
    // These records contain only command identity/bytes, never an observation.
    struct WrapperRelease { Uint64 pollSequence; BuildingRef target; };
    Uint32 wrapperGeneration = 0;
    std::vector<WrapperRelease> wrapperReleases;
    std::vector<AIEngine::ExecutionReceipt> deferredControllerReceipts;
    void saveWrapperFeedback(GAGCore::OutputStream*) const;
    void loadWrapperFeedback(GAGCore::InputStream*);
	void initializeRandom();

public:

	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	// Called when the owning game's seed is changed before a new match.
	void resetRandom() { randomInitialized = false; }

	std::shared_ptr<Order> getOrder(bool paused);
	// Owner setup precedes dispatch; decide borrows captured inputs only. The
	// completed sample is published independently from the controller's stream.
	void prepareDecision();
	SimulationSnapshot::Requirements observationRequirements() const;
	AIEngine::Command decide(const AIEngine::DecisionContext& context);
	void publishDecision(const AIEngine::Command& output);

};
