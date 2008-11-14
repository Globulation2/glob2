/*
  Copyright (C) 2007 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "GameGUIDefaultAssignManager.h"
#include "BuildingsTypes.h"
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
	for(int i=0; i<size; ++i)
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

