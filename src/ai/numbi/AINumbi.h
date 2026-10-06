// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "BuildingType.h"
#include "AIImplementation.h"
#include "AINumbiTuning.h"
#include "BuildingCapabilities.h"
#include <array>
#include "ai/observation/AIWorldView.h"
#include "NumbiResourceCache.h"
namespace AIEngine { class WorldQueries; }

class Game;
class Map;
class Order;
class Player;
class Team;
class Building;

class AINumbi : public AIImplementation
{
public:
  void captureTelemetry() override;
  const std::vector<AITelemetry::Field> &telemetrySchema() const override
  {
	  return AITelemetry::schema(1);
  }
  Uint32 telemetrySchemaVersion() const override { return 2; }
	AINumbi(Player *player);
	AINumbi(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	~AINumbi();

	Player *player;
	Team *team;
	Game *game;
	Map *map;
	
	bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	
	std::shared_ptr<Order>getOrder(void);
	bool supportsObservation() const override { return true; }
 SimulationSnapshot::Requirements observationRequirements() const override { return SimulationSnapshot::All & ~SimulationSnapshot::bit(SimulationSnapshot::Component::Growth); }
 std::optional<Uint64> retainedQueryVectorBytes() const override
 {
  Uint64 bytes = 0;
  for (const auto& [key, field] : resourceInitializations)
   if (field.values) bytes += Uint64(field.values->capacity()) * sizeof(Uint16);
  return bytes;
 }
	std::shared_ptr<Order> getOrder(const AIEngine::DecisionContext&) override;
	void orderExecutionCompleted(const Order&,bool) override;
	
private:
	using Intent = AIPlanning::BuildingIntent;
	struct PendingRequest {
	 Uint32 tick=0;
 Uint64 pollSequence=0;
	 BuildingRef target;
	 std::shared_ptr<Order> order;
	};
	std::vector<PendingRequest> pendingRequests;
	int requestedWorkers(const AIEngine::BuildingView&) const;
	Sint32 requestedRatio(const AIEngine::BuildingView&,int unit) const;
	bool hasPending(const AIEngine::BuildingView&) const;
	int pendingBuildings(Intent intent) const;
	std::shared_ptr<Order> remember(std::shared_ptr<Order>,Uint32 tick,Uint64 pollSequence);
	int teamNumber=0;
	const AIEngine::AIWorldView* observation=nullptr;
	AIEngine::WorldQueries* queries=nullptr;
	NumbiObservation::ResourceInitializations resourceInitializations;
	std::array<const AIEngine::BuildingView*,1024> observedBuildings{};
	std::array<const AIEngine::UnitView*,1024> observedUnits{};
	std::shared_ptr<Order> decide();
	int timer;
	int phase;
	int attackPhase;
	int phaseTime;
	int criticalWarriors;
	int criticalTime;
	int attackTimer;
	std::array<int, static_cast<unsigned>(Intent::Count)> mainBuilding{};
	int selectBuilding(Intent intent);
	bool provides(const AIEngine::BuildingView& building, Intent intent) const;
	void init(Player *player);
	int estimateFood(const AIEngine::BuildingView *building);
	int countUnits(void);
	int countUnits(const int medicalState);
	std::shared_ptr<Order>swarmsForWorkers(const int minSwarmNumbers, const int nbWorkersFactor, const int workers, const int explorers, const int warriors);
	void nextMainBuilding(Intent intent);
	int nbFreeAround( int posX, int posY, int width, int height);
	void squareCircleScan(int &dx, int &dy, int &sx, int &sy, int &x, int &y, int &mx, int &my);
	bool findNewEmplacement(Intent intent, int typeNum, int *posX, int *posY);
	std::shared_ptr<Order>mayAttack(int criticalMass, int criticalTimeout, Sint32 numberRequested);
	std::shared_ptr<Order>adjustBuildings(const int numbers, const int numbersInc, const int workers, Intent intent);
	std::shared_ptr<Order>checkoutExpands(const int numbers, const int workers);
	std::shared_ptr<Order>mayUpgrade(const int ptrigger, const int ntrigger);
};


 

