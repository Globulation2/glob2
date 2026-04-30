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
#include <GraphicContext.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"

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
