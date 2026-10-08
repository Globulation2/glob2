// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <iostream>


#include "Game.h"
#include "GameGUI.h"
#include "render/scene/BuildingCatalogView.h"
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

    if (selectionMode == BUILDING_SELECTION)
    {
        const auto* b = drawnScene().entities.building(newSelection);
        selection = b ? BuildingRef{b->gid, b->scriptIdentity} : BuildingRef{};
        syncSelectionView();
        return;
    }
    if (selectionMode == UNIT_SELECTION)
    {
        const auto* u = drawnScene().entities.unit(newSelection);
        selection = u ? UnitRef{u->gid, u->scriptIdentity} : UnitRef{};
        syncSelectionView();
        return;
    }
	if (selectionMode==RESOURCE_SELECTION)
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
	view.selectedBuilding=nullptr;
	view.selectedUnit=nullptr;
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

        bool valid = true;
        const auto& scene = drawnScene();
        if (const auto* ref = std::get_if<BuildingRef>(&selection))
        {
            const auto* b = scene.entities.building(ref->gid);
            valid = b && b->scriptIdentity == ref->generation;
        }
        else if (const auto* ref = std::get_if<UnitRef>(&selection))
        {
            const auto* u = scene.entities.unit(ref->gid);
            valid = u && u->scriptIdentity == ref->generation;
        }
        else if (selectionMode == RESOURCE_SELECTION)
            valid = scene.map.getW() && scene.map.getResource(selectionResource()).type != NO_RES_TYPE;
        if (!valid) clearSelection(); else syncSelectionView();
}

void GameGUI::iterateSelection(void)
{
	// The selected entity may have died since the last draw; clear the
	// selection first so neither branch below sees a null referent.
	checkSelection();

        const auto& entities = drawnScene().entities;
        if (selectionMode == BUILDING_SELECTION || selectionMode == TOOL_SELECTION)
        {
            const auto* ref = std::get_if<BuildingRef>(&selection);
            const auto* selected = ref ? entities.building(ref->gid) : nullptr;
            if (selectionMode == BUILDING_SELECTION && (!selected || selected->team != localTeamNo)) return;
            const int type = selected ? selected->typeNum : BuildingCatalogView(*drawnScene().buildingTypes).getFinishedTypeNum(toolManager.getBuildingName());
            const int start = selected ? Building::GIDtoID(selected->gid) : -1;
            for (int n = 1; n <= Building::MAX_COUNT; ++n)
            {
                const auto* b = entities.building(Building::GIDfrom((start+n)%Building::MAX_COUNT, localTeamNo));
                if (b && b->typeNum == type) { setSelection(BUILDING_SELECTION, unsigned(b->gid)); centerViewportOnSelection(); break; }
            }
        }
        else if (selectionMode == UNIT_SELECTION)
        {
            const auto* ref = std::get_if<UnitRef>(&selection);
            const auto* selected = ref ? entities.unit(ref->gid) : nullptr;
            if (!selected) return;
            const int start = selected->team == localTeamNo ? Unit::GIDtoID(selected->gid) : 0;
            for (int n = 1; n < Unit::MAX_COUNT; ++n)
            {
                const auto* u = entities.unit(Unit::GIDfrom((start+n)%Unit::MAX_COUNT, localTeamNo));
                if (u && u->typeNum == selected->typeNum) { setSelection(UNIT_SELECTION, unsigned(u->gid)); centerViewportOnSelection(); break; }
            }
        }
}

void GameGUI::centerViewportOnSelection(void)
{

        const auto& entities = drawnScene().entities;
        if (const auto* ref = std::get_if<BuildingRef>(&selection))
        {
            const auto* b = entities.building(ref->gid);
            if (b && b->scriptIdentity == ref->generation)
                centerViewportOn(b->posX+entities.type(*b)->width/2, b->posY+entities.type(*b)->height/2);
        }
        else if (const auto* ref = std::get_if<UnitRef>(&selection))
        {
            const auto* u = entities.unit(ref->gid);
            if (u && u->scriptIdentity == ref->generation) centerViewportOn(u->posX,u->posY);
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

const SceneBuildingPanel* GameGUI::inputBuildingPanel()
{
    const auto* ref = std::get_if<BuildingRef>(&selection);
    if (selectionMode != BUILDING_SELECTION || !ref) return nullptr;
    const auto& panel=drawnScene().panels.building;
    return panel.valid && panel.state().identity==*ref ? &panel : nullptr;
}

const SnapshotBuilding* GameGUI::inputBuilding(BuildingRef ref) const
{
    const auto* b=drawnScene().entities.building(ref.gid);
    if (b && b->scriptIdentity==ref.generation) return b;
    return nullptr;
}
