/*
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

#include <iostream>

#include <GraphicContext.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "Unit.h"

void GameGUI::cleanOldSelection(void)
{
	if (selectionMode==BUILDING_SELECTION)
	{
		game.selectedBuilding=NULL;
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		game.selectedUnit=NULL;
	}
	else if (selectionMode==BRUSH_SELECTION)
	{
		toolManager.deactivateTool();
	}
	else if (selectionMode==TOOL_SELECTION)
	{
		toolManager.deactivateTool();
	}
}

void GameGUI::setSelection(SelectionMode newSelMode, unsigned newSelection)
{
	if (selectionMode!=newSelMode)
	{
		cleanOldSelection();
		selectionMode=newSelMode;
	}

	if (selectionMode==BUILDING_SELECTION)
	{
		int id=Building::GIDtoID(newSelection);
		int team=Building::GIDtoTeam(newSelection);
		selection.building=game.teams[team]->myBuildings[id];
		game.selectedBuilding=selection.building;
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		int id=Unit::GIDtoID(newSelection);
		int team=Unit::GIDtoTeam(newSelection);
		selection.unit=game.teams[team]->myUnits[id];
		game.selectedUnit=selection.unit;
	}
	else if (selectionMode==RESSOURCE_SELECTION)
	{
		selection.ressource=newSelection;
	}
}

void GameGUI::setSelection(SelectionMode newSelMode, void* newSelection)
{
	if (selectionMode!=newSelMode)
	{
		cleanOldSelection();
		selectionMode=newSelMode;
	}

	if (selectionMode==BUILDING_SELECTION)
	{
		selection.building=(Building*)newSelection;
		game.selectedBuilding=selection.building;
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		selection.unit=(Unit*)newSelection;
		game.selectedUnit=selection.unit;
	}
	else if (selectionMode==TOOL_SELECTION)
	{
		toolManager.activateBuildingTool((char*)(newSelection));
	}
}

void GameGUI::checkSelection(void)
{
	if ((selectionMode==BUILDING_SELECTION) && (game.selectedBuilding==NULL))
	{
		clearSelection();
	}
	else if ((selectionMode==UNIT_SELECTION) && (game.selectedUnit==NULL))
	{
		clearSelection();
	}
}


void GameGUI::iterateSelection(void)
{
	if (selectionMode==BUILDING_SELECTION)
	{
		Building* selBuild=selection.building;
		Uint16 selectionGBID=selBuild->gid;
		assert(selBuild);
		assert(selectionGBID!=NOGBID);
		int pos=Building::GIDtoID(selectionGBID);
		int team=Building::GIDtoTeam(selectionGBID);
		int i=pos;
		if (team==localTeamNo)
		{
			while (i<pos+Building::MAX_COUNT)
			{
				i++;
				Building *b=game.teams[team]->myBuildings[i % Building::MAX_COUNT];
				if (b && b->typeNum==selBuild->typeNum)
				{
					setSelection(BUILDING_SELECTION, b);
					centerViewportOnSelection();
					break;
				}
			}
		}
	}
	else if (selectionMode==TOOL_SELECTION)
	{
		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(toolManager.getBuildingName(), 0, false);
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			Building *b=game.teams[localTeamNo]->myBuildings[i];
			if (b && b->typeNum==typeNum)
			{
				setSelection(BUILDING_SELECTION, b);
				centerViewportOnSelection();
				break;
			}
		}
	}
	else if (selectionMode == UNIT_SELECTION)
	{
		Unit * selUnit = selection.unit;
		assert(selUnit);
		Uint16 gid = selUnit->gid;
		/* to be safe should check if gid is valid here? */
		/* if looking at one of our pieces, continue with the next
			one of our pieces of same type, otherwise start at the
			beginning of our pieces of that type. */
		Sint32 id = ((Unit::GIDtoTeam(gid) == localTeamNo) ? Unit::GIDtoID(gid) : 0);
		id %= Unit::MAX_COUNT; /* just in case! */
		// std::cerr << "starting id: " << id << std::endl;
		Sint32 i = id;
		while (1)
		{
			i = ((i + 1) % Unit::MAX_COUNT);
			if (i == id) break;
			// std::cerr << "trying id: " << i << std::endl;
			Unit * u = game.teams[localTeamNo]->myUnits[i];
			if (u && (u->typeNum == selUnit->typeNum))
			{
				// std::cerr << "found id: " << i << std::endl;
				setSelection(UNIT_SELECTION, u);
				centerViewportOnSelection();
				break;
			}
		}
	}
}

void GameGUI::centerViewportOnSelection(void)
{
	if ((selectionMode==BUILDING_SELECTION) || (selectionMode==UNIT_SELECTION))
	{
		Sint32 posX, posY;
		if (selectionMode==BUILDING_SELECTION)
		{
			Building* b=selection.building;
			//assert (selBuild);
			//Building *b=game.teams[Building::GIDtoTeam(selectionGBID)]->myBuildings[Building::GIDtoID(selectionGBID)];
			assert(b);
			posX = b->getMidX();
			posY = b->getMidY();
		}
		else if (selectionMode==UNIT_SELECTION)
		{
			Unit * u = selection.unit;
			assert (u);
			posX = u->posX;
			posY = u->posY;
		}

		/* It violates good abstraction principles that we know here
			that the size of the right panel is RIGHT_MENU_WIDTH pixels, and that each
			map cell is 32 pixels.  This information should be
			abstracted. */

		int oldViewportX = viewportX;
		int oldViewportY = viewportY;

		viewportX = posX - ((globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)>>6);
		viewportY = posY - ((globalContainer->gfx->getH())>>6);
		viewportX = viewportX & game.map.getMaskW();
		viewportY = viewportY & game.map.getMaskH();

		moveParticles(oldViewportX, viewportX, oldViewportY, viewportY);
	}
}


void GameGUI::dumpUnitInformation(void)
{
	if(game.selectedUnit != NULL)
	{
		Unit* unit = game.selectedUnit;
		std::cout<<"unit->posx = "<<unit->posX<<std::endl;
		std::cout<<"unit->posy = "<<unit->posY<<std::endl;
		std::cout<<"unit->gid = "<<unit->gid<<std::endl;
		std::cout<<"unit->medical = "<<unit->medical<<std::endl;
		std::cout<<"unit->activity = "<<unit->activity<<std::endl;
		std::cout<<"unit->displacement = "<<unit->displacement<<std::endl;
		std::cout<<"unit->movement = "<<unit->movement<<std::endl;
		std::cout<<"unit->action = "<<unit->action<<std::endl;
		if(unit->targetBuilding)
			std::cout<<"unit->targetBuilding->gid = "<<unit->targetBuilding->gid<<std::endl;
	}
}
