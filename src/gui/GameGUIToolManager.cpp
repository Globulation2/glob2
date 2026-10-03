// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GameGUIToolManager.h"
#include "scene/Scene.h"
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
}



void GameGUIToolManager::activateBuildingTool(const std::string& nbuilding)
{
	mode = PlaceBuilding;
	building = nbuilding;
	firstPlacement.reset();
}



bool GameGUIToolManager::farmAreasAvailable() const
{
	return game.gameHeader.hasExperiment(ExperimentId::FarmAreas);
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



void GameGUIToolManager::drawTool(int mouseX, int mouseY, int localteam, int viewportX, int viewportY, int modifiers)
{
	if(mode == PlaceBuilding)
	{
		// Get the type and sprite
		int typeNum = globalContainer->buildingsTypes.getFinishedTypeNum(building);
		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		
		// Translate the mouse position to a building position, and check if there is room
		// on the map
		int mapX, mapY;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &mapX, &mapY, viewportX, viewportY);
		
		
		const int modState = modifiers;
		if(!(modState & SDL_KMOD_CTRL || modState & SDL_KMOD_SHIFT) || !firstPlacement)
		{
			drawBuildingAt(mapX, mapY, localteam, viewportX, viewportY);
		}
		///This allows the drag-placing of walls
		else if(modState & SDL_KMOD_CTRL)
		{
			computeBuildingLine(firstPlacement->x, firstPlacement->y, mapX, mapY, localteam, viewportX, viewportY, 1);
		}
		///This allows the placing of a square of buildings
		else if(modState & SDL_KMOD_SHIFT)
		{
			computeBuildingBox(firstPlacement->x, firstPlacement->y, mapX, mapY, localteam, viewportX, viewportY, 1);
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
		switch(getZoneType()) {
		case Forbidden:
			c = Color(255,0,0);
			break;
		case Guard:
			c = Color(27,0,255);
			break;
		case Clearing:
			c = Color(251,206,0);
			break;
		case Farm:
			c = Color(110,240,120);
			break;
		}
		/* Instead of using a dimmer intensity to indicate
			removing of areas, this should rather use dashed
			lines.  (The intensities used below are 2/3 as
			bright for the case of removing areas.) */
		/* This reasoning should be abstracted out and reused
			in MapEdit.cpp to choose a color for those cases
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
		if (firstPlacement)
			brush.drawBrush(mouseX, mouseY, c, viewportX, viewportY, firstPlacement->x, firstPlacement->y);
		else
			brush.drawBrush(mouseX, mouseY, c, viewportX, viewportY);
	}
}



std::string GameGUIToolManager::getBuildingName() const
{
	return building;
}



GameGUIToolManager::ZoneType GameGUIToolManager::getZoneType() const
{
	// The farm zone of a game without the experiment (asked for by a key, or
	// left over from an earlier game) falls back to the first zone.
	return int(zoneType) < zoneTypeCount() ? zoneType : Forbidden;
}



void GameGUIToolManager::handleMouseDown(int mouseX, int mouseY, int localteam, int viewportX, int viewportY)
{
	if(mode == PlaceBuilding)
	{
		// we get the type of building
		Sint32 typeNum = globalContainer->buildingsTypes.getPlaceableTypeNum(building);
		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		int tempX, tempY;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);
		firstPlacement = FirstPlacement{tempX, tempY};
	}
	if(mode == PlaceZone)
	{
		//To make it easier for the user, if the brush is just 1x1, then it will cause toggling
		//of the zone value, rather than adding/removing (which may have no effect), so that
		//incorrect areas can be adjusted easily
		int mapX, mapY;
		game.map.displayToMapCaseAligned(mouseX, mouseY, &mapX, &mapY,  viewportX, viewportY);
		
		if(!firstPlacement)
		{
			firstPlacement = FirstPlacement{mapX, mapY};
			brushAccumulator.firstX=mapX;
			brushAccumulator.firstY=mapY;
		}

		handleZonePlacement(mouseX, mouseY, localteam, viewportX, viewportY);
	}
}



void GameGUIToolManager::handleMouseUp(int mouseX, int mouseY, int localteam, int viewportX, int viewportY, int modifiers)
{
	if(mode == PlaceZone)
	{
		flushBrushOrders(localteam);
	}
	if(mode == PlaceBuilding)
	{
		// we get the type of building
		Sint32 typeNum = globalContainer->buildingsTypes.getPlaceableTypeNum(building);
		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);

		int mapX, mapY;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &mapX, &mapY, viewportX, viewportY);

		const int modState = modifiers;
		if(!(modState & SDL_KMOD_CTRL || modState & SDL_KMOD_SHIFT) || !firstPlacement)
		{
			placeBuildingAt(mapX, mapY, localteam);
		}
		///This allows the placing of a line of buildings
		else if(modState & SDL_KMOD_CTRL)
		{
			computeBuildingLine(firstPlacement->x, firstPlacement->y, mapX, mapY, localteam, viewportX, viewportY, 2);
		}
		///This allows the placing of a square of buildings
		else if(modState & SDL_KMOD_SHIFT)
		{
			computeBuildingBox(firstPlacement->x, firstPlacement->y, mapX, mapY, localteam, viewportX, viewportY, 2);
		}
	}
	firstPlacement.reset();
}



void GameGUIToolManager::handleMouseDrag(int mouseX, int mouseY, int localteam, int viewportX, int viewportY)
{
	if(mode == PlaceZone)
	{
		handleZonePlacement(mouseX, mouseY, localteam, viewportX, viewportY);
	}
}



std::shared_ptr<Order> GameGUIToolManager::getOrder()
{
	if(!orders.empty())
	{
		std::shared_ptr<Order> order = orders.front();
		orders.pop();
		return order;
	}
	return std::shared_ptr<Order>();
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
	// BrushTool treats -1 as "no stroke origin" for checkerboard parity alignment
	const int firstX = firstPlacement ? firstPlacement->x : -1;
	const int firstY = firstPlacement ? firstPlacement->y : -1;
	// we update local values
	const unsigned brushMode = brush.getType();
	if (brushMode == BrushTool::MODE_ADD || brushMode == BrushTool::MODE_DEL)
	{
		const bool value = (brushMode == BrushTool::MODE_ADD);
		const ZoneType zone = getZoneType();
		Utilities::BitArray& view = displayedViewForZone(zone);
		// The farm brush does not paint ground nothing can grow on. The order
		// refuses those tiles anyway; skipping them here too keeps the overlay
		// the player sees from disagreeing with what actually lands.
		const bool honourFarmTerrain = (zone == Farm) && value;
		for (int y=startY; y<startY+height; y++)
		{
			for (int x=startX; x<startX+width; x++)
			{
				if (!BrushTool::getBrushValue(fig, x-startX, y-startY, mapX, mapY, firstX, firstY))
					continue;
				if (honourFarmTerrain && !game.map.canPaintFarmArea(x, y))
					continue;
				view.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), value);
			}
		}
	}
	
	// if we have an area over 32x32, which mean over 128 bytes, send it
	if (brushAccumulator.getAreaSurface() > 32*32)
	{
		flushBrushOrders(localteam);
	}
}



Utilities::BitArray& GameGUIToolManager::displayedViewForZone(ZoneType type)
{
	switch (type)
	{
	case Forbidden:
		return game.map.displayedForbiddenView;
	case Guard:
		return game.map.displayedGuardAreaView;
	case Clearing:
		return game.map.displayedClearAreaView;
	case Farm:
		return game.map.displayedFarmAreaView;
	}
	assert(false);
	return game.map.displayedForbiddenView;
}



namespace
{
	// One order class per zone type, built from whatever OrderAlterArea's
	// constructors accept (a brush accumulator, or a box and mask).
	template <typename... Args>
	std::shared_ptr<Order> zoneOrder(GameGUIToolManager::ZoneType type, Args&&... args)
	{
		switch (type)
		{
		case GameGUIToolManager::Forbidden:
			return std::make_shared<OrderAlterForbidden>(std::forward<Args>(args)...);
		case GameGUIToolManager::Guard:
			return std::make_shared<OrderAlterGuardArea>(std::forward<Args>(args)...);
		case GameGUIToolManager::Clearing:
			return std::make_shared<OrderAlterClearArea>(std::forward<Args>(args)...);
		case GameGUIToolManager::Farm:
			return std::make_shared<OrderAlterFarmArea>(std::forward<Args>(args)...);
		}
		assert(false);
		return nullptr;
	}
}



std::shared_ptr<Order> GameGUIToolManager::makeZoneOrder(ZoneType type, Uint8 team, Uint8 mode,
	Sint16 left, Sint16 top, Sint16 width, Sint16 height, const Utilities::BitArray& mask)
{
	return zoneOrder(type, team, mode, left, top, width, height, mask);
}



void GameGUIToolManager::flushBrushOrders(int localteam)
{
	if (brushAccumulator.getApplicationCount() > 0)
	{
		orders.push(zoneOrder(getZoneType(), Uint8(localteam), Uint8(brush.getType()), &brushAccumulator, &game.map));
		brushAccumulator.clear();
	}
}



bool GameGUIToolManager::confirmBuilding(int mouseX, int mouseY, int localteam, int viewportX, int viewportY)
{
    if (mode != PlaceBuilding) return false;
    const auto* type=globalContainer->buildingsTypes.get(globalContainer->buildingsTypes.getPlaceableTypeNum(building));
    int x,y;
    game.map.cursorToBuildingPos(mouseX,mouseY,type->width,type->height,&x,&y,viewportX,viewportY);
    return placeBuildingAt(x,y,localteam);
}

bool GameGUIToolManager::placeBuildingAt(int mapX, int mapY, int localteam)
{
	// Count down whether a building site can be placed
	if (game.teams[localteam]->noMoreBuildingSitesCountdown==0)
	{
		// we get the type of building
		Sint32 typeNum = globalContainer->buildingsTypes.getPlaceableTypeNum(building);
		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);

		int tempX = mapX, tempY = mapY;
		bool isRoom;
		if (bt->isVirtual)
			isRoom=game.checkRoomForBuilding(tempX, tempY, bt, &mapX, &mapY, localteam);
		else
			isRoom=game.checkHardRoomForBuilding(tempX, tempY, bt, &mapX, &mapY);
			
	
		if(ghostManager.isGhostBuilding(mapX, mapY, bt->width, bt->height))
			isRoom = false;
		
		int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum);
		int unitWorkingFuture = defaultAssign.getDefaultAssignedUnits(globalContainer->buildingsTypes.getFinishedTypeNum(building));
		
		if (isRoom)
		{
			int r = 0;
			if(bt->isVirtual)
				r = globalContainer->settings.defaultFlagRadius[bt->shortTypeNum - IntBuildingType::EXPLORATION_FLAG];
			ghostManager.addBuilding(typeNum, mapX, mapY);
			orders.push(std::shared_ptr<Order>(new OrderCreate(localteam, mapX, mapY, typeNum, unitWorking, unitWorkingFuture, r)));
            return true;
		}
	}
    return false;
}



void GameGUIToolManager::drawBuildingAt(int mapX, int mapY, int localteam, int viewportX, int viewportY)
{
	// Get the type and sprite
	int typeNum = globalContainer->buildingsTypes.getFinishedTypeNum(building);
	BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
	Sprite *sprite = bt->gameSpritePtr;
		
	// Room as Game::checkRoomForBuilding / checkHardRoomForBuilding decide it, read
	// from the drawn Scene: flags need no own flag on the tile, buildings hard space.
	assert(drawnScene);
	const Scene &scene = *drawnScene;
	int tempX = mapX + bt->decLeft, tempY = mapY + bt->decTop;
	bool isRoom = true;
	if (bt->isVirtual)
	{
		if (localteam >= 0)
			for (Uint16 flag : scene.entities.virtualBuildings[localteam])
			{
				const SceneBuilding *b = scene.entities.building(flag);
				if (b && b->posX == (tempX & scene.map.getMaskW()) && b->posY == (tempY & scene.map.getMaskH()))
					isRoom = false;
			}
	}
	else
		isRoom = scene.map.isHardSpaceForBuilding(tempX, tempY, bt->width, bt->height);
			
	
	if(ghostManager.isGhostBuilding(tempX, tempY, bt->width, bt->height))
		isRoom = false;
	
	// Increase/Decrease highlight strength, given whether there is room or not
	if (!globalContainer->gfx->isPeriodicCopy()) {
	if (isRoom)
		highlightStrength = std::min(highlightStrength + 0.1f, 1.0f);
	else
		highlightStrength = std::max(highlightStrength - 0.1f, 0.0f);
    }
		
	// we get the screen dimensions of the building
	int rectW = (bt->width) * 32;
	int rectH = sprite->getH(bt->gameSpriteImage);
	int rectX = (((tempX-viewportX)&(scene.map.getMaskW())) * 32);
	int rectY = (((tempY-viewportY)&(scene.map.getMaskH())) * 32)-(rectH-(bt->height * 32));
	
	// Draw the building
	sprite->setBaseColor(scene.panels.local.color);
	int spriteIntensity = 127+static_cast<int>(128.0f*splineInterpolation(1.f, 0.f, 1.f, highlightStrength));
	globalContainer->gfx->drawSprite(rectX, rectY, sprite, bt->gameSpriteImage, spriteIntensity);
	globalContainer->gfx->finishDrawingSprite(sprite, spriteIntensity);

	if (!bt->isVirtual)
	{
		// Count down whether a building site can be placed
		const int countdown = scene.panels.local.noMoreBuildingSitesCountdown;
		if (countdown>0)
		{
			globalContainer->gfx->drawRect(rectX, rectY, rectW, rectH, 255, 0, 0, 127);
			globalContainer->gfx->drawLine(rectX, rectY, rectX+rectW-1, rectY+rectH-1, 255, 0, 0, 127);
			globalContainer->gfx->drawLine(rectX+rectW-1, rectY, rectX, rectY+rectH-1, 255, 0, 0, 127);
			
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 0, 0, 127));
			globalContainer->gfx->drawString(rectX, rectY-12, globalContainer->littleFont, FormattableString("%0.%1").arg(countdown/40).arg((countdown%40)/4).c_str());
			globalContainer->littleFont->popStyle();
		}
		else
		{
			// Draw the square around the building, denoting its size when upgraded
			if (isRoom)
				globalContainer->gfx->drawRect(rectX, rectY, rectW, rectH, 255, 255, 255, 127);
			else
				globalContainer->gfx->drawRect(rectX, rectY, rectW, rectH, 255, 0, 0, 127);
			
			BuildingType *upgradedType=globalContainer->buildingsTypes.getLastLevel(typeNum);
			const int upgradedMapX = mapX + upgradedType->decLeft, upgradedMapY = mapY + upgradedType->decTop;
			bool isUpgradedRoom = scene.map.isHardSpaceForBuilding(upgradedMapX, upgradedMapY, upgradedType->width, upgradedType->height);
			int upgradedRectX=((upgradedMapX-viewportX)&(scene.map.getMaskW())) * 32;
			int upgradedRectY=((upgradedMapY-viewportY)&(scene.map.getMaskH())) * 32;
			int upgradedRectW=(upgradedType->width) * 32;
			int upgradedRectH=(upgradedType->height) * 32;

			if (isRoom && isUpgradedRoom)
				globalContainer->gfx->drawRect(upgradedRectX-1, upgradedRectY-1, upgradedRectW+2, upgradedRectH+2, 255, 255, 255, 127);
			else
				globalContainer->gfx->drawRect(upgradedRectX-1, upgradedRectY-1, upgradedRectW+2, upgradedRectH+2, 255, 0, 0, 127);
		}
	}
}

void GameGUIToolManager::computeBuildingLine(int sx, int sy, int ex, int ey, int localteam, int viewportX, int viewportY, int mode)
{
	// Get the type and sprite
	int typeNum = globalContainer->buildingsTypes.getFinishedTypeNum(building);
	BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		
	int startx = sx;
	int endx = ex;
	int starty = sy;
	int endy = ey;
	
	int dirx = (endx > startx ? 1 : -1);
	int distx = std::abs(endx - startx);
	if(distx > game.map.getW()/2)
	{
		dirx = -dirx;
		distx = game.map.getW() -  distx;
	}
			
	int diry = (endy > starty ? 1 : -1);
	int disty = std::abs(endy - starty);
	if(disty > game.map.getH()/2)
	{
		diry = -diry;
		disty = game.map.getH() -  disty;
	}
	
	int bw = 0;
	int bh = 0;
	if(distx > disty)
	{
		int px = 0;
		int py = 0;
		int y = starty;
		bool didBuilding=false;
		bool finishing=false;
		for(int x=startx; (!finishing && x!=endx) || !didBuilding;)
		{
			if(x == endx)
				finishing=true;
			didBuilding=false;
			bw-=1;
			px+=1;
			if(bw <= 0)
			{
				if(mode == 1)
					drawBuildingAt(x, y, localteam, viewportX, viewportY);
				else if(mode == 2)
					placeBuildingAt(x, y, localteam);
				bw = bt->width;
				bh = bt->height;
				didBuilding=true;
			}
			if(std::abs(px * disty - py * distx) > std::abs(px * disty - (py+1) * distx))
			{
				y=game.map.normalizeY(y+diry);
				bh-=1;
				py+=1;
				if(bh <= 0)
				{
					if(mode == 1)
						drawBuildingAt(x, y, localteam, viewportX, viewportY);
					else if(mode == 2)
						placeBuildingAt(x, y, localteam);
					bw = bt->width;
					bh = bt->height;
					didBuilding=true;
				}
			}
			x=game.map.normalizeX(x+dirx);
		}
	}
	else
	{
		int px = 0;
		int py = 0;
		int x = startx;
		bool didBuilding=false;
		bool finishing=false;
		for(int y=starty; (!finishing && y!=endy) || !didBuilding;)
		{
			if(y == endy)
				finishing=true;
			didBuilding=false;
			bh-=1;
			py+=1;
			if(bh <= 0)
			{
				if(mode == 1)
					drawBuildingAt(x, y, localteam, viewportX, viewportY);
				else if(mode == 2)
					placeBuildingAt(x, y, localteam);
				bw = bt->width;
				bh = bt->height;
				didBuilding=true;
			}
			if(std::abs(py * distx - px * disty) > std::abs(py * distx - (px+1) * disty))
			{
				x=game.map.normalizeX(x+dirx);
				bw-=1;
				px+=1;
				if(bw <= 0)
				{
					if(mode == 1)
						drawBuildingAt(x, y, localteam, viewportX, viewportY);
					else if(mode == 2)
						placeBuildingAt(x, y, localteam);
					bw = bt->width;
					bh = bt->height;
					didBuilding=true;
				}
			}
			y=game.map.normalizeY(y+diry);
		}
	}
	if(bt->width == 1 && bt->height==1)
	{
		if(mode == 1)
		{
			drawBuildingAt(endx, endy, localteam, viewportX, viewportY);
		}
		else if(mode == 2)
		{
			placeBuildingAt(endx, endy, localteam);
		}
	}
}

void GameGUIToolManager::computeBuildingBox(int sx, int sy, int ex, int ey, int localteam, int viewportX, int viewportY, int mode)
{
	// Get the type and sprite
	int typeNum = globalContainer->buildingsTypes.getFinishedTypeNum(building);
	BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
	
	int startx = sx;
	int endx = ex;
	int starty = sy;
	int endy = ey;
	
	int dirx = (endx > startx ? 1 : -1);
	int distx = std::abs(endx - startx);
	if(distx > game.map.getW()/2)
	{
		dirx = -dirx;
		distx = game.map.getW() -  distx;
	}
			
	int diry = (endy > starty ? 1 : -1);
	int disty = std::abs(endy - starty);
	if(disty > game.map.getH()/2)
	{
		diry = -diry;
		disty = game.map.getH() -  disty;
	}
	
	endx = game.map.normalizeX(endx + (distx % bt->width + 1) * dirx);
	endy = game.map.normalizeY(endy + (disty % bt->height + 1) * diry);
	
	int bx=0;
	for(int x=startx; x!=endx;)
	{
		bx-=1;
		if(bx <= 0)
		{
			int by=0;
			for(int y=starty; y!=endy;)
			{
				by -= 1;
				if(by <= 0)
				{
					if(mode == 1)
						drawBuildingAt(x, y, localteam, viewportX, viewportY);
					else if(mode == 2)
						placeBuildingAt(x, y, localteam);
					by = bt->height;
				}
				y=game.map.normalizeY(y+diry);
			}
			bx = bt->width;	
		}	
		x=game.map.normalizeX(x+dirx);
	}
}

void GameGUIToolManager::finishPointerGesture(int localteam)
{
	cancelDrag(localteam);
}

void GameGUIToolManager::cancelDrag(int localteam)
{
    if (mode == PlaceZone) flushBrushOrders(localteam);
    firstPlacement.reset();
}
