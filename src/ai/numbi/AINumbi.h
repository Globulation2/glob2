// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "BuildingType.h"
#include "AIImplementation.h"
#include "AINumbiTuning.h"
#include "BuildingCapabilities.h"
#include <array>

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
	
private:
	int timer;
	int phase;
	int attackPhase;
	int phaseTime;
	int criticalWarriors;
	int criticalTime;
	int attackTimer;
	using Intent = AIPlanning::BuildingIntent;
	std::array<int, static_cast<unsigned>(Intent::Count)> mainBuilding{};
	int selectBuilding(Intent intent);
	bool provides(const Building& building, Intent intent) const;
	void init(Player *player);
	int estimateFood(Building *building);
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


 

