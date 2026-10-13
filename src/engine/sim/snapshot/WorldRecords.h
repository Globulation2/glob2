// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "BuildingCatalog.h"
#include "BuildingState.h"
#include "BuildingType.h"
#include "Ressource.h"
#include "TerrainRegistry.h"
#include "TeamStat.h"
#include "UnitState.h"
#include "sim/EntityRef.h"
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <type_traits>
#include <new>

class Game;

namespace SimulationSnapshot
{
// Observation records contain values only. They deliberately do not inherit
// simulation objects: a const Game would still expose mutable pointees/caches.
struct UnitRange { Uint32 offset = 0, count = 0; };
struct BuildingView : BuildingStateRecord
{
	Uint32 extraProductionOffset=0, extraProductionCount=0;
    std::array<Sint32, 3> constructionOriginRatios{};
	BuildingRef identity;
	int team = 0;
	int maxHp = 0;
	bool usesTeamResources = false;
	MaterialMask availableSupplyMask = 0; // Stock after reservations, captured for gradient readers.
	UnitRange working, inside;
};

// Compact authoritative prefix plus generation-checked observation relationships.
struct UnitCoreView
{
    UnitCoreState core;
    UnitRef identity;
    int team = 0;
    BuildingRef attached, target;
    const Sint32& performance(int ability) const { return core.performance(ability); }
    Sint32& performance(int ability) { return core.performance(ability); }
};
static_assert(std::is_standard_layout_v<UnitCoreView>);
static_assert(std::is_trivially_copyable_v<UnitCoreView>);
static_assert(sizeof(UnitCoreView)<=192);

// A full observation borrows the two captured arrays; it owns no unit payload.
struct UnitView
{
    const UnitCoreState& core;
    const UnitDetailState& detail;
    const UnitRef& identity;
    const int& team;
    const BuildingRef& attached;
    const BuildingRef& target;
    UnitView(const UnitCoreView& hot, const UnitDetailState& cold)
        : core(hot.core),detail(cold),identity(hot.identity),team(hot.team),attached(hot.attached),target(hot.target) {}
    UnitView(const UnitView&)=default;
    UnitView& operator=(const UnitView& other) {
        if(this!=&other) { this->~UnitView(); new(this) UnitView(other); }
        return *this;
    }
    bool operator==(const UnitView& other) const { return &core==&other.core && &detail==&other.detail; }
    const Sint32& performance(int ability) const {
        if (ability<0 || ability>=NB_ABILITY) throw std::out_of_range("unit ability");
        const int index=UnitCoreState::abilityIndex(ability);
        return index>=0 ? core.abilities[index] : detail.abilities[UnitState::detailAbilityIndex(ability)];
    }
};

struct ColorRecord { Uint8 r = 0, g = 0, b = 0, a = 255; };
struct TeamView
{
	int number = 0, prestige = 0, startX = 0, startY = 0;
	bool alive = false, won = false, lost = false;
	ColorRecord color;
	std::string firstPlayerName;
	std::string outcomeReasonKey; // Translation key, resolved at the simulation read boundary.
	int unitConversionGained = 0, unitConversionLost = 0, noMoreBuildingSitesCountdown = 0;
	std::array<Sint32, MaterialSlotCount> reservedMaterials{};
	Uint32 mask = 0, allies = 0, enemies = 0, playersMask = 0;
	Uint32 foodVision = 0, exchangeVision = 0, otherVision = 0;
	TeamStat statistics;
	std::array<Sint32, MaterialSlotCount> materials{};
	int workerBalance = 0, starving = 0;
	std::array<int, NB_UNIT_LEVELS> workersLevel{};
	std::vector<BuildingRef> virtualBuildings, swarms;
};

struct BuildingKindView
{
	std::string key, legacyType;
	BuildingType resolvedType;
	std::array<Sint32, MaterialSlotCount> materialMultiplier{};
	BuildingSemantics semantics;
	int width = 0, height = 0, level = 0;
	int previous = -1, next = -1, maximumWorkers = 0, maximumInside = 0;
	int maximumRange = 0, shootingRange = 0;
	bool site = false, available = false;
	bool isVirtual = false, isCloaked = false, suppliesStock = false, fetchesStock = false;
	int shortTypeNum = 0, hpMax = 0, shootSpeed = 0, shootRhythm = 0, decLeft = 0, decTop = 0;
	std::array<Sint32, MaterialSlotCount> maxMaterial{};
	std::array<Sint32, NB_UNIT_TYPE> zonable{};
	Uint64 capabilityMask = 0, rawCapabilityMask = 0;
	int lineagePosition = 0;
};

struct TileView
{
	// Index into the snapshot's cell rules; rule GRASS is a uniform grass cell.
	Uint16 cellRule = GRASS;
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

static_assert(std::is_trivially_copyable_v<BuildProjectView>);
} // namespace SimulationSnapshot
