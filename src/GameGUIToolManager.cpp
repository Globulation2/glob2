/*
Copyright (C) 2007 Bradley Arsenault

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

#include "GameGUIToolManager.h"
#include "GlobalContainer.h"
#include "GUIBase.h"
#include "FormatableString.h"


using namespace GAGGUI;
using namespace GAGCore;

GameGUIToolManager::GameGUIToolManager(Game& game, BrushTool& brush, GameGUIDefaultAssignManager& defaultAssign, GameGUIGhostBuildingManager& ghostManager)
	: game(game), brush(brush), defaultAssign(defaultAssign), ghostManager(ghostManager)
{
	hilightStrength = 0;
	mode = NoTool;
	zoneType = Forbidden;
	firstPlacementX=-1;
	firstPlacementY=-1;
}



void GameGUIToolManager::activateBuildingTool(const std::string& nbuilding)
{
	mode = PlaceBuilding;
	building = nbuilding;
}



void GameGUIToolManager::activateZoneTool(ZoneType type)
{
	mode = PlaceZone;
	zoneType = type;
}



void GameGUIToolManager::activateZoneTool()
{
	mode = PlaceZone;
}



void GameGUIToolManager::deactivateTool()
{
	if(mode == PlaceZone)
		brush.unselect();
}



void GameGUIToolManager::drawTool(int mouseX, int mouseY, int localteam, int viewportX, int viewportY)
{
	if(mode == PlaceBuilding)
	{
		// Get the type and sprite
		int typeNum = globalContainer->buildingsTypes.getTypeNum(building, 0, false);
		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		Sprite *sprite = bt->gameSpritePtr;
		
		// Translate the mouse position to a building position, and check if there is room
		// on the map
		int building_x, building_y;
		int mapX, mapY;
		bool isRoom;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &building_x, &building_y, viewportX, viewportY);
		if (bt->isVirtual)
			isRoom = game.checkRoomForBuilding(building_x, building_y, bt, &mapX, &mapY, localteam);
		else
			isRoom = game.checkHardRoomForBuilding(building_x, building_y, bt, &mapX, &mapY);
			
		// Increase/Decrease hilight strength, given whether there is room or not
		if (isRoom)
			hilightStrength = std::min(hilightStrength + 0.1f, 1.0f);
		else
			hilightStrength = std::max(hilightStrength - 0.1f, 0.0f);
			
		// we get the screen dimensions of the building
		int batW = (bt->width) * 32;
		int batH = sprite->getH(bt->gameSpriteImage);
		int batX = (((mapX-viewportX)&(game.map.wMask)) * 32);
		int batY = (((mapY-viewportY)&(game.map.hMask)) * 32)-(batH-(bt->height * 32));
		
		// Draw the building
		sprite->setBaseColor(game.teams[localteam]->color);
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH());
		int spriteIntensity = 127+static_cast<int>(128.0f*splineInterpolation(1.f, 0.f, 1.f, hilightStrength));
		globalContainer->gfx->drawSprite(batX, batY, sprite, bt->gameSpriteImage, spriteIntensity);

		if (!bt->isVirtual)
		{
			// Count down whether a building site can be placed
			if (game.teams[localteam]->noMoreBuildingSitesCountdown>0)
			{
				globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(batX, batY, batX+batW-1, batY+batH-1, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(batX+batW-1, batY, batX, batY+batH-1, 255, 0, 0, 127);
				
				globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 0, 0, 127));
				globalContainer->gfx->drawString(batX, batY-12, globalContainer->littleFont, FormatableString("%0.%1").arg(game.teams[localteam]->noMoreBuildingSitesCountdown/40).arg((game.teams[localteam]->noMoreBuildingSitesCountdown%40)/4).c_str());
				globalContainer->littleFont->popStyle();
			}
			else
			{
				// Draw the square arround the building, denoting its size when upgraded
				if (isRoom)
					globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 255, 255, 127);
				else
					globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 0, 0, 127);
				
				// We look for its maximum extension size
				// we find last's level type num:
				BuildingType *lastbt=globalContainer->buildingsTypes.get(typeNum);
				int lastTypeNum=typeNum;
				int max=0;
				while (lastbt->nextLevel>=0)
				{
					lastTypeNum=lastbt->nextLevel;
					lastbt=globalContainer->buildingsTypes.get(lastTypeNum);
					if (max++>200)
					{
						printf("GameGUI: Error: nextLevel architecture is broken.\n");
						assert(false);
						break;
					}
				}
					
				int exMapX, exMapY; // ex prefix means EXtended building; the last level building type.
				bool isExtendedRoom = game.checkHardRoomForBuilding(building_x, building_y, lastbt, &exMapX, &exMapY);
				int exBatX=((exMapX-viewportX)&(game.map.wMask)) * 32;
				int exBatY=((exMapY-viewportY)&(game.map.hMask)) * 32;
				int exBatW=(lastbt->width) * 32;
				int exBatH=(lastbt->height) * 32;

				if (isRoom && isExtendedRoom)
					globalContainer->gfx->drawRect(exBatX-1, exBatY-1, exBatW+2, exBatH+2, 255, 255, 255, 127);
				else
					globalContainer->gfx->drawRect(exBatX-1, exBatY-1, exBatW+2, exBatH+2, 255, 0, 0, 127);
			}
		}
	}
	else if(mode == PlaceZone)
	{
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH());
		/* Instead of using a dimmer intensity to indicate
			removing of areas, this should rather use dashed
			lines.  (The intensities used below are 2/3 as
			bright for the case of removing areas.) */
		/* This reasoning should be abstracted out and reused
			in MapEdit.cpp to choose a color for those cases
			where areas are being drawn. */
		unsigned mode = brush.getType();
		Color c = Color(0,0,0);
		/* The following colors have been chosen to match the
			colors in the .png files for the animations of
			areas as of 2007-04-29.  If those .png files are
			updated with different colors, then the following
			code should change accordingly. */
		if (zoneType == Forbidden)
		{
			if (mode == BrushTool::MODE_ADD)
			{
				c = Color(255,0,0);
			}
			else
			{
				c = Color(170,0,0);
			}
		}
		else if (zoneType == Guard)
		{
			if (mode == BrushTool::MODE_ADD)
			{
				c = Color(27,0,255);
			}
			else
			{
				c = Color(18,0,170);
			}
		}
		else if (zoneType == Clearing)
		{
			if (mode == BrushTool::MODE_ADD)
			{
			/* some of the clearing area images use (252,207,0) instead */
				c = Color(251,206,0);
			}
			else
			{
				c = Color(167,137,0);
			}
		}
		brush.drawBrush(mouseX, mouseY, c, viewportX, viewportY, firstPlacementX, firstPlacementY);
	}
}



std::string GameGUIToolManager::getBuildingName() const
{
	return building;
}



GameGUIToolManager::ZoneType GameGUIToolManager::getZoneType() const
{
	return zoneType;
}



void GameGUIToolManager::handleMouseDown(int mouseX, int mouseY, int localteam, int viewportX, int viewportY)
{
	if(mode == PlaceBuilding)
	{
		// Count down whether a building site can be placed
		if (game.teams[localteam]->noMoreBuildingSitesCountdown==0)
		{
			// we get the type of building
			// try to get the building site, if it doesn't exists, get the finished building (for flags)
			Sint32  typeNum=globalContainer->buildingsTypes.getTypeNum(building, 0, true);
			if (typeNum==-1)
			{
				typeNum=globalContainer->buildingsTypes.getTypeNum(building, 0, false);
				assert(globalContainer->buildingsTypes.get(typeNum)->isVirtual);
			}
			assert (typeNum!=-1);

			BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);

			int mapX, mapY;
			int tempX, tempY;
			bool isRoom;
			game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);
			if (bt->isVirtual)
				isRoom=game.checkRoomForBuilding(tempX, tempY, bt, &mapX, &mapY, localteam);
			else
				isRoom=game.checkHardRoomForBuilding(tempX, tempY, bt, &mapX, &mapY);
			
			int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum);
			int unitWorkingFuture = defaultAssign.getDefaultAssignedUnits(typeNum+1);
			
			if (isRoom)
			{
				ghostManager.addBuilding(building, mapX, mapY);
				orders.push(boost::shared_ptr<Order>(new OrderCreate(localteam, mapX, mapY, typeNum, unitWorking, unitWorkingFuture)));
			}
		}
	}
	if(mode == PlaceZone)
	{
		//To make it easier for the user, if the brush is just 1x1, then it will cause toggling
		//of the zone value, rather than adding/removing (which may have no effect), so that
		//incorrect areas can be adjusted easily
		int mapX, mapY;
		game.map.displayToMapCaseAligned(mouseX, mouseY, &mapX, &mapY,  viewportX, viewportY);
		
		if(firstPlacementX == -1)
		{
			firstPlacementX=mapX;
			firstPlacementY=mapY;
			brushAccumulator.firstX=mapX;
			brushAccumulator.firstY=mapY;
		}

		handleZonePlacement(mouseX, mouseY, localteam, viewportX, viewportY);
	}
}



void GameGUIToolManager::handleMouseUp(int mouseX, int mouseY, int localteam, int viewportX, int viewportY)
{
	if(mode == PlaceZone)
	{
		flushBrushOrders(localteam);
		firstPlacementX=-1;
		firstPlacementY=-1;
	}
}



void GameGUIToolManager::handleMouseDrag(int mouseX, int mouseY, int localteam, int viewportX, int viewportY)
{
	if(mode == PlaceZone)
	{
		handleZonePlacement(mouseX, mouseY, localteam, viewportX, viewportY);
	}
}



boost::shared_ptr<Order> GameGUIToolManager::getOrder()
{
	if(!orders.empty())
	{
		boost::shared_ptr<Order> order = orders.front();
		orders.pop();
		return order;
	}
	return boost::shared_ptr<Order>();
}



void GameGUIToolManager::handleZonePlacement(int mouseX, int mouseY, int localteam, int viewportX, int viewportY)
{
	// we add brush to accumulator
	int mapX, mapY;
	game.map.displayToMapCaseAligned(mouseX, mouseY, &mapX, &mapY,  viewportX, viewportY);
	int fig = brush.getFigure();
	brushAccumulator.applyBrush(BrushApplication(mapX, mapY, fig), &game.map);

	// we get coordinates
	int startX = mapX-BrushTool::getBrushDimXMinus(fig);
	int startY = mapY-BrushTool::getBrushDimYMinus(fig);
	int width  = BrushTool::getBrushWidth(fig);
	int height = BrushTool::getBrushHeight(fig);
	// we update local values
	if (brush.getType() == BrushTool::MODE_ADD)
	{
		for (int y=startY; y<startY+height; y++)
		{
			for (int x=startX; x<startX+width; x++)
			{
				if (BrushTool::getBrushValue(fig, x-startX, y-startY, mapX, mapY, firstPlacementX, firstPlacementY))
				{
					if (zoneType == Forbidden)
						game.map.localForbiddenMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), true);
					else if (zoneType == Guard)
						game.map.localGuardAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), true);
					else if (zoneType == Clearing)
						game.map.localClearAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), true);
				}
			}
		}
	}
	else if (brush.getType() == BrushTool::MODE_DEL)
	{
		for (int y=startY; y<startY+height; y++)
		{
			for (int x=startX; x<startX+width; x++)
			{
				if (BrushTool::getBrushValue(fig, x-startX, y-startY, mapX, mapY, firstPlacementX, firstPlacementY))
				{
					if (zoneType == Forbidden)
						game.map.localForbiddenMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), false);
					else if (zoneType == Guard)
						game.map.localGuardAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), false);
					else if (zoneType == Clearing)
						game.map.localClearAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), false);
				}
			}
		}
	}
	
	// if we have an area over 32x32, which mean over 128 bytes, send it
	if (brushAccumulator.getAreaSurface() > 32*32)
	{
		flushBrushOrders(localteam);
	}
}



void GameGUIToolManager::flushBrushOrders(int localteam)
{
	if (brushAccumulator.getApplicationCount() > 0)
	{
		if (zoneType == Forbidden)
		{
			orders.push(boost::shared_ptr<Order>(new OrderAlterateForbidden(localteam, brush.getType(), &brushAccumulator, &game.map)));
		}
		else if (zoneType == Guard)
		{
			orders.push(boost::shared_ptr<Order>(new OrderAlterateGuardArea(localteam, brush.getType(), &brushAccumulator, &game.map)));
		}
		else if (zoneType == Clearing)
		{
			orders.push(boost::shared_ptr<Order>(new OrderAlterateClearArea(localteam, brush.getType(), &brushAccumulator, &game.map)));
		}
		else
			assert(false);
		brushAccumulator.clear();
	}
}
