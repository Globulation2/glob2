// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <algorithm>

#include <FormatableString.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "TeamDisplay.h"
#include "Unit.h"
#include "UnitDisplayNames.h"

void GameGUI::drawBuildingHeader(Building* selBuild, BuildingType* buildingType, int& ypos)
{
	Uint8 r, g, b;

	// draw "building" of "player"
	std::string title;
	std::string key = "[" + buildingType->type + "]";
	title += Toolkit::getStringTable()->getString(key.c_str());
	{
		title += " (";
		title += displayPlayerName(*selBuild->owner);
		title += ")";
	}

	if (localTeam->teamNumber == selBuild->owner->teamNumber)
		{ r=160; g=160; b=255; }
	else if (localTeam->allies & selBuild->owner->me)
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
	if ((buildingType->nextLevel>=0) ||  (buildingType->prevLevel>=0))
	{
		const std::string textT = Toolkit::getStringTable()->getString("[level]");
		title += FormatableString("%0 %1").arg(textT).arg(buildingType->level+1);
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

void GameGUI::drawBuildingIcon(Building* selBuild, BuildingType* buildingType, int ypos)
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
	int dx = (56-miniSprite->getW(imgid))>>1;
	int dy = (46-miniSprite->getH(imgid))>>1;
	int ddx = (RIGHT_MENU_HALF_WIDTH - 56) / 2 + 2;
	miniSprite->setBaseColor(selBuild->owner->color);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx+dx, ypos+4+dy, miniSprite, imgid);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx, ypos+4, globalContainer->gamegui, 18);
	globalContainer->gfx->finishDrawingSprite(miniSprite, 255);
}

void GameGUI::drawBuildingHP(Building* selBuild, BuildingType* buildingType, int ypos)
{
	if (!buildingType->hpMax)
		return;

	Uint8 r, g, b;
	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos, globalContainer->littleFont, Toolkit::getStringTable()->getString("[hp]"));
	globalContainer->littleFont->popStyle();

	if (selBuild->hp <= buildingType->hpMax/5)
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0/%1").arg(selBuild->hp).arg(buildingType->hpMax).c_str());
	globalContainer->littleFont->popStyle();
}

void GameGUI::drawBuildingInsideStats(Building* selBuild, BuildingType* buildingType, int ypos)
{
	if (!buildingType->maxUnitInside)
		return;
	if (!((selBuild->owner->allies) & (1<<localTeamNo)))
		return;

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+YOFFSET_TEXT_LINE, globalContainer->littleFont, Toolkit::getStringTable()->getString("[inside]"));
	globalContainer->littleFont->popStyle();
	if (selBuild->buildingState==Building::ALIVE)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0/%1").arg(selBuild->unitsInside.size()).arg(buildingType->maxUnitInside).c_str());
	}
	else
	{
		if (selBuild->unitsInside.size()>1)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0%1").arg(Toolkit::getStringTable()->getString("[Still (i)]")).arg(selBuild->unitsInside.size()).c_str());
		}
		else if (selBuild->unitsInside.size()==1)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont,
				Toolkit::getStringTable()->getString("[Still one]") );
		}
	}
}

void GameGUI::drawBuildingFlagInfo(Building* selBuild, BuildingType* buildingType, int ypos)
{
	if (!buildingType->defaultUnitStayRange)
		return;
	if (!((selBuild->owner->allies) & (1<<localTeamNo)))
		return;

	// get flag stat — feed the displayed (optimistic) position and range
	// so the count tracks the cursor during a flag drag or scroll-resize.
	int goingTo, onSpot;
	computeFlagStatDisplayed(*selBuild,
		displayedPosX(*selBuild), displayedPosY(*selBuild),
		displayedUnitStayRange(*selBuild),
		&goingTo, &onSpot);
	// display flag stat
	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos, globalContainer->littleFont, FormatableString("%0").arg(Toolkit::getStringTable()->getString("[In way]")).c_str());
	globalContainer->littleFont->popStyle();
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0").arg(goingTo).c_str());
	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+YOFFSET_TEXT_LINE,
	globalContainer->littleFont, FormatableString(Toolkit::getStringTable()->getString("[On the spot]")).c_str());
	globalContainer->littleFont->popStyle();
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-+RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0").arg(onSpot).c_str());
}

void GameGUI::drawBuildingWorkingControls(Building* selBuild, BuildingType* buildingType, int& ypos)
{
	if (!buildingType->maxUnitWorking)
		return;

	if ((selBuild->owner->allies)&(1<<localTeamNo))
	{
		if (selBuild->buildingState==Building::ALIVE)
		{
			// If we're replaying, display the actual number, not the locally cached one (changable by the gui user)
			const int maxUnitsWorking = (globalContainer->replaying?selBuild->maxUnitWorking:displayedMaxUnitWorking(*selBuild));

			std::string working = Toolkit::getStringTable()->getString("[working]");
			const int len = globalContainer->littleFont->getStringWidth(working)+4;
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, working);
			globalContainer->littleFont->popStyle();
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4+len, ypos, globalContainer->littleFont, FormatableString("%0/%1").arg((int)selBuild->unitsWorking.size()).arg(maxUnitsWorking).c_str());
			drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos+YOFFSET_TEXT_BAR, maxUnitsWorking, selBuild->unitsWorking.size(), MAX_UNIT_WORKING);
		}
		else
		{
			if (selBuild->unitsWorking.size()>1)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0%1%2").arg(Toolkit::getStringTable()->getString("[still (w)]")).arg(selBuild->unitsWorking.size()).arg(Toolkit::getStringTable()->getString("[units working]")).c_str());
			}
			else if (selBuild->unitsWorking.size()==1)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
					Toolkit::getStringTable()->getString("[still one unit working]") );
			}
		}
	}
	if(hilights.find(HilightUnitsAssignedBar) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36, ypos+6, 38));
	}
	ypos += YOFFSET_BAR+YOFFSET_B_SEP;
}

void GameGUI::drawBuildingPriorityControls(Building* selBuild, BuildingType* buildingType, int& ypos)
{
	if (!buildingType->maxUnitWorking)
		return;
	if (!((selBuild->owner->allies)&(1<<localTeamNo)))
		return;
	if (selBuild->buildingState != Building::ALIVE)
		return;

	// If we're replaying, display the actual number, not the locally cached one (changable by the gui user)
	const int priority = (globalContainer->replaying?selBuild->priority:selBuild->priorityLocal);

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

void GameGUI::drawBuildingRangeControls(Building* selBuild, BuildingType* buildingType, int& ypos)
{
	if (!buildingType->defaultUnitStayRange)
		return;

	if ((selBuild->owner->allies)&(1<<localTeamNo))
	{
		// If we're replaying, display the actual number, not the locally cached one (changeable by the gui user)
		const int unitStayRange = (globalContainer->replaying?selBuild->unitStayRange:displayedUnitStayRange(*selBuild));

		std::string range = Toolkit::getStringTable()->getString("[range]");
		const int len = globalContainer->littleFont->getStringWidth(range)+4;
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, range);
		globalContainer->littleFont->popStyle();
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4+len, ypos, globalContainer->littleFont, FormatableString("%0").arg(selBuild->unitStayRange).c_str());
		drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos+YOFFSET_TEXT_BAR, unitStayRange, 0, selBuild->type->maxUnitStayRange);
	}
	ypos += YOFFSET_BAR+YOFFSET_B_SEP;
}

void GameGUI::drawBuildingCombatStats(Building* selBuild, BuildingType* buildingType, int& ypos)
{
	(void)selBuild;
	if (buildingType->armor)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[armor]")).arg(buildingType->armor).c_str());
		ypos+=YOFFSET_TEXT_LINE;
	}
	if (buildingType->maxUnitInside)
		ypos += YOFFSET_INFOS;
	if (buildingType->shootDamage)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+1, globalContainer->littleFont, FormatableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[damage]")).arg(buildingType->shootDamage).c_str());
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+12, globalContainer->littleFont, FormatableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[range]")).arg(buildingType->shootingRange).c_str());
		ypos += YOFFSET_TOWER;
	}
}

void GameGUI::drawBuildingExchange(Building* selBuild, BuildingType* buildingType, int& ypos)
{
	if (!buildingType->canExchange)
		return;
	if (!((selBuild->owner->sharedVisionExchange)&(1<<localTeamNo)))
		return;

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, Toolkit::getStringTable()->getString("[market]"));
	globalContainer->littleFont->popStyle();
	//globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-36-3, ypos+1, globalContainer->gamegui, EXCHANGE_BUILDING_ICONS);
	ypos += YOFFSET_TEXT_PARA;
	for (unsigned i=0; i<HAPPYNESS_COUNT; i++)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1/%2)").arg(getRessourceName(i+HAPPYNESS_BASE)).arg(selBuild->ressources[i+HAPPYNESS_BASE]).arg(buildingType->maxRessource[i+HAPPYNESS_BASE]).c_str());

		/*
		int inId, outId;
		if (selBuild->receiveRessourceMaskLocal & (1<<i))
			inId = 20;
		else
			inId = 19;
		if (selBuild->sendRessourceMaskLocal & (1<<i))
			outId = 20;
		else
			outId = 19;
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-36, ypos+2, globalContainer->gamegui, inId);
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-18, ypos+2, globalContainer->gamegui, outId);
		*/

		ypos += YOFFSET_TEXT_PARA;
	}
}

void GameGUI::drawBuildingResources(Building* selBuild, BuildingType* buildingType, int& ypos)
{
	if (!((selBuild->owner->allies) & (1<<localTeamNo)))
		return;
	if (buildingType->canExchange)
		return;

	// ressources in
	for (unsigned i=0; i<globalContainer->ressourcesTypes.size(); i++)
	{
		if (buildingType->maxRessource[i])
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1/%2").arg(getRessourceName(i)).arg(selBuild->ressources[i]).arg(buildingType->maxRessource[i]).c_str());
			ypos += YOFFSET_RESSOURCE_LINE;
		}
	}
	if (buildingType->maxBullets)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1/%2").arg(Toolkit::getStringTable()->getString("[Bullets]")).arg(selBuild->bullets).arg(buildingType->maxBullets).c_str());
		ypos += YOFFSET_RESSOURCE_LINE;
	}
	ypos += YOFFSET_RESSOURCE_SECTION_PAD;
}

// Draws the swarm-building production-timeout progress bar followed by one
// scrollbox per unit type for the local-vs-actual unit ratios. The progress
// bar is split into an "elapsed" (blue) and "remaining" (gray) segment scaled
// to SWARM_PROGRESS_BAR_WIDTH. Each ratio scrollbox shows two channels:
// ratioLocal[i] (the user's pending input, drawn as the lighter bar) and
// ratio[i] (the simulation-confirmed value, drawn as the darker overlay).
// During replay both channels equal ratio[i] and overlay exactly; during
// normal play they differ briefly while OrderModifySwarm is in flight.
void GameGUI::drawBuildingSwarmRatios(Building* selBuild, BuildingType* buildingType, int& ypos)
{
	if (!((selBuild->owner->allies) & (1<<localTeamNo)))
		return;
	if (!buildingType->unitProductionTime)
		return;

	int left=(selBuild->productionTimeout*SWARM_PROGRESS_BAR_WIDTH)/buildingType->unitProductionTime;
	int elapsed=SWARM_PROGRESS_BAR_WIDTH-left;
	globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, elapsed, SWARM_PROGRESS_BAR_HEIGHT, 100, 100, 255);
	globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+elapsed, ypos, left, SWARM_PROGRESS_BAR_HEIGHT, 128, 128, 128);

	ypos += YOFFSET_SWARM_PROGRESS_BAR;
	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		// If we're replaying, display the actual number, not the locally cached one (changable by the gui user)
		const int ratio = (globalContainer->replaying?selBuild->ratio[i]:selBuild->ratioLocal[i]);

		drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, ratio, selBuild->ratio[i], MAX_RATIO_RANGE);
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+24, ypos, globalContainer->littleFont, getUnitName(i));

		if(i==1 && hilights.find(HilightRatioBar) != hilights.end())
		{
			arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET-36, ypos-8, 38));
		}

		ypos += YOFFSET_SWARM_RATIO_LINE;
	}
}

void GameGUI::drawBuildingFailureReasons(Building* selBuild, BuildingType* buildingType, int& ypos)
{
	if (!((selBuild->owner->allies) & (1<<localTeamNo)))
		return;

	// data on whether or not the building is recieving units
	bool otherFailure=true;
	for(unsigned j=0; j<Building::UnitCantWorkReasonSize; ++j)
	{
		int n = selBuild->unitsFailingRequirements[j];
		if(j!=0 && n>0)
			otherFailure=true;
	}
	if(!otherFailure)
		return;

	for(unsigned j=0; j<Building::UnitCantWorkReasonSize; ++j)
	{
		int n = selBuild->unitsFailingRequirements[j];
		if(n>0 && (int)selBuild->unitsWorking.size() < selBuild->desiredMaxUnitWorking)
		{
			std::string s;
			if(j == Building::UnitNotAvailable)
				s = FormatableString(Toolkit::getStringTable()->getString("[%0 units not available]")).arg(n);
			if(j == Building::UnitTooLowLevel)
				s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too low level]")).arg(n);
			else if(j == Building::UnitCantAccessBuilding)
			{
				if (buildingType->isVirtual)
					s = FormatableString(Toolkit::getStringTable()->getString("[%0 units can't access flag]")).arg(n);
				else
					s = FormatableString(Toolkit::getStringTable()->getString("[%0 units can't access building]")).arg(n);
			}
			else if(j == Building::UnitTooFarFromBuilding)
			{
				if (buildingType->isVirtual)
					s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too far from flag]")).arg(n);
				else
					s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too far from building]")).arg(n);
			}
			else if(j == Building::UnitCantAccessResource)
				s = FormatableString(Toolkit::getStringTable()->getString("[%0 units can't access resource]")).arg(n);
			else if(j == Building::UnitCantAccessFruit)
				s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too far from resource]")).arg(n);
			else if(j == Building::UnitTooFarFromResource)
				s = FormatableString(Toolkit::getStringTable()->getString("[%0 units can't access fruit]")).arg(n);
			else if(j == Building::UnitTooFarFromFruit)
				s = FormatableString(Toolkit::getStringTable()->getString("[%0 units too far from fruit]")).arg(n);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+10, ypos, globalContainer->littleFont, s.c_str());
			ypos += YOFFSET_RESSOURCE_LINE;
		}
	}
}

void GameGUI::drawBuildingActionButtons(Building* selBuild, BuildingType* buildingType, unsigned unitInsideBarYDec)
{
	if (!((selBuild->owner->allies) & (1<<localTeamNo)))
		return;
	if (selBuild->owner != localTeam)
		return;

	const int btnX = globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET;
	const int primaryY = globalContainer->gfx->getH()-BOTTOM_BUTTON_PRIMARY_YOFFSET;
	const int secondaryY = globalContainer->gfx->getH()-BOTTOM_BUTTON_SECONDARY_YOFFSET;

	if (selBuild->constructionResultState==Building::REPAIR)
	{
		if (buildingType->isBuildingSite)
			assert(buildingType->nextLevel!=-1);
		drawBlueButton(btnX, primaryY, "[cancel repair]");
	}
	else if (selBuild->constructionResultState==Building::UPGRADE)
	{
		assert(buildingType->nextLevel!=-1);
		if (buildingType->isBuildingSite)
			assert(buildingType->prevLevel!=-1);
		drawBlueButton(btnX, primaryY, "[cancel upgrade]");
	}
	else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE) && !buildingType->isBuildingSite)
	{
		if (selBuild->hp<buildingType->hpMax)
		{
			// repair
			if (selBuild->type->regenerationSpeed==0 && selBuild->isHardSpaceForBuildingSite(Building::REPAIR) && localTeam->maxBuildLevel()>=buildingType->level)
			{
				drawBlueButton(btnX, primaryY, "[repair]");
				if ( mouseX>btnX+12 && mouseX<globalContainer->gfx->getW()-12
					&& mouseY>primaryY && mouseY<primaryY+BOTTOM_BUTTON_HEIGHT )
					{
						globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 255));
						int ressources[BASIC_COUNT];
						selBuild->getRessourceCountToRepair(ressources);
						drawCosts(ressources, globalContainer->littleFont);
						globalContainer->littleFont->popStyle();
					}
			}
		}
		else if (buildingType->nextLevel!=-1)
		{
			// upgrade
			if (selBuild->isHardSpaceForBuildingSite(Building::UPGRADE) && (localTeam->maxBuildLevel()>buildingType->level))
			{
				drawBlueButton(btnX, primaryY, "[upgrade]");
				if ( mouseX>btnX+12 && mouseX<globalContainer->gfx->getW()-12
					&& mouseY>primaryY && mouseY<primaryY+BOTTOM_BUTTON_HEIGHT )
					{
						globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 255));
						drawBuildingUpgradePreview(selBuild, buildingType, unitInsideBarYDec);
						globalContainer->littleFont->popStyle();
					}
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

void GameGUI::drawBuildingTimeToLeaveBar(Building* selBuild, BuildingType* buildingType, int& ypos, unsigned& unitInsideBarYDec)
{
	if (!((selBuild->owner->allies) & (1<<localTeamNo)))
		return;

	// we select food buildings, heal buildings, and upgrade buildings:
	int maxTimeTo=0;
	if (buildingType->timeToFeedUnit)
		maxTimeTo=buildingType->timeToFeedUnit;
	else if (buildingType->timeToHealUnit)
		maxTimeTo=buildingType->timeToHealUnit;
	else
		for (int i=0; i<NB_ABILITY; i++)
			if (buildingType->upgradeTime[i])
				maxTimeTo=std::max(maxTimeTo, buildingType->upgradeTime[i]);
	int dec = (RIGHT_MENU_RIGHT_OFFSET-128);
	if (maxTimeTo)
	{
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, 128, 7, 168, 150, 90);
		for (std::list<Unit *>::iterator it=selBuild->unitsInside.begin(); it!=selBuild->unitsInside.end(); ++it)
		{
			Unit *u=*it;
			assert(u);
			if (u->displacement==Unit::DIS_INSIDE)
			{
				int dividend=-u->insideTimeout*128+128-u->delta/2;
				int divisor=1+maxTimeTo;
				int left=dividend/divisor;
				int alpha=((dividend%divisor)*255)/divisor;

				if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
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

void GameGUI::drawBuildingFlagControls(Building* selBuild, BuildingType* buildingType, int& ypos)
{
	if (!((selBuild->owner->allies) & (1<<localTeamNo)))
		return;

	// cleared ressources for clearing flags:
	if (buildingType->type == "clearingflag")
	{
		ypos += YOFFSET_B_SEP;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
			Toolkit::getStringTable()->getString("[Clearing:]"));
		ypos += YOFFSET_TEXT_PARA;
		for (int i=0; i<BASIC_COUNT; i++)
			if (i!=STONE)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+28, ypos, globalContainer->littleFont,
					getRessourceName(i));
				int spriteId;
				if (globalContainer->replaying?selBuild->clearingRessources[i]:selBuild->clearingRessourcesLocal[i])
					spriteId=20;
				else
					spriteId=19;
				globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+10, ypos+2, globalContainer->gamegui, spriteId);

				ypos+=YOFFSET_TEXT_PARA;
			}
	}
	// min war level for war flags:
	else if (buildingType->type == "warflag")
	{
		ypos += YOFFSET_B_SEP;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
			Toolkit::getStringTable()->getString("[Min required level:]"));
		ypos += YOFFSET_TEXT_PARA;
		for (int i=0; i<4; i++)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+28, ypos, globalContainer->littleFont, 1+i);
			int spriteId;
			if (i==(globalContainer->replaying?selBuild->minLevelToFlag:selBuild->minLevelToFlagLocal))
				spriteId=20;
			else
				spriteId=19;
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+10, ypos+2, globalContainer->gamegui, spriteId);

			ypos+=YOFFSET_TEXT_PARA;
		}
	}
	else if (buildingType->type == "explorationflag")
	{
		int spriteId;

		ypos += YOFFSET_B_SEP;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont,
			Toolkit::getStringTable()->getString("[Min required level:]"));
		ypos += YOFFSET_TEXT_PARA;

		// we use minLevelToFlag as an int which says what magic effect at minimum an explorer
		// must be able to do to be accepted at this flag
		// 0 == any explorer
		// 1 == must be able to attack ground
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+28, ypos, globalContainer->littleFont,Toolkit::getStringTable()->getString("[any explorer]"));
		if ((globalContainer->replaying?selBuild->minLevelToFlag:selBuild->minLevelToFlagLocal) == 0)
			spriteId = 20;
		else
			spriteId = 19;
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+10, ypos+2, globalContainer->gamegui, spriteId);

		ypos += YOFFSET_TEXT_PARA;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+28, ypos, globalContainer->littleFont,Toolkit::getStringTable()->getString("[ground attack]"));
		if ((globalContainer->replaying?selBuild->minLevelToFlag:selBuild->minLevelToFlagLocal) == 1)
			spriteId = 20;
		else
			spriteId = 19;
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+10, ypos+2, globalContainer->gamegui, spriteId);
		ypos += YOFFSET_TEXT_PARA;
	}
}

void GameGUI::drawBuildingUpgradePreview(Building* selBuild, BuildingType* buildingType, unsigned unitInsideBarYDec)
{
	// We draw the ressources cost.
	int typeNum=buildingType->nextLevel;
	BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
	drawCosts(bt->maxRessource, globalContainer->littleFont);

	// We draw the new abilities:
	int blueYpos = YPOS_BASE_BUILDING + YOFFSET_NAME;

	bt=globalContainer->buildingsTypes.get(bt->nextLevel);

	if (bt->hpMax)
		drawValueAlignedRight(blueYpos+YOFFSET_TEXT_LINE, bt->hpMax);
	if (bt->maxUnitInside)
		drawValueAlignedRight(blueYpos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, bt->maxUnitInside);
	blueYpos += YOFFSET_ICON+YOFFSET_B_SEP;

	if (buildingType->maxUnitWorking)
		blueYpos += YOFFSET_BAR+YOFFSET_B_SEP;

	if (bt->armor)
	{
		if (!buildingType->armor)
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, blueYpos-1, globalContainer->littleFont, Toolkit::getStringTable()->getString("[armor]"));
		drawValueAlignedRight(blueYpos-1, bt->armor);
		blueYpos+=YOFFSET_TEXT_LINE;
	}
	if (buildingType->maxUnitInside)
		blueYpos += YOFFSET_INFOS;
	if (bt->shootDamage)
	{
		drawValueAlignedRight(blueYpos+1, bt->shootDamage);
		drawValueAlignedRight(blueYpos+12, bt->shootingRange);
		blueYpos += YOFFSET_TOWER;
	}
	blueYpos += unitInsideBarYDec;
	blueYpos += YOFFSET_B_SEP;

	unsigned j = 0;
	for (unsigned i=0; i<globalContainer->ressourcesTypes.size(); i++)
	{
		if (buildingType->maxRessource[i])
		{
			drawValueAlignedRight(blueYpos+(j*11), bt->maxRessource[i]);
			j++;
		}
	}

	if (bt->maxBullets)
	{
		drawValueAlignedRight(blueYpos+(j*11), bt->maxBullets);
		j++;
	}
}
