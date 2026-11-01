#include "BuildingPresentation.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <optional>
#include <algorithm>


#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"

using std::shared_ptr;
using std::static_pointer_cast;

bool GameGUI::requestWorkerAllocation(const SceneBuildingPanel& building, int requested)
{
    if (globalContainer->isViewingGame() || building.owner().number!=localTeamNo ||
        !building.type->maxUnitWorking || building.state().buildingState!=Building::ALIVE) return false;
    requested=std::clamp(requested,0,building.type->semantics.assignmentLimit);
    if (requested==displayedMaxUnitWorking(building)) return false;
    pendingFor(building.state().gid).pendingMaxUnitWorking=requested;
    enqueueOrder(std::make_shared<OrderModifyBuilding>(building.state().gid,requested));
    defaultAssign.setDefaultAssignedUnits(drawnScene(), building.state().typeNum,requested);
    return true;
}

bool GameGUI::requestBuildingPriority(const SceneBuildingPanel& building, int requested)
{
    if (globalContainer->isViewingGame() || building.owner().number!=localTeamNo ||
        !building.type->maxUnitWorking || building.state().buildingState!=Building::ALIVE) return false;
    requested=std::clamp(requested,-1,1);
    if (requested==displayedPriority(building)) return false;
    pendingFor(building.state().gid).pendingPriority=requested;
    enqueueOrder(std::make_shared<OrderChangePriority>(building.state().gid,requested));
    return true;
}

bool GameGUI::requestFlagRange(const SceneBuildingPanel& building, int requested)
{
    if (globalContainer->isViewingGame() || building.owner().number!=localTeamNo ||
        !(building.type->zonable[WORKER] || building.type->zonable[WARRIOR] || building.type->zonable[EXPLORER])) return false;
    requested=std::clamp(requested,0,building.type->maxUnitStayRange);
    if (requested==displayedUnitStayRange(building)) return false;
    pendingFor(building.state().gid).pendingUnitStayRange=requested;
    enqueueOrder(std::make_shared<OrderModifyFlag>(building.state().gid,requested));
    return true;
}

void GameGUI::handleMenuClickBuildingSelection(int mx, int my, int button)
{
	if (button != SDL_BUTTON_LEFT || globalContainer->isViewingGame()) return;
	activateHudControlAt({globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+mx,my},"building/");
}
