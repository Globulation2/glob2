/*
  Copyright (C) 2007 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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
