// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Ressource.h"
#include "UnitConsts.h"
#include "AITelemetryValue.h"
#include "SceneEntities.h"

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct BuildingType;
class TeamStats;

//! Selection calculations borrow authoritative records from the retained world.
struct SceneBuildingPanel
{
    const SnapshotBuilding* record = nullptr;
    bool valid = false;
    const SnapshotBuilding& state() const { static const SnapshotBuilding empty{}; return record ? *record : empty; }
    const SimulationSnapshot::TeamView* ownerRecord = nullptr;
    const SimulationSnapshot::TeamView& owner() const { static const SimulationSnapshot::TeamView empty{}; return ownerRecord ? *ownerRecord : empty; }
    const BuildingType* type = nullptr;
    const Sint32* materials = nullptr; // borrowed from the retained world
    int productionDuration = 0;
    std::span<const UnitRef> insideUnits, workingUnits;
    bool hardSpaceForRepair = false, hardSpaceForUpgrade = false, showLevel = false;
    int repairCost[MaterialCount] = {};
    int buildingHpMultiplier = 1;
};
struct SceneUnitPanel
{
    const SnapshotUnit* record = nullptr;
    bool valid = false;
    const SnapshotUnit& state() const { static const SnapshotUnit empty{}; return record ? *record : empty; }
    const SimulationSnapshot::TeamView* ownerRecord = nullptr;
    const SimulationSnapshot::TeamView& owner() const { static const SimulationSnapshot::TeamView empty{}; return ownerRecord ? *ownerRecord : empty; }
    std::span<const UnitType> unitTypes;
    bool unitHungry = false;
    int realArmor = 0, nextLevelThreshold = 0, glassCannonScale = 0;
};

//! The local team and game-wide HUD borrow their authoritative records.
struct ScenePanelLocal
{
    const SimulationSnapshot::TeamView* record = nullptr;
    const SimulationSnapshot::TeamView& state() const { static const SimulationSnapshot::TeamView empty{}; return record ? *record : empty; }
    int maxBuildLevel = 0;
};
struct SceneHud
{
    const SimulationSnapshot::Session* session = nullptr;
    const SimulationSnapshot::Teams* teams = nullptr;
    const SimulationSnapshot::TeamView* local = nullptr;
    const SimulationSnapshot::Session& state() const { static const SimulationSnapshot::Session empty{}; return session ? *session : empty; }
    const SimulationSnapshot::TeamView& localState() const { static const SimulationSnapshot::TeamView empty{}; return local ? *local : empty; }
    int totalPrestige() const { return teams ? teams->totalPrestige : 0; }
    bool drawn = false, localDraw = false;
    int winningTeam = -1;
    struct WinChance { std::string name; GAGCore::Color color; int permille = 0; bool alive = false; };
    std::vector<WinChance> winChances;
};

struct SceneAITelemetry
{
	int team = 0, player = 0;
	std::string name;
	bool available = false;
	std::vector<AITelemetry::NamedValue> values;
};
struct ScenePanels
{
    SimulationSnapshot::Handle world;
	std::vector<SceneAITelemetry> aiTelemetry;
	ScenePanelLocal local;
	SceneBuildingPanel building;
	SceneUnitPanel unit;
	SceneHud hud;
	//! Shared immutable display ring, reused until statistics change.
	std::shared_ptr<const TeamStats> localStats;
};
