// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <GAGSys.h>
#include <cstddef>
#include <string>

#include "Ressource.h"
#include "UnitConsts.h"

namespace GAGCore { class Sprite; }
using GAGCore::Sprite;

// BuildingType describes the static configuration of one building variant
// (e.g. "swarm0c" — the level-0 swarm under construction). Historically these
// values were loaded at runtime from data/buildings.default.txt + data/buildings.txt
// via the ConfigVector<BuildingType> template; they are now baked into a
// per-variant table in buildings.cpp. The fields remain Sint32 for ABI parity
// with the old loader (booleans were stored as ints).
//
// The default member initializers below mirror data/buildings.default.txt so
// that each entry in the table only has to spell out the fields that differ
// from the defaults — same defaults+overrides shape as the text format used
// to provide.
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

	// could be Uint8, if non 0 tell the number of maximum units locked by bulding for:
	// by order of priority (top = max)
	Sint32 upgrade[NB_ABILITY] = {}; // What kind on units can be upgraded here
	Sint32 upgradeTime[NB_ABILITY] = {}; // Time to upgrade an unit, given the upgrade type needed.
	Sint32 upgradeInParallel = 0; // if true, can learn all upgardes with one learning time into the building
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
	Sint32 useTeamRessources = 0;

	Sint32 width = 0, height = 0; // Uint8, size in square
	Sint32 decLeft = 0, decTop = 0;
	Sint32 isVirtual = 0; // bool, doesn't occupy ground occupation map, used for war-flag and exploration-flag.
	Sint32 isCloacked = 0; // bool, graphicaly invisible for enemy.
	Sint32 shootingRange = 0; // Uint8, if 0 can't shoot
	Sint32 shootDamage = 0; // Uint8
	Sint32 shootSpeed = 0; // Uint8, the actual speed at which the shots fly through the air.
	Sint32 shootRythme = 0; // Uint8, The frequency with which a tower fires. It fires once every
	                        // SHOOTING_COOLDOWN_MAX/shootRythme ticks.
	Sint32 maxBullets = 0;
	Sint32 multiplierStoneToBullets = 0; // The tower gets this many bullets every time a worker delivers stone to it.

	Sint32 unitProductionTime = 0; // Uint8, nb tick to produce one unit
	Sint32 ressourceForOneUnit = 0; // The amount of wheat consumed in the production of a unit.

	Sint32 maxRessource[MAX_NB_RESSOURCES] = {};
	// multiplierRessource defaults: 1 for the basic 5 (wood/corn/papyrus/stone/algue), 10 for fruits 0..9.
	Sint32 multiplierRessource[MAX_NB_RESSOURCES] = { 1, 1, 1, 1, 1, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10 };
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
	Sint32 shortTypeNum = 0; // BuildingTypeShortNumber, Should not be used by the main engine, but only to choose the next level building.
	Sint32 isBuildingSite = 0;

	// Flag usefull
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
};

// BuildingsTypes is the read-only registry of building variants, indexed by an
// integer ID that is the position in the const table (0=swarm0c, 1=swarm0,
// 2=inn0c, …). Those IDs are persisted in saves, replays and network traffic,
// so reordering is a behavioral change. The class keeps the same external
// surface (.get / .getTypeNum / .getByType) as the old ConfigVector<BuildingType>
// subclass so existing callers compile unchanged; it is now backed by a
// static array rather than a parsed text file.
class BuildingsTypes
{
public:
	// Resolve sprite pointers and prev/next-level cross-references, and run
	// the same integrity checks the old loader did. Replaces the old
	// load("data/buildings.default.txt") + load("data/buildings.txt") chain.
	void init();

	BuildingType *get(std::size_t id);
	std::size_t size() const;

	Sint32 getTypeNum(const char *type, int level, bool isBuildingSite);
	Sint32 getTypeNum(const std::string &s, int level, bool isBuildingSite);
	// Resolve the variant a new player placement of `name` creates: the
	// level-0 construction site if one exists, otherwise the finished
	// level-0 building (flags and other virtual buildings have no
	// construction site). Asserts that the fallback only happens for
	// virtual buildings and that the name resolves at all.
	Sint32 getPlaceableTypeNum(const std::string &name);
	BuildingType *getByType(const char *type, int level, bool isBuildingSite);
	BuildingType *getByType(const std::string &s, int level, bool isBuildingSite);
};
