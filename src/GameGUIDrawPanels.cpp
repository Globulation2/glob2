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

#include <FormatableString.h>
#include <GUIStyle.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Player.h"
#include "ReplayReader.h"
#include "Unit.h"

void GameGUI::drawChoice(int pos, std::vector<std::string> &types, std::vector<bool> &states, unsigned numberPerLine)
{
	assert(numberPerLine >= 2);
	assert(numberPerLine <= 3);
	int sel=-1;
	int width = (RIGHT_MENU_WIDTH/numberPerLine);
	size_t i;

	for (i=0; i<types.size(); i++)
	{
		std::string &type = types[i];

		if ((selectionMode==TOOL_SELECTION) && (type == toolManager.getBuildingName()))
			sel = i;

		if (states[i])
		{
			BuildingType *bt = globalContainer->buildingsTypes.getByType(type.c_str(), 0, false);
			assert(bt);
			int imgid = bt->miniSpriteImage;
			int x, y;

			x=((i % numberPerLine)*width)+globalContainer->gfx->getW()-RIGHT_MENU_WIDTH;
			y=((i / numberPerLine)*46)+YPOS_BASE_BUILDING;
			globalContainer->gfx->setClipRect(x, y, 64, 46);

			Sprite *buildingSprite;
			if (imgid >= 0)
			{
				buildingSprite = bt->miniSpritePtr;
			}
			else
			{
				buildingSprite = bt->gameSpritePtr;
				imgid = bt->gameSpriteImage;
			}

			int decX = (width-buildingSprite->getW(imgid))>>1;
			int decY = (46-buildingSprite->getW(imgid))>>1;

			buildingSprite->setBaseColor(localTeam->color);
			globalContainer->gfx->drawSprite(x+decX, y+decY, buildingSprite, imgid);
			globalContainer->gfx->finishDrawingSprite(buildingSprite, 255);

			globalContainer->gfx->setClipRect();
			if(hilights.find(HilightBuildingOnPanel+IntBuildingType::shortNumberFromType(type)) != hilights.end())
			{
				arrowPositions.push_back(HilightArrowPosition(x+decX-36, y-6+decX, 38));
			}
		}
	}
	int count = i;

	globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 128, RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-128);

	// draw building selection if needed
	if (selectionMode == TOOL_SELECTION)
	{
		int sw;
		if (numberPerLine == 2)
			sw = globalContainer->gamegui->getW(8);
		else
			sw = globalContainer->gamegui->getW(23);


		assert(sel>=0);
		int x=((sel  % numberPerLine)*width)+globalContainer->gfx->getW()-RIGHT_MENU_WIDTH;
		int y=((sel / numberPerLine)*46)+YPOS_BASE_BUILDING;

		int decX = (width - sw) / 2;

		if (numberPerLine == 2)
			globalContainer->gfx->drawSprite(x+decX, y+1, globalContainer->gamegui, 8);
		else
			globalContainer->gfx->drawSprite(x+decX, y+4, globalContainer->gamegui, 23);
	}

	int toDrawInfoFor = -1;
	if (mouseX>globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)
	{
		if (mouseY>pos)
		{
			int xNum=(mouseX-globalContainer->gfx->getW()+RIGHT_MENU_WIDTH) / width;
			int yNum=(mouseY-pos)/46;
			int id=yNum*numberPerLine+xNum;
			if (id<count)
			{
				toDrawInfoFor = id;
			}
		}
	}
	if(toDrawInfoFor == -1)
	{
		if(toolManager.getBuildingName() != "")
		{
			std::vector<std::string>::iterator i = std::find(types.begin(), types.end(), toolManager.getBuildingName());
			if(i != types.end())
			{
				toDrawInfoFor = i - types.begin();
			}
		}
	}

	// draw infos
	if (toDrawInfoFor != -1)
	{
		int id = toDrawInfoFor;

		std::string &type = types[id];
		if (states[id])
		{
			int buildingInfoStart=globalContainer->gfx->getH()-50;

			std::string key = "[" + type + "]";
			drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, key.c_str());

			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 128, 128, 128));
			key = "[" + type + " explanation]";
			drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-20, key.c_str());
			key = "[" + type + " explanation 2]";
			drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-8, key.c_str());
			globalContainer->littleFont->popStyle();
			BuildingType *bt = globalContainer->buildingsTypes.getByType(type, 0, true);
			if (bt)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+4+(RIGHT_MENU_WIDTH-128)/2, buildingInfoStart+6, globalContainer->littleFont,
					FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Wood]")).arg(bt->maxRessource[0]).c_str());
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+4+(RIGHT_MENU_WIDTH-128)/2, buildingInfoStart+17, globalContainer->littleFont,
					FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Stone]")).arg(bt->maxRessource[3]).c_str());

				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+4+64+(RIGHT_MENU_WIDTH-128)/2, buildingInfoStart+6, globalContainer->littleFont,
					FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Alga]")).arg(bt->maxRessource[4]).c_str());
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+4+64+(RIGHT_MENU_WIDTH-128)/2, buildingInfoStart+17, globalContainer->littleFont,
					FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Corn]")).arg(bt->maxRessource[1]).c_str());

				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+4+(RIGHT_MENU_WIDTH-128)/2, buildingInfoStart+28, globalContainer->littleFont,
					FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Papyrus]")).arg(bt->maxRessource[2]).c_str());
			}
		}
	}
}


void GameGUI::drawUnitInfos(void)
{
	Unit* selUnit=selection.unit;
	assert(selUnit);
	int ypos = YPOS_BASE_UNIT;
	Uint8 r, g, b;

	// draw "unit" of "player"
	std::string title;
	title += getUnitName(selUnit->typeNum);
	title += " (";

	std::string textT=selUnit->owner->getFirstPlayerName();
	if (textT.empty())
		textT=Toolkit::getStringTable()->getString("[Uncontrolled]");
	title += textT;
	title += ")";

	if (localTeam->teamNumber == selUnit->owner->teamNumber)
		{ r=160; g=160; b=255; }
	else if (localTeam->allies & selUnit->owner->me)
		{ r=255; g=210; b=20; }
	else
		{ r=255; g=50; b=50; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	int titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	int titlePos = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+((RIGHT_MENU_WIDTH-titleLen)>>1);
	globalContainer->gfx->drawString(titlePos, ypos+5, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();

	ypos += YOFFSET_NAME;

	// draw unit's image
	Unit* unit=selUnit;
	int imgid;
	UnitType *ut=unit->race->getUnitType(unit->typeNum, 0);
	assert(unit->action>=0);
	assert(unit->action<NB_MOVE);
	imgid=ut->startImage[unit->action];

	int dir=unit->direction;
	int delta=unit->delta;
	assert(dir>=0);
	assert(dir<9);
	assert(delta>=0);
	assert(delta<256);
	if (dir==8)
	{
		imgid+=8*(delta>>5);
	}
	else
	{
		imgid+=8*dir;
		imgid+=(delta>>5);
	}

	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(unit->owner->color);
	int decX = (32-unitSprite->getW(imgid))>>1;
	int decY = (32-unitSprite->getH(imgid))>>1;
	int ddx = (RIGHT_MENU_HALF_WIDTH - 56) / 2 + 2;
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx+12+decX, ypos+7+4+decY, unitSprite, imgid);

	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx, ypos+4, globalContainer->gamegui, 18);

	// draw HP
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos, globalContainer->littleFont, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[hp]")).c_str());

	if (selUnit->hp<=selUnit->trigHP)
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0/%1").arg(selUnit->hp).arg(selUnit->performance[HP]).c_str());
	globalContainer->littleFont->popStyle();

	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE+YOFFSET_TEXT_PARA, globalContainer->littleFont, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[food]")).c_str());

	// draw food
	if (selUnit->isUnitHungry())
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+2*YOFFSET_TEXT_LINE+YOFFSET_TEXT_PARA, globalContainer->littleFont, FormatableString("%0 % (%1)").arg(((float)selUnit->hungry*100.0f)/(float)Unit::HUNGRY_MAX, 0, 0).arg(selUnit->fruitCount).c_str());
	globalContainer->littleFont->popStyle();

	ypos += YOFFSET_ICON+10;

	int rdec = (RIGHT_MENU_WIDTH-128)/2;

	if (selUnit->performance[HARVEST])
	{
		if (selUnit->carriedRessource>=0)
		{
			const RessourceType* r = globalContainer->ressourcesTypes.get(selUnit->carriedRessource);
			unsigned resImg = r->gfxId + r->sizesCount - 1;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+8, globalContainer->littleFont, Toolkit::getStringTable()->getString("[carry]"));
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-32-8-rdec, ypos, globalContainer->ressources, resImg);
		}
		else
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+8, globalContainer->littleFont, Toolkit::getStringTable()->getString("[don't carry anything]"));
		}
	}
	ypos += YOFFSET_CARYING+10;

	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[current speed]")).arg(selUnit->speed).c_str());
	ypos += YOFFSET_TEXT_PARA+10;

	if (selUnit->performance[ARMOR])
	{
		int armorReductionPerHappyness = selUnit->race->getUnitType(selUnit->typeNum, selUnit->level[ARMOR])->armorReductionPerHappyness;
		int realArmor = selUnit->performance[ARMOR] - selUnit->fruitCount * armorReductionPerHappyness;
		if (realArmor < 0)
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 0, 0));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1 = %2 - %3 * %4").arg(Toolkit::getStringTable()->getString("[armor]")).arg(realArmor).arg(selUnit->performance[ARMOR]).arg(selUnit->fruitCount).arg(armorReductionPerHappyness).c_str());
		if (realArmor < 0)
			globalContainer->littleFont->popStyle();
	}
	ypos += YOFFSET_TEXT_PARA;

	if (selUnit->typeNum!=EXPLORER)
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[levels]")).c_str());
	ypos += YOFFSET_TEXT_PARA;

	if (selUnit->performance[WALK])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[Walk]")).arg((1+selUnit->level[WALK])).arg(selUnit->performance[WALK]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[SWIM])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[Swim]")).arg(selUnit->level[SWIM]).arg(selUnit->performance[SWIM]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[BUILD])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[Build]")).arg(1+selUnit->level[BUILD]).arg(selUnit->performance[BUILD]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[HARVEST])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[Harvest]")).arg(1+selUnit->level[HARVEST]).arg(selUnit->performance[HARVEST]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[ATTACK_SPEED])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[At. speed]")).arg(1+selUnit->level[ATTACK_SPEED]).arg(selUnit->performance[ATTACK_SPEED]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[ATTACK_STRENGTH])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[At. strength]")).arg(1+selUnit->level[ATTACK_STRENGTH]).arg(selUnit->experienceLevel).arg(selUnit->performance[ATTACK_STRENGTH]).arg(selUnit->experienceLevel).c_str());

		ypos += YOFFSET_TEXT_PARA + 2;
	}

	if (selUnit->performance[MAGIC_ATTACK_AIR])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[Magic At. Air]")).arg(1+selUnit->level[MAGIC_ATTACK_AIR]).arg(selUnit->experienceLevel).arg(selUnit->performance[MAGIC_ATTACK_AIR]).arg(selUnit->experienceLevel).c_str());

		ypos += YOFFSET_TEXT_PARA + 2;
	}

	if (selUnit->performance[MAGIC_ATTACK_GROUND])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[Magic At. Ground]")).arg(1+selUnit->level[MAGIC_ATTACK_GROUND]).arg(selUnit->experienceLevel).arg(selUnit->performance[MAGIC_ATTACK_GROUND]).arg(selUnit->experienceLevel).c_str());

		ypos += YOFFSET_TEXT_PARA + 2;
	}

	if (selUnit->performance[ATTACK_STRENGTH] || selUnit->performance[MAGIC_ATTACK_AIR] || selUnit->performance[MAGIC_ATTACK_GROUND])
		drawXPProgressBar(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, selUnit->experience, selUnit->getNextLevelThreshold());
}

void GameGUI::drawBuildingInfos(void)
{
	Building* selBuild = selection.building;
	assert(selBuild);
	BuildingType *buildingType = selBuild->type;
	int ypos = YPOS_BASE_BUILDING;
	Uint8 r, g, b;
	unsigned unitInsideBarYDec = 0;

	// draw "building" of "player"
	std::string title;
	std::string key ="[" + buildingType->type + "]";
	title += Toolkit::getStringTable()->getString(key.c_str());
	{
		title += " (";
		std::string textT=selBuild->owner->getFirstPlayerName();
		if (textT.empty())
			textT=Toolkit::getStringTable()->getString("[Uncontrolled]");
		title += textT;
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


	// building icon
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

	// draw HP
	if (buildingType->hpMax)
	{
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

	// inside
	if (buildingType->maxUnitInside && ((selBuild->owner->allies)&(1<<localTeamNo)))
	{
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

	// it is a unit ranged attractor (aka flag)
	if (buildingType->defaultUnitStayRange && ((selBuild->owner->allies)&(1<<localTeamNo)))
	{
		// get flag stat
		int goingTo, onSpot;
		selBuild->computeFlagStatLocal(&goingTo, &onSpot);
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

	ypos += YOFFSET_ICON+YOFFSET_B_SEP;

	// working bar
	if (buildingType->maxUnitWorking)
	{
		if ((selBuild->owner->allies)&(1<<localTeamNo))
		{
			if (selBuild->buildingState==Building::ALIVE)
			{
				// If we're replaying, display the actual number, not the locally cached one (changable by the gui user)
				const int maxUnitsWorking = (globalContainer->replaying?selBuild->maxUnitWorking:selBuild->maxUnitWorkingLocal);

				std::string working = Toolkit::getStringTable()->getString("[working]");
				const int len = globalContainer->littleFont->getStringWidth(working)+4;
				globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, working);
				globalContainer->littleFont->popStyle();
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4+len, ypos, globalContainer->littleFont, FormatableString("%0/%1").arg((int)selBuild->unitsWorking.size()).arg(maxUnitsWorking).c_str());
				drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos+YOFFSET_TEXT_BAR, maxUnitsWorking, maxUnitsWorking, selBuild->unitsWorking.size(), MAX_UNIT_WORKING);
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

	// priority buttons
	if(buildingType->maxUnitWorking)
	{
		if((selBuild->owner->allies)&(1<<localTeamNo))
		{
			if(selBuild->buildingState==Building::ALIVE)
			{
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
		}
	}

	// flag range bar
	if (buildingType->defaultUnitStayRange)
	{
		if ((selBuild->owner->allies)&(1<<localTeamNo))
		{
			// If we're replaying, display the actual number, not the locally cached one (changable by the gui user)
			const int unitStayRange = (globalContainer->replaying?selBuild->unitStayRange:selBuild->unitStayRangeLocal);

			std::string range = Toolkit::getStringTable()->getString("[range]");
			const int len = globalContainer->littleFont->getStringWidth(range)+4;
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, range);
			globalContainer->littleFont->popStyle();
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4+len, ypos, globalContainer->littleFont, FormatableString("%0").arg(selBuild->unitStayRange).c_str());
			drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos+YOFFSET_TEXT_BAR, selBuild->unitStayRange, unitStayRange, 0, selBuild->type->maxUnitStayRange);
		}
		ypos += YOFFSET_BAR+YOFFSET_B_SEP;
	}

	// flag control of team and allies
	if ((selBuild->owner->allies) & (1<<localTeamNo))
	{
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

	globalContainer->gfx->finishDrawingSprite(globalContainer->gamegui, 255);

	// other infos
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

	// There is unit inside, show time to leave
	if ((selBuild->owner->allies) & (1<<localTeamNo))
	{
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

	ypos += YOFFSET_B_SEP;

	// exchange building
	if (buildingType->canExchange && ((selBuild->owner->sharedVisionExchange)&(1<<localTeamNo)))
	{
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

	if ((selBuild->owner->allies) & (1<<localTeamNo))
	{
		// ressorces for every building except exchange building
		if (!buildingType->canExchange)
		{

			// ressources in
			for (unsigned i=0; i<globalContainer->ressourcesTypes.size(); i++)
			{
				if (buildingType->maxRessource[i])
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1/%2").arg(getRessourceName(i)).arg(selBuild->ressources[i]).arg(buildingType->maxRessource[i]).c_str());
					ypos += 11;
				}
			}
			if (buildingType->maxBullets)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1/%2").arg(Toolkit::getStringTable()->getString("[Bullets]")).arg(selBuild->bullets).arg(buildingType->maxBullets).c_str());
				ypos += 11;
			}
			ypos+=5;
		}
		//Unit production ratios and unit production
		if (buildingType->unitProductionTime) // swarm
		{
			int left=(selBuild->productionTimeout*128)/buildingType->unitProductionTime;
			int elapsed=128-left;
			globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, elapsed, 7, 100, 100, 255);
			globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+elapsed, ypos, left, 7, 128, 128, 128);

			ypos+=15;
			for (int i=0; i<NB_UNIT_TYPE; i++)
			{
				// If we're replaying, display the actual number, not the locally cached one (changable by the gui user)
				const int ratio = (globalContainer->replaying?selBuild->ratio[i]:selBuild->ratioLocal[i]);

				drawScrollBox(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, selBuild->ratio[i], ratio, 0, MAX_RATIO_RANGE);
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+24, ypos, globalContainer->littleFont, getUnitName(i));

				if(i==1 && hilights.find(HilightRatioBar) != hilights.end())
				{
					arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET-36, ypos-8, 38));
				}

				ypos+=20;
			}

		}

		// data on whether or not the building is recieving units
		bool otherFailure=true;
		for(unsigned j=0; j<Building::UnitCantWorkReasonSize; ++j)
		{
			int n = selBuild->unitsFailingRequirements[j];
			if(j!=0 && n>0)
				otherFailure=true;
		}
		if(otherFailure)
		{
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
					ypos+=11;
				}
			}
		}

		// repair and upgrade
		if(selBuild->owner == localTeam)
		{
			if (selBuild->constructionResultState==Building::REPAIR)
			{
				if (buildingType->isBuildingSite)
					assert(buildingType->nextLevel!=-1);
				drawBlueButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-48, "[cancel repair]");
			}
			else if (selBuild->constructionResultState==Building::UPGRADE)
			{
				assert(buildingType->nextLevel!=-1);
				if (buildingType->isBuildingSite)
					assert(buildingType->prevLevel!=-1);
				drawBlueButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-48, "[cancel upgrade]");
			}
			else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE) && !buildingType->isBuildingSite)
			{
				if (selBuild->hp<buildingType->hpMax)
				{
					// repair
					if (selBuild->type->regenerationSpeed==0 && selBuild->isHardSpaceForBuildingSite(Building::REPAIR) && localTeam->maxBuildLevel()>=buildingType->level)
					{
						drawBlueButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-48, "[repair]");
						if ( mouseX>globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+12 && mouseX<globalContainer->gfx->getW()-12
							&& mouseY>globalContainer->gfx->getH()-48 && mouseY<globalContainer->gfx->getH()-48+16 )
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
						drawBlueButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-48, "[upgrade]");
						if ( mouseX>globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+12 && mouseX<globalContainer->gfx->getW()-12
							&& mouseY>globalContainer->gfx->getH()-48 && mouseY<globalContainer->gfx->getH()-48+16 )
							{
								globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 255));

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

								globalContainer->littleFont->popStyle();
							}
					}
				}
			}

			// building destruction
			if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
			{
				drawRedButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-24, "[cancel destroy]");
			}
			else if (selBuild->buildingState==Building::ALIVE)
			{
				drawRedButton(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, globalContainer->gfx->getH()-24, "[destroy]");
			}
		}
	}
}

void GameGUI::drawRessourceInfos(void)
{
	const Ressource &r = game.map.getRessource(selection.ressource);
	int ypos = YPOS_BASE_RESSOURCE;
	if (r.type!=NO_RES_TYPE)
	{
		// Draw ressource name
		const std::string &ressourceName = getRessourceName(r.type);
		int titleLen = globalContainer->littleFont->getStringWidth(ressourceName.c_str());
		int titlePos = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+((RIGHT_MENU_WIDTH-titleLen)>>1);
		globalContainer->gfx->drawString(titlePos, ypos+(YOFFSET_TEXT_PARA>>1), globalContainer->littleFont, ressourceName.c_str());
		ypos += 2*YOFFSET_TEXT_PARA;

		// Draw ressource image
		const RessourceType* rt = globalContainer->ressourcesTypes.get(r.type);
		unsigned resImg = rt->gfxId + r.variety*rt->sizesCount + r.amount;
		if (!rt->eternal)
			resImg--;
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+16, ypos, globalContainer->ressources, resImg);

		// Draw ressource count
		if (rt->granular)
		{
			int sizesCount=rt->sizesCount;
			int amount=r.amount;
			const std::string amountS = FormatableString("%0/%1").arg(amount).arg(sizesCount);
			int amountSH = globalContainer->littleFont->getStringHeight(amountS.c_str());
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-64, ypos+((32-amountSH)>>1), globalContainer->littleFont, amountS.c_str());
		}
	}
	else
	{
		clearSelection();
	}
}

void GameGUI::drawReplayPanel(void)
{
	Font *font=globalContainer->littleFont;

	int x = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + REPLAY_PANEL_XOFFSET;
	int y = REPLAY_PANEL_YOFFSET;
	int inc = REPLAY_PANEL_SPACE_BETWEEN_OPTIONS;

	globalContainer->gfx->drawString(x, y, font, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[Options]")));

	drawCheckButton(x, y + 1*inc, Toolkit::getStringTable()->getString("[fog of war]"), globalContainer->replayShowFog);
	drawCheckButton(x, y + 2*inc, Toolkit::getStringTable()->getString("[combined vision]"), (globalContainer->replayVisibleTeams == 0xFFFFFFFF));
	drawCheckButton(x, y + 3*inc, Toolkit::getStringTable()->getString("[show areas]"), (globalContainer->replayShowAreas));
	drawCheckButton(x, y + 4*inc, Toolkit::getStringTable()->getString("[show flags]"), (globalContainer->replayShowFlags));

	globalContainer->gfx->drawString(x, y + REPLAY_PANEL_PLAYERLIST_YOFFSET, font, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[players]")));

	for (int i = 0; i < game.teamsCount(); i++)
	{
		// I know this is a matter of taste, but I prefer checkboxes here. Radio buttons are a totally different style
		//drawRadioButton(x, y + REPLAY_PANEL_PLAYERLIST_YOFFSET + (i+1)*inc, game.teams[i]->getFirstPlayerName().c_str(), localTeamNo == i);
		drawRadioButton(x + 1, y + REPLAY_PANEL_PLAYERLIST_YOFFSET + (i+1)*inc + 1, localTeamNo == i);
		globalContainer->gfx->drawString(x + 20, y + REPLAY_PANEL_PLAYERLIST_YOFFSET + (i+1)*inc, font, game.teams[i]->getFirstPlayerName().c_str());
	}
}

void GameGUI::drawReplayProgressBar(bool drawBackground)
{
	assert(globalContainer->replaying);
	assert(globalContainer->replayReader);
	assert(globalContainer->replayReader->isValid());

	// set the clipping rectangle
	globalContainer->gfx->setClipRect( 0, REPLAY_BAR_Y - 4, REPLAY_BAR_WIDTH, REPLAY_BAR_HEIGHT + 4);

	// draw menu background, black if low speed graphics, transparent otherwise
	if (drawBackground)
	{
		if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
			globalContainer->gfx->drawFilledRect( 0, REPLAY_BAR_Y, REPLAY_BAR_WIDTH, REPLAY_BAR_HEIGHT, 0, 0, 0);
		else
			globalContainer->gfx->drawFilledRect( 0, REPLAY_BAR_Y, REPLAY_BAR_WIDTH, REPLAY_BAR_HEIGHT, 0, 0, 40, 180);
	}

	// Progress bar y
	int y = REPLAY_BAR_Y + REPLAY_PROGRESS_BAR_Y_OFFSET;

	// Draw the actual progress bar
	Style::style->drawProgressBar(globalContainer->gfx,
		REPLAY_PROGRESS_BAR_X_OFFSET + REPLAY_PROGRESS_BAR_CAP_WIDTH - 1, y,
		REPLAY_BAR_WIDTH - 2*REPLAY_PROGRESS_BAR_X_OFFSET - REPLAY_PROGRESS_BAR_NUM_BUTTONS * REPLAY_PROGRESS_BAR_BUTTON_WIDTH - 2*REPLAY_PROGRESS_BAR_CAP_WIDTH + 2,
		globalContainer->replayReader->getCurrentStep(),
		globalContainer->replayReader->getNumStepsTotal());

	// Draw the round caps
	globalContainer->gfx->drawSprite(
		REPLAY_PROGRESS_BAR_X_OFFSET, y,
		globalContainer->gamegui,
		REPLAY_BAR_LEFT_CAP_SPRITE);
	globalContainer->gfx->drawSprite(
		REPLAY_BAR_WIDTH - REPLAY_PROGRESS_BAR_X_OFFSET - REPLAY_PROGRESS_BAR_CAP_WIDTH, y,
		globalContainer->gamegui,
		REPLAY_BAR_RIGHT_CAP_SPRITE);

	// Draw the buttons for play, pause and fast-forward
	int x = REPLAY_BAR_WIDTH - REPLAY_PROGRESS_BAR_X_OFFSET - REPLAY_PROGRESS_BAR_CAP_WIDTH;
	int inc = REPLAY_PROGRESS_BAR_BUTTON_WIDTH;

	globalContainer->gfx->drawSprite( x - inc*3, y, globalContainer->gamegui, (!gamePaused && !globalContainer->replayFastForward ? REPLAY_BAR_PLAY_BUTTON_ACTIVE_SPRITE : REPLAY_BAR_PLAY_BUTTON_SPRITE));
	globalContainer->gfx->drawSprite( x - inc*2, y, globalContainer->gamegui, (gamePaused ? REPLAY_BAR_PAUSE_BUTTON_ACTIVE_SPRITE : REPLAY_BAR_PAUSE_BUTTON_SPRITE));
	globalContainer->gfx->drawSprite( x - inc*1, y, globalContainer->gamegui, (!gamePaused && globalContainer->replayFastForward ? REPLAY_BAR_FAST_FORWARD_BUTTON_ACTIVE_SPRITE : REPLAY_BAR_FAST_FORWARD_BUTTON_SPRITE));

	// Calculate the time
	// This is based on default speed 25 fps, not the actual Engine's speed
	// because if we fast-forward we still want to see the old time
	unsigned int time1_sec = (globalContainer->replayReader->getCurrentStep()/25)%60;
	unsigned int time1_min = (globalContainer->replayReader->getCurrentStep()/(25*60))%60;
	unsigned int time1_hour = (globalContainer->replayReader->getCurrentStep()/(25*3600));

	unsigned int time2_sec = (globalContainer->replayReader->getNumStepsTotal()/25)%60;
	unsigned int time2_min = (globalContainer->replayReader->getNumStepsTotal()/(25*60))%60;
	unsigned int time2_hour = (globalContainer->replayReader->getNumStepsTotal()/(25*3600));

	// Draw the time
	if (time2_hour <= 99)
	{
		globalContainer->gfx->drawString(REPLAY_BAR_TIMER_X, y+3, globalContainer->littleFont,
			FormatableString("%0:%1:%2 / %3:%4:%5")
			.arg(time1_hour)
			.arg(time1_min,2,10,'0')
			.arg(time1_sec,2,10,'0')
			.arg(time2_hour)
			.arg(time2_min,2,10,'0')
			.arg(time2_sec,2,10,'0')
			.c_str());
	}
	else
	{
		// Time did not get saved properly, don't show it
		globalContainer->gfx->drawString(REPLAY_BAR_TIMER_X, y+3, globalContainer->littleFont,
			FormatableString("%0:%1:%2")
			.arg(time1_hour)
			.arg(time1_min,2,10,'0')
			.arg(time1_sec,2,10,'0')
			.c_str());
	}

	// Draw the filename of the replay
	std::string replayName = glob2FilenameToName(globalContainer->replayFileName);
	int stringWidth = globalContainer->littleFont->getStringWidth(replayName.c_str());
	int pos = (globalContainer->settings.screenWidth-RIGHT_MENU_WIDTH)/2 - stringWidth/2;
	globalContainer->gfx->drawString(pos, y+3, globalContainer->littleFont, replayName.c_str());

	// Draw the border
	if (drawBackground)
	{
		for (int i = 0; i < REPLAY_BAR_WIDTH; i += 32)
		{
			globalContainer->gfx->drawSprite(i, REPLAY_BAR_Y-4, globalContainer->gamegui, 16);
		}
	}
}

void GameGUI::drawFlagView(void)
{
	int dec = (RIGHT_MENU_WIDTH - 128)/2;
	// draw flags
	drawChoice(YPOS_BASE_FLAG, flagsChoiceName, flagsChoiceState, 3);

	// draw choice of area
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 13);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+48+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 14);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+88+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 25);
	if (brush.getType() != BrushTool::MODE_NONE)
	{
		int decX = 8 + ((int)toolManager.getZoneType()) * 40 + dec;
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 22);
	}
	globalContainer->gfx->finishDrawingSprite(globalContainer->gamegui, 255);
	if(hilights.find(HilightForbiddenZoneOnPanel) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36+8+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, 38));
	}
	if(hilights.find(HilightGuardZoneOnPanel) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36+48+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, 38));
	}
	if(hilights.find(HilightClearingZoneOnPanel) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36+88+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, 38));
	}

	// draw brush
	brush.draw(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH+40);

	if(hilights.find(HilightBrushSelector) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH+40+30, 38));
	}

	// draw brush help text
	if ((mouseX>globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+dec) && (mouseY>YPOS_BASE_FLAG+YOFFSET_BRUSH))
	{
		int buildingInfoStart = globalContainer->gfx->getH()-50;
		if (mouseY<YPOS_BASE_FLAG+YOFFSET_BRUSH+40)
		{
			int panelMouseX = mouseX - globalContainer->gfx->getW() + RIGHT_MENU_WIDTH;
			if (panelMouseX < 44)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[forbidden area]");
			else if (panelMouseX < 84)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[guard area]");
			else
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[clear area]");
		}
		else
		{
			if (toolManager.getZoneType() == GameGUIToolManager::Forbidden)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[forbidden area]");
			else if (toolManager.getZoneType() == GameGUIToolManager::Guard)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[guard area]");
			else if (toolManager.getZoneType() == GameGUIToolManager::Clearing)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[clear area]");
			else
				assert(false);
		}
	}
}
