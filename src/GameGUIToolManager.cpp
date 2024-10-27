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
#include "GameGUI.h"
#include <cmath>
#include "GameGUIDefaultAssignManager.h"
#include "GameGUIGhostBuildingManager.h"
#include "Order.h"

using namespace GAGGUI;
using namespace GAGCore;

GameGUIToolManager::GameGUIToolManager(Game& game, BrushTool& brush, GameGUIDefaultAssignManager& defaultAssign, GameGUIGhostBuildingManager& ghostManager)
	: game(game), brush(brush), defaultAssign(defaultAssign), ghostManager(ghostManager)
{
	highlightStrength = 0;
	mode = NoTool;
	zoneType = Forbidden;
	firstPlacementX=-1;
	firstPlacementY=-1;
}



void GameGUIToolManager::activateBuildingTool(const std::string& building)
{
	mode = PlaceBuilding;
	this->building = building;
	firstPlacementX = -1;
	firstPlacementY = -1;
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
	{
		flushBrushOrders(game.gui->localTeamNo);
		brush.unselect();
	}
	if(mode == PlaceBuilding)
	{
		building = "";
	}
	mode = NoTool;
}



void GameGUIToolManager::drawTool(int mouseX, int mouseY, int localTeam, int viewportX, int viewportY)
{
	if(mode == PlaceBuilding)
	{
		// Get the type and sprite
		int typeNum = globalContainer->buildingsTypes.getTypeNum(building, 0, false);
		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		
		// Translate the mouse position to a building position, and check if there is room
		// on the map
		int mapX, mapY;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &mapX, &mapY, viewportX, viewportY);
		
		
		SDL_Keymod modState = SDL_GetModState();
		if(!(modState & KMOD_CTRL || modState & KMOD_SHIFT) || firstPlacementX==-1)
		{
			drawBuildingAt(mapX, mapY, localTeam, viewportX, viewportY);
		}
		///This allows the drag-placing of walls
		else if(modState & KMOD_CTRL)
		{
			computeBuildingLine(firstPlacementX, firstPlacementY, mapX, mapY, localTeam, viewportX, viewportY, 1);
		}
		///This allows the placing of a square of buildings
		else if(modState & KMOD_SHIFT)
		{
			computeBuildingBox(firstPlacementX, firstPlacementY, mapX, mapY, localTeam, viewportX, viewportY, 1);
		}
	}
	else if(mode == PlaceZone)
	{
		Color c;
		/* The following colors have been chosen to match the
			colors in the .png files for the animations of
			areas as of 2007-04-29.  If those .png files are
			updated with different colors, then the following
			code should change accordingly. */
		switch(zoneType) {
		case Forbidden:
			c = Color(255,0,0);
			break;
		case Guard:
			c = Color(27,0,255);
			break;
		case Clearing:
			c = Color(251,206,0);
			break;
		}
		/* Instead of using a dimmer intensity to indicate
			removing of areas, this should rather use dashed
			lines.  (The intensities used below are 2/3 as
			bright for the case of removing areas.) */
		/* This reasoning should be abstracted out and reused
			in MapEdit.cpp to choose a color for those tiles
			where areas are being drawn. */
		unsigned mode = brush.getType();
		switch(mode)
		{
		case BrushTool::MODE_DEL:
			c = Color(c.r*2/3,c.g*2/3,c.b*2/3);
			break;
		case BrushTool::MODE_ADD:
			break;
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



void GameGUIToolManager::handleMouseDown(int mouseX, int mouseY, int localTeam, int viewportX, int viewportY)
{
	if(mode == PlaceBuilding)
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
		int tempX, tempY;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);
		firstPlacementX=tempX;
		firstPlacementY=tempY;
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

		handleZonePlacement(mouseX, mouseY, localTeam, viewportX, viewportY);
	}
}



void GameGUIToolManager::handleMouseUp(int mouseX, int mouseY, int localTeam, int viewportX, int viewportY)
{
	if(mode == PlaceZone)
	{
		flushBrushOrders(localTeam);
	}
	if(mode == PlaceBuilding)
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
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &mapX, &mapY, viewportX, viewportY);

		SDL_Keymod modState = SDL_GetModState();
		if(!(modState & KMOD_CTRL || modState & KMOD_SHIFT) || firstPlacementX==-1)
		{
			placeBuildingAt(mapX, mapY, localTeam);
		}
		///This allows the placing of a line of buildings
		else if(modState & KMOD_CTRL)
		{
			computeBuildingLine(firstPlacementX, firstPlacementY, mapX, mapY, localTeam, viewportX, viewportY, 2);
		}
		///This allows the placing of a square of buildings
		else if(modState & KMOD_SHIFT)
		{
			computeBuildingBox(firstPlacementX, firstPlacementY, mapX, mapY, localTeam, viewportX, viewportY, 2);
		}
	}
	firstPlacementX=-1;
	firstPlacementY=-1;
}



void GameGUIToolManager::handleMouseDrag(int mouseX, int mouseY, int localTeam, int viewportX, int viewportY)
{
	if(mode == PlaceZone)
	{
		handleZonePlacement(mouseX, mouseY, localTeam, viewportX, viewportY);
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



void GameGUIToolManager::handleZonePlacement(int mouseX, int mouseY, int localTeam, int viewportX, int viewportY)
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
		flushBrushOrders(localTeam);
	}
}



void GameGUIToolManager::flushBrushOrders(int localTeam)
{
	if (brushAccumulator.getApplicationCount() > 0)
	{
		if (zoneType == Forbidden)
		{
			orders.push(boost::shared_ptr<Order>(new OrderAlterForbidden(localTeam, brush.getType(), &brushAccumulator, &game.map)));
		}
		else if (zoneType == Guard)
		{
			orders.push(boost::shared_ptr<Order>(new OrderAlterGuardArea(localTeam, brush.getType(), &brushAccumulator, &game.map)));
		}
		else if (zoneType == Clearing)
		{
			orders.push(boost::shared_ptr<Order>(new OrderAlterClearArea(localTeam, brush.getType(), &brushAccumulator, &game.map)));
		}
		else
			assert(false);
		brushAccumulator.clear();
	}
}



void GameGUIToolManager::placeBuildingAt(int mapX, int mapY, int localTeam)
{
	// Count down whether a building site can be placed
	if (game.teams[localTeam]->noMoreBuildingSitesCountdown==0)
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

		int tempX = mapX, tempY = mapY;
		bool isRoom;
		if (bt->isVirtual)
			isRoom=game.checkRoomForBuilding(tempX, tempY, bt, &mapX, &mapY, localTeam);
		else
			isRoom=game.checkHardRoomForBuilding(tempX, tempY, bt, &mapX, &mapY);
			
	
		if(ghostManager.isGhostBuilding(mapX, mapY, bt->width, bt->height))
			isRoom = false;
		
		int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum);
		int unitWorkingFuture = defaultAssign.getDefaultAssignedUnits(typeNum+1);
		
		if (isRoom)
		{
			int r = 0;
			if(bt->isVirtual)
				r = globalContainer->settings.defaultFlagRadius[bt->shortTypeNum - IntBuildingType::EXPLORATION_FLAG];
			ghostManager.addBuilding(building, mapX, mapY);
			orders.push(boost::shared_ptr<Order>(new OrderCreate(localTeam, mapX, mapY, typeNum, unitWorking, unitWorkingFuture, r)));
		}
	}
}



void GameGUIToolManager::drawBuildingAt(int mapX, int mapY, int localTeam, int viewportX, int viewportY)
{
	// Get the type and sprite
	int typeNum = globalContainer->buildingsTypes.getTypeNum(building, 0, false);
	BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
	Sprite *sprite = bt->gameSpritePtr;
		
	int tempX = mapX, tempY = mapY;
	bool isRoom;
	if (bt->isVirtual)
		isRoom=game.checkRoomForBuilding(mapX, mapY, bt, &tempX, &tempY, localTeam);
	else
		isRoom=game.checkHardRoomForBuilding(mapX, mapY, bt, &tempX, &tempY);
			
	
	if(ghostManager.isGhostBuilding(tempX, tempY, bt->width, bt->height))
		isRoom = false;
	
	// Increase/Decrease highlight strength, given whether there is room or not
	if (isRoom)
		highlightStrength = std::min(highlightStrength + 0.1f, 1.0f);
	else
		highlightStrength = std::max(highlightStrength - 0.1f, 0.0f);
		
	// we get the screen dimensions of the building
	int batW = (bt->width) * 32;
	int batH = sprite->getH(bt->gameSpriteImage);
	int batX = (((tempX-viewportX)&(game.map.wMask)) * 32);
	int batY = (((tempY-viewportY)&(game.map.hMask)) * 32)-(batH-(bt->height * 32));
	
	// Draw the building
	sprite->setBaseColor(game.teams[localTeam]->color);
	int spriteIntensity = 127+static_cast<int>(128.0f*splineInterpolation(1.f, 0.f, 1.f, highlightStrength));
	globalContainer->gfx->drawSprite(batX, batY, sprite, bt->gameSpriteImage, spriteIntensity);

	if (!bt->isVirtual)
	{
		// Count down whether a building site can be placed
		if (game.teams[localTeam]->noMoreBuildingSitesCountdown>0)
		{
			globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 0, 0, 127);
			globalContainer->gfx->drawLine(batX, batY, batX+batW-1, batY+batH-1, 255, 0, 0, 127);
			globalContainer->gfx->drawLine(batX+batW-1, batY, batX, batY+batH-1, 255, 0, 0, 127);
			
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 0, 0, 127));
			globalContainer->gfx->drawString(batX, batY-12, globalContainer->littleFont, FormattableString("%0.%1").arg(game.teams[localTeam]->noMoreBuildingSitesCountdown/40).arg((game.teams[localTeam]->noMoreBuildingSitesCountdown%40)/4).c_str());
			globalContainer->littleFont->popStyle();
		}
		else
		{
			// Draw the square around the building, denoting its size when upgraded
			if (isRoom)
				globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 255, 255, 127);
			else
				globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 0, 0, 127);
			
			// We look for its maximum extension size
			// we find last's level type num:
			BuildingType *lastBt=globalContainer->buildingsTypes.get(typeNum);
			int lastTypeNum=typeNum;
			int max=0;
			while (lastBt->nextLevel>=0)
			{
				lastTypeNum=lastBt->nextLevel;
				lastBt=globalContainer->buildingsTypes.get(lastTypeNum);
				if (max++>200)
				{
					printf("GameGUI: Error: nextLevel architecture is broken.\n");
					assert(false);
					break;
				}
			}
				
			int exMapX, exMapY; // ex prefix means EXtended building; the last level building type.
			bool isExtendedRoom = game.checkHardRoomForBuilding(mapX, mapY, lastBt, &exMapX, &exMapY);
			int exBatX=((exMapX-viewportX)&(game.map.wMask)) * 32;
			int exBatY=((exMapY-viewportY)&(game.map.hMask)) * 32;
			int exBatW=(lastBt->width) * 32;
			int exBatH=(lastBt->height) * 32;

			if (isRoom && isExtendedRoom)
				globalContainer->gfx->drawRect(exBatX-1, exBatY-1, exBatW+2, exBatH+2, 255, 255, 255, 127);
			else
				globalContainer->gfx->drawRect(exBatX-1, exBatY-1, exBatW+2, exBatH+2, 255, 0, 0, 127);
		}
	}
}

void GameGUIToolManager::computeBuildingLine(int sx, int sy, int ex, int ey, int localTeam, int viewportX, int viewportY, int mode)
{
	// Get the type and sprite
	int typeNum = globalContainer->buildingsTypes.getTypeNum(building, 0, false);
	BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		
	int startX = sx;
	int endX = ex;
	int startY = sy;
	int endY = ey;
	
	int dirX = (endX > startX ? 1 : -1);
	int distX = std::abs(endX - startX);
	if(distX > game.map.getW()/2)
	{
		dirX = -dirX;
		distX = game.map.getW() -  distX;
	}
			
	int dirY = (endY > startY ? 1 : -1);
	int distY = std::abs(endY - startY);
	if(distY > game.map.getH()/2)
	{
		dirY = -dirY;
		distY = game.map.getH() -  distY;
	}
	
	int bw = 0;
	int bh = 0;
	if(distX > distY)
	{
		int px = 0;
		int py = 0;
		int y = startY;
		bool didBuilding=false;
		bool finishing=false;
		for(int x=startX; (!finishing && x!=endX) || !didBuilding;)
		{
			if(x == endX)
				finishing=true;
			didBuilding=false;
			bw-=1;
			px+=1;
			if(bw <= 0)
			{
				if(mode == 1)
					drawBuildingAt(x, y, localTeam, viewportX, viewportY);
				else if(mode == 2)
					placeBuildingAt(x, y, localTeam);
				bw = bt->width;
				bh = bt->height;
				didBuilding=true;
			}
			if(std::abs(px * distY - py * distX) > std::abs(px * distY - (py+1) * distX))
			{
				y=game.map.normalizeY(y+dirY);
				bh-=1;
				py+=1;
				if(bh <= 0)
				{
					if(mode == 1)
						drawBuildingAt(x, y, localTeam, viewportX, viewportY);
					else if(mode == 2)
						placeBuildingAt(x, y, localTeam);
					bw = bt->width;
					bh = bt->height;
					didBuilding=true;
				}
			}
			x=game.map.normalizeX(x+dirX);
		}
	}
	else
	{
		int px = 0;
		int py = 0;
		int x = startX;
		bool didBuilding=false;
		bool finishing=false;
		for(int y=startY; (!finishing && y!=endY) || !didBuilding;)
		{
			if(y == endY)
				finishing=true;
			didBuilding=false;
			bh-=1;
			py+=1;
			if(bh <= 0)
			{
				if(mode == 1)
					drawBuildingAt(x, y, localTeam, viewportX, viewportY);
				else if(mode == 2)
					placeBuildingAt(x, y, localTeam);
				bw = bt->width;
				bh = bt->height;
				didBuilding=true;
			}
			if(std::abs(py * distX - px * distY) > std::abs(py * distX - (px+1) * distY))
			{
				x=game.map.normalizeX(x+dirX);
				bw-=1;
				px+=1;
				if(bw <= 0)
				{
					if(mode == 1)
						drawBuildingAt(x, y, localTeam, viewportX, viewportY);
					else if(mode == 2)
						placeBuildingAt(x, y, localTeam);
					bw = bt->width;
					bh = bt->height;
					didBuilding=true;
				}
			}
			y=game.map.normalizeY(y+dirY);
		}
	}
	if(bt->width == 1 && bt->height==1)
	{
		if(mode == 1)
		{
			drawBuildingAt(endX, endY, localTeam, viewportX, viewportY);
		}
		else if(mode == 2)
		{
			placeBuildingAt(endX, endY, localTeam);
		}
	}
}

void GameGUIToolManager::computeBuildingBox(int sx, int sy, int ex, int ey, int localTeam, int viewportX, int viewportY, int mode)
{
	// Get the type and sprite
	int typeNum = globalContainer->buildingsTypes.getTypeNum(building, 0, false);
	BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
	
	int startX = sx;
	int endX = ex;
	int startY = sy;
	int endY = ey;
	
	int dirX = (endX > startX ? 1 : -1);
	int distX = std::abs(endX - startX);
	if(distX > game.map.getW()/2)
	{
		dirX = -dirX;
		distX = game.map.getW() -  distX;
	}
			
	int dirY = (endY > startY ? 1 : -1);
	int distY = std::abs(endY - startY);
	if(distY > game.map.getH()/2)
	{
		dirY = -dirY;
		distY = game.map.getH() -  distY;
	}
	
	endX = game.map.normalizeX(endX + (distX % bt->width + 1) * dirX);
	endY = game.map.normalizeY(endY + (distY % bt->height + 1) * dirY);
	
	int bx=0;
	for(int x=startX; x!=endX;)
	{
		bx-=1;
		if(bx <= 0)
		{
			int by=0;
			for(int y=startY; y!=endY;)
			{
				by -= 1;
				if(by <= 0)
				{
					if(mode == 1)
						drawBuildingAt(x, y, localTeam, viewportX, viewportY);
					else if(mode == 2)
						placeBuildingAt(x, y, localTeam);
					by = bt->height;
				}
				y=game.map.normalizeY(y+dirY);
			}
			bx = bt->width;	
		}	
		x=game.map.normalizeX(x+dirX);
	}
}
