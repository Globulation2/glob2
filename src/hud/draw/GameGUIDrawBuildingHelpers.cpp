// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <algorithm>

#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "BuildingFailureDisplay.h"
#include "BuildingPresentation.h"
#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "SpriteCentering.h"
#include "TeamDisplay.h"
#include "Unit.h"
#include "UnitDisplayNames.h"
#include "FailureShapes.h"

void GameGUI::drawBuildingHeader(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos)
{
	Uint8 r, g, b;

	// draw "building" of "player"
	std::string title;
	title += buildingDisplayName(*buildingType);
	{
		title += " (";
		title += displayPlayerName(selBuild->owner.firstPlayerName);
		title += ")";
	}

	if (drawnScene().panels.local.teamNumber == selBuild->owner.teamNumber)
		{ r=160; g=160; b=255; }
	else if (drawnScene().panels.local.allies & selBuild->owner.me)
		{ r=255; g=210; b=20; }
	else
		{ r=255; g=50; b=50; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	int titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	int titlePos = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+((RIGHT_MENU_WIDTH-titleLen)>>1);
	globalContainer->gfx->drawString(titlePos, ypos, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();

	// building text
	title = "";
	if (selBuild->showLevel)
	{
		const std::string textT = Toolkit::getStringTable()->getString("[level]");
		title += FormattableString("%0 %1").arg(textT).arg(buildingType->level+1);
	}
	if (buildingType->isBuildingSite)
	{
		title += " (";
		title += Toolkit::getStringTable()->getString("[building site]");
		title += ")";
	}
	if (buildingType->prestige)
	{
		title += " - ";
		title += Toolkit::getStringTable()->getString("[Prestige]");
	}
	titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	titlePos = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+((RIGHT_MENU_WIDTH-titleLen)>>1);

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 200));
	globalContainer->gfx->drawString(titlePos, ypos+YOFFSET_TEXT_PARA-1, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();

	ypos += YOFFSET_NAME;
}

void GameGUI::drawBuildingIcon(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int ypos)
{
	Sprite *miniSprite;
	int imgid;
	if (buildingType->miniSpriteImage >= 0)
	{
		miniSprite = buildingType->miniSpritePtr;
		imgid = buildingType->miniSpriteImage;
	}
	else
	{
		miniSprite = buildingType->gameSpritePtr;
		imgid = buildingType->gameSpriteImage;
	}
	// The building icon is centered in a 56 (wide) x 46 (tall) frame.
	constexpr int BUILDING_ICON_BOX_W_PX = 56;
	constexpr int BUILDING_ICON_BOX_H_PX = 46;
	const SpriteCenterOffset off = centerSprite(BUILDING_ICON_BOX_W_PX, BUILDING_ICON_BOX_H_PX, miniSprite, imgid);
	int ddx = (RIGHT_MENU_HALF_WIDTH - 56) / 2 + 2;
	miniSprite->setBaseColor(selBuild->owner.color);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx+off.dx, ypos+4+off.dy, miniSprite, imgid);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx, ypos+4, globalContainer->gamegui, 18);
	globalContainer->gfx->finishDrawingSprite(miniSprite, 255);
}

void GameGUI::drawBuildingHP(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int ypos, BuildingPreviewRows& rows)
{
	if (!buildingType->hpMax)
		return;
	rows.hp=ypos+YOFFSET_TEXT_LINE;

	Uint8 r, g, b;
	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos, globalContainer->littleFont, Toolkit::getStringTable()->getString("[hp]"));
	globalContainer->littleFont->popStyle();

	if (selBuild->hp <= selBuild->effectiveMaxHp/5)
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormattableString("%0/%1").arg(selBuild->hp).arg(selBuild->effectiveMaxHp).c_str());
	globalContainer->littleFont->popStyle();
}

void GameGUI::drawBuildingInsideStats(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int ypos, BuildingPreviewRows& rows)
{
	if (!buildingType->maxUnitInside)
		return;
	if (!((selBuild->owner.allies) & (Team::teamNumberToMask(localTeamNo))))
		return;

	rows.inside=ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE;
	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+YOFFSET_TEXT_LINE, globalContainer->littleFont, Toolkit::getStringTable()->getString("[inside]"));
	globalContainer->littleFont->popStyle();
	if (selBuild->buildingState==Building::ALIVE)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormattableString("%0/%1").arg(selBuild->unitsInside).arg(buildingType->maxUnitInside).c_str());
	}
	else
	{
		if (selBuild->unitsInside>1)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormattableString(Toolkit::getStringTable()->getString("[Units still inside: %0]")).arg(selBuild->unitsInside).c_str());
		}
		else if (selBuild->unitsInside==1)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont,
				Toolkit::getStringTable()->getString("[Still one]") );
		}
	}
}

void GameGUI::drawBuildingFlagInfo(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int ypos)
{
	if (!(buildingType->zonable[WORKER] || buildingType->zonable[EXPLORER] || buildingType->zonable[WARRIOR]))
		return;
	if (!((selBuild->owner.allies) & (Team::teamNumberToMask(localTeamNo))))
		return;

	// get flag stat — feed the displayed (optimistic) position and range
	// so the count tracks the cursor during a flag move or range edit.
	int goingTo = 0, onSpot = 0;
	{
		const int posX = displayedPosX(*selBuild), posY = displayedPosY(*selBuild);
		const int stayRange = displayedUnitStayRange(*selBuild);
		const Sint32 stayRangeSquare = (1 + stayRange) * (1 + stayRange);
		const int w = drawnScene().map.getW(), h = drawnScene().map.getH();
		// Torus distance, exactly as Map::warpDist1d.
		const auto warp = [](int a, int b, int size) { int d = std::abs(a - b) % size; return d > size / 2 ? size - d : d; };
		for (const auto &[x, y] : selBuild->workerPositions)
		{
			const Sint32 dx = warp(posX, x, w), dy = warp(posY, y, h);
			if (dx * dx + dy * dy < stayRangeSquare)
				onSpot++;
			else
				goingTo++;
		}
	}
	// display flag stat
	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos, globalContainer->littleFont, FormattableString("%0").arg(Toolkit::getStringTable()->getString("[In way]")).c_str());
	globalContainer->littleFont->popStyle();
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormattableString("%0").arg(goingTo).c_str());
	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+YOFFSET_TEXT_LINE,
	globalContainer->littleFont, FormattableString(Toolkit::getStringTable()->getString("[On the spot]")).c_str());
	globalContainer->littleFont->popStyle();
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-+RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormattableString("%0").arg(onSpot).c_str());
}

void GameGUI::drawBuildingWorkingControls(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos)
{
	if (!buildingType->maxUnitWorking)
		return;

	if ((selBuild->owner.allies)&(Team::teamNumberToMask(localTeamNo)))
	{
		if (selBuild->buildingState==Building::ALIVE)
		{
			// If we're replaying, display the actual number, not the locally cached one (changeable by the gui user)
			const int maxUnitsWorking = (globalContainer->isViewingGame()?selBuild->maxUnitWorking:displayedMaxUnitWorking(*selBuild));

			std::string working = Toolkit::getStringTable()->getString("[working]");
			const int len = globalContainer->littleFont->getStringWidth(working)+4;
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, working);
			globalContainer->littleFont->popStyle();
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4+len, ypos, globalContainer->littleFont, FormattableString("%0/%1").arg(selBuild->unitsWorking).arg(maxUnitsWorking).c_str());
			drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos+YOFFSET_TEXT_BAR, maxUnitsWorking, selBuild->unitsWorking, buildingType->semantics.assignmentLimit);
		}
		else
		{
			if (selBuild->unitsWorking>1)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormattableString(Toolkit::getStringTable()->getString("[Units still working: %0]")).arg(selBuild->unitsWorking).c_str());
			}
			else if (selBuild->unitsWorking==1)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
					Toolkit::getStringTable()->getString("[still one unit working]") );
			}
		}
	}
	if(highlights.find(HighlightUnitsAssignedBar) != highlights.end())
	{
		arrowPositions.push_back(HighlightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36, ypos+6, 38));
	}
	ypos += YOFFSET_BAR+YOFFSET_B_SEP;
}

void GameGUI::drawBuildingPriorityControls(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos)
{
	if (!buildingType->maxUnitWorking)
		return;
	if (!((selBuild->owner.allies)&(Team::teamNumberToMask(localTeamNo))))
		return;
	if (selBuild->buildingState != Building::ALIVE)
		return;

	// If we're replaying, display the actual value, not the locally cached one (changeable by the gui user)
	const int priority = (globalContainer->isViewingGame()?selBuild->priority:displayedPriority(*selBuild));

	ypos += YOFFSET_B_SEP;

	int width = 128/3;
	std::string prioritystr = Toolkit::getStringTable()->getString("[priority]");
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, prioritystr);

	std::string lowstr = Toolkit::getStringTable()->getString("[low priority]");
	std::string medstr = Toolkit::getStringTable()->getString("[medium priority]");
	std::string highstr = Toolkit::getStringTable()->getString("[high priority]");

	drawRadioButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos+12+4, (priority==-1));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+14, ypos+12+2, globalContainer->littleFont, lowstr);

	drawRadioButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+width, ypos+12+4, (priority==0));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+14+width, ypos+12+2, globalContainer->littleFont, medstr);

	drawRadioButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+width*2, ypos+12+4, (priority==1));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+14+width*2, ypos+12+2, globalContainer->littleFont, highstr);

	ypos += YOFFSET_BAR+YOFFSET_B_SEP;
}

void GameGUI::drawBuildingRangeControls(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos)
{
	if (buildingType->maxUnitStayRange<=0 || !(buildingType->zonable[WORKER] || buildingType->zonable[EXPLORER] || buildingType->zonable[WARRIOR]))
		return;

	if ((selBuild->owner.allies)&(Team::teamNumberToMask(localTeamNo)))
	{
		// If we're replaying, display the actual number, not the locally cached one (changeable by the gui user)
		const int unitStayRange = (globalContainer->isViewingGame()?selBuild->unitStayRange:displayedUnitStayRange(*selBuild));

		std::string range = Toolkit::getStringTable()->getString("[range]");
		const int len = globalContainer->littleFont->getStringWidth(range)+4;
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, range);
		globalContainer->littleFont->popStyle();
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4+len, ypos, globalContainer->littleFont, FormattableString("%0").arg(selBuild->unitStayRange).c_str());
		drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos+YOFFSET_TEXT_BAR, unitStayRange, 0, selBuild->type->maxUnitStayRange);
	}
	ypos += YOFFSET_BAR+YOFFSET_B_SEP;
}

void GameGUI::drawBuildingCombatStats(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos, BuildingPreviewRows& rows)
{
	(void)selBuild;
	if (buildingType->armor)
	{
		rows.armor=ypos;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormattableString("%0: %1").arg(Toolkit::getStringTable()->getString("[armor]")).arg(buildingType->armor).c_str());
		ypos+=YOFFSET_TEXT_LINE;
	}
	if (buildingType->maxUnitInside)
		ypos += YOFFSET_INFOS;
	const int damageRows=buildingProjectileDamageRows(*buildingType);
	if (damageRows)
	{
		rows.damageRows=damageRows;
		rows.range=ypos+1+damageRows*11;
		for (int row=0; row<damageRows; ++row)
		{
			rows.damage[row]=ypos+1+row*11;
			const std::string label=damageRows==1 ? Toolkit::getStringTable()->getString("[damage]") : getUnitName(row);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+1+row*11, globalContainer->littleFont,
				FormattableString("%0 : %1").arg(label).arg(buildingType->semantics.projectileDamage[row]).c_str());
		}
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+1+damageRows*11, globalContainer->littleFont, FormattableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[range]")).arg(buildingType->shootingRange).c_str());
		ypos += buildingProjectileStatsHeight(*buildingType);
	}
}

void GameGUI::drawBuildingExchange(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos, BuildingPreviewRows& rows)
{
	if (!buildingType->canExchange)
		return;
	if (!((selBuild->owner.sharedVisionExchange)&(Team::teamNumberToMask(localTeamNo))))
		return;

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, Toolkit::getStringTable()->getString("[market]"));
	globalContainer->littleFont->popStyle();
	//globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-36-3, ypos+1, globalContainer->gamegui, EXCHANGE_BUILDING_ICONS);
	ypos += YOFFSET_TEXT_PARA;
	for (unsigned i=0; i<HAPPINESS_COUNT; i++)
	{
		rows.resource[i+HAPPINESS_BASE]=ypos;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormattableString("%0 (%1/%2)").arg(getResourceName(i+HAPPINESS_BASE)).arg(selBuild->resources[i+HAPPINESS_BASE]).arg(buildingType->maxResource[i+HAPPINESS_BASE]).c_str());

		/*
		// Exchange feature is broken/disabled. If revived, this should use
		// BuildingGuiState::pendingReceiveResourceMask /
		// pendingSendResourceMask (TODO: add) for the in-flight mask, falling
		// back to receiveResourceMask / sendResourceMask. See the equivalent
		// pattern for ratio / priority.
		int inId, outId;
		if (selBuild->receiveResourceMask & (1<<i))
			inId = 20;
		else
			inId = 19;
		if (selBuild->sendResourceMask & (1<<i))
			outId = 20;
		else
			outId = 19;
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-36, ypos+2, globalContainer->gamegui, inId);
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-18, ypos+2, globalContainer->gamegui, outId);
		*/

		ypos += YOFFSET_TEXT_PARA;
	}
}

void GameGUI::drawBuildingResources(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos, BuildingPreviewRows& rows)
{
	if (!((selBuild->owner.allies) & (Team::teamNumberToMask(localTeamNo))))
		return;
	// resources in. A market's fruit is drawn by drawBuildingExchange; its
	// basic-resource stock, from level 2 on, is listed here like any store.
	for (unsigned i=0; i<globalContainer->resourcesTypes.size(); i++)
	{
		if (buildingType->canExchange && i>=BASIC_COUNT)
			continue;
		if (buildingType->maxResource[i])
		{
			rows.resource[i]=ypos;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormattableString("%0 : %1/%2").arg(getResourceName(i)).arg(selBuild->resources[i]).arg(buildingType->maxResource[i]).c_str());
			ypos += YOFFSET_RESOURCE_LINE;
		}
	}
	if (buildingType->maxBullets)
	{
		rows.bullets=ypos;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormattableString("%0 : %1/%2").arg(Toolkit::getStringTable()->getString("[Bullets]")).arg(selBuild->bullets).arg(buildingType->maxBullets).c_str());
		ypos += YOFFSET_RESOURCE_LINE;
	}
	ypos += YOFFSET_RESOURCE_SECTION_PAD;
}

// Draws the swarm-building production-timeout progress bar followed by one
// scrollbox per unit type for the pending-vs-authoritative unit ratios. The
// progress bar is split into an "elapsed" (blue) and "remaining" (gray)
// segment scaled to SWARM_PROGRESS_BAR_WIDTH. Each ratio scrollbox shows two
// channels: the pending value (the user's in-flight slider input, drawn as the
// lighter bar) and ratio[i] (the simulation-confirmed value, drawn as the
// darker overlay). During replay no pending state exists, so both channels
// equal ratio[i] and overlay exactly; during normal play they differ briefly
// while OrderModifySwarm is in flight.
void GameGUI::drawBuildingSwarmRatios(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos)
{
	if (!((selBuild->owner.allies) & (Team::teamNumberToMask(localTeamNo))))
		return;
	if (!buildingType->semantics.production.enabledUnitMask)
		return;

	const int duration=std::max(1,selBuild->productionDuration);
	int left=std::clamp(int(Sint64(selBuild->productionTimeout)*SWARM_PROGRESS_BAR_WIDTH/duration),0,SWARM_PROGRESS_BAR_WIDTH);
	int elapsed=SWARM_PROGRESS_BAR_WIDTH-left;
	globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, elapsed, SWARM_PROGRESS_BAR_HEIGHT, 100, 100, 255);
	globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+elapsed, ypos, left, SWARM_PROGRESS_BAR_HEIGHT, 128, 128, 128);

	ypos += YOFFSET_SWARM_PROGRESS_BAR;
	const std::array<Sint32, NB_UNIT_TYPE> displayed = displayedRatio(*selBuild);
	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		if (!buildingType->semantics.production.recipes[i].enabled) continue;
		drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, displayed[i], selBuild->ratio[i], MAX_RATIO_RANGE);
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+24, ypos, globalContainer->littleFont, getUnitName(i));

		if(i==1 && highlights.find(HighlightRatioBar) != highlights.end())
		{
			arrowPositions.push_back(HighlightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET-36, ypos-8, 38));
		}

		ypos += YOFFSET_SWARM_RATIO_LINE;
	}
}

// Returns the string-table key for a unit-can't-work reason. The two
// access/too-far-from-building rows reword "building" → "flag" when the
// selected building type is virtual (a flag), so isVirtual is consulted
// only for those two reasons. Table is indexed by Building::UnitCantWorkReason;
// the static_assert keeps it locked to the enum size so future additions
// to UnitCantWorkReason can't silently fall off the end.
namespace { constexpr int FAILURE_SHAPE_HALF = 5; }

static const char* failureReasonKey(Building::UnitCantWorkReason reason, bool isVirtual)
{
	static constexpr const char* kReasonKey[Building::UnitCantWorkReasonSize] = {
		/* UnitNotAvailable        */ "[%0 units not available]",
		/* UnitTooLowLevel         */ "[%0 units too low level]",
		/* UnitCantAccessBuilding  */ "[%0 units can't access building]",
		/* UnitTooFarFromBuilding  */ "[%0 units too far from building]",
		/* UnitCantAccessResource  */ "[%0 units can't access resource]",
		/* UnitCantAccessFruit     */ "[%0 units can't access fruit]",
		/* UnitTooFarFromResource  */ "[%0 units too far from resource]",
		/* UnitTooFarFromFruit     */ "[%0 units too far from fruit]",
	};
	static_assert(Building::UnitCantWorkReasonSize == 8,
		"failureReasonKey table must stay in sync with Building::UnitCantWorkReason");

	if (isVirtual)
	{
		if (reason == Building::UnitCantAccessBuilding)
			return "[%0 units can't access flag]";
		if (reason == Building::UnitTooFarFromBuilding)
			return "[%0 units too far from flag]";
	}
	return kReasonKey[reason];
}

void GameGUI::drawBuildingFailureReasons(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos)
{
	if (!((selBuild->owner.allies) & (Team::teamNumberToMask(localTeamNo))))
		return;

	// Only show the failure-reason rows when the building is still asking for
	// units and a *real* obstruction exists. A building that merely lacks spare
	// idle units (UnitNotAvailable only) is in its normal state and gets no
	// rows; see shouldShowFailingUnitMarkers, which the map view's badges ask
	// too so that the rows and the badges cannot disagree.
	if (!shouldShowFailingUnitMarkers(selBuild->unitsFailingRequirements.data(),
	                                  Building::UnitCantWorkReasonSize,
	                                  Building::UnitNotAvailable,
	                                  selBuild->unitsWorking,
	                                  selBuild->desiredMaxUnitWorking))
		return;

	for(unsigned j=0; j<Building::UnitCantWorkReasonSize; ++j)
	{
		int n = selBuild->unitsFailingRequirements[j];
		if(n>0)
		{
			const Building::UnitCantWorkReason reason = static_cast<Building::UnitCantWorkReason>(j);
			const char* key = failureReasonKey(reason, buildingType->isVirtual);
			std::string s = FormattableString(Toolkit::getStringTable()->getString(key)).arg(n);
			// The shape the same units wear in the map view.
			const int shapeX = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+10;
			if (reason != Building::UnitNotAvailable)
				drawFailureShape(globalContainer->gfx, shapeX+FAILURE_SHAPE_HALF, ypos+FAILURE_SHAPE_HALF+1, FAILURE_SHAPE_HALF, reason, failureShapeColor());
			globalContainer->gfx->drawString(shapeX+2*FAILURE_SHAPE_HALF+6, ypos, globalContainer->littleFont, s.c_str());
			ypos += YOFFSET_RESOURCE_LINE;
		}
	}
}

GameGUI::BuildingPreview GameGUI::hoveredBuildingPreview(const SceneBuildingPanel& building) const
{
    if (!building.valid || building.owner.teamNumber!=drawnScene().panels.local.teamNumber
        || !(building.owner.allies&Team::teamNumberToMask(localTeamNo))
        || building.constructionResultState!=Building::NO_CONSTRUCTION
        || building.buildingState!=Building::ALIVE || building.type->isBuildingSite)
        return BuildingPreview::None;
    const int x=globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET;
    const int y=globalContainer->gfx->getH()-BOTTOM_BUTTON_PRIMARY_YOFFSET;
    if (mouseX<=x+12 || mouseX>=globalContainer->gfx->getW()-12
        || mouseY<=y || mouseY>=y+BOTTOM_BUTTON_HEIGHT)
        return BuildingPreview::None;
    if (building.hp<building.effectiveMaxHp)
        return building.type->semantics.repairable && building.hardSpaceForRepair
            ? BuildingPreview::Repair : BuildingPreview::None;
    return building.type->nextLevel>=0 && building.hardSpaceForUpgrade
        ? BuildingPreview::Upgrade : BuildingPreview::None;
}

void GameGUI::drawBuildingActionButtons(const SceneBuildingPanel* selBuild, BuildingType* buildingType)
{
	if (!((selBuild->owner.allies) & (Team::teamNumberToMask(localTeamNo))))
		return;
	if (selBuild->owner.teamNumber != drawnScene().panels.local.teamNumber)
		return;

	const int btnX = globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET;
	const int primaryY = globalContainer->gfx->getH()-BOTTOM_BUTTON_PRIMARY_YOFFSET;
	const int secondaryY = globalContainer->gfx->getH()-BOTTOM_BUTTON_SECONDARY_YOFFSET;

	if (selBuild->constructionResultState==Building::REPAIR)
	{
		drawBlueButton(btnX, primaryY, "[cancel repair]");
	}
	else if (selBuild->constructionResultState==Building::UPGRADE)
	{
		assert(buildingType->nextLevel!=-1);
		drawBlueButton(btnX, primaryY, "[cancel upgrade]");
	}
	else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE) && !buildingType->isBuildingSite)
	{
		if (selBuild->hp<selBuild->effectiveMaxHp)
		{
			// repair
			if (selBuild->type->semantics.repairable && selBuild->hardSpaceForRepair)
			{
				drawBlueButton(btnX, primaryY, "[repair]");
			}
		}
		else if (buildingType->nextLevel!=-1)
		{
			// upgrade
			if (selBuild->hardSpaceForUpgrade)
			{
				drawBlueButton(btnX, primaryY, "[upgrade]");
			}
		}
	}

	// building destruction
	if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
	{
		drawRedButton(btnX, secondaryY, "[cancel destroy]");
	}
	else if (selBuild->buildingState==Building::ALIVE)
	{
		drawRedButton(btnX, secondaryY, "[destroy]");
	}
}

void GameGUI::drawBuildingTimeToLeaveBar(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos, unsigned& unitInsideBarYDec)
{
	if (!((selBuild->owner.allies) & (Team::teamNumberToMask(localTeamNo))))
		return;

	const int maxTimeTo=buildingServiceProgressTimeout(*buildingType);
	int dec = (RIGHT_MENU_RIGHT_OFFSET-128);
	if (maxTimeTo)
	{
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, 128, 7, 168, 150, 90);
		for (const SceneBuildingPanel::InsideUnit &inside : selBuild->insideUnits)
		{
			const SceneBuildingPanel::InsideUnit *u = &inside;
			if (u->inside)
			{
				int dividend=-u->insideTimeout*128+128-u->delta/2;
				int divisor=1+maxTimeTo;
				int left=dividend/divisor;
				int alpha=((dividend%divisor)*255)/divisor;

				if (!globalContainer->settings.smoothProgressIndicators)
				{
					globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-1-dec, ypos, 7, 17, 30, 64);
					globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-dec, ypos, 7, 63, 111, 149);
					globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left+1-dec, ypos, 7, 17, 30, 64);
				}
				else
				{
					globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-2-dec, ypos, 7, 17, 30, 64, alpha);
					globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-1-dec, ypos, 7, 17, 30, 64);
					globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-dec, ypos, 7, 17, 30, 64);
					globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left+1-dec, ypos, 7, 17, 30, 64);
					globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left+2-dec, ypos, 7, 17, 30, 64, 255-alpha);

					globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-1-dec, ypos, 7, 63, 111, 149, alpha);
					globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-dec, ypos, 7, 63, 111, 149);
					globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left+1-dec, ypos, 7, 63, 111, 149, 255-alpha);
				}
			}
		}

		ypos += YOFFSET_PROGRESS_BAR;
		unitInsideBarYDec = YOFFSET_PROGRESS_BAR;
	}
}

void GameGUI::drawBuildingFlagControls(const SceneBuildingPanel* selBuild, BuildingType* buildingType, int& ypos)
{
	if (!((selBuild->owner.allies) & (Team::teamNumberToMask(localTeamNo))))
		return;

	// cleared resources for clearing flags: one checkbox row per clearable
	// resource (stone is never cleared, so it has no row)
	if (buildingType->zonable[WORKER])
	{
		ypos += YOFFSET_B_SEP;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
			Toolkit::getStringTable()->getString("[Clearing:]"));
		ypos += YOFFSET_TEXT_PARA;
		for (int i=0; i<BASIC_COUNT; i++)
			if (i!=STONE)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+28, ypos, globalContainer->littleFont,
					getResourceName(i));
				int spriteId;
				if (displayedClearingResource(*selBuild, i))
					spriteId=20;
				else
					spriteId=19;
				globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+10, ypos+2, globalContainer->gamegui, spriteId);

				ypos+=YOFFSET_TEXT_PARA;
			}
	}
	// min worker qualification for war flags: one radio row per worker level;
	// row i means minLevelToFlag==i, shown to the player as level 1+i
	if (buildingType->zonable[WORKER])
	{
		ypos += YOFFSET_B_SEP;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
			std::string(getUnitName(WORKER))+": "+Toolkit::getStringTable()->getString("[Min required level:]"));
		ypos += YOFFSET_TEXT_PARA;
		for (int i=0; i<NB_UNIT_LEVELS; i++)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+28, ypos, globalContainer->littleFont, 1+i);
			int spriteId;
			if (i==displayedMinWorkerLevelToFlag(*selBuild))
				spriteId=20;
			else
				spriteId=19;
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+10, ypos+2, globalContainer->gamegui, spriteId);

			ypos+=YOFFSET_TEXT_PARA;
		}
	}
	// min war level for war flags: one radio row per warrior level;
	// row i means minLevelToFlag==i, shown to the player as level 1+i
	if (buildingType->zonable[WARRIOR])
	{
		ypos += YOFFSET_B_SEP;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
			Toolkit::getStringTable()->getString("[Min required level:]"));
		ypos += YOFFSET_TEXT_PARA;
		for (int i=0; i<NB_UNIT_LEVELS; i++)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+28, ypos, globalContainer->littleFont, 1+i);
			int spriteId;
			if (i==displayedMinLevelToFlag(*selBuild))
				spriteId=20;
			else
				spriteId=19;
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+10, ypos+2, globalContainer->gamegui, spriteId);

			ypos+=YOFFSET_TEXT_PARA;
		}
	}
	// explorer requirement for exploration flags: one radio row per
	// EXPLORATION_FLAG_OPTION_* value, independent of ground-unit filters.
	if (buildingType->zonable[EXPLORER])
	{
		static const char* const optionKeys[EXPLORATION_FLAG_OPTION_COUNT] = {
			"[any explorer]",  // EXPLORATION_FLAG_OPTION_ANY_EXPLORER
			"[ground attack]", // EXPLORATION_FLAG_OPTION_GROUND_ATTACK
		};

		ypos += YOFFSET_B_SEP;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
			Toolkit::getStringTable()->getString("[Min required level:]"));
		ypos += YOFFSET_TEXT_PARA;

		for (int i=0; i<EXPLORATION_FLAG_OPTION_COUNT; i++)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+28, ypos, globalContainer->littleFont,
				Toolkit::getStringTable()->getString(optionKeys[i]));
			int spriteId;
			if (displayedExplorersRequireBombing(*selBuild) == i)
				spriteId = 20;
			else
				spriteId = 19;
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+10, ypos+2, globalContainer->gamegui, spriteId);

			ypos += YOFFSET_TEXT_PARA;
		}
	}
}

void GameGUI::drawBuildingUpgradePreview(const SceneBuildingPanel* selBuild, BuildingType* buildingType,
    const BuildingPreviewRows& rows, int& ypos)
{
    // Dense IDs belong to the published Scene's retained catalog. A newer
    // simulation catalog must not change the meaning of an older frame.
    const auto& types=drawnScene().buildingTypes;
    if (!types || buildingType->nextLevel<0 || size_t(buildingType->nextLevel)>=types->size()) return;
    const auto* site=&(*types)[buildingType->nextLevel];
    if (site->nextLevel<0 || size_t(site->nextLevel)>=types->size()) return;
    const auto* target=&(*types)[site->nextLevel];
    // Existing rows receive the target value at the exact coordinate recorded
    // by their draw helper. Target-only metrics use labeled appendix rows, so
    // none of the current controls or their click regions move on hover.
    bool heading=false;
    const auto beginAppendix=[&]() {
        if (heading) return;
        ypos+=YOFFSET_B_SEP;
        globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4,
            ypos,globalContainer->littleFont,Toolkit::getStringTable()->getString("[upgrade]"));
        ypos+=YOFFSET_TEXT_PARA;heading=true;
    };
    const auto metric=[&](int row,const std::string& label,int value) {
        if (row>=0) { drawValueAlignedRight(row,value);return; }
        if (!value) return;
        beginAppendix();
        globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4,
            ypos,globalContainer->littleFont,label);
        drawValueAlignedRight(ypos,value);ypos+=YOFFSET_RESOURCE_LINE;
    };
    metric(rows.hp,Toolkit::getStringTable()->getString("[hp]"),target->hpMax*selBuild->buildingHpMultiplier);
    metric(rows.inside,Toolkit::getStringTable()->getString("[inside]"),target->maxUnitInside);
    metric(rows.armor,Toolkit::getStringTable()->getString("[armor]"),target->armor);
    const int damageRows=buildingProjectileDamageRows(*target);
    if (rows.damageRows==1 && damageRows<=1)
        metric(rows.damage[0],Toolkit::getStringTable()->getString("[damage]"),damageRows ? target->semantics.projectileDamage[0] : 0);
    else if (rows.damageRows==NB_UNIT_TYPE || damageRows==NB_UNIT_TYPE)
        for (int unit=0;unit<NB_UNIT_TYPE;++unit)
            metric(rows.damageRows==NB_UNIT_TYPE ? rows.damage[unit] : -1,getUnitName(unit),
                damageRows ? target->semantics.projectileDamage[unit] : 0);
    else if (damageRows)
        metric(-1,Toolkit::getStringTable()->getString("[damage]"),target->semantics.projectileDamage[0]);
    metric(rows.range,Toolkit::getStringTable()->getString("[range]"),damageRows ? target->shootingRange : 0);
    for (int resource=0;resource<MAX_RESOURCES;++resource)
        metric(rows.resource[resource],getResourceName(resource),target->maxResource[resource]);
    metric(rows.bullets,Toolkit::getStringTable()->getString("[Bullets]"),target->maxBullets);
    beginAppendix();
    drawCosts(site->semantics.constructionCost.data(),globalContainer->littleFont,ypos);
}
