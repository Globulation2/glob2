// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "BuildingCatalog.h"
#include "BuildingType.h"
#include "Ressource.h"
#include "TerrainRegistry.h"
#include "TeamStat.h"
#include "sim/EntityRef.h"
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <type_traits>

class Game;

namespace SimulationSnapshot
{
// Observation records contain values only. They deliberately do not inherit
// simulation objects: a const Game would still expose mutable pointees/caches.
struct UnitRange { Uint32 offset = 0, count = 0; };
struct BuildingView
{
	BuildingRef identity;
	int team = 0, type = 0, x = 0, y = 0;
	int state = 0, construction = 0, originType = -1;
	int hp = 0, maxHp = 0, workers = 0, futureWorkers = 0, desiredWorkers = 0;
	int shortType = 0, productionTimeout = 0, receiveMask = 0, sendMask = 0, bullets = 0;
	bool requireBombing = false;
	bool upgradeAvailable = false, hardSpaceUpgrade = false, hardSpaceRepair = false;
	std::array<bool, 6> locked{};
	std::array<bool, BASIC_COUNT> clearingResources{};
	int maxInside = 0, priority = 0, range = 0, minimumLevel = 0, minimumWorkerLevel = 0;
	Uint32 seenBy = 0;
	Uint8 underAttack = 0;
	std::array<Sint32, MAX_NB_RESOURCES> resources{}, wishedResources{};
	std::array<Sint32, NB_UNIT_TYPE> ratios{};
	UnitRange working, inside;
};

struct UnitView
{
	UnitRef identity;
	int team = 0, type = 0, x = 0, y = 0;
	int medical = 0, activity = 0, displacement = 0;
	int hp = 0, hungry = 0, hungryTrigger = 0, constructionLevel = 0, hungriness = 0;
	bool dead = false;
	Uint8 underAttack = 0;
	BuildingRef attached, target;
	int dx = 0, dy = 0, insideTimeout = 0, experience = 0, experienceLevel = 0, fruitCount = 0;
	int movement = 0, action = 0, carriedResource = 0, speed = 0, direction = 0;
	int fruitMask = 0, destinationPurpose = 0, targetX = 0, targetY = 0;
	std::array<Sint32, NB_ABILITY> performance{}, levels{};
	std::array<bool, NB_ABILITY> canLearn{};
};

struct TeamView
{
	int number = 0, prestige = 0, startX = 0, startY = 0;
	bool alive = false;
	Uint32 mask = 0, allies = 0, enemies = 0;
	Uint32 foodVision = 0, exchangeVision = 0, otherVision = 0;
	TeamStat statistics;
	std::array<Sint32, MAX_NB_RESOURCES> resources{};
	int workerBalance = 0, starving = 0;
	std::array<int, NB_UNIT_LEVELS> workersLevel{};
	std::vector<BuildingRef> virtualBuildings, swarms;
};

struct BuildingKindView
{
	std::string key, legacyType;
	BuildingType resolvedType;
	std::array<Sint32, MAX_NB_RESOURCES> multiplierResource{};
	BuildingSemantics semantics;
	int width = 0, height = 0, level = 0;
	int previous = -1, next = -1, maximumWorkers = 0, maximumInside = 0;
	int maximumRange = 0, shootingRange = 0;
	bool site = false, available = false;
	bool isVirtual = false, isCloaked = false, suppliesStock = false, fetchesStock = false;
	int shortTypeNum = 0, hpMax = 0, shootSpeed = 0, shootRhythm = 0, decLeft = 0, decTop = 0;
	std::array<Sint32, MAX_NB_RESOURCES> maxResource{};
	std::array<Sint32, NB_UNIT_TYPE> zonable{};
	Uint64 capabilityMask = 0, rawCapabilityMask = 0;
	int lineagePosition = 0;
};

struct TileView
{
	TerrainType terrain = GRASS;
	Uint16 legacyTerrain = 0;
	Uint8 immobileUnit = 255;
	Resource resource;
	Uint16 building = 0xffff, groundUnit = 0xffff, airUnit = 0xffff;
	Uint32 forbidden = 0, guard = 0, clear = 0, farm = 0;
	Uint32 discovered = 0, visible = 0;
	Uint16 fertility = 0;
	bool resourcesMayGrow = false, canPaintFarm = false;
};

struct RuleView
{
	bool hungerDisabled = false, upgradesDisabled = false, peaceful = false;
	bool instantConstruction = false, resourceGrowthDisabled = false;
};

struct BuildProjectView { int posX, posY, teamNumber, typeNum, unitWorking, unitWorkingFuture; };
static_assert(std::is_trivially_copyable_v<BuildingView>);
static_assert(std::is_trivially_copyable_v<UnitView>);
static_assert(std::is_trivially_copyable_v<BuildProjectView>);
} // namespace SimulationSnapshot
