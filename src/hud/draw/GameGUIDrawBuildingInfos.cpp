// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière


#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include <RenderStateScope.h>
#include <algorithm>

void GameGUI::drawBuildingInfos(void)
{
	const SceneBuildingPanel* selBuild = &drawnScene().panels.building;
	const auto* selected = std::get_if<BuildingRef>(&selection);
    if (!selBuild->valid || !selected || selected->gid != selBuild->state().gid || selected->generation != selBuild->state().scriptIdentity
        || drawnScene().panels.local.state().number != localTeamNo)
		return;
	const BuildingType* buildingType = selBuild->type;
	int ypos = YPOS_BASE_BUILDING;
	unsigned unitInsideBarYDec = 0;

	if (buildingInfoScrollGid!=selBuild->state().gid)
	{
		buildingInfoScrollGid=selBuild->state().gid;
		buildingInfoScroll=0; buildingInfoScrollMaximum=0;
	}
	BuildingPreviewRows previewRows;
	const auto preview=hoveredBuildingPreview(*selBuild);
	const int bottom=globalContainer->gfx->getH()-BOTTOM_BUTTON_PRIMARY_YOFFSET-4;
	const SDL_Rect bounds{globalContainer->gfx->getW()-RIGHT_MENU_WIDTH,YPOS_BASE_BUILDING,RIGHT_MENU_WIDTH,std::max(0,bottom-YPOS_BASE_BUILDING)};
	const auto arrowStart=arrowPositions.size();
	{
		GAGCore::UITransformScope scrolled(*globalContainer->gfx,1,0,-buildingInfoScroll,&bounds);
		// Title row + level/site/prestige subtitle.
		drawBuildingHeader(selBuild, buildingType, ypos);

		// Icon row: icon + HP / inside-count / flag stat all share this row.
		drawBuildingIcon(selBuild, buildingType, ypos);
		drawBuildingHP(selBuild, buildingType, ypos, previewRows);
		drawBuildingInsideStats(selBuild, buildingType, ypos, previewRows);
		if (!buildingHasSeparateAttractionHeader(*buildingType))
			drawBuildingFlagInfo(selBuild, buildingType, ypos);
		ypos += YOFFSET_ICON+YOFFSET_B_SEP;
		if (buildingHasSeparateAttractionHeader(*buildingType))
			drawBuildingFlagInfo(selBuild, buildingType, ypos);
		ypos += buildingExtraHeaderHeight(*buildingType);

		// Worker assignment row, priority radios, flag stay-range.
		drawBuildingWorkingControls(selBuild, buildingType, ypos);
		drawBuildingPriorityControls(selBuild, buildingType, ypos);
		drawBuildingRangeControls(selBuild, buildingType, ypos);

		// flag control of team and allies (clearing/war/exploration)
		drawBuildingFlagControls(selBuild, buildingType, ypos);

		globalContainer->gfx->finishDrawingSprite(globalContainer->gamegui, 255);

		// armor / shoot damage / shoot range, then time-to-leave progress bar.
		drawBuildingCombatStats(selBuild, buildingType, ypos, previewRows);
		drawBuildingTimeToLeaveBar(selBuild, buildingType, ypos, unitInsideBarYDec);

		ypos += YOFFSET_B_SEP;

		// Lower body: market, resources, swarm ratios, failure reasons, action buttons.
		drawBuildingExchange(selBuild, buildingType, ypos, previewRows);
		drawBuildingResources(selBuild, buildingType, ypos, previewRows);
		drawBuildingSwarmRatios(selBuild, buildingType, ypos);
		drawBuildingFailureReasons(selBuild, buildingType, ypos);
		if (preview!=BuildingPreview::None)
		{
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 255));
			if (preview==BuildingPreview::Upgrade)
				drawBuildingUpgradePreview(selBuild, buildingType, previewRows, ypos);
			else
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, Toolkit::getStringTable()->getString("[repair]"));
				ypos += YOFFSET_TEXT_PARA;
				drawCosts(selBuild->repairCost, globalContainer->littleFont, ypos);
			}
			globalContainer->littleFont->popStyle();
		}
	}
	for (size_t i=arrowStart; i<arrowPositions.size(); ++i) arrowPositions[i].y-=buildingInfoScroll;
	buildingInfoScrollMaximum=std::max(0,ypos-bottom);
	buildingInfoScroll=std::min(buildingInfoScroll,buildingInfoScrollMaximum);
	drawBuildingActionButtons(selBuild, buildingType);
}
