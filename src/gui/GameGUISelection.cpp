// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
	// Drop any payload so the about-to-be-set mode starts from monostate; the
	// payload-bearing setters below re-establish the matching alternative.
	selection = std::monostate{};
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
		Building* b=game.teams[team]->myBuildings[id];
		selection=b;
		game.selectedBuilding=b;
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		int id=Unit::GIDtoID(newSelection);
		int team=Unit::GIDtoTeam(newSelection);
		Unit* u=game.teams[team]->myUnits[id];
		selection=u;
		game.selectedUnit=u;
	}
	else if (selectionMode==RESSOURCE_SELECTION)
	{
		selection=static_cast<int>(newSelection);
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
		Building* b=(Building*)newSelection;
		selection=b;
		game.selectedBuilding=b;
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		Unit* u=(Unit*)newSelection;
		selection=u;
		game.selectedUnit=u;
	}
	else if (selectionMode==TOOL_SELECTION)
	{
		toolManager.activateBuildingTool((char*)(newSelection));
	}
}

// Validate the current selection's referent and clear it if the referent is gone.
// Called from drawPanel() before dispatching to the per-mode draw routines so
// that those routines can assume the selection is still valid. Keep selection
// validation here rather than in draw functions — draws should be pure.
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
	else if ((selectionMode==RESSOURCE_SELECTION)
		&& (game.map.getRessource(selectionRessource()).type==NO_RES_TYPE))
	{
		clearSelection();
	}
}


// Cycle the local team's selection forward to the next building or unit of
// the same type, wrapping at MAX_COUNT. No-op when the selection cache is
// stale (e.g. the selected building was destroyed in the previous sim tick
// and the keyboard shortcut fires before checkSelection() runs at draw time),
// when no peer of the same type exists, or when the selected entity is not
// owned by the local team. Invoked from the local keyboard handler only —
// never produces a network order, never reads RNG, never mutates sim state.
void GameGUI::iterateSelection(void)
{
	if (selectionMode==BUILDING_SELECTION)
	{
		Building* selBuild=selectionBuilding();
		if (!selBuild) return;
		Uint16 selectionGBID=selBuild->gid;
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
		Unit * selUnit = selectionUnit();
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
		// Default-init so a future selectionMode that slips past the outer
		// guard can't read uninitialized stack in release builds (where
		// the asserts below are stripped).
		Sint32 posX = 0, posY = 0;
		if (selectionMode==BUILDING_SELECTION)
		{
			Building* b=selectionBuilding();
			assert(b);
			posX = b->getMidX();
			posY = b->getMidY();
		}
		else if (selectionMode==UNIT_SELECTION)
		{
			Unit * u = selectionUnit();
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


// Called from the sim path (Team::syncStep) when a unit is about to be
// deleted. Clears the GUI's selected-unit pointer if it referred to the
// dying unit. The sim never reads game.selectedUnit directly — going
// through this hook keeps the per-client GUI read out of the sim path,
// where a divergent predicate could become a desync if anyone extended
// the branch with sim-touching code.
void GameGUI::onUnitDestroyed(Unit *u)
{
	if (game.selectedUnit == u)
		game.selectedUnit = NULL;
}

// Mirror of onUnitDestroyed for building demolition. See that comment.
void GameGUI::onBuildingDestroyed(Building *b)
{
	if (game.selectedBuilding == b)
		game.selectedBuilding = NULL;

	// Drop this building's pending GUI shadow. buildingGuiState is keyed by
	// gid, and gids are recycled by Game::addBuilding (lowest free slot), so a
	// leftover entry would be inherited by the next building created on the
	// same slot. For a dragged-then-destroyed flag that left pendingPosX/Y set,
	// a freshly placed flag reusing the gid would render at the dead flag's
	// position while the simulation used the real posX/posY.
	buildingGuiState.erase(b->gid);
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
