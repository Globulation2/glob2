// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include "Ressource.h"
#include "Material.h"
#include "UnitConsts.h"

struct BuildingCatalogExperiment
{
	std::string key, label, help;
};

// Costs are integer units of the existing resource kinds, in their wire order.
// Admission reservations and shared inventories belong to game state, not here.
using BuildingMaterialCost = std::array<Sint32, MaterialSlotCount>;

constexpr unsigned BUILDING_ALL_UNIT_TYPES = (1u << NB_UNIT_TYPE) - 1;

// Authored stable keys resolve once against the match's immutable unit catalog.
// The historical three-bit mask remains an import and presentation field only.
struct BuildingUnitSelection
{
    bool specified = false;
    std::vector<std::string> keys;
    std::vector<Uint8> resolved;
    bool matches(unsigned unit, unsigned legacyMask) const {
        return specified ? unit<resolved.size() && resolved[unit]
            : legacyMask==BUILDING_ALL_UNIT_TYPES || (unit<NB_UNIT_TYPE && (legacyMask&(1u<<unit)));
    }
};

enum class BuildingProductionScheduling { WeightedLateChoice, WeightedCommittedJob };
enum class BuildingPartialService { None, ProportionalFullCost };
enum class BuildingSightSharing { Other, Food, Exchange };

struct BuildingServiceSpec
{
	bool enabled = false;
	unsigned unitMask = BUILDING_ALL_UNIT_TYPES;
	BuildingUnitSelection units;
	Sint32 duration = 0;
	BuildingMaterialCost cost{};
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
	BuildingUnitSelection units;
	Sint32 targetLevel = 0, duration = 0;
	Sint32 constructionLevel = -1; // -1 leaves independent construction qualification unchanged
	BuildingMaterialCost cost{};
	std::uint16_t costMask = 0; // compiled, excluded from authored snapshots
};

struct BuildingProductionRecipe
{
	bool enabled = false;
	Sint32 duration = 0;
	BuildingMaterialCost cost{};
	std::uint16_t costMask = 0; // compiled, excluded from authored snapshots
	bool costExplicit = true; // omitted JSON cost inherits the unit's production cost
};

struct BuildingProductionSpec
{
	BuildingProductionScheduling scheduling = BuildingProductionScheduling::WeightedCommittedJob;
	std::vector<BuildingProductionRecipe> recipes = std::vector<BuildingProductionRecipe>(NB_UNIT_TYPE);
	std::vector<Uint16> enabledUnits; // compiled cold iteration index
	std::map<std::string,BuildingProductionRecipe> additionalRecipes;
	unsigned enabledUnitMask = 0; // compiled, excluded from snapshots
	Sint32 fallbackUnit = WORKER;
	std::vector<Sint32> initialRatios{1, 0, 0};
	std::map<std::string,Sint32> additionalInitialRatios;
};

struct BuildingMarketSpec
{
	MaterialMask suppliesStockMask = (1u << MaterialCount) - 1, suppliesDirectStockMask = (1u << MaterialCount) - 1;
	MaterialMask fetchesStockMask = (1u << MaterialCount) - 1, fetchesDirectStockMask = (1u << MaterialCount) - 1;
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

// Optional cold catalog data; does not enlarge BuildingRuntimeTraits.
struct BuildingAreaEffectsSpec
{
    Sint32 radius = 0;
    BuildingMaterialCost cost{};
    std::uint16_t costMask = 0;
    Sint32 healingQ8 = 0, damageQ8 = 0, feedingQ8 = 0;
    // Positive bonuses and negative weaknesses are independently authored.
    Sint32 attackBuffBps = 0, attackWeaknessBps = 0;
    Sint32 armorBuffBps = 0, armorWeaknessBps = 0;
    Sint32 fertilityBuffBps = 0, fertilityWeaknessBps = 0;
    bool enabled() const {
        return healingQ8 || damageQ8 || feedingQ8 || attackBuffBps || attackWeaknessBps ||
            armorBuffBps || armorWeaknessBps || fertilityBuffBps || fertilityWeaknessBps;
    }
    bool operator==(const BuildingAreaEffectsSpec&) const = default;
};

struct BuildingSemantics
{
	MaterialMask replenishMaterialMask = (1u << MaterialCount) - 1;
	Sint32 requiredWorkerLevel = 0;
	Sint32 assignmentLimit = 0;
	Sint32 regenerationPerTick = 0;
	bool repairable = false;
	BuildingMaterialCost constructionCost{}, repairCost{};
	bool placeable = false;
	bool instantPlacement = false;
	bool relocatable = false;
	bool occupiesGround = true;
	unsigned admittedUnitMask = BUILDING_ALL_UNIT_TYPES;
	BuildingUnitSelection admittedUnits;
	std::array<BuildingUnitSelection,3> attractionUnits; // clear, explore, defend jobs
	Sint32 workPriorityBias = 1;
	BuildingSightSharing sightSharing = BuildingSightSharing::Other;
	BuildingServiceSpec feeding, healing;
	BuildingAreaEffectsSpec areaEffects;
	std::array<BuildingTrainingSpec, NB_ABILITY> training{};
	std::uint16_t trainingCostMask = 0; // compiled union; not serialized
	BuildingProductionSpec production;
	BuildingMarketSpec market;
	std::array<Sint32, NB_UNIT_TYPE> projectileDamage{};
	std::map<std::string,Sint32> additionalProjectileDamage;
	std::vector<Sint32> resolvedProjectileDamage;
	Sint32 projectileBuildingDamage = 0;
	Sint32 ammunitionMaterial = materialIndex(MaterialId::Stone), ammunitionCost = 1;
	// This is independent of upgrade tier; each training output names its level.
	bool trainingInParallel = false;
};

struct BuildingUnitInteraction
{
    enum Flag : Uint8 { Admitted=1,Feeds=2,Heals=4,Clear=8,Explore=16,Defend=32,Produces=64 };
    Sint32 projectileDamage=0;
    std::uint32_t trainingMask=0;
    Uint8 flags=0;
    // New-hire policy consumes existing padding; semantic flags remain stable.
    Uint8 recruitmentMask=0;
    bool has(Flag flag) const { return flags&flag; }
    bool recruits(unsigned role) const { return role<3 && (recruitmentMask&(1u<<role)); }
};
static_assert(sizeof(BuildingUnitInteraction)==12);

// Immutable, dense simulation projection. No strings, graphics handles, recipes
// or variable-size storage enter this single cache line.
struct alignas(64) BuildingRuntimeTraits
{
    const BuildingUnitInteraction* interactions=nullptr;
    Sint32 hpMax=0, armor=0, regenerationPerTick=0;
    Sint32 shootSpeed=0, projectileBuildingDamage=0, workPriorityBias=0;
    std::uint32_t trainingMask=0;
    Uint8 width=0,height=0;
    Sint8 decLeft=0,decTop=0;
    Uint16 shootRhythm=0,assignmentLimit=0,shootingRange=0;
    MaterialMask suppliesStockMask=0,suppliesDirectStockMask=0,fetchesStockMask=0,fetchesDirectStockMask=0;
    MaterialMask replenishMaterialMask=0;
    Uint16 unitCount=0;
    Uint8 flags=0,requiredWorkerLevel=0,productionEnabledMask=0;
    Uint8 admittedUnitMask=0,attractionMask=0,attractionRoles=0;
    enum Flag : Uint8 { OccupiesGround=1,SharedStock=2,Site=4,Feeds=8,Heals=16,TrainingParallel=32,Available=64,CommittedProduction=128 };
    bool has(Flag flag) const { return flags&flag; }
    const BuildingUnitInteraction& interaction(unsigned unit) const {
        static constexpr BuildingUnitInteraction absent{};
        return unit<unitCount ? interactions[unit] : absent;
    }
    Sint32 damage(unsigned unit) const { return interaction(unit).projectileDamage; }
    bool attracts(unsigned unit) const { return interaction(unit).flags&(BuildingUnitInteraction::Clear|BuildingUnitInteraction::Explore|BuildingUnitInteraction::Defend); }
    bool attractsRole(unsigned role) const { return role<3 && (attractionRoles&(1u<<role)); }
    bool produces(unsigned unit) const { return interaction(unit).has(BuildingUnitInteraction::Produces); }
};
static_assert(sizeof(BuildingRuntimeTraits)==64);
static_assert(NB_ABILITY<=32 && MaterialCount<=16);
