// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Umbrella file for the static building-type table (was data/buildings.txt +
// data/buildings.default.txt parsed at startup). The 51-entry table is split
// across two siblings:
//   - buildings_part_a.cpp : entries 0..25  (swarm/inn/hospital/racetrack/swimmingpool)
//   - buildings_part_b.cpp : entries 26..50 (barracks/school/defencetower/flags/stonewall/market)
// each declaring a non-static BuildingType[] array; this file stitches them
// together via extern declarations and exposes the BuildingsTypes accessor
// surface. The split is purely a file-size accommodation (each part stays
// well under 500 lines); the resulting table is logically a single flat
// vector indexed 0..50, in the same order data/buildings.txt declared.
//
// The order is the in-game integer ID and is persisted in saves, replays
// and network traffic — reordering is a behavioral change.

#include <cassert>
#include <cstddef>
#include <iostream>

#include <Toolkit.h>
#include <GraphicContext.h>

#include "BuildingType.h"
#include "GlobalContainer.h"

using namespace GAGCore;

// Defined in buildings_part_a.cpp / buildings_part_b.cpp.
extern BuildingType g_buildingsPartA[];
extern const std::size_t g_buildingsPartACount;
extern BuildingType g_buildingsPartB[];
extern const std::size_t g_buildingsPartBCount;

// Resolve table[i] for a flat 0..(N-1) index across the two parts.
static BuildingType *entry(std::size_t i)
{
	if (i < g_buildingsPartACount)
		return &g_buildingsPartA[i];
	return &g_buildingsPartB[i - g_buildingsPartACount];
}

static std::size_t entryCount()
{
	return g_buildingsPartACount + g_buildingsPartBCount;
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
		assert(get(typeNum)->isVirtual);
	}
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
