// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include "Ressource.h"
#include "UnitConsts.h"

struct BuildingCatalogExperiment
{
	std::string key, label, help;
};

// Costs are integer units of the existing resource kinds, in their wire order.
// Admission reservations and shared inventories belong to game state, not here.
using BuildingResourceCost = std::array<Sint32, MAX_NB_RESOURCES>;
constexpr unsigned BUILDING_ALL_UNIT_TYPES = (1u << NB_UNIT_TYPE) - 1;

enum class BuildingProductionScheduling { WeightedLateChoice, WeightedCommittedJob };
enum class BuildingPartialService { None, ProportionalFullCost };
enum class BuildingSightSharing { Other, Food, Exchange };

struct BuildingServiceSpec
{
	bool enabled = false;
	unsigned unitMask = BUILDING_ALL_UNIT_TYPES;
	Sint32 duration = 0;
	BuildingResourceCost cost{};
	std::uint16_t costMask = 0; // compiled, excluded from authored snapshots
	BuildingPartialService partial = BuildingPartialService::None;
	bool holdAdmissionUntilExit = false;
	unsigned optionalFruitMask = 0;
	bool convertsUnits = false;
};

struct BuildingTrainingSpec
{
	bool enabled = false;
	unsigned unitMask = BUILDING_ALL_UNIT_TYPES;
	Sint32 targetLevel = 0, duration = 0;
	Sint32 constructionLevel = -1; // -1 leaves independent construction qualification unchanged
	BuildingResourceCost cost{};
	std::uint16_t costMask = 0; // compiled, excluded from authored snapshots
};

struct BuildingProductionRecipe
{
	bool enabled = false;
	Sint32 duration = 0;
	BuildingResourceCost cost{};
	std::uint16_t costMask = 0; // compiled, excluded from authored snapshots
};

struct BuildingProductionSpec
{
	BuildingProductionScheduling scheduling = BuildingProductionScheduling::WeightedCommittedJob;
	std::array<BuildingProductionRecipe, NB_UNIT_TYPE> recipes{};
	unsigned enabledUnitMask = 0; // compiled, excluded from snapshots
	Sint32 fallbackUnit = WORKER;
	std::array<Sint32, NB_UNIT_TYPE> initialRatios{{1, 0, 0}};
};

struct BuildingMarketSpec
{
	std::uint8_t suppliesStockMask = 255, suppliesDirectStockMask = 255;
	std::uint8_t fetchesStockMask = 255, fetchesDirectStockMask = 255;
	bool sharedStock = false;
	bool interTeamFruitExchange = false;
	bool suppliesDirectStock = false;
	bool fetchesDirectStock = false;
	bool suppliesStock = false;
	std::string suppliesStockExperiment;
	bool fetchesStock = false;
	std::string fetchesStockExperiment;
	Sint32 pickupPenalty = 5;
};

struct BuildingPresentationSpec
{
	std::string displayName, skinSlot, connectionGroup;
	Sint32 iconFrame = 0, iconPriority = 100;
	Sint32 defaultAssigned = -1; // omitted preference uses min(2, assignmentLimit)
	bool iconTile = false, showLevel = true, connectsAcrossTeams = false;
	Sint32 connectionGroupId = -1; // compiled, excluded from snapshots
};

struct BuildingSemantics
{
	std::uint8_t replenishResourceMask = 255;
	Sint32 requiredWorkerLevel = 0;
	Sint32 assignmentLimit = 0;
	Sint32 regenerationPerTick = 0;
	bool repairable = false;
	BuildingResourceCost constructionCost{}, repairCost{};
	bool placeable = false;
	bool instantPlacement = false;
	bool relocatable = false;
	bool occupiesGround = true;
	unsigned admittedUnitMask = BUILDING_ALL_UNIT_TYPES;
	Sint32 workPriorityBias = 1;
	BuildingSightSharing sightSharing = BuildingSightSharing::Other;
	BuildingServiceSpec feeding, healing;
	std::array<BuildingTrainingSpec, NB_ABILITY> training{};
	std::uint16_t trainingCostMask = 0; // compiled union; not serialized
	BuildingProductionSpec production;
	BuildingMarketSpec market;
	std::array<Sint32, NB_UNIT_TYPE> projectileDamage{};
	Sint32 projectileBuildingDamage = 0;
	Sint32 ammunitionResource = STONE, ammunitionCost = 1;
	// This is independent of upgrade tier; each training output names its level.
	bool trainingInParallel = false;
};

// Immutable, dense simulation projection. No strings, graphics handles, recipes
// or variable-size storage enter this single cache line.
struct alignas(64) BuildingRuntimeTraits
{
    Sint32 hpMax=0, armor=0, regenerationPerTick=0;
    std::array<Sint32,NB_UNIT_TYPE> projectileDamage{};
    Sint32 shootSpeed=0, projectileBuildingDamage=0, workPriorityBias=0;
    std::uint32_t trainingMask=0;
    Sint16 width=0,height=0,decLeft=0,decTop=0;
    Uint16 shootRhythm=0,assignmentLimit=0,shootingRange=0;
    Uint8 flags=0,admittedUnitMask=0,attractionMask=0,requiredWorkerLevel=0;
    Uint8 suppliesStockMask=0,suppliesDirectStockMask=0,fetchesStockMask=0,fetchesDirectStockMask=0;
    Uint8 replenishResourceMask=0,productionEnabledMask=0;
    enum Flag : Uint8 { OccupiesGround=1,SharedStock=2,Site=4,Feeds=8,Heals=16,TrainingParallel=32,Available=64,CommittedProduction=128 };
    bool has(Flag flag) const { return flags&flag; }
    bool attracts(int unit) const { return attractionMask&(1u<<unit); }
};
static_assert(sizeof(BuildingRuntimeTraits)==64);
static_assert(NB_ABILITY<=32 && NB_UNIT_TYPE<=8 && MAX_RESOURCES<=8);
