// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <GAGSys.h>
#include <cstddef>
#include <string>
#include <set>
#include <vector>
#include <memory>
#include "BuildingCatalog.h"

#include "Ressource.h"
#include "UnitConsts.h"

namespace GAGCore { class Sprite; }
using GAGCore::Sprite;

// Runtime descriptor of one concrete building variant. The catalog owns these
// values and resolves links once, before a match starts. No parsing or string
// lookup belongs in a simulation loop. Legacy scalar fields remain during the
// consumer migration; semantic descriptors remove the old family assumptions.
struct BuildingType
{
	// basic infos
	std::string type = "null";

	// visualisation
	std::string gameSprite = "ERROR_NO_GAME_SPRITE_DEFINED";
	Sint32 gameSpriteImage = 0;
	Sint32 gameSpriteCount = 1;
	std::string miniSprite = "ERROR_NO_MINI_SPRITE_DEFINED";
	Sint32 miniSpriteImage = 0;

	Sint32 hueImage = 0; // bool. The way we show the building's team (false=we draw a flag, true=we hue all the sprite)
	Sint32 flagImage = 49;
	Sint32 crossConnectMultiImage = 0; // If true, mean we have a wall-like building

	// could be Uint8, if non 0 tell the number of maximum units locked by building for:
	// by order of priority (top = max)
	Sint32 upgrade[NB_ABILITY] = {}; // What kind on units can be upgraded here
	Sint32 upgradeTime[NB_ABILITY] = {}; // Time to upgrade an unit, given the upgrade type needed.
	Sint32 upgradeInParallel = 0; // if true, can learn all upgrades with one learning time into the building
	Sint32 foodable = 0;
	Sint32 fillable = 0;
	Sint32 zonable[NB_UNIT_TYPE] = {}; // If an unit is required for a presence.
	Sint32 zonableForbidden = 0;

	Sint32 canFeedUnit = 0;
	Sint32 timeToFeedUnit = 0;
	Sint32 canHealUnit = 0;
	Sint32 timeToHealUnit = 0;
	Sint32 insideSpeed = 12;
	Sint32 canExchange = 0;
	Sint32 useTeamResources = 0;

	Sint32 width = 0, height = 0; // Uint8, size in square
	Sint32 decLeft = 0, decTop = 0;
	Sint32 isVirtual = 0; // bool, doesn't occupy ground occupation map, used for war-flag and exploration-flag.
	Sint32 isCloaked = 0; // bool, graphically invisible for enemy.
	Sint32 shootingRange = 0; // Uint8, if 0 can't shoot
	Sint32 shootDamage = 0; // Uint8
	Sint32 shootSpeed = 0; // Uint8, the actual speed at which the shots fly through the air.
	Sint32 shootRhythm = 0; // Uint8, The frequency with which a tower fires. It fires once every
	                        // SHOOTING_COOLDOWN_MAX/shootRhythm ticks.
	Sint32 maxBullets = 0;
	Sint32 multiplierStoneToBullets = 0; // The tower gets this many bullets every time a worker delivers stone to it.

	Sint32 unitProductionTime = 0; // Uint8, nb tick to produce one unit
	Sint32 resourceForOneUnit = 0; // The amount of wheat consumed in the production of a unit.

	Sint32 maxResource[MAX_NB_RESOURCES] = {};
	// multiplierResource defaults: 1 for the basic 5 (wood/wheat/papyrus/stone/algue), 10 for fruits 0..9.
	Sint32 multiplierResource[MAX_NB_RESOURCES] = { 1, 1, 1, 1, 1, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10 };
	Sint32 maxUnitInside = 0;
	Sint32 maxUnitWorking = 0;

	Sint32 hpInit = 0; // (Uint16) Initial HP of the building. This is generally equal to hpMax for completed buildings,
	                   // equal to 1 for newly created buildings, and equal to the hpMax of the original building for
	                   // upgrading buildings.
	Sint32 hpMax = 0;
	Sint32 hpInc = 0; // The amount by which the building's hitpoints are incremented when a resource is added to it,
	                  // for buildings under construction.
	Sint32 armor = 0; // (Uint8) Any damage the building takes is reduced by this much, although it has a minumum of 1
	                  // for most damage, 0 only for Explorers.
	Sint32 level = 0; // (Uint8)
	Sint32 shortTypeNum = 0; // Frozen family alias for old saves, authored scripts and legacy statistics only.
	Sint32 isBuildingSite = 0;

	// Flag useful
	Sint32 defaultUnitStayRange = 0;
	Sint32 maxUnitStayRange = 0;

	Sint32 viewingRange = 1;
	Sint32 regenerationSpeed = 0;

	Sint32 prestige = 0;

	// Regenerated parameters — set by BuildingsTypes::init() at startup, not part of the data table.
	Sprite *gameSpritePtr = nullptr;
	Sprite *miniSpritePtr = nullptr;
	int prevLevel = -1;
	int nextLevel = -1;

	// Stable authored identity and explicit variant transitions. Numeric IDs are
	// dense within the immutable match snapshot, never inferred from filenames.
	std::string key;
	std::string previousKey;
	std::string nextKey;
	std::string requiredExperiment;
	BuildingSemantics semantics;
	BuildingPresentationSpec presentation;
	// Effective match gates; rebuilt once from the saved feature keys.
	Sint32 terminalTypeNum = -1; // compiled final forward successor
	std::uint8_t runtimeSuppliesStockMask = 0, runtimeSuppliesDirectStockMask = 0;
	std::uint8_t runtimeFetchesStockMask = 0, runtimeFetchesDirectStockMask = 0;
	bool runtimeSuppliesDirectStock = false, runtimeFetchesDirectStock = false;
	bool runtimeAvailable = true;
	bool runtimeSuppliesStock = false;
	bool runtimeFetchesStock = false;

};

// Value-owned catalog: copying it makes independent descriptors and indexes.
// Sprite handles refer to Toolkit-owned graphics and are deliberately non-owning.
class BuildingsTypes
{
public:
	BuildingsTypes() = default;
	BuildingsTypes(const BuildingsTypes& other);
	BuildingsTypes& operator=(const BuildingsTypes& other);
	BuildingsTypes(BuildingsTypes&&) noexcept = default;
	BuildingsTypes& operator=(BuildingsTypes&&) noexcept = default;
	std::shared_ptr<const std::vector<BuildingType>> retainTypes() const { return entries_; }
	void init();
	// Frozen pre-catalog definitions for importing older supported saves. Never
	// reads installed JSON files or shares mutable descriptors with another game.
	void initLegacy();
	void loadManifest(const std::string& path);
	void loadSnapshotJson(const std::string& json);
	std::string snapshotJson() const;
	std::string fingerprint() const;
	const std::string& catalogKey() const { return catalogKey_; }
	Sint32 getStartingBuildingTypeNum() const { return startingBuildingId_; }
	const std::vector<BuildingCatalogExperiment>& experiments() const { return experiments_; }
	// Returns -1 when no stable key matches.
	Sint32 findByKey(const std::string& key) const;
	bool isAvailable(std::size_t id, const std::set<std::string>& enabledExperiments) const;
	void configureExperiments(const std::vector<std::string>& keys);
	std::uint8_t stockSupplyMask() const { return stockSupplyMask_; }
	std::uint8_t directSupplyMask() const { return directSupplyMask_; }
	std::uint8_t extraDirectSupplyMask() const { return extraDirectSupplyMask_; }
	bool usesMarketRouting() const { return usesMarketRouting_; }
	bool usesOverlaySuppliers() const { return usesOverlaySuppliers_; }
	void loadSprites();
	static void loadSpritesForTypes(std::vector<BuildingType>& types);
	const BuildingRuntimeTraits* getRuntime(std::size_t id) const { return &runtimeTypes_.at(id); }
	BuildingType *get(std::size_t id);
	const BuildingType *get(std::size_t id) const;
	std::size_t size() const { return entries_->size(); }
	BuildingType *getLastLevel(Sint32 typeNum);
	Sint32 getTypeNum(const char *type, int level, bool isBuildingSite);
	Sint32 getTypeNum(const std::string &s, int level, bool isBuildingSite);
	Sint32 getPlaceableTypeNum(const std::string &name);
	Sint32 getFinishedTypeNum(const std::string &name);
	BuildingType *getByType(const char *type, int level, bool isBuildingSite);
	BuildingType *getByType(const std::string &s, int level, bool isBuildingSite);
private:
	void resolveAndValidate();
	void compileRuntimeTraits();
	std::vector<BuildingRuntimeTraits> runtimeTypes_;
	std::shared_ptr<std::vector<BuildingType>> entries_ = std::make_shared<std::vector<BuildingType>>();
	std::vector<BuildingCatalogExperiment> experiments_;
	std::string catalogKey_;
	std::string startingBuildingKey_;
	Sint32 startingBuildingId_ = -1;
	std::uint8_t stockSupplyMask_ = 0;
	std::uint8_t directSupplyMask_ = 0;
	std::uint8_t extraDirectSupplyMask_ = 0;
	bool usesMarketRouting_ = false;
	bool usesOverlaySuppliers_ = false;
};
