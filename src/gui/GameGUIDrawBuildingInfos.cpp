// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <GraphicContext.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"

void GameGUI::drawBuildingInfos(void)
{
	Building* selBuild = selectionBuilding();
	assert(selBuild);
	BuildingType *buildingType = selBuild->type;
	int ypos = YPOS_BASE_BUILDING;
	unsigned unitInsideBarYDec = 0;

	// Title row + level/site/prestige subtitle.
	drawBuildingHeader(selBuild, buildingType, ypos);

	// Icon row: icon + HP / inside-count / flag stat all share this row.
	drawBuildingIcon(selBuild, buildingType, ypos);
	drawBuildingHP(selBuild, buildingType, ypos);
	drawBuildingInsideStats(selBuild, buildingType, ypos);
	drawBuildingFlagInfo(selBuild, buildingType, ypos);
	ypos += YOFFSET_ICON+YOFFSET_B_SEP;

	// Worker assignment row, priority radios, flag stay-range.
	drawBuildingWorkingControls(selBuild, buildingType, ypos);
	drawBuildingPriorityControls(selBuild, buildingType, ypos);
	drawBuildingRangeControls(selBuild, buildingType, ypos);

	// flag control of team and allies (clearing/war/exploration)
	drawBuildingFlagControls(selBuild, buildingType, ypos);

	globalContainer->gfx->finishDrawingSprite(globalContainer->gamegui, 255);

	// armor / shoot damage / shoot range, then time-to-leave progress bar.
	drawBuildingCombatStats(selBuild, buildingType, ypos);
	drawBuildingTimeToLeaveBar(selBuild, buildingType, ypos, unitInsideBarYDec);

	ypos += YOFFSET_B_SEP;

	// Lower body: market, resources, swarm ratios, failure reasons, action buttons.
	drawBuildingExchange(selBuild, buildingType, ypos);
	drawBuildingResources(selBuild, buildingType, ypos);
	drawBuildingSwarmRatios(selBuild, buildingType, ypos);
	drawBuildingFailureReasons(selBuild, buildingType, ypos);
	drawBuildingActionButtons(selBuild, buildingType, unitInsideBarYDec);
}
