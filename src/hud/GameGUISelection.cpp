// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <iostream>


#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "EngineTiming.h"

void GameGUI::cleanOldSelection(void)
{
	// Failing-unit recording follows the published observed building; the
	// simulation switches it at the next tick boundary (Game::applyClientRequests).
	if ((selectionMode==BRUSH_SELECTION) || (selectionMode==TOOL_SELECTION))
		toolManager.deactivateTool();
	// Drop any payload so the about-to-be-set mode starts from monostate; the
	// payload-bearing setters below re-establish the matching alternative.
	selection = std::monostate{};
	syncSelectionView();
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
		selection=Game::refOf(game.teams[team]->myBuildings[id]);
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		int id=Unit::GIDtoID(newSelection);
		int team=Unit::GIDtoTeam(newSelection);
		selection=Game::refOf(game.teams[team]->myUnits[id]);
	}
	else if (selectionMode==RESOURCE_SELECTION)
	{
		selection=static_cast<int>(newSelection);
	}
	syncSelectionView();
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
		selection=Game::refOf(static_cast<Building*>(newSelection));
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		selection=Game::refOf(static_cast<Unit*>(newSelection));
	}
	else if (selectionMode==TOOL_SELECTION)
	{
		toolManager.activateBuildingTool((char*)(newSelection));
	}
	syncSelectionView();
}

// These tolerate the transient state inside setSelection/cleanOldSelection,
// where selectionMode still names the old mode but the payload is cleared.
Building* GameGUI::selectedBuildingOrNull() const
{
	const BuildingRef *ref = std::get_if<BuildingRef>(&selection);
	return (selectionMode==BUILDING_SELECTION && ref) ? game.resolveBuilding(*ref) : nullptr;
}

Unit* GameGUI::selectedUnitOrNull() const
{
	const UnitRef *ref = std::get_if<UnitRef>(&selection);
	return (selectionMode==UNIT_SELECTION && ref) ? game.resolveUnit(*ref) : nullptr;
}

void GameGUI::syncSelectionView(void)
{
	view.selectedBuilding=selectedBuildingOrNull();
	view.selectedUnit=selectedUnitOrNull();
	const BuildingRef *observed = std::get_if<BuildingRef>(&selection);
	clientRequests.publishObservedBuilding(
		(selectionMode==BUILDING_SELECTION && observed) ? *observed : BuildingRef());
}

// Validate the current selection's referent and clear it if the referent is gone.
// Called before cycling and from drawPanel() before dispatching to the per-mode
// draw routines so those routines can assume the selection is valid. Keep selection
// validation here rather than in draw functions — draws should be pure.
void GameGUI::checkSelection(void)
{
	if ((selectionMode==BUILDING_SELECTION) && (selectionBuilding()==NULL))
	{
		clearSelection();
	}
	else if ((selectionMode==UNIT_SELECTION) && (selectionUnit()==NULL))
	{
		clearSelection();
	}
	else if ((selectionMode==RESOURCE_SELECTION)
		&& (game.map.getResource(selectionResource()).type==NO_RES_TYPE))
	{
		clearSelection();
	}
	else
	{
		syncSelectionView();
	}
}


// Cycle the local team's selection forward to the next building or unit of
// the same type, wrapping at MAX_COUNT. No-op when the selected entity is
// gone (e.g. the selected building was destroyed in the previous sim tick
// and the keyboard shortcut fires before the next draw),
// when no peer of the same type exists, or when the selected entity is not
// owned by the local team. Invoked from the local keyboard handler only —
// never produces a network order, never reads RNG, never mutates sim state.
void GameGUI::iterateSelection(void)
{
	// The selected entity may have died since the last draw; clear the
	// selection first so neither branch below sees a null referent.
	checkSelection();

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
		Sint32 typeNum=game.buildingsTypes.getFinishedTypeNum(toolManager.getBuildingName());
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
		Sint32 i = id;
		while (1)
		{
			i = ((i + 1) % Unit::MAX_COUNT);
			if (i == id) break;
			Unit * u = game.teams[localTeamNo]->myUnits[i];
			if (u && (u->typeNum == selUnit->typeNum))
			{
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

		stopViewportMotion();
		viewportX = posX - int(camera.visibleW()/64);
		viewportY = posY - int(camera.visibleH()/64);
		viewportX = viewportX & game.map.getMaskW();
		viewportY = viewportY & game.map.getMaskH();

		viewportChanged(oldViewportX, viewportX, oldViewportY, viewportY);
	}
}


void GameGUI::consumeClientEvents()
{
	clientEvents.drain([this](ClientEventVariant&& event) { handleClientEvent(std::move(event)); });
	// Age undelivered GameEvents exactly as Team::updateEvents did before the
	// client owned them: drop those older than GAME_EVENT_MAX_AGE_TICKS
	// relative to the last simulated tick.
	const ClientEvents::TickPulse pulse = clientEvents.pulse();
	if (pulse.valid)
		for (auto &queue : pendingTeamEvents)
			while (!queue.empty() && (pulse.tick - queue.front().getStep()) > GAME_EVENT_MAX_AGE_TICKS)
				queue.pop_front();
	syncSelectionView();
}
