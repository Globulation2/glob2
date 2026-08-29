// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Umbrella file for the static building-type table (was data/buildings.txt +
// data/buildings.default.txt parsed at startup). The 51 entries are grouped
// by role across four siblings, following the grouping IntBuildingType::Number
// already uses:
//   - BuildingTypesColony.cpp  : swarm, inn, hospital, market
//   - BuildingTypesUpgrade.cpp : racetrack, swimmingpool, barracks, school
//   - BuildingTypesDefence.cpp : defencetower, stonewall
//   - BuildingTypesFlags.cpp   : exploration, war and clearing flags
// each declaring one or more non-static BuildingType[] arrays; this file
// splices them into a single flat vector indexed 0..50, in the same order
// data/buildings.txt declared.
//
// Role grouping and ID order do not agree — market sits at the end of the
// table and the flags sit between the defencetower and the stonewall — so a
// role file may hold more than one array. g_tableParts below is the single
// authoritative statement of ID order.
//
// The order is the in-game integer ID and is persisted in saves, replays
// and network traffic — reordering is a behavioral change.

#include <cassert>
#include <cstddef>
#include <iostream>

#include <Toolkit.h>

#include "BuildingType.h"
#include "GlobalContainer.h"

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

// Listed in flat-index order — this is the in-game building ID order and is
// persisted in saves, replays and network traffic.
static const TablePart g_tableParts[] = {
	{ g_buildingTypesColony,       g_buildingTypesColonyCount },        //  0..13
	{ g_buildingTypesUpgrade,      g_buildingTypesUpgradeCount },       // 14..37
	{ g_buildingTypesDefenceTower, g_buildingTypesDefenceTowerCount },  // 38..43
	{ g_buildingTypesFlags,        g_buildingTypesFlagsCount },         // 44..46
	{ g_buildingTypesStoneWall,    g_buildingTypesStoneWallCount },     // 47..48
	{ g_buildingTypesMarket,       g_buildingTypesMarketCount },        // 49..50
};

// Resolve table[i] for a flat 0..(N-1) index across the parts.
static BuildingType *entry(std::size_t i)
{
	for (const TablePart &part : g_tableParts)
	{
		if (i < part.count)
			return &part.entries[i];
		i -= part.count;
	}
	assert(false && "building type index out of range");
	return nullptr;
}

static std::size_t entryCount()
{
	std::size_t total = 0;
	for (const TablePart &part : g_tableParts)
		total += part.count;
	return total;
}

// Mirror of the legacy ConfigVector::checkIntegrity assertions.
static void checkIntegrity()
{
	const std::size_t count = entryCount();
	for (std::size_t i = 0; i < count; ++i)
	{
		BuildingType *bt = entry(i);

		// Need ressource integrity:
		bool needRessource = false;
		for (unsigned j = 0; j < MAX_RESSOURCES; ++j)
			if (bt->maxRessource[j])
			{
				needRessource = true;
				break;
			}
		if (needRessource)
			assert(bt->fillable || bt->foodable);

		// hpInc integrity:
		if (bt->isBuildingSite)
			assert(bt->hpInc > 0);
		else
			assert(bt->hpInc == 0);

		// hpMax/hpInit integrity (warning only, matches legacy std::cerr behavior):
		if (bt->isBuildingSite && bt->level)
		{
			assert(bt->prevLevel != -1);
			BuildingType *bt2 = entry(static_cast<std::size_t>(bt->prevLevel));
			if (bt->hpInit != bt2->hpMax)
			{
				std::cerr << "BuildingsTypes::init() : warning : " << bt->type
					<< " : Building site has hpInit=" << bt->hpInit
					<< ", but final building (level " << bt2->level
					<< ") has hpMax=" << bt2->hpMax << std::endl;
			}
		}

		// hpInit/hpInc integrity (warning only):
		if (bt->isBuildingSite)
		{
			int resSum = 0;
			for (int j = 0; j < MAX_RESSOURCES; ++j)
				resSum += bt->maxRessource[j];
			int hpSum = bt->hpInit + resSum * bt->hpInc;
			if (hpSum < bt->hpMax)
			{
				std::cerr << "BuildingsTypes::init() : warning : " << bt->type
					<< " : hpSum(" << hpSum << ") < hpMax(" << bt->hpMax
					<< ") with hpInit=" << bt->hpInit << ", hpInc=" << bt->hpInc
					<< ", resSum=" << resSum << ". Make hpInc>="
					<< (resSum ? (bt->hpMax - bt->hpInit + resSum - 1) / resSum : 0)
					<< std::endl;
			}
		}

		// flag integrity:
		if (bt->isVirtual)
		{
			assert(bt->isCloacked);
			assert(bt->defaultUnitStayRange);
		}
		if (bt->isCloacked)
		{
			assert(bt->isVirtual);
			assert(bt->defaultUnitStayRange);
		}
		if (bt->defaultUnitStayRange)
		{
			assert(bt->isCloacked);
			assert(bt->isVirtual);
		}
		if (bt->zonableForbidden)
		{
			assert(bt->isCloacked);
			assert(bt->isVirtual);
			assert(bt->defaultUnitStayRange);
		}
	}
}

// Walk the table once to set prevLevel/nextLevel, mirroring the loader's
// resolveUpgradeReferences. Bidirectional: building-site entries link
// forward to the completed building of the same type+level, and that
// completed building links forward to the next-level building site.
static void resolveUpgradeReferences()
{
	const std::size_t count = entryCount();
	for (std::size_t i = 0; i < count; ++i)
	{
		entry(i)->prevLevel = -1;
		entry(i)->nextLevel = -1;
	}

	for (std::size_t i = 0; i < count; ++i)
	{
		BuildingType *bt1 = entry(i);
		for (std::size_t j = 0; j < count; ++j)
		{
			BuildingType *bt2 = entry(j);
			if (bt1 == bt2)
				continue;

			if (bt1->isBuildingSite)
			{
				if (bt2->level == bt1->level && bt2->type == bt1->type && !bt2->isBuildingSite)
				{
					bt1->nextLevel = static_cast<int>(j);
					bt2->prevLevel = static_cast<int>(i);
					break;
				}
			}
			else
			{
				if (bt2->level == bt1->level + 1 && bt2->type == bt1->type && bt2->isBuildingSite)
				{
					bt1->nextLevel = static_cast<int>(j);
					bt2->prevLevel = static_cast<int>(i);
					break;
				}
			}
		}
	}
}

void BuildingsTypes::init()
{
	resolveUpgradeReferences();

	// Resolve sprite pointers, replacing the lazy load that happened inside
	// the old loadFromConfigFile. Skips the "null" default block (not in
	// this table) and skips on headless runs, same as the original loader.
	if (!globalContainer->runNoX)
	{
		const std::size_t count = entryCount();
		for (std::size_t i = 0; i < count; ++i)
		{
			BuildingType *bt = entry(i);
			if (bt->type == "null")
				continue;
			bt->gameSpritePtr = Toolkit::getSprite(bt->gameSprite.c_str());
			if (bt->miniSpriteImage >= 0)
				bt->miniSpritePtr = Toolkit::getSprite(bt->miniSprite.c_str());
		}
	}

	checkIntegrity();
}

BuildingType *BuildingsTypes::get(std::size_t id)
{
	if (id < entryCount())
		return entry(id);
	std::cerr << "BuildingsTypes::get(" << static_cast<unsigned int>(id)
		<< ") : warning : id is not valid" << std::endl;
	assert(false);
	return nullptr;
}

std::size_t BuildingsTypes::size() const
{
	return entryCount();
}

Sint32 BuildingsTypes::getTypeNum(const char *type, int level, bool isBuildingSite)
{
	assert(type);
	const std::size_t count = entryCount();
	for (std::size_t i = 0; i < count; ++i)
	{
		const BuildingType *bt = entry(i);
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
	// Try to get the building site; if it doesn't exist, get the finished
	// building (for flags).
	Sint32 typeNum = getTypeNum(name, 0, true);
	if (typeNum == -1)
	{
		typeNum = getTypeNum(name, 0, false);
		// Check the name resolved at all before handing it to get(), which
		// would otherwise convert -1 to SIZE_MAX and return nullptr.
		assert(typeNum != -1);
		assert(get(typeNum)->isVirtual);
	}
	return typeNum;
}

Sint32 BuildingsTypes::getFinishedTypeNum(const std::string &name)
{
	Sint32 typeNum = getTypeNum(name, 0, false);
	assert(typeNum != -1);
	return typeNum;
}

BuildingType *BuildingsTypes::getByType(const char *type, int level, bool isBuildingSite)
{
	assert(type);
	const std::size_t count = entryCount();
	for (std::size_t i = 0; i < count; ++i)
	{
		BuildingType *bt = entry(i);
		if (bt->type == type && bt->level == level && (bt->isBuildingSite != 0) == isBuildingSite)
			return bt;
	}
	return nullptr;
}

BuildingType *BuildingsTypes::getByType(const std::string &s, int level, bool isBuildingSite)
{
	return getByType(s.c_str(), level, isBuildingSite);
}
