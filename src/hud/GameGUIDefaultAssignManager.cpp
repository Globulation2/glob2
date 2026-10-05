// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GameGUIDefaultAssignManager.h"
#include "BuildingType.h"
#include "Game.h"
#include "Unit.h"
#include <algorithm>
#include "FileFormatVersions.h"
#include "GlobalContainer.h"
#include "Stream.h"
#include <stdexcept>

GameGUIDefaultAssignManager::GameGUIDefaultAssignManager(Game& game) : game(game) {}

int GameGUIDefaultAssignManager::getDefaultAssignedUnits(int typenum)
{
	if (typenum < 0 || static_cast<std::size_t>(typenum) >= game.buildingsTypes.size()) return 0;
	const auto* type = game.buildingsTypes.get(typenum);
	if (const auto saved = unitCount.find(type->key); saved != unitCount.end())
        return std::clamp(saved->second,0,type->semantics.assignmentLimit);
    return globalContainer->settings.buildingAssignment(game.buildingsTypes.fingerprint(),*type);
}



void GameGUIDefaultAssignManager::setDefaultAssignedUnits(int typenum, int value)
{
    if(typenum<0 || std::size_t(typenum)>=game.buildingsTypes.size()) return;
    const auto& type=*game.buildingsTypes.get(typenum);
    value=std::clamp(value,0,type.semantics.assignmentLimit);
    unitCount[type.key]=value;
    if(globalContainer->settings.rememberUnit)
        globalContainer->settings.setBuildingAssignment(game.buildingsTypes.fingerprint(),type,value);
}



void GameGUIDefaultAssignManager::save(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("GameGUIDefaultAssignManager");
	stream->writeEnterSection("unitCount");
	stream->writeUint32(unitCount.size(), "size");
	Uint32 n = 0;
	for(auto i = unitCount.begin(); i != unitCount.end(); ++i)
	{
		stream->writeEnterSection(n);
		stream->writeText(i->first, "building_key");
		stream->writeSint32(i->second, "default_assigned");
		stream->writeLeaveSection();
		n+=1;
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



void GameGUIDefaultAssignManager::load(GAGCore::InputStream* stream, Sint32 versionMinor)
{
	stream->readEnterSection("GameGUIDefaultAssignManager");
	stream->readEnterSection("unitCount");
	Uint32 size = stream->readUint32("size");
	if (size > game.buildingsTypes.size()) throw std::runtime_error("Invalid default assignment count");
	unitCount.clear();
	for(int i=0; i<(int)size; ++i)
	{
		stream->readEnterSection(i);
		        const std::string key = versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG
            ? stream->readText("building_key") : std::string();
        const int f = versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG
            ? game.buildingsTypes.findByKey(key) : stream->readSint32("building_type");
		int s = stream->readSint32("default_assigned");
		if (f < 0 || static_cast<std::size_t>(f) >= game.buildingsTypes.size() || s < 0 || s > Unit::MAX_COUNT)
			throw std::runtime_error("Invalid saved building assignment");
		if (!unitCount.emplace(game.buildingsTypes.get(f)->key,s).second)
            throw std::runtime_error("Duplicate saved building assignment");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
}

