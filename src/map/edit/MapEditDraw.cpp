// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include <FormatableString.h>
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include "Unit.h"
#include "UnitType.h"
#include "Utilities.h"
#include "FertilityCalculatorDialog.h"
#include "SDLCompat.h"

void MapEdit::drawMap(int sx, int sy, int sw, int sh, bool needUpdate, bool doPaintEditMode)
{
// 	Utilities::rectClipRect(sx, sy, sw, sh, mapClip);

	globalContainer->gfx->setClipRect(sx, sy, sw, sh);

	Uint32 drawOptions = Game::DRAW_WHOLE_MAP | Game::DRAW_BUILDING_RECT | Game::DRAW_AREA | Game::DRAW_HEALTH_FOOD_BAR | Game::DRAW_SCRIPT_AREAS | Game::DRAW_NO_RESSOURCE_GROWTH_AREAS;
	if(isFertilityOn)
	{
		drawOptions |= Game::DRAW_OVERLAY;
	}

	game.drawMap(sx, sy, sw, sh, RIGHT_MENU_WIDTH, 16, viewportX, viewportY, team, view, drawOptions);
// 	if (doPaintEditMode)
// 		paintEditMode(false, false);

	if(widgetRectangle(sx, sy, sw, sh).is_in(mouseX, mouseY))
	{
		// BrushTool treats -1 as "no stroke origin" for checkerboard parity alignment
		const int firstX = firstPlacement ? firstPlacement->x : -1;
		const int firstY = firstPlacement ? firstPlacement->y : -1;
		if(selectionMode==PlaceBuilding)
			drawBuildingSelectionOnMap();
		if(selectionMode==PlaceZone)
			brush.drawBrush(mouseX, mouseY, viewportX, viewportY, firstX, firstY);
		if(selectionMode==PlaceTerrain)
			brush.drawBrush(mouseX, mouseY, viewportX, viewportY, firstX, firstY, (terrainType>TerrainSelector::Water ? 0 : 1));
		if(selectionMode==PlaceUnit)
			drawPlacingUnitOnMap();
		if(selectionMode==RemoveObject)
			brush.drawBrush(mouseX, mouseY, viewportX, viewportY, firstX, firstY);
		if(selectionMode==EditingBuilding)
		{
			Building* selBuild=game.teams[Building::GIDtoTeam(selectedBuildingGID)]->myBuildings[Building::GIDtoID(selectedBuildingGID)];
			globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH());
			int centerX, centerY;
			// Map editor mutates buildings directly — no orderQueue, no pending shadow.
			// Use the authoritative position straight from the Building.
			game.map.buildingPosToCursor(selBuild->posX, selBuild->posY,  selBuild->type->width, selBuild->type->height, &centerX, &centerY, viewportX, viewportY);
			if (selBuild->owner->teamNumber==team)
				globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 0, 0, 190);
			else if ((game.teams[team]->allies) & (selBuild->owner->me))
				globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 255, 196, 0);
			else if (!selBuild->type->isVirtual)
				globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 190, 0, 0);
			globalContainer->gfx->setClipRect();
		}
		if(selectionMode==ChangeAreas)
		{
			brush.drawBrush(mouseX, mouseY, viewportX, viewportY, firstX, firstY);
		}
		if(selectionMode==ChangeNoRessourceGrowthAreas)
			brush.drawBrush(mouseX, mouseY, viewportX, viewportY, firstX, firstY);
	}

	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());
}



void MapEdit::drawMiniMap(void)
{
	minimap.draw(team, viewportX, viewportY, (globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)/32, globalContainer->gfx->getH()/32 );
// 	paintCoordinates();
}



void MapEdit::drawMenu(void)
{
 	int menuStartW=globalContainer->gfx->getW()-RIGHT_MENU_WIDTH;
	int yposition=133;

	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		globalContainer->gfx->drawFilledRect(menuStartW, yposition, RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-128, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(menuStartW, yposition, RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-128, 0, 0, 40, 180);

	drawMenuEyeCandy();
}



void MapEdit::drawBuildingSelectionOnMap()
{
	if (selectionName!="")
	{
		// we get the type of building
		int typeNum=globalContainer->buildingsTypes.getTypeNum(selectionName, buildingLevel, false);
		if(!isUpgradable(IntBuildingType::shortNumberFromType(selectionName)))
			typeNum = globalContainer->buildingsTypes.getTypeNum(selectionName, 0, false);
		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		Sprite *sprite = bt->gameSpritePtr;
		
		// we translate dimensions and situation
		int tempX, tempY;
		int mapX, mapY;
		bool isRoom;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);
		if (bt->isVirtual)
			isRoom = game.checkRoomForBuilding(tempX, tempY, bt, &mapX, &mapY, team);
		else
			isRoom = game.checkHardRoomForBuilding(tempX, tempY, bt, &mapX, &mapY);
			
		// modifiy highlight given room
// 		if (isRoom)
// 			highlightSelection = std::min(highlightSelection + 0.1f, 1.0f);
// / 		else
//  			highlightSelection = std::max(highlightSelection - 0.1f, 0.0f);
		
		// we get the screen dimensions of the building
		int batW = (bt->width)<<5;
		int batH = sprite->getH(bt->gameSpriteImage);
		int batX = (((mapX-viewportX)&(game.map.wMask))<<5);
		int batY = (((mapY-viewportY)&(game.map.hMask))<<5)-(batH-(bt->height<<5));
		
		// we draw the building
		sprite->setBaseColor(game.teams[team]->color);
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH());
// 		int spriteIntensity = 127+static_cast<int>(128.0f*splineInterpolation(1.f, 0.f, 1.f, highlightSelection));
	 	int spriteIntensity = 127;
		globalContainer->gfx->drawSprite(batX, batY, sprite, bt->gameSpriteImage, spriteIntensity);
		
		if (!bt->isVirtual)
		{
			if (game.teams[team]->noMoreBuildingSitesCountdown>0)
			{
				globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(batX, batY, batX+batW-1, batY+batH-1, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(batX+batW-1, batY, batX, batY+batH-1, 255, 0, 0, 127);
				
				globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 0, 0, 127));
				globalContainer->gfx->drawString(batX, batY-12, globalContainer->littleFont, FormatableString("%0.%1").arg(game.teams[team]->noMoreBuildingSitesCountdown/40).arg((game.teams[team]->noMoreBuildingSitesCountdown%40)/4).c_str());
				globalContainer->littleFont->popStyle();
			}
			else
			{
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
				bool isExtendedRoom = game.checkHardRoomForBuilding(tempX, tempY, lastbt, &exMapX, &exMapY);
				int exBatX=((exMapX-viewportX)&(game.map.wMask))<<5;
				int exBatY=((exMapY-viewportY)&(game.map.hMask))<<5;
				int exBatW=(lastbt->width)<<5;
				int exBatH=(lastbt->height)<<5;

				if (isRoom && isExtendedRoom)
					globalContainer->gfx->drawRect(exBatX-1, exBatY-1, exBatW+2, exBatH+2, 255, 255, 255, 127);
				else
					globalContainer->gfx->drawRect(exBatX-1, exBatY-1, exBatW+2, exBatH+2, 255, 0, 0, 127);
			}
		}

	}

}



bool MapEdit::isUpgradable(int buildingLevel)
{
	if(buildingLevel==IntBuildingType::SWARM_BUILDING)
		return false;
	if(buildingLevel==IntBuildingType::EXPLORATION_FLAG)
		return false;
	if(buildingLevel==IntBuildingType::WAR_FLAG)
		return false;
	if(buildingLevel==IntBuildingType::CLEARING_FLAG)
		return false;
	if(buildingLevel==IntBuildingType::STONE_WALL)
		return false;
	if(buildingLevel==IntBuildingType::MARKET_BUILDING)
		return false;
	return true;
}



void MapEdit::drawMenuEyeCandy()
{
	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());

	// bar background 
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 16, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 16, 0, 0, 40, 180);

	// draw window bar
	int pos=globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-32;
	for (int i=0; i<=pos; i+=32)
	{
		globalContainer->gfx->drawSprite(i, 16, globalContainer->gamegui, 16);
	}
	for (int i=16; i<globalContainer->gfx->getH(); i+=32)
	{
		globalContainer->gfx->drawSprite(pos+28, i, globalContainer->gamegui, 17);
	}
}



void MapEdit::drawPlacingUnitOnMap()
{
	int type=0;
	if(placingUnit==Worker)
		type=WORKER;
	else if(placingUnit==Warrior)
		type=WARRIOR;
	else if(placingUnit==Explorer)
		type=EXPLORER;
	
	int level=placingUnitLevel;
	
	int cx=(mouseX>>5)+viewportX;
	int cy=(mouseY>>5)+viewportY;

	int px=mouseX&0xFFFFFFE0;
	int py=mouseY&0xFFFFFFE0;
	int pw=32;
	int ph=32;

	bool isRoom;
	if (type==EXPLORER)
		isRoom=game.map.isFreeForAirUnit(cx, cy);
	else
	{
		UnitType *ut=game.teams[team]->race.getUnitType(type, level);
		isRoom=game.map.isFreeForGroundUnit(cx, cy, ut->performance[SWIM], Team::teamNumberToMask(team));
	}

	int imgid;
	if (type==WORKER)
		imgid=64;
	else if (type==EXPLORER)
		imgid=0;
	else if (type==WARRIOR)
		imgid=256;
	else
	{
		imgid=0;
		assert(false);
	}

	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(game.teams[team]->color);

	globalContainer->gfx->drawSprite(px, py, unitSprite, imgid);

	if (isRoom)
		globalContainer->gfx->drawRect(px, py, pw, ph, 255, 255, 255, 128);
	else
		globalContainer->gfx->drawRect(px, py, pw, ph, 255, 0, 0, 128);
}
