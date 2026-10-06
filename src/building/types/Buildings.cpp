// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Only the old-save importer uses these frozen tables. New games load the JSON
// manifest; changing installed definitions must never reinterpret old save IDs.
// The 55 historical entries are grouped across four siblings:
//   - BuildingTypesColony.cpp  : swarm, inn, hospital, market
//   - BuildingTypesUpgrade.cpp : racetrack, swimming pool, barracks, school
//   - BuildingTypesDefence.cpp : defencetower, stonewall
//   - BuildingTypesFlags.cpp   : exploration, war and clearing flags
// Each declares one or more BuildingType[] arrays. initLegacy() copies them into
// a flat vector indexed 0..54 in the original data/buildings.txt order, followed
// by the upgraded market variants at 51..54.
//
// Role grouping and ID order do not agree — market sits at the end of the
// table and the flags sit between the defencetower and the stonewall — so a
// role file may hold more than one array. g_tableParts below defines the frozen
// import order; it does not define the order of an installed custom catalog.
//
// Reordering these entries would reinterpret persisted IDs in supported saves.

#include "Material.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <map>
#include <stdexcept>

#include <Toolkit.h>

#include "BuildingType.h"

using namespace GAGCore;

// Defined in the four BuildingTypes*.cpp siblings.
extern BuildingType g_buildingTypesColony[];
extern const std::size_t g_buildingTypesColonyCount;
extern BuildingType g_buildingTypesUpgrade[];
extern const std::size_t g_buildingTypesUpgradeCount;
extern BuildingType g_buildingTypesDefenceTower[];
extern const std::size_t g_buildingTypesDefenceTowerCount;
extern BuildingType g_buildingTypesFlags[];
extern const std::size_t g_buildingTypesFlagsCount;
extern BuildingType g_buildingTypesStoneWall[];
extern const std::size_t g_buildingTypesStoneWallCount;
extern BuildingType g_buildingTypesMarket[];
extern const std::size_t g_buildingTypesMarketCount;

struct TablePart
{
	BuildingType *entries;
	const std::size_t &count;
};

// Frozen flat-index order used for pre-catalog saves.
static const TablePart g_tableParts[] = {
	{ g_buildingTypesColony,       g_buildingTypesColonyCount },        //  0..13
	{ g_buildingTypesUpgrade,      g_buildingTypesUpgradeCount },       // 14..37
	{ g_buildingTypesDefenceTower, g_buildingTypesDefenceTowerCount },  // 38..43
	{ g_buildingTypesFlags,        g_buildingTypesFlagsCount },         // 44..46
	{ g_buildingTypesStoneWall,    g_buildingTypesStoneWallCount },     // 47..48
	{ g_buildingTypesMarket,       g_buildingTypesMarketCount },        // 49..54
};

void BuildingsTypes::init()
{
	loadManifest("data/buildings/manifest.json");
}

void BuildingsTypes::initLegacy()
{
	BuildingsTypes imported;
	imported.catalogKey_ = "stock";
	imported.startingBuildingKey_ = "swarm.0.finished";
	imported.experiments_.push_back({"markets-v2", "Markets V2",
		"Workers fetch shared market stock; upgrades add food and wood, then all materials."});
	for (const TablePart& part : g_tableParts)
		for (std::size_t i = 0; i < part.count; ++i)
		{
			BuildingType bt = part.entries[i];
			bt.gameSpritePtr = bt.miniSpritePtr = nullptr;
			bt.key = bt.type + "." + std::to_string(bt.level) + (bt.isBuildingSite ? ".site" : ".finished");
			bt.previousKey.clear(); bt.nextKey.clear();
			bt.prevLevel = bt.nextLevel = -1;
			imported.entries_->push_back(std::move(bt));
		}
	for (BuildingType& bt : *imported.entries_)
	{
		for (const BuildingType& other : *imported.entries_)
		{
			if (other.type != bt.type) continue;
			if (bt.isBuildingSite && !other.isBuildingSite && other.level == bt.level)
				bt.nextKey = other.key;
			if (!bt.isBuildingSite && other.isBuildingSite && other.level == bt.level + 1)
				bt.nextKey = other.key;
			if (!bt.isBuildingSite && other.isBuildingSite && other.level == bt.level)
				bt.previousKey = other.key;
			if (bt.isBuildingSite && !other.isBuildingSite && other.level == bt.level - 1)
				bt.previousKey = other.key;
		}
		// Identity tests are confined to this frozen old-save importer.
		if (bt.type == "market" && bt.level > 0) bt.requiredExperiment = "markets-v2";
		bt.presentation.displayName = bt.type;
		static const std::map<std::string, std::array<int,6>> stockAssigned = {
			{"swarm", {7,4,0,0,0,0}},
			{"inn", {3,2,5,3,15,8}},
			{"hospital", {2,0,4,0,6,0}},
			{"racetrack", {3,0,7,0,12,0}},
			{"swimmingpool", {2,0,5,0,12,0}},
			{"barracks", {3,0,6,0,9,0}},
			{"school", {5,0,10,0,20,0}},
			{"defencetower", {3,2,5,2,8,2}},
			{"stonewall", {1,0,0,0,0,0}},
			{"market", {3,3,0,0,0,0}},
			{"warflag", {0,10,0,0,0,0}},
			{"clearingflag", {0,5,0,0,0,0}},
			{"explorationflag", {0,2,0,0,0,0}},
		};
		bt.presentation.defaultAssigned = stockAssigned.at(bt.type)[bt.level*2+!bt.isBuildingSite];

		bt.presentation.iconFrame = bt.type == "market" ? 11 : std::clamp(bt.shortTypeNum, 0, 10);
		bt.presentation.iconTile = bt.type == "stonewall";
		bt.presentation.iconPriority = bt.type == "defencetower" ? 150 : bt.type == "swarm" ? 120 : 100;
		bt.presentation.skinSlot = bt.type == "swarm" && !bt.isBuildingSite ? "swarm" : "";
		if (bt.crossConnectMultiImage) bt.presentation.connectionGroup = bt.key;
		BuildingSemantics& p = bt.semantics;
		p.replenishMaterialMask=0;
		for (int r=0; r<MaterialCount; ++r) if (bt.maxMaterial[r]>0) p.replenishMaterialMask|=1u<<r;
		p.market.suppliesStockMask=p.market.suppliesDirectStockMask=p.replenishMaterialMask;
		p.market.fetchesStockMask=p.market.fetchesDirectStockMask=p.replenishMaterialMask;
		p.requiredWorkerLevel = bt.level;
		p.assignmentLimit = bt.maxUnitWorking ? 20 : 0;
		p.regenerationPerTick = bt.unitProductionTime ? 1 : 0;
		p.repairable = !bt.isBuildingSite && !bt.previousKey.empty();
		if (bt.isBuildingSite) std::copy_n(bt.maxMaterial, MAX_NB_RESOURCES, p.constructionCost.begin());
		if (p.repairable)
			for (const auto& site : *imported.entries_)
				if (site.key == bt.previousKey) std::copy_n(site.maxMaterial, MAX_NB_RESOURCES, p.repairCost.begin());
		p.placeable = bt.previousKey.empty();
		p.instantPlacement = p.relocatable = bt.isVirtual;
		p.occupiesGround = !bt.isVirtual;
		p.workPriorityBias = (bt.canFeedUnit ? 2 : 1) + bt.level * 10;
		p.sightSharing = bt.canExchange ? BuildingSightSharing::Exchange :
			bt.canFeedUnit ? BuildingSightSharing::Food : BuildingSightSharing::Other;
		p.feeding.enabled = bt.canFeedUnit;
		p.feeding.duration = bt.timeToFeedUnit;
		if (bt.canFeedUnit)
		{
			p.feeding.cost[materialIndex(MaterialId::Food)] = 1;
			p.feeding.partial = BuildingPartialService::ProportionalFullCost;
			p.feeding.holdAdmissionUntilExit = true;
			p.feeding.optionalFruitMask = (1u << HAPPINESS_COUNT) - 1;
			p.feeding.convertsUnits = true;
		}
		p.healing.enabled = bt.canHealUnit;
		p.healing.duration = bt.timeToHealUnit;
		if (bt.canHealUnit) p.healing.partial = BuildingPartialService::ProportionalFullCost;
		p.trainingInParallel = bt.upgradeInParallel;
		for (int a = 0; a < NB_ABILITY; ++a)
		{
			p.training[a].enabled = bt.upgrade[a];
			p.training[a].duration = bt.upgradeTime[a];
			p.training[a].targetLevel = bt.upgrade[a] ? bt.level + 1 : 0;
			if (a == BUILD && bt.upgrade[a]) p.training[a].constructionLevel = bt.level + 1;
		}
		p.production.scheduling = BuildingProductionScheduling::WeightedLateChoice;
		for (auto& recipe : p.production.recipes)
		{
			recipe.enabled = bt.unitProductionTime != 0;
			recipe.duration = bt.unitProductionTime;
			if (recipe.enabled) recipe.cost[materialIndex(MaterialId::Food)] = bt.foodPerUnit;
		}
		p.market.sharedStock = bt.useTeamMaterials;
		p.market.interTeamFruitExchange = bt.canExchange;
		p.market.suppliesDirectStock = bt.canExchange;
		p.market.fetchesDirectStock = bt.canFeedUnit;
		p.market.suppliesStock = bt.canExchange;
		p.market.suppliesStockExperiment = "markets-v2";
		p.market.fetchesStock = bt.type != "market";
		p.market.fetchesStockExperiment = "markets-v2";
		p.projectileDamage.fill(bt.shootDamage);
		p.projectileBuildingDamage = bt.shootDamage;
	}
	imported.resolveAndValidate();
	*this = std::move(imported);
}

void BuildingsTypes::loadSprites()
{
	loadSpritesForTypes(*entries_);
}

void BuildingsTypes::loadSpritesForTypes(std::vector<BuildingType>& types)
{
	// Simulation-only catalogs do not require graphics. Callers that publish a
	// rendered Scene bind installed artwork here before exposing descriptors.
	const std::size_t count = types.size();
	for (std::size_t i = 0; i < count; ++i)
	{
		BuildingType *bt = &types[i];
		bt->gameSpritePtr = Toolkit::getSprite(bt->gameSprite.c_str());
		if (!bt->gameSpritePtr) throw std::runtime_error("Cannot load building sprite: " + bt->gameSprite);
		if (bt->miniSpriteImage >= 0)
		{
			bt->miniSpritePtr = Toolkit::getSprite(bt->miniSprite.c_str());
			if (!bt->miniSpritePtr) throw std::runtime_error("Cannot load building mini sprite: " + bt->miniSprite);
		}
	}
}

BuildingType *BuildingsTypes::get(std::size_t id)
{
	return const_cast<BuildingType*>(static_cast<const BuildingsTypes&>(*this).get(id));
}

const BuildingType *BuildingsTypes::get(std::size_t id) const
{
	if (id >= entries_->size()) throw std::out_of_range("Invalid building type ID: " + std::to_string(id));
	return &(*entries_)[id];
}

Sint32 BuildingsTypes::findByKey(const std::string& key) const
{
	for (std::size_t i = 0; i < entries_->size(); ++i)
		if ((*entries_)[i].key == key) return static_cast<Sint32>(i);
	return -1;
}

bool BuildingsTypes::isAvailable(std::size_t id, const std::set<std::string>& enabled) const
{
	if (id >= entries_->size()) return false;
	const auto& key = (*entries_)[id].requiredExperiment;
	return key.empty() || enabled.count(key) != 0;
}

BuildingType *BuildingsTypes::getLastLevel(Sint32 typeNum)
{
    return get(get(typeNum)->terminalTypeNum);
}

Sint32 BuildingsTypes::getTypeNum(const char *type, int level, bool isBuildingSite)
{
	assert(type);
	if (!*type) return -1;
	const std::size_t count = entries_->size();
	for (std::size_t i = 0; i < count; ++i)
	{
		const BuildingType *bt = &(*entries_)[i];
		if (bt->type == type && bt->level == level && (bt->isBuildingSite != 0) == isBuildingSite)
			return static_cast<Sint32>(i);
	}
	// Reachable when the caller asks for a flag (which has only one variant).
	return -1;
}

Sint32 BuildingsTypes::getTypeNum(const std::string &s, int level, bool isBuildingSite)
{
	return getTypeNum(s.c_str(), level, isBuildingSite);
}

Sint32 BuildingsTypes::getPlaceableTypeNum(const std::string &name)
{
	if (name.empty()) return -1;
	const int keyed = findByKey(name);
	if (keyed >= 0) return get(keyed)->semantics.placeable ? keyed : -1;
	for (std::size_t i = 0; i < entries_->size(); ++i)
		if ((*entries_)[i].type == name && (*entries_)[i].semantics.placeable)
			return static_cast<Sint32>(i);
	return -1;
}

Sint32 BuildingsTypes::getFinishedTypeNum(const std::string &name)
{
	if (name.empty()) return -1;
	Sint32 typeNum = findByKey(name);
	if (typeNum < 0) typeNum = getPlaceableTypeNum(name);
	if (typeNum < 0) return -1;
	while (get(typeNum)->isBuildingSite)
	{
		typeNum = get(typeNum)->nextLevel;
		if (typeNum < 0) return -1;
	}
	return typeNum;
}

BuildingType *BuildingsTypes::getByType(const char *type, int level, bool isBuildingSite)
{
	assert(type);
	if (!*type) return nullptr;
	// A stable key identifies a concrete variant, so its authored tier is already explicit.
	if (const int keyed=findByKey(type); keyed>=0)
	{
		const int resolved=isBuildingSite ? keyed : getFinishedTypeNum(type);
		return resolved>=0 && (get(resolved)->isBuildingSite!=0)==isBuildingSite ? get(resolved) : nullptr;
	}
	const std::size_t count = entries_->size();
	for (std::size_t i = 0; i < count; ++i)
	{
		BuildingType *bt = &(*entries_)[i];
		if (bt->type == type && bt->level == level && (bt->isBuildingSite != 0) == isBuildingSite)
			return bt;
	}
	return nullptr;
}

BuildingType *BuildingsTypes::getByType(const std::string &s, int level, bool isBuildingSite)
{
	return getByType(s.c_str(), level, isBuildingSite);
}

void BuildingsTypes::configureExperiments(const std::vector<std::string>& keys)
{
	const std::set<std::string> enabled(keys.begin(), keys.end());
	const auto permitted = [&](const std::string& key) { return key.empty() || enabled.count(key) != 0; };
	usesMarketRouting_ = false;
	stockSupplyMask_ = directSupplyMask_ = extraDirectSupplyMask_ = 0;
	usesOverlaySuppliers_ = false;
	for (auto& b : *entries_)
	{
		b.runtimeAvailable = permitted(b.requiredExperiment);
		b.runtimeSuppliesStock = b.runtimeAvailable && b.semantics.market.suppliesStock && permitted(b.semantics.market.suppliesStockExperiment);
		b.runtimeFetchesStock = b.runtimeAvailable && b.semantics.market.fetchesStock && permitted(b.semantics.market.fetchesStockExperiment);
		b.runtimeSuppliesDirectStock = b.runtimeAvailable && b.semantics.market.suppliesDirectStock;
		b.runtimeSuppliesStockMask=b.runtimeSuppliesStock ? b.semantics.market.suppliesStockMask : 0;
		b.runtimeSuppliesDirectStockMask=b.runtimeSuppliesDirectStock ? b.semantics.market.suppliesDirectStockMask : 0;
		b.runtimeFetchesStockMask=b.runtimeFetchesStock ? b.semantics.market.fetchesStockMask : 0;
		b.runtimeFetchesDirectStockMask=b.runtimeAvailable && b.semantics.market.fetchesDirectStock
			? b.semantics.market.fetchesDirectStockMask : 0;
		b.runtimeSuppliesStock=b.runtimeSuppliesStockMask!=0;
		b.runtimeSuppliesDirectStock=b.runtimeSuppliesDirectStockMask!=0;
		b.runtimeFetchesStock=b.runtimeFetchesStockMask!=0;
		b.runtimeFetchesDirectStock=b.runtimeFetchesDirectStockMask!=0;
		stockSupplyMask_ |= b.runtimeSuppliesStockMask;
		directSupplyMask_ |= b.runtimeSuppliesDirectStockMask;
		extraDirectSupplyMask_ |= b.runtimeSuppliesDirectStockMask & ~b.runtimeSuppliesStockMask;
		usesMarketRouting_ = usesMarketRouting_ || b.runtimeSuppliesStock;
		usesOverlaySuppliers_ = usesOverlaySuppliers_ || (b.runtimeSuppliesStock && !b.semantics.occupiesGround);
	}
	compileRuntimeTraits();
}

BuildingsTypes::BuildingsTypes(const BuildingsTypes& other)
	: runtimeTypes_(other.runtimeTypes_), entries_(std::make_shared<std::vector<BuildingType>>(*other.entries_)),
	  experiments_(other.experiments_), catalogKey_(other.catalogKey_),
	  startingBuildingKey_(other.startingBuildingKey_), startingBuildingId_(other.startingBuildingId_), stockSupplyMask_(other.stockSupplyMask_), directSupplyMask_(other.directSupplyMask_), extraDirectSupplyMask_(other.extraDirectSupplyMask_), usesMarketRouting_(other.usesMarketRouting_), usesOverlaySuppliers_(other.usesOverlaySuppliers_)
{
}

BuildingsTypes& BuildingsTypes::operator=(const BuildingsTypes& other)
{
	if (this != &other)
	{
		BuildingsTypes copy(other);
		*this = std::move(copy);
	}
	return *this;
}


void BuildingsTypes::compileRuntimeTraits()
{
    runtimeTypes_.resize(entries_->size());
    for (std::size_t id=0; id<entries_->size(); ++id)
    {
        const auto& b=(*entries_)[id]; const auto& s=b.semantics;
        auto& hot=runtimeTypes_[id]; hot={};
        hot.hpMax=b.hpMax; hot.armor=b.armor; hot.regenerationPerTick=s.regenerationPerTick;
        hot.projectileDamage=s.projectileDamage; hot.projectileBuildingDamage=s.projectileBuildingDamage;
        hot.shootSpeed=b.shootSpeed; hot.workPriorityBias=s.workPriorityBias;
        for (int ability=0; ability<NB_ABILITY; ++ability) if (s.training[ability].enabled) hot.trainingMask|=1u<<ability;
        hot.width=b.width; hot.height=b.height; hot.decLeft=b.decLeft; hot.decTop=b.decTop;
        hot.shootRhythm=b.shootingRange ? b.shootRhythm : 0; hot.shootingRange=b.shootingRange; hot.assignmentLimit=s.assignmentLimit;
        hot.flags=(s.occupiesGround ? BuildingRuntimeTraits::OccupiesGround : 0)
            | (b.useTeamMaterials ? BuildingRuntimeTraits::SharedStock : 0) | (b.isBuildingSite ? BuildingRuntimeTraits::Site : 0)
            | (s.feeding.enabled ? BuildingRuntimeTraits::Feeds : 0) | (s.healing.enabled ? BuildingRuntimeTraits::Heals : 0)
            | (s.trainingInParallel ? BuildingRuntimeTraits::TrainingParallel : 0) | (b.runtimeAvailable ? BuildingRuntimeTraits::Available : 0);
        hot.admittedUnitMask=s.admittedUnitMask; hot.requiredWorkerLevel=s.requiredWorkerLevel;
        for (int unit=0; unit<NB_UNIT_TYPE; ++unit) if (b.zonable[unit]) hot.attractionMask|=1u<<unit;
        hot.suppliesStockMask=b.runtimeSuppliesStockMask; hot.suppliesDirectStockMask=b.runtimeSuppliesDirectStockMask;
        hot.fetchesStockMask=b.runtimeFetchesStockMask; hot.fetchesDirectStockMask=b.runtimeFetchesDirectStockMask;
        hot.replenishMaterialMask=s.replenishMaterialMask; hot.productionEnabledMask=s.production.enabledUnitMask;
        if (s.production.scheduling==BuildingProductionScheduling::WeightedCommittedJob) hot.flags|=BuildingRuntimeTraits::CommittedProduction;
    }
}
