// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GameGUIDefaultAssignManager.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "GlobalContainer.h"
#include "Stream.h"

GameGUIDefaultAssignManager::GameGUIDefaultAssignManager()
{
	BuildingsTypes& types = globalContainer->buildingsTypes;
	for(int i = IntBuildingType::SWARM_BUILDING; i!=IntBuildingType::NB_BUILDING; ++i)
	{
		for(int level=0; level<3; ++level)
		{
			//the normal building
			if(types.getByType(IntBuildingType::typeFromShortNumber(i), level, false))
			{
				unitCount[types.getTypeNum(IntBuildingType::typeFromShortNumber(i), level, false)] = globalContainer->settings.defaultUnitsAssigned[i][level*2 + 1];
			}
			//the construction site
			if(types.getByType(IntBuildingType::typeFromShortNumber(i), level, true))
			{
				unitCount[types.getTypeNum(IntBuildingType::typeFromShortNumber(i), level, true)] = globalContainer->settings.defaultUnitsAssigned[i][level*2];
			}
		}
	}
}



int GameGUIDefaultAssignManager::getDefaultAssignedUnits(int typenum)
{
	return unitCount[typenum];
}



void GameGUIDefaultAssignManager::setDefaultAssignedUnits(int typenum, int value)
{
	unitCount[typenum] = value;
}



void GameGUIDefaultAssignManager::save(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("GameGUIDefaultAssignManager");
	stream->writeEnterSection("unitCount");
	stream->writeUint32(unitCount.size(), "size");
	Uint32 n = 0;
	for(std::map<int, int>::const_iterator i = unitCount.begin(); i != unitCount.end(); ++i)
	{
		stream->writeEnterSection(n);
		stream->writeSint32(i->first, "building_type");
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
	unitCount.clear();
	for(int i=0; i<(int)size; ++i)
	{
		stream->readEnterSection(i);
		int f = stream->readSint32("building_type");
		int s = stream->readSint32("default_assigned");
		unitCount[f] = s;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
}

