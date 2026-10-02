// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Ressource.h"
#include "UnitConsts.h"
#include "scene/SceneEntities.h"

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class BuildingType;
class Race;
class TeamStats;

//! What the selection panels show, extracted for the selected building or unit.
//! Field names follow Building/Unit so panel drawing reads the same; `owner` is a
//! copy of the owning team plus its display name.
struct ScenePanelOwner
{
	int teamNumber = 0;
	Uint32 me = 0, allies = 0, sharedVisionExchange = 0;
	GAGCore::Color color;
	std::string firstPlayerName; //!< empty when uncontrolled
};

struct SceneBuildingPanel
{
	bool valid = false;
	Uint16 gid = 0xFFFF;
	Uint32 generation = 0;
	ScenePanelOwner owner;
	BuildingType *type = nullptr; //!< static building type definition
	Sint32 typeNum = 0, posX = 0, posY = 0;
	Sint32 hp = 0, effectiveMaxHp = 0, buildingState = 0, constructionResultState = 0;
	Sint32 maxUnitWorking = 0, desiredMaxUnitWorking = 0, priority = 0, unitStayRange = 0, minLevelToFlag = 0;
	bool clearingResources[BASIC_COUNT] = {};
	Sint32 resources[MAX_RESOURCES] = {};
	Sint32 bullets = 0, productionTimeout = 0;
	Sint32 ratio[NB_UNIT_TYPE] = {};
	std::array<Uint32, SceneSelectedBuilding::FailReasons> unitsFailingRequirements{};
	Sint32 unitsInside = 0, unitsWorking = 0;
	//! Units inside, for the time-to-leave bar.
	struct InsideUnit { bool inside = false; Sint32 insideTimeout = 0, delta = 0; };
	std::vector<InsideUnit> insideUnits;
	//! Positions of the working units, for the flag's on-the-spot count.
	std::vector<std::pair<Sint32, Sint32>> workerPositions;
	//! Queries answered during extraction.
	bool hardSpaceForRepair = false, hardSpaceForUpgrade = false;
	int repairCost[BASIC_COUNT] = {};
	int buildingHpMultiplier = 1;
};

struct SceneUnitPanel
{
	bool valid = false;
	Uint16 gid = 0xFFFF;
	Uint32 generation = 0;
	ScenePanelOwner owner;
	Race *race = nullptr; //!< static per-team unit definitions
	Sint32 typeNum = 0, action = 0, direction = 0, delta = 0;
	Sint32 hp = 0, trigHP = 0, hungry = 0, speed = 0, carriedResource = 0;
	Uint32 fruitCount = 0;
	Sint32 experience = 0, experienceLevel = 0;
	Sint32 performance[NB_ABILITY] = {};
	Sint32 level[NB_ABILITY] = {};
	//! Queries answered during extraction.
	bool unitHungry = false;
	int realArmor = 0, nextLevelThreshold = 0, glassCannonScale = 0;
};

//! The local team's values the panels and the top bar show or compare against.
struct ScenePanelLocal
{
	int teamNumber = 0;
	Uint32 allies = 0;
	int maxBuildLevel = 0;
	GAGCore::Color color;
	Sint32 prestige = 0, unitConversionGained = 0, unitConversionLost = 0;
	Sint32 noMoreBuildingSitesCountdown = 0;
};

//! Game-wide values the HUD shows.
struct SceneHud
{
	int totalPrestige = 0, prestigeToReach = 0;
	bool anyPlayerWaited = false;
	Uint32 maskAwayPlayer = 0;
	struct Player { std::string name; int teamNumber = 0; };
	std::vector<Player> players; //!< in player order
	bool legacyScriptTextShown = false;
	std::string legacyScriptText;
	int legacyScriptTimer = 0;
};

struct ScenePanels
{
	ScenePanelLocal local;
	SceneBuildingPanel building;
	SceneUnitPanel unit;
	SceneHud hud;
	//! Copy of the local team's statistics (top bar counters, statistics pages).
	//! Drawing methods are non-const, so the copy is held through a non-const pointer;
	//! it belongs to this Scene alone.
	std::shared_ptr<TeamStats> localStats;
};
