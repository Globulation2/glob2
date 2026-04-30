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

#include <algorithm>

#include <GraphicContext.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Unit.h"

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
