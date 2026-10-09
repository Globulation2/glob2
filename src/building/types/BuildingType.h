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

// Resolved descriptor of one concrete building variant. The catalog combines
// authored properties, semantic capabilities and presentation, then derives
// scalar mirrors and transition IDs before a match starts. Mirrors are marked
// below; they are not independent authoring inputs. Hot simulation code uses
// BuildingRuntimeTraits or hoisted descriptors rather than parsing or key lookup.
struct BuildingType
{
	// Optional historical family alias for old saves and authored scripts.
	std::string type = "null";

	// visualisation
	std::string gameSprite = "ERROR_NO_GAME_SPRITE_DEFINED";
	Sint32 gameSpriteImage = 0;
	Sint32 gameSpriteCount = 1;
	std::string miniSprite = "ERROR_NO_MINI_SPRITE_DEFINED";
	Sint32 miniSpriteImage = 0;

	Sint32 hueImage = 0; // bool. The way we show the building's team (false=we draw a flag, true=we hue all the sprite)
	Sint32 flagImage = 49;
	Sint32 crossConnectMultiImage = 0; // Use connected-segment artwork and presentation connection rules.

	// Derived mirrors of semantics.training and semantics.trainingInParallel.
	Sint32 upgrade[NB_ABILITY] = {};
	Sint32 upgradeTime[NB_ABILITY] = {};
	Sint32 upgradeInParallel = 0;
	Sint32 foodable = 0;
	Sint32 fillable = 0;
	Sint32 zonable[NB_UNIT_TYPE] = {}; // Attraction enabled independently for each unit class.
	Sint32 zonableForbidden = 0;

	// Derived mirrors of the feeding/healing capabilities.
	Sint32 canFeedUnit = 0;
	Sint32 timeToFeedUnit = 0;
	Sint32 canHealUnit = 0;
	Sint32 timeToHealUnit = 0;
	// Authored base speed for the interior service clock.
	Sint32 insideSpeed = 12;
	// Derived mirrors of market.interTeamFruitExchange and market.sharedStock.
	Sint32 canExchange = 0;
	Sint32 useTeamMaterials = 0;

	Sint32 width = 0, height = 0; // Footprint in map tiles.
	Sint32 decLeft = 0, decTop = 0;
	Sint32 isVirtual = 0; // Derived inverse of semantics.occupiesGround.
	Sint32 isCloaked = 0; // bool, graphically invisible for enemy.
	Sint32 shootingRange = 0; // Zero disables projectile firing.
	Sint32 shootDamage = 0; // Frozen import/snapshot field; simulation uses semantic damage by target.
	Sint32 shootSpeed = 0; // Projectile travel speed in fixed-point map coordinates.
	Sint32 shootRhythm = 0; // Cooldown increment; firing interval is SHOOTING_COOLDOWN_MAX/shootRhythm ticks.
	Sint32 maxBullets = 0;
	Sint32 multiplierStoneToBullets = 0; // Bullets per semantic ammunition recipe, regardless of its resource kind.

	// Frozen importer inputs and derived compatibility summaries of the first
	// enabled recipe. They cannot describe heterogeneous production recipes;
	// simulation and strategy code must use semantics.production instead.
	Sint32 unitProductionTime = 0;
	Sint32 foodPerUnit = 0;

	Sint32 maxMaterial[MaterialSlotCount] = {};
	// materialMultiplier defaults: 1 for the basic 5 (wood/wheat/papyrus/stone/algue), 10 for fruits 0..9.
	Sint32 materialMultiplier[MaterialSlotCount] = { 1, 1, 1, 1, 1, 10, 10, 10, 1, 1, 1, 1, 1, 1, 1 };
	Sint32 maxUnitInside = 0;
	Sint32 maxUnitWorking = 0; // Derived boolean: semantics.assignmentLimit is positive.

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
	Sint32 regenerationSpeed = 0; // Frozen snapshot field; simulation uses semantics.regenerationPerTick.

	Sint32 prestige = 0;

	// Non-owning Toolkit artwork handles, populated only by loadSprites().
	Sprite *gameSpritePtr = nullptr;
	Sprite *miniSpritePtr = nullptr;
	// Resolved explicit transition keys; -1 means no transition.
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
	MaterialMask runtimeSuppliesStockMask = 0, runtimeSuppliesDirectStockMask = 0;
	MaterialMask runtimeFetchesStockMask = 0, runtimeFetchesDirectStockMask = 0;
	bool runtimeSuppliesDirectStock = false, runtimeFetchesDirectStock = false;
	bool runtimeAvailable = true;
	bool runtimeSuppliesStock = false;
	bool runtimeFetchesStock = false;

};

// Each game owns its catalog. Copies have independent descriptors and runtime
// rows; retained Scenes share only a const view of that game's descriptor storage.
// Complete loading and experiment configuration before publishing entities;
// bind sprites before publishing rendered Scenes. Descriptor and runtime pointers
// remain stable until catalog replacement; there is no supported mid-game
// definition mutation or hot reload.
// Sprite handles refer to Toolkit-owned graphics and are deliberately non-owning.
class BuildingsTypes
{
public:
	BuildingsTypes() = default;
	BuildingsTypes(const BuildingsTypes& other);
	BuildingsTypes& operator=(const BuildingsTypes& other);
	BuildingsTypes(BuildingsTypes&&) noexcept = default;
	BuildingsTypes& operator=(BuildingsTypes&&) noexcept = default;
	// Keeps descriptors alive across catalog replacement while a PresentationFrame uses them.
	// This is lifetime retention, not a deep copy or synchronization mechanism.
	std::shared_ptr<const std::vector<BuildingType>> retainTypes() const { return entries_; }
	void init();
	// Frozen pre-catalog definitions for importing older supported saves. Never
	// reads installed JSON files or shares mutable descriptors with another game.
	void initLegacy();
	void loadManifest(const std::string& path);
	void loadSnapshotJson(const std::string& json);
	// Add portable families to this immutable base; input order does not affect identity.
	// Package sprites resolve to content-addressed paths, without doing file I/O.
	void composePackages(const std::vector<std::string>& packages);
	// Canonical authored/resolved data only: artwork handles and effective match
	// gates are excluded. Enabled experiments are persisted separately by GameHeader.
	std::string snapshotJson() const;
	std::string fingerprint() const;
	const std::string& catalogKey() const { return catalogKey_; }
	Sint32 getStartingBuildingTypeNum() const { return startingBuildingId_; }
	const std::vector<BuildingCatalogExperiment>& experiments() const { return experiments_; }
	// Returns -1 when no stable key matches.
	Sint32 findByKey(const std::string& key) const;
	bool isAvailable(std::size_t id, const std::set<std::string>& enabledExperiments) const;
	// Rebuild effective gates and compact runtime rows in place during setup.
	void configureExperiments(const std::vector<std::string>& keys);
	MaterialMask stockSupplyMask() const { return stockSupplyMask_; }
	MaterialMask directSupplyMask() const { return directSupplyMask_; }
	MaterialMask extraDirectSupplyMask() const { return extraDirectSupplyMask_; }
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
	// Manifest-only diagnostic provenance; source paths never enter the catalog.
	void loadSnapshotJson(const std::string& json, const std::vector<std::string>& variantSources);
	void resolveAndValidate();
	void compileRuntimeTraits();
	std::vector<BuildingRuntimeTraits> runtimeTypes_;
	std::shared_ptr<std::vector<BuildingType>> entries_ = std::make_shared<std::vector<BuildingType>>();
	std::vector<BuildingCatalogExperiment> experiments_;
	std::string catalogKey_;
	std::string startingBuildingKey_;
	Sint32 startingBuildingId_ = -1;
	MaterialMask stockSupplyMask_ = 0;
	MaterialMask directSupplyMask_ = 0;
	MaterialMask extraDirectSupplyMask_ = 0;
	bool usesMarketRouting_ = false;
	bool usesOverlaySuppliers_ = false;
};
