/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  Copyright (C) 2006 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <cmath>

#include <GAG.h>
#include "Game.h"
#include "GameGUILoadSave.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include "UnitEditorScreen.h"
#include "Unit.h"
#include "UnitType.h"
#include "Utilities.h"

#include <FormatableString.h>
#include <Stream.h>
#include <StreamFilter.h>

#include <sstream>

MapEdit::MapEdit() : game(NULL)
{
	do_quit=false;

	// default value;
	viewportX=0;
	viewportY=0;
	xspeed=0;
	yspeed=0;
	mouseX=0;
	mouseY=0;

	// load menu
	menu=Toolkit::getSprite("data/gui/editor");
	font=globalContainer->littleFont;

	// editor facilities
	hasMapBeenModified=false;
	team=0;

	selectionMode=PlaceNothing;

	panelmode=AddBuildings;
	buildingsChoiceName.clear();
	buildingsChoiceName.push_back("swarm");
	buildingsChoiceName.push_back("inn");
	buildingsChoiceName.push_back("hospital");
	buildingsChoiceName.push_back("racetrack");
	buildingsChoiceName.push_back("swimmingpool");
	buildingsChoiceName.push_back("barracks");
	buildingsChoiceName.push_back("school");
	buildingsChoiceName.push_back("defencetower");
	buildingsChoiceName.push_back("stonewall");
	buildingsChoiceName.push_back("market");
	selectionName="";
	building_level=0;

	flagsChoiceName.clear();
	flagsChoiceName.push_back("explorationflag");
	flagsChoiceName.push_back("warflag");
	flagsChoiceName.push_back("clearingflag");
	brushType = NoBrush;

	is_dragging_minimap=false;
	is_dragging_zone=false;
	is_dragging_terrain=false;

	last_placement_x=-1;
	last_placement_y=-1;

	showingMenuScreen=false;
	menuscreen = NULL;
	showing_load=false;
	showing_save=false;
	showingScriptEditor=false;

	terrainType=NoTerrain;

	team_view_selector_keys.push_back("[human]");
	selector_positions.resize(16, 0);
	
	selectedUnit=NoUnit;
	unit_level=0;

	register_buttons();
}



MapEdit::~MapEdit()
{
	Toolkit::releaseSprite("data/gui/editor");
}


bool MapEdit::load(const char *filename)
{
	assert(filename);

	InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "MapEdit::load(\"" << filename << "\") : error, can't open file." << std::endl;
		delete stream;
		return false;
	}
	else
	{
		bool rv = game.load(stream);
		
		delete stream;
		if (!rv)
			return false;
		
		// set the editor default values
		team = 0;
	
		renderMiniMap();
		return true;
	}
}



bool MapEdit::save(const char *filename, const char *name)
{
	assert(filename);
	assert(name);

	OutputStream *stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "MapEdit::save(\"" << filename << "\",\"" << name << "\") : error, can't open file." << std::endl;
		delete stream;
		return false;
	}
	else
	{
		game.save(stream, true, name);
		delete stream;
		return true;
	}
}



int MapEdit::run(int sizeX, int sizeY, TerrainType terrainType)
{
	game.map.setSize(sizeX, sizeY, terrainType);
	game.map.setGame(&game);
	return run();
}



int MapEdit::run(void)
{
	//globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight , 32, globalContainer->graphicFlags, (DrawableSurface::GraphicContextType)globalContainer->settings.graphicType);

// 	regenerateClipRect();
	globalContainer->gfx->setClipRect();
	renderMiniMap();
	drawMenu();
	drawMap(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getW(), true, true);
	drawMiniMap();

	bool isRunning=true;
	int returnCode=0;
	Uint32 startTick, endTick, deltaTick;
	while (isRunning)
	{
		//SDL_Event event;
		startTick=SDL_GetTicks();
	
		// we get all pending events but for mousemotion we only keep the last one
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{

 			returnCode=(processEvent(event) == -1) ? -1 : returnCode;
		}
			

		// redraw on scroll
// 		bool doRedraw=false;
		viewportX+=xspeed;
		viewportY+=yspeed;
		viewportX&=game.map.getMaskW();
		viewportY&=game.map.getMaskH();

		drawMap(0, 0, globalContainer->gfx->getW()-0, globalContainer->gfx->getH(), true, true);
		drawMiniMap();
		drawMenu();
		if(showingMenuScreen)
		{
			globalContainer->gfx->setClipRect();
			menuscreen->dispatchPaint();
			globalContainer->gfx->drawSurface((int)menuscreen->decX, (int)menuscreen->decY, menuscreen->getSurface());
		}
		if(showing_load || showing_save)
		{
			globalContainer->gfx->setClipRect();
			loadsavescreen->dispatchPaint();
			globalContainer->gfx->drawSurface((int)loadsavescreen->decX, (int)loadsavescreen->decY, loadsavescreen->getSurface());
		}
		if(showingScriptEditor)
		{
			globalContainer->gfx->setClipRect();
			script_editor->dispatchPaint();
			globalContainer->gfx->drawSurface((int)script_editor->decX, (int)script_editor->decY, script_editor->getSurface());
		}
		globalContainer->gfx->nextFrame();

		endTick=SDL_GetTicks();
		deltaTick=endTick-startTick;
		if (deltaTick<33)
			SDL_Delay(33-deltaTick);
		if (returnCode==-1)
		{
			isRunning=false;
		}
		if(do_quit)
			isRunning=false;
	}

	//globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight , 32, globalContainer->graphicFlags, (DrawableSurface::GraphicContextType)globalContainer->settings.graphicType);
	return returnCode;
}



void MapEdit::drawMap(int sx, int sy, int sw, int sh, bool needUpdate, bool doPaintEditMode)
{
// 	Utilities::rectClipRect(sx, sy, sw, sh, mapClip);

	globalContainer->gfx->setClipRect(sx, sy, sw, sh);

	game.drawMap(sx, sy, sw, sh, viewportX, viewportY, team, Game::DRAW_WHOLE_MAP | Game::DRAW_BUILDING_RECT | Game::DRAW_AREA);
// 	if (doPaintEditMode)
// 		paintEditMode(false, false);

	if(Rectangle(sx, sy, sw, sh).is_in(mouseX, mouseY))
	{
		if(selectionMode==PlaceBuilding)
			drawBuildingSelectionOnMap();
		if(selectionMode==PlaceZone)
			brush.drawBrush(mouseX, mouseY);
		if(selectionMode==PlaceTerrain)
			brush.drawBrush(mouseX, mouseY, (terrainType>Water ? 0 : 1));
		if(selectionMode==PlaceUnit)
			drawUnitOnMap();
	}

	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());
}



void MapEdit::drawMiniMap(void)
{
	game.drawMiniMap(globalContainer->gfx->getW()-128, 0, 128, 128, viewportX, viewportY, team);
// 	paintCoordinates();
}



void MapEdit::renderMiniMap(void)
{
	game.renderMiniMap(team, true);
}



void MapEdit::drawMenu(void)
{
 	int menuStartW=globalContainer->gfx->getW()-128;
	int yposition=128;

	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		globalContainer->gfx->drawFilledRect(menuStartW, yposition, 128, globalContainer->gfx->getH()-128, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(menuStartW, yposition, 128, globalContainer->gfx->getH()-128, 0, 0, 40, 180);

	drawPanelButtons(yposition);
	yposition+=32;
	if(panelmode==AddBuildings)
	{
		drawChoice(yposition, buildingsChoiceName);
		int buildingInfoStart=globalContainer->gfx->getH()-36;
		drawTeamSelector(menuStartW+16, globalContainer->gfx->getH()-74);
		///Draw the level1, level2, and level3 symbols
		globalContainer->gfx->drawSprite(menuStartW+0, buildingInfoStart, menu, 30);
		globalContainer->gfx->drawSprite(menuStartW+48, buildingInfoStart, menu, 31);
		globalContainer->gfx->drawSprite(menuStartW+96, buildingInfoStart, menu, 32);
	}
	else if(panelmode==AddFlagsAndZones)
	{
		drawFlagView();
	}
	else if(panelmode==Terrain)
	{
		drawTerrainView();
	}
	else if(panelmode==Teams)
	{
		drawTeamView();
	}
	drawMenuEyeCandy();
}



void MapEdit::drawPanelButtons(int pos)
{
	// draw buttons

	if (panelmode==AddBuildings)
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128, pos, globalContainer->gamegui, 1);
	else
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128, pos, globalContainer->gamegui, 0);


	if (panelmode==AddFlagsAndZones)
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-96, pos, globalContainer->gamegui, 1);
	else
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-96, pos, globalContainer->gamegui, 0);


	if (panelmode==Terrain)
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-64, pos, globalContainer->gamegui, 3);
	else
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-64, pos, globalContainer->gamegui, 2);

	if (panelmode==Teams)
 		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-32, pos, globalContainer->gamegui, 5);
 	else
 		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-32, pos, globalContainer->gamegui, 4);
}



void MapEdit::drawChoice(int pos, std::vector<std::string> &types, unsigned numberPerLine)
{
	assert(numberPerLine >= 2);
	assert(numberPerLine <= 3);
 	int sel=-1;
	int width = (128/numberPerLine);
	size_t i;

	for (i=0; i<types.size(); i++)
	{
		std::string &type = types[i];

		if (selectionName==type)
			sel = i;

		BuildingType *bt = globalContainer->buildingsTypes.getByType(type.c_str(), building_level, false);
		if(bt==NULL || !is_upgradable(IntBuildingType::shortNumberFromType(type)))
			bt = globalContainer->buildingsTypes.getByType(type.c_str(), 0, false);
		assert(bt);

		int imgid = bt->miniSpriteImage;
		int x, y;

		x=((i % numberPerLine)*width)+globalContainer->gfx->getW()-128;
		y=((i / numberPerLine)*46)+128+32;
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

		buildingSprite->setBaseColor(game.teams[team]->colorR, game.teams[team]->colorG, game.teams[team]->colorB);
		globalContainer->gfx->drawSprite(x+decX, y+decY, buildingSprite, imgid);
	}
//	int count = i;


	globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-128, 128, 128, globalContainer->gfx->getH()-128);

	// draw selection if needed
	if (selectionName != "")
	{
		assert(sel>=0);
		int x=((sel  % numberPerLine)*width)+globalContainer->gfx->getW()-128;
		int y=((sel / numberPerLine)*46)+128+32;
		if (numberPerLine == 2)
			globalContainer->gfx->drawSprite(x+4, y+1, globalContainer->gamegui, 8);
		else
			globalContainer->gfx->drawSprite(x+((width-40)>>1), y+4, globalContainer->gamegui, 23);
	}

}



void MapEdit::drawTextCenter(int x, int y, const char *caption, int i)
{
	const char *text;

	if (i==-1)
		text=Toolkit::getStringTable()->getString(caption);
	else
		text=Toolkit::getStringTable()->getString(caption, i);

	int dec=(128-globalContainer->littleFont->getStringWidth(text))>>1;
	globalContainer->gfx->drawString(x+dec, y, globalContainer->littleFont, text);
}



void MapEdit::drawBuildingSelectionOnMap()
{
	if (selectionName!="")
	{
		// we get the type of building
		int typeNum=globalContainer->buildingsTypes.getTypeNum(selectionName, building_level, false);
		if(!is_upgradable(IntBuildingType::shortNumberFromType(selectionName)))
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
		sprite->setBaseColor(game.teams[team]->colorR, game.teams[team]->colorG, game.teams[team]->colorB);
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH());
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




bool MapEdit::is_upgradable(int building_level)
{
	if(building_level==IntBuildingType::SWARM_BUILDING)
		return false;
	if(building_level==IntBuildingType::EXPLORATION_FLAG)
		return false;
	if(building_level==IntBuildingType::WAR_FLAG)
		return false;
	if(building_level==IntBuildingType::CLEARING_FLAG)
		return false;
	if(building_level==IntBuildingType::STONE_WALL)
		return false;
	if(building_level==IntBuildingType::MARKET_BUILDING)
		return false;
	return true;
}



void MapEdit::disableBuildingsView()
{
	for (size_t i=0; i<buildingsChoiceName.size(); i++)
	{
		deactivate_area("building "+buildingsChoiceName[i]);
	}
	for(int n=0; n<16; ++n)
	{
		if(game.teams[n])
		{
			std::stringstream str;
			str<<n;
			deactivate_area("select active team "+str.str());
			str.str("");
		}
	}
	deactivate_area("building level 1");
	deactivate_area("building level 2");
	deactivate_area("building level 3");
}



void MapEdit::enableBuildingsView()
{
	for (size_t i=0; i<buildingsChoiceName.size(); i++)
	{
		activate_area("building "+buildingsChoiceName[i]);
	}
	for(int n=0; n<16; ++n)
	{
		if(game.teams[n])
		{
			std::stringstream str;
			str<<n;
			activate_area("select active team "+str.str());
			str.str("");
		}
	}
	activate_area("building level 1");
	activate_area("building level 2");
	activate_area("building level 3");
}


void MapEdit::disableFlagView()
{
	for (size_t i=0; i<flagsChoiceName.size(); i++)
	{
		deactivate_area("building "+flagsChoiceName[i]);
	}
	deactivate_area("forbidden zone");
	deactivate_area("clearing zone");
	deactivate_area("guard zone");
	deactivate_area("zone selection");
	for(int n=0; n<16; ++n)
	{
		if(game.teams[n])
		{
			std::stringstream str;
			str<<n;
			deactivate_area("select active team "+str.str());
			str.str("");
		}
	}
	deactivate_area("select worker");
	deactivate_area("select warrior");
	deactivate_area("select explorer");
	deactivate_area("select unit level 1");
	deactivate_area("select unit level 2");
	deactivate_area("select unit level 3");
	deactivate_area("select unit level 4");
}


void MapEdit::enableFlagView()
{
	for (size_t i=0; i<flagsChoiceName.size(); i++)
	{
		activate_area("building "+flagsChoiceName[i]);
	}
	activate_area("forbidden zone");
	activate_area("clearing zone");
	activate_area("guard zone");
	activate_area("zone selection");
	for(int n=0; n<16; ++n)
	{
		if(game.teams[n])
		{
			std::stringstream str;
			str<<n;
			activate_area("select active team "+str.str());
			str.str("");
		}
	}
	activate_area("select worker");
	activate_area("select warrior");
	activate_area("select explorer");
	activate_area("select unit level 1");
	activate_area("select unit level 2");
	activate_area("select unit level 3");
	activate_area("select unit level 4");
}


void MapEdit::disableTerrainView()
{
	deactivate_area("grass button");
	deactivate_area("sand button");
	deactivate_area("water button");
	deactivate_area("wheat button");
	deactivate_area("trees button");
	deactivate_area("stone button");
	deactivate_area("algae button");
	deactivate_area("cherry tree button");
	deactivate_area("orange tree button");
	deactivate_area("prune tree button");
	deactivate_area("terrain selection");
}


void MapEdit::enableTerrainView()
{
	activate_area("grass button");
	activate_area("sand button");
	activate_area("water button");
	activate_area("wheat button");
	activate_area("trees button");
	activate_area("stone button");
	activate_area("algae button");
	activate_area("cherry tree button");
	activate_area("orange tree button");
	activate_area("prune tree button");
	activate_area("terrain selection");
}



void MapEdit::disableTeamView()
{
	int count=0;
	for(int x=0; x<16; ++x)
	{
		if(game.teams[x])
		{
			count++;
			std::stringstream str;
			str<<x;
			deactivate_area("select team "+str.str());
			str.str("");
		}
	}
	std::stringstream str;
	str<<count;
	deactivate_area("add team "+str.str());
	deactivate_area("remove team "+str.str());
}



void MapEdit::enableTeamView()
{	int count=0;
	for(int x=0; x<16; ++x)
	{
		if(game.teams[x])
		{
			count++;
			std::stringstream str;
			str<<x;
			activate_area("select team "+str.str());
			str.str("");
		}
	}
	std::stringstream str;
	str<<count;
	activate_area("add team "+str.str());
	activate_area("remove team "+str.str());
}



void MapEdit::drawScrollBox(int x, int y, int value, int valueLocal, int act, int max)
{
	globalContainer->gfx->setClipRect(x+8, y, 112, 16);
	globalContainer->gfx->drawSprite(x+8, y, globalContainer->gamegui, 9);

	int size=(valueLocal*92)/max;
	globalContainer->gfx->setClipRect(x+18, y, size, 16);
	globalContainer->gfx->drawSprite(x+18, y+3, globalContainer->gamegui, 10);
	
	size=(act*92)/max;
	globalContainer->gfx->setClipRect(x+18, y, size, 16);
	globalContainer->gfx->drawSprite(x+18, y+4, globalContainer->gamegui, 11);
	
	globalContainer->gfx->setClipRect();
}



void MapEdit::drawFlagView()
{
	const int YPOS_FLAG=160;
	const int YPOS_BRUSH=YPOS_FLAG+56;
	const int YPOS_UNIT=YPOS_BRUSH+96+40+8;

	// draw flags
	drawChoice(YPOS_FLAG, flagsChoiceName, 3);
	// draw choice of area
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+8, YPOS_BRUSH, globalContainer->gamegui, 13);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+48, YPOS_BRUSH, globalContainer->gamegui, 14);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+88, YPOS_BRUSH, globalContainer->gamegui, 25);
	if (selectionMode==PlaceZone)
	{
		int decX = 8 + ((int)brushType) * 40;
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+decX, YPOS_BRUSH, globalContainer->gamegui, 22);
	}
	// draw brush
	brush.draw(globalContainer->gfx->getW()-128, YPOS_BRUSH+40);

	//Team selector
	drawTeamSelector(globalContainer->gfx->getW()-128+16, globalContainer->gfx->getH()-74);

	// draw units
	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(game.teams[team]->colorR, game.teams[team]->colorG, game.teams[team]->colorB);

	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+8, YPOS_UNIT, unitSprite, 64);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+48, YPOS_UNIT, unitSprite, 0);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+88, YPOS_UNIT, unitSprite, 256);
	if(selectionMode==PlaceUnit)
	{
		int posx = 8 + static_cast<int>(selectedUnit) * 40;
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+posx, YPOS_UNIT, globalContainer->gamegui, 23);
	}

	///Draw the level 1, level 2, level 3, and level 4 symbols
	int unitInfoStart=globalContainer->gfx->getH()-36;
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+0, unitInfoStart, menu, 30, (unit_level==0 ? 128 : 255));
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+32, unitInfoStart, menu, 31, (unit_level==1 ? 128 : 255));
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+64, unitInfoStart, menu, 32, (unit_level==2 ? 128 : 255));
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+96, unitInfoStart, menu, 33, (unit_level==3 ? 128 : 255));
}



void MapEdit::drawMenuEyeCandy()
{
	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());

	// bar background 
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-128, 16, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-128, 16, 0, 0, 40, 180);

	// draw window bar
	int pos=globalContainer->gfx->getW()-128-32;
	for (int i=0; i<pos; i+=32)
	{
		globalContainer->gfx->drawSprite(i, 16, globalContainer->gamegui, 16);
	}
	for (int i=16; i<globalContainer->gfx->getH(); i+=32)
	{
		globalContainer->gfx->drawSprite(pos+28, i, globalContainer->gamegui, 17);
	}

	// draw main menu button
	if (showingMenuScreen)
		globalContainer->gfx->drawSprite(pos, 0, globalContainer->gamegui, 7);
	else
		globalContainer->gfx->drawSprite(pos, 0, globalContainer->gamegui, 6);
}



void MapEdit::drawTerrainView()
{
	int menuStartW=globalContainer->gfx->getW()-128;
	globalContainer->gfx->drawSprite(menuStartW+0, 172, globalContainer->terrain, 0);
	globalContainer->gfx->drawSprite(menuStartW+48, 172, globalContainer->terrain, 128);
	globalContainer->gfx->drawSprite(menuStartW+96, 172, globalContainer->terrain, 259);

	globalContainer->gfx->drawSprite(menuStartW+0, 210, globalContainer->ressources, 19);
	globalContainer->gfx->drawSprite(menuStartW+32, 210, globalContainer->ressources, 2);
	globalContainer->gfx->drawSprite(menuStartW+64, 210, globalContainer->ressources, 34);
	globalContainer->gfx->drawSprite(menuStartW+96, 210, globalContainer->ressources, 44);

	globalContainer->gfx->drawSprite(menuStartW+0, 248, globalContainer->ressources, 54);
	globalContainer->gfx->drawSprite(menuStartW+32, 248, globalContainer->ressources, 59);
	globalContainer->gfx->drawSprite(menuStartW+64, 248, globalContainer->ressources, 64);

	int selected_x_position=0;
	int selected_y_position=0;
	if(terrainType==Grass)
	{
		selected_x_position=0;
		selected_y_position=172;
	}
	else if(terrainType==Sand)
	{
		selected_x_position=48;
		selected_y_position=172;
	}
	else if(terrainType==Water)
	{
		selected_x_position=96;
		selected_y_position=172;
	}
	else if(terrainType==Wheat)
	{
		selected_x_position=0;
		selected_y_position=210;
	}
	else if(terrainType==Trees)
	{
		selected_x_position=32;
		selected_y_position=210;
	}
	else if(terrainType==Stone)
	{
		selected_x_position=64;
		selected_y_position=210;
	}
	else if(terrainType==Algae)
	{
		selected_x_position=96;
		selected_y_position=210;
	}
	else if(terrainType==CherryTree)
	{
		selected_x_position=0;
		selected_y_position=248;
	}
	else if(terrainType==OrangeTree)
	{
		selected_x_position=32;
		selected_y_position=248;
	}
	else if(terrainType==PruneTree)
	{
		selected_x_position=64;
		selected_y_position=248;
	}
	else if(terrainType==NoTerrain)
	{
		selected_x_position=-1;
		selected_y_position=-1;
	}

	if(!(selected_x_position==-1))
		globalContainer->gfx->drawSprite(menuStartW+selected_x_position, selected_y_position, globalContainer->gamegui, 22);
	brush.draw(menuStartW, 294);
}



void MapEdit::drawTeamView()
{
	const int xpos=globalContainer->gfx->getW()-128;
	const int ypos=128+ 40;
	int count=0;
	for(int x=0; x<16; ++x)
	{
		const int nypos=ypos + x*18;
		if(game.teams[x])
		{
			count++;
			globalContainer->gfx->drawFilledRect(xpos, nypos, 16, 16, Color(game.teams[x]->colorR, game.teams[x]->colorG, game.teams[x]->colorB));
			drawMultipleSelection(xpos+20, nypos+4, team_view_selector_keys, selector_positions[x]);
		}
	}
	globalContainer->gfx->drawFilledRect(xpos, ypos + count*18 + 24, 32, 32, Color(75,0,200));
	globalContainer->gfx->drawRect(xpos, ypos + count*18 + 24, 32, 32, Color::white);
	globalContainer->gfx->drawFilledRect(xpos+15, ypos + count*18 + 24 + 2, 2, 28, Color::white);
	globalContainer->gfx->drawFilledRect(xpos + 2, ypos + count*18 + 24 + 15, 28, 2, Color::white);

	globalContainer->gfx->drawFilledRect(xpos + 40, ypos + count*18 + 24, 32, 32, Color(75,0,200));
	globalContainer->gfx->drawRect(xpos + 40, ypos + count*18 + 24, 32, 32, Color::white);
	globalContainer->gfx->drawFilledRect(xpos + 40 + 2, ypos + count*18 + 24 + 15, 28, 2, Color::white);
}



void MapEdit::drawMultipleSelection(int x, int y, std::vector<std::string>& strings, unsigned int pos)
{
	globalContainer->gfx->drawString(x, y, globalContainer->littleFont, Toolkit::getStringTable()->getString(strings[pos].c_str()));
}



void MapEdit::drawTeamSelector(int x, int y)
{
	for(int n=0; n<16; ++n)
	{
		const int xpos = x + (n%6)*16;
		const int ypos = y + (n/6)*16;
		if(game.teams[n])
		{
			if(team==n)
				globalContainer->gfx->drawFilledRect(xpos, ypos, 16, 16, Color(game.teams[n]->colorR, game.teams[n]->colorG, game.teams[n]->colorB, 128));
			else
				globalContainer->gfx->drawFilledRect(xpos, ypos, 16, 16, Color(game.teams[n]->colorR, game.teams[n]->colorG, game.teams[n]->colorB));

		}
	}
}



void MapEdit::drawUnitOnMap()
{
	int type;
	if(selectedUnit==Worker)
		type=WORKER;
	else if(selectedUnit==Warrior)
		type=WARRIOR;
	else if(selectedUnit==Explorer)
		type=EXPLORER;
	
	int level=unit_level;
	
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
	unitSprite->setBaseColor(game.teams[team]->colorR, game.teams[team]->colorG, game.teams[team]->colorB);

	globalContainer->gfx->drawSprite(px, py, unitSprite, imgid);

	if (isRoom)
		globalContainer->gfx->drawRect(px, py, pw, ph, 255, 255, 255, 128);
	else
		globalContainer->gfx->drawRect(px, py, pw, ph, 255, 0, 0, 128);
}


void MapEdit::register_buttons()
{
	add_area("minimap drag start", Rectangle(globalContainer->gfx->getW()-114, 14, 100, 100), "minimap drag start", true);
	add_area("building view button", Rectangle(globalContainer->gfx->getW()-128, 128, 32, 32), "switch to building view", true);
	add_area("flag view button", Rectangle(globalContainer->gfx->getW()-96, 128, 32, 32), "switch to flag view", true);
	add_area("terrain view button", Rectangle(globalContainer->gfx->getW()-64, 128, 32, 32), "switch to terrain view", true);
	add_area("teams view button", Rectangle(globalContainer->gfx->getW()-32, 128, 32, 32), "switch to teams view", true);

	int width = (128 / 2);
	for (size_t i=0; i<buildingsChoiceName.size(); i++)
	{
		std::string &type = buildingsChoiceName[i];
		int x=((i % 2)*width)+globalContainer->gfx->getW()-128;
		int y=((i / 2)*46)+128+32;
		add_area("building "+type, Rectangle(x, y, 40, 40), "set place building selection "+type, true);
	}
	add_area("building level 1", Rectangle(globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-36, 32, 32), "switch to building level 1", true);
	add_area("building level 2", Rectangle(globalContainer->gfx->getW()-80, globalContainer->gfx->getH()-36, 32, 32), "switch to building level 2", true);
	add_area("building level 3", Rectangle(globalContainer->gfx->getW()-32, globalContainer->gfx->getH()-36, 32, 32), "switch to building level 3", true);
	add_area("menubutton", Rectangle(globalContainer->gfx->getW()-160, 0, 32, 32), "open menu screen", true);

 	const int YPOS_FLAG=160;
 	const int YPOS_BRUSH=YPOS_FLAG+56;
	width = (128 / 3);
	for (size_t i=0; i<flagsChoiceName.size(); i++)
	{
		std::string &type = flagsChoiceName[i];
		int x=((i % 3)*width)+globalContainer->gfx->getW()-128;
		int y=((i / 3)*46)+128+32;
		add_area("building "+type, Rectangle(x, y, 40, 40), "set place building selection "+type, false);
	}
	add_area("forbidden zone", Rectangle(globalContainer->gfx->getW()-128+8, YPOS_BRUSH, 32, 32), "select forbidden zone", false);
	add_area("guard zone", Rectangle(globalContainer->gfx->getW()-128+48, YPOS_BRUSH, 32, 32), "select guard zone", false);
	add_area("clearing zone", Rectangle(globalContainer->gfx->getW()-128+88, YPOS_BRUSH, 32, 32), "select clearing zone", false);
	add_area("zone selection", Rectangle(globalContainer->gfx->getW()-128, YPOS_BRUSH+40, 128, 96), "handle zone click", false);

	add_area("select worker", Rectangle(globalContainer->gfx->getW()-128 + 8, 360, 38, 38), "select worker", false);
	add_area("select explorer", Rectangle(globalContainer->gfx->getW()-128 + 48, 360, 38, 38), "select explorer", false);
	add_area("select warrior", Rectangle(globalContainer->gfx->getW()-128 + 88, 360, 38, 38), "select warrior", false);

	int unitInfoStart=globalContainer->gfx->getH()-36;
	add_area("select unit level 1", Rectangle(globalContainer->gfx->getW()-128 + 0, unitInfoStart, 32, 32), "select unit level 1", false);
	add_area("select unit level 2", Rectangle(globalContainer->gfx->getW()-128 + 32, unitInfoStart, 32, 32), "select unit level 2", false);
	add_area("select unit level 3", Rectangle(globalContainer->gfx->getW()-128 + 64, unitInfoStart, 32, 32), "select unit level 3", false);
	add_area("select unit level 4", Rectangle(globalContainer->gfx->getW()-128 + 96, unitInfoStart, 32, 32), "select unit level 4", false);

	add_area("grass button", Rectangle(globalContainer->gfx->getW()-128, 172, 32, 32), "select grass", false);
	add_area("sand button", Rectangle(globalContainer->gfx->getW()-128+48, 172, 32, 32), "select sand", false);
	add_area("water button", Rectangle(globalContainer->gfx->getW()-128+96, 172, 32, 32), "select water", false);
	add_area("wheat button", Rectangle(globalContainer->gfx->getW()-128+0, 210, 32, 32), "select wheat", false);
	add_area("trees button", Rectangle(globalContainer->gfx->getW()-128+32, 210, 32, 32), "select trees", false);
	add_area("stone button", Rectangle(globalContainer->gfx->getW()-128+64, 210, 32, 32), "select stone", false);
	add_area("algae button", Rectangle(globalContainer->gfx->getW()-128+96, 210, 32, 32), "select algae", false);
	add_area("cherry tree button", Rectangle(globalContainer->gfx->getW()-128+0, 248, 32, 32), "select cherry tree", false);
	add_area("orange tree button", Rectangle(globalContainer->gfx->getW()-128+32, 248, 32, 32), "select orange tree", false);
	add_area("prune tree button", Rectangle(globalContainer->gfx->getW()-128+64, 248, 32, 32), "select prune tree", false);
	add_area("terrain selection", Rectangle(globalContainer->gfx->getW()-128, 294, 128, 96), "handle terrain click", false);

	const int xpos=globalContainer->gfx->getW()-128;
	for(int x=0; x<12; ++x)
	{
		const int ypos=128 + 40 + x*18;
		std::stringstream str;
		str<<x;
		add_area("select team "+str.str(), Rectangle(xpos+20, ypos, 108, 18), "click team "+str.str(), false);
		add_area("add team "+str.str(), Rectangle(globalContainer->gfx->getW()-128, ypos+ 24, 32, 32), "add team", false);
		add_area("remove team "+str.str(), Rectangle(globalContainer->gfx->getW()-128 + 40, ypos+ 24, 32, 32), "remove team", false);
		str.str("");
	}

	for(int n=0; n<12; ++n)
	{
		const int xpos = globalContainer->gfx->getW()-128 + 16 + (n%6)*16;
		const int ypos = globalContainer->gfx->getH()-74 + (n/6)*16;
		std::stringstream str;
		str<<n;
		add_area("select active team "+str.str(), Rectangle(xpos, ypos, 16, 16), "select active team "+str.str(), true);
		str.str("");
	}
}



int MapEdit::processEvent(SDL_Event& event)
{
	int returnCode=0;
	if (event.type==SDL_QUIT)
	{
		returnCode=-1;
	}
	else if(showingMenuScreen || showing_load || showing_save || showingScriptEditor)
	{
		delegateMenu(event);
		return 0;
	}
	else if(event.type==SDL_MOUSEMOTION)
	{
		mouseX=event.motion.x;
		mouseY=event.motion.y;
		if(is_dragging_minimap)
		{
			if(Rectangle(globalContainer->gfx->getW()-114, 14, 100, 100).is_in(mouseX, mouseY))
				perform_action("minimap drag motion");
			perform_action("scroll horizontal stop");
			perform_action("scroll vertical stop");
		}
		else if(is_dragging_zone)
		{
			if(Rectangle(0, 16, globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-16).is_in(mouseX, mouseY))
				perform_action("zone drag motion");
		}
		else if(is_dragging_terrain)
		{
			if(Rectangle(0, 16, globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-16).is_in(mouseX, mouseY))
				perform_action("terrain drag motion");
		}
		else
		{
			if(globalContainer->gfx->getW()-event.motion.x<15)
			{
				perform_action("scroll right");
			}
			else if(event.motion.x<15)
			{
				perform_action("scroll left");
			}
			else if(xspeed!=0)
			{
				perform_action("scroll horizontal stop");
			}
	
			if(globalContainer->gfx->getH()-event.motion.y<15)
			{
				perform_action("scroll down");
			}
			else if(event.motion.y<15)
			{
				perform_action("scroll up");
			}
			else if(yspeed!=0)
			{
				perform_action("scroll vertical stop");
			}
		}
	}
	else if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT)
	{
		std::string action=get_action(event.button.x, event.button.y, false);
		if(action=="nothing" && Rectangle(0, 16, globalContainer->gfx->getW()-128, globalContainer->gfx->getH()).is_in(mouseX, mouseY))
		{
			//The button wasn't clicked in any registered area
			if(selectionMode==PlaceBuilding)
				perform_action("place building");
			else if(selectionMode==PlaceZone)
				perform_action("zone drag start");
			else if(selectionMode==PlaceTerrain)
				perform_action("terrain drag start");
			else if(selectionMode==PlaceUnit)
				perform_action("place unit");
		}
		else
		{
			perform_action(action);
		}
	}
	else if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_RIGHT)
	{
		perform_action("unselect");
	}
	else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)
	{
		if(is_dragging_minimap)
			perform_action("minimap drag stop");
		if(is_dragging_zone)
			perform_action("zone drag end");
		if(is_dragging_terrain)
			perform_action("terrain drag end");
	}
	else if(event.type==SDL_KEYDOWN)
	{
		handleKeyPressed(event.key.keysym.sym, true);
	}
	else if(event.type==SDL_KEYUP)
	{
		handleKeyPressed(event.key.keysym.sym, false);
	}
	return returnCode;
}



void MapEdit::handleKeyPressed(SDLKey key, bool pressed)
{
	switch(key)
	{
		case SDLK_UP:
			if(pressed)
				perform_action("scroll up");
			else
				perform_action("scroll vertical stop");
			break;
		case SDLK_DOWN:
			if(pressed)
				perform_action("scroll down");
			else
				perform_action("scroll vertical stop");
			break;
		case SDLK_LEFT:
			if(pressed)
				perform_action("scroll left");
			else
				perform_action("scroll horizontal stop");
			break;
		case SDLK_RIGHT:
			if(pressed)
				perform_action("scroll right");
			else
				perform_action("scroll horizontal stop");
			break;
		case SDLK_a :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["akey"]);
			break;
		case SDLK_b :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["bkey"]);
			break;
		case SDLK_c :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["ckey"]);
			break;
		case SDLK_d :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["dkey"]);
			break;
		case SDLK_e :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["ekey"]);
			break;
		case SDLK_f :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["fkey"]);
			break;
		case SDLK_g :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["gkey"]);
			break;
		case SDLK_h :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["hkey"]);
			break;
		case SDLK_i :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["ikey"]);
			break;
		case SDLK_j :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["jkey"]);
			break;
		case SDLK_k :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["kkey"]);
			break;
		case SDLK_l :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["lkey"]);
			break;
		case SDLK_m :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["mkey"]);
			break;
		case SDLK_n :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["nkey"]);
			break;
		case SDLK_o :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["okey"]);
			break;
		case SDLK_p :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["pkey"]);
			break;
		case SDLK_q :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["qkey"]);
			break;
		case SDLK_r :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["rkey"]);
			break;
		case SDLK_s :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["skey"]);
			break;
		case SDLK_t :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["tkey"]);
			break;
		case SDLK_u :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["ukey"]);
			break;
		case SDLK_v :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["vkey"]);
			break;
		case SDLK_w :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["wkey"]);
			break;
		case SDLK_x :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["xkey"]);
			break;
		case SDLK_y :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["ykey"]);
			break;
		case SDLK_z :
			if(pressed)
				perform_action(globalContainer->settings.editor_keyboard_shortcuts["zkey"]);
			break;
		default:
		break;
	}
}



void MapEdit::perform_action(const std::string& action)
{
	if(action.find("&")!=std::string::npos)
	{
		int pos=action.find("&");
		perform_action(action.substr(0, pos));
		perform_action(action.substr(pos+1, action.size()-pos-1));
	}
	if (action=="scroll left")
	{
		xspeed=-1;
	}
	else if(action=="scroll right")
	{
		xspeed=1;
	}
	else if(action=="scroll horizontal stop")
	{
		xspeed=0;
	}
	else if(action=="scroll up")
	{
		yspeed=-1;
	}
	else if(action=="scroll down")
	{
		yspeed=1;
	}
	else if(action=="scroll vertical stop")
	{
		yspeed=0;
	}
	else if(action=="switch to building view")
	{
		perform_action("unselect");
		panelmode=AddBuildings;
		disableTeamView();
		disableFlagView();
		disableTerrainView();
		enableBuildingsView();
	}
	else if(action=="switch to flag view")
	{
		perform_action("unselect");
		panelmode=AddFlagsAndZones;
		disableTeamView();
		disableBuildingsView();
		disableTerrainView();
		enableFlagView();
	}
	else if(action=="switch to terrain view")
	{
		perform_action("unselect");
		panelmode=Terrain;
		disableTeamView();
		disableBuildingsView();
		disableFlagView();
		enableTerrainView();
	}
	else if(action=="switch to teams view")
	{
		perform_action("unselect");
		panelmode=Teams;
		disableBuildingsView();
		disableFlagView();
		disableTerrainView();
		enableTeamView();
	}
	else if(action.substr(0, 29)=="set place building selection ")
	{
		std::string type=action.substr(29, action.size()-29);
		selectionName=type;
		selectionMode=PlaceBuilding;
	}
	else if(action=="unselect")
	{
		selectionName="";
		brush.unselect();
		selectionMode=PlaceNothing;
		brushType=NoBrush;
		terrainType=NoTerrain;
		selectedUnit=NoUnit;
	}
	else if(action=="minimap drag start")
	{
		is_dragging_minimap=true;
		minimapMouseToPos(mouseX-globalContainer->gfx->getW()+128, mouseY, &viewportX, &viewportY, true);
	}
	else if(action=="minimap drag motion")
	{
		minimapMouseToPos(mouseX-globalContainer->gfx->getW()+128, mouseY, &viewportX, &viewportY, true);
	}
	else if(action=="minimap drag stop")
	{
		is_dragging_minimap=false;
	}
	else if(action=="place building")
	{
		int typeNum=globalContainer->buildingsTypes.getTypeNum(selectionName, building_level, false);
		if(!is_upgradable(IntBuildingType::shortNumberFromType(selectionName)))
			typeNum = globalContainer->buildingsTypes.getTypeNum(selectionName, 0, false);
		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		int tempX, tempY, x, y;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);

		if (game.checkRoomForBuilding(tempX, tempY, bt, &x, &y, team, false))
		{
			game.addBuilding(x, y, typeNum, team);
			if (selectionName=="swarm")
			{
				if (game.teams[team]->startPosSet<3)
				{
					game.teams[team]->startPosX=tempX;
					game.teams[team]->startPosY=tempY;
					game.teams[team]->startPosSet=3;
				}
			}
			else
			{
				if (game.teams[team]->startPosSet<2)
				{
					game.teams[team]->startPosX=tempX;
					game.teams[team]->startPosY=tempY;
					game.teams[team]->startPosSet=2;
				}
			}
			game.regenerateDiscoveryMap();
			hasMapBeenModified = true;
		}
	}
	else if(action=="switch to building level 1")
	{
		building_level=0;
	}
	else if(action=="switch to building level 2")
	{
		building_level=1;
	}
	else if(action=="switch to building level 3")
	{
		building_level=2;
	}
	else if(action=="open menu screen")
	{
		perform_action("unselect");
		perform_action("scroll horizontal stop");
		perform_action("scroll vertical stop");
		menuscreen=new MapEditMenuScreen;
		showingMenuScreen=true;
	}
	else if(action=="close menu screen")
	{
		delete menuscreen;
		menuscreen=NULL;
		showingMenuScreen=false;
	}
	else if(action=="open load screen")
	{
		perform_action("unselect");
		perform_action("scroll horizontal stop");
		perform_action("scroll vertical stop");
		loadsavescreen=new LoadSaveScreen("maps", "map", true, game.session.getMapNameC(), glob2FilenameToName, glob2NameToFilename);
		showing_load=true;
	}
	else if(action=="close load screen")
	{
		delete loadsavescreen;
		showing_load=false;
		loadsavescreen=NULL;
	}
	else if(action=="open save screen")
	{
		perform_action("unselect");
		perform_action("scroll horizontal stop");
		perform_action("scroll vertical stop");
		loadsavescreen=new LoadSaveScreen("maps", "map", false, game.session.getMapNameC(), glob2FilenameToName, glob2NameToFilename);
		showing_save=true;
	}
	else if(action=="close save screen")
	{
		delete loadsavescreen;
		showing_save=false;
		loadsavescreen=NULL;
	}
	else if(action=="open script editor")
	{
		perform_action("unselect");
		perform_action("scroll horizontal stop");
		perform_action("scroll vertical stop");
		script_editor=new ScriptEditorScreen(&game.script, &game);
		showingScriptEditor=true;
	}
	else if(action=="close script editor")
	{
		delete script_editor;
		showingScriptEditor=false;
		script_editor=NULL;
	}
	else if(action=="select forbidden zone")
	{
		brushType = ForbiddenBrush;
		selectionMode=PlaceZone;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select clearing zone")
	{
		brushType = ClearAreaBrush;
		selectionMode=PlaceZone;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select guard zone")
	{
		brushType = GuardAreaBrush;
		selectionMode=PlaceZone;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="handle zone click")
	{
		if(brushType==NoBrush)
			brushType=ForbiddenBrush;
		brush.handleClick(mouseX-globalContainer->gfx->getW()+128, mouseY-256);
	}
	else if(action=="zone drag start")
	{
		is_dragging_zone=true;
		handleBrushClick(mouseX, mouseY);
	}
	else if(action=="zone drag motion")
	{
		handleBrushClick(mouseX, mouseY);
	}
	else if(action=="zone drag end")
	{
		is_dragging_zone=false;
		last_placement_x=-1;
		last_placement_y=-1;
	}
	else if(action=="select grass")
	{
		terrainType=Grass;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select sand")
	{
		terrainType=Sand;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select water")
	{
		terrainType=Water;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select wheat")
	{
		terrainType=Wheat;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select trees")
	{
		terrainType=Trees;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select stone")
	{
		terrainType=Stone;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select algae")
	{
		terrainType=Algae;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select cherry tree")
	{
		terrainType=CherryTree;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select orange tree")
	{
		terrainType=OrangeTree;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select prune tree")
	{
		terrainType=PruneTree;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="handle terrain click")
	{
		if(terrainType==NoTerrain)
			terrainType=Grass;
		brush.handleClick(mouseX-globalContainer->gfx->getW()+128, mouseY-294);
	}
	else if(action=="terrain drag start")
	{
		is_dragging_terrain=true;
		handleTerrainClick(mouseX, mouseY);
	}
	else if(action=="terrain drag motion")
	{
		handleTerrainClick(mouseX, mouseY);
	}
	else if(action=="terrain drag end")
	{
		is_dragging_terrain=false;
		last_placement_x=-1;
		last_placement_y=-1;
	}
	else if(action.substr(0, 11)=="click team ")
	{
		std::stringstream str;
		str<<action.substr(11, action.size()-11);
		unsigned int n;
		str>>n;
		selector_positions[n]++;
		if(selector_positions[n]==team_view_selector_keys.size())
		{
			selector_positions[n]=0;
		}
	}
	else if(action=="add team")
	{
		disableTeamView();
		if(game.session.numberOfTeam < 12)
			game.addTeam();
		enableTeamView();
	}
	else if(action=="remove team")
	{
		disableTeamView();
		if(game.session.numberOfTeam > 1)
			game.removeTeam();
		enableTeamView();
	}
	else if(action.substr(0, 19)=="select active team ")
	{
		std::stringstream str;
		str<<action.substr(19, action.size());
		int tmp;
		str>>tmp;
		if(game.teams[tmp])
		{
			team=tmp;
			renderMiniMap();
			game.map.computeLocalForbidden(team);
			game.map.computeLocalClearArea(team);
			game.map.computeLocalGuardArea(team);
		}
	}
	else if(action=="select worker")
	{
		perform_action("unselect");
		selectedUnit=Worker;
		selectionMode=PlaceUnit;
	}
	else if(action=="select warrior")
	{
		perform_action("unselect");
		selectedUnit=Warrior;
		selectionMode=PlaceUnit;
	}
	else if(action=="select explorer")
	{
		perform_action("unselect");
		selectedUnit=Explorer;
		selectionMode=PlaceUnit;
	}
	else if(action=="select unit level 1")
	{
		unit_level=0;
	}
	else if(action=="select unit level 2")
	{
		unit_level=1;
	}
	else if(action=="select unit level 3")
	{
		unit_level=2;
	}
	else if(action=="select unit level 4")
	{
		unit_level=3;
	}
	else if(action=="place unit")
	{
		int type;
		if(selectedUnit==Worker)
			type=WORKER;
		else if(selectedUnit==Warrior)
			type=WARRIOR;
		else if(selectedUnit==Explorer)
			type=EXPLORER;
		int level=unit_level;

		int x;
		int y;
		game.map.displayToMapCaseAligned(mouseX, mouseY, &x, &y, viewportX, viewportY);

		Unit *unit=game.addUnit(x, y, team, type, level, rand()%256, 0, 0);
		if (unit)
		{
			if (game.teams[team]->startPosSet<1)
			{
				game.teams[team]->startPosX=viewportX;
				game.teams[team]->startPosY=viewportY;
				game.teams[team]->startPosSet=1;
			}
			game.regenerateDiscoveryMap();
		}
	}
	if(action=="quit editor")
	{
		do_quit=true;
	}
}



void MapEdit::delegateMenu(SDL_Event& event)
{
	if(showingMenuScreen)
	{
		menuscreen->translateAndProcessEvent(&event);
		switch (menuscreen->endValue)
		{
			case MapEditMenuScreen::LOAD_MAP:
			{
				perform_action("close menu screen");
				perform_action("open load screen");
			}
			break;
			case MapEditMenuScreen::SAVE_MAP:
			{
				perform_action("close menu screen");
				perform_action("open save screen");
			}
			break;
			case MapEditMenuScreen::OPEN_SCRIPT_EDITOR:
			{
				perform_action("close menu screen");
				perform_action("open script editor");
			}
			case MapEditMenuScreen::RETURN_EDITOR:
			{
				perform_action("close menu screen");
			}
			break;
			case MapEditMenuScreen::QUIT_EDITOR:
			{
				perform_action("close menu screen");
				perform_action("quit editor");
			}
			break;
		}
	}
	if(showing_load)
	{
		loadsavescreen->translateAndProcessEvent(&event);
		switch (loadsavescreen->endValue)
		{
			case LoadSaveScreen::OK:
			{
				load(loadsavescreen->getFileName());
				perform_action("close load screen");
			}
			break;
			case LoadSaveScreen::CANCEL:
			{
				perform_action("close load screen");
			}
			break;
		}
	}
	if(showing_save)
	{
		loadsavescreen->translateAndProcessEvent(&event);
		switch (loadsavescreen->endValue)
		{
			case LoadSaveScreen::OK:
			{
				save(loadsavescreen->getFileName(), loadsavescreen->getName());
				perform_action("close save screen");
			}
			case LoadSaveScreen::CANCEL:
			{
				perform_action("close save screen");
			}
		}
	}
	if(showingScriptEditor)
	{
		script_editor->translateAndProcessEvent(&event);
		switch(script_editor->endValue)
		{
			case ScriptEditorScreen::OK:
			case ScriptEditorScreen::CANCEL:
			{
				perform_action("close script editor");
			}
		}
	}
}



void MapEdit::activate_area(const std::string& name)
{
	button_areas[name].get<2>()=true;
}



void MapEdit::deactivate_area(const std::string& name)
{
	button_areas[name].get<2>()=false;
}



bool MapEdit::is_activated(const std::string& name)
{
	return button_areas[name].get<2>();
}



void MapEdit::add_area(const std::string& name, const Rectangle& area, const std::string& action, bool is_activated, bool on_release)
{
	button_areas[name]=boost::make_tuple(area, action, is_activated, on_release);
}



std::string MapEdit::get_action(int x, int y, bool is_release)
{
	for(std::map<std::string, boost::tuple<Rectangle, std::string, bool, bool> >::iterator i=button_areas.begin(); i!=button_areas.end(); ++i)
	{
		if(i->second.get<2>() && is_release==i->second.get<3>() && i->second.get<0>().is_in(x, y))
			return i->second.get<1>();
	}
	return "nothing";
}



void MapEdit::minimapMouseToPos(int mx, int my, int *cx, int *cy, bool forScreenViewport)
{
	// get data for minimap
	int mMax;
	int szX, szY;
	int decX, decY;
	Utilities::computeMinimapData(100, game.map.getW(), game.map.getH(), &mMax, &szX, &szY, &decX, &decY);

	mx-=14+decX;
	my-=14+decY;
	*cx=((mx*game.map.getW())/szX);
	*cy=((my*game.map.getH())/szY);
	*cx+=game.teams[team]->startPosX-(game.map.getW()/2);
	*cy+=game.teams[team]->startPosY-(game.map.getH()/2);
	if (forScreenViewport)
	{
		*cx-=((globalContainer->gfx->getW()-128)>>6);
		*cy-=((globalContainer->gfx->getH())>>6);
	}

	*cx&=game.map.getMaskW();
	*cy&=game.map.getMaskH();
}



void MapEdit::handleBrushClick(int mx, int my)
{
	// if we have an area over 32x32, which mean over 128 bytes, send it
// 	if (brushAccumulator.getAreaSurface() > 32*32)
// 	{
// 		sendBrushOrders();
// 	}
	// we add brush to accumulator
	int mapX, mapY;
	game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY,  viewportX, viewportY);
	if(last_placement_x==mapX && last_placement_y==mapY)
		return;
	int fig = brush.getFigure();
	brushAccumulator.applyBrush(&game.map, BrushApplication(mapX, mapY, fig));
	// we get coordinates
	int startX = mapX-BrushTool::getBrushDimX(fig);
	int startY = mapY-BrushTool::getBrushDimY(fig);
	int width  = BrushTool::getBrushWidth(fig);
	int height = BrushTool::getBrushHeight(fig);
	// we update local values
	if (brush.getType() == BrushTool::MODE_ADD)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					if (brushType == ForbiddenBrush)
					{
						game.map.getCase(x, y).forbidden |= (1<<team);
						game.map.localForbiddenMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), true);
					}
					else if (brushType == GuardAreaBrush)
					{
						game.map.getCase(x, y).guardArea |= (1<<team);
						game.map.localGuardAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), true);
					}
					else if (brushType == ClearAreaBrush)
					{
						game.map.getCase(x, y).clearArea |= (1<<team);
						game.map.localClearAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), true);
					}
					else
						assert(false);
				}
	}
	else if (brush.getType() == BrushTool::MODE_DEL)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					if (brushType == ForbiddenBrush)
					{
						game.map.getCase(x, y).forbidden ^= game.map.getCase(x, y).forbidden & (1<<team);
						game.map.localForbiddenMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), false);
					}
					else if (brushType == GuardAreaBrush)
					{
						game.map.getCase(x, y).guardArea ^= game.map.getCase(x, y).guardArea & (1<<team);
						game.map.localGuardAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), false);
					}
					else if (brushType == ClearAreaBrush)
					{
						game.map.getCase(x, y).clearArea ^= game.map.getCase(x, y).clearArea & (1<<team);
						game.map.localClearAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), false);
					}
					else
						assert(false);
				}
	}
	else
		assert(false);
	last_placement_x=mapX;
	last_placement_y=mapY;
}



void MapEdit::handleTerrainClick(int mx, int my)
{
	// if we have an area over 32x32, which mean over 128 bytes, send it
// 	if (brushAccumulator.getAreaSurface() > 32*32)
// 	{
// 		sendBrushOrders();
// 	}
	// we add brush to accumulator
	int mapX, mapY;
	game.map.displayToMapCaseAligned(mx+(terrainType>Water ? 0 : 16), my+(terrainType>Water ? 0 : 16), &mapX, &mapY,  viewportX, viewportY);
	if(last_placement_x==mapX && last_placement_y==mapY)
		return;
	int fig = brush.getFigure();
	brushAccumulator.applyBrush(&game.map, BrushApplication(mapX, mapY, fig));
	// we get coordinates
	int startX = mapX-BrushTool::getBrushDimX(fig);
	int startY = mapY-BrushTool::getBrushDimY(fig);
	int width  = BrushTool::getBrushWidth(fig);
	int height = BrushTool::getBrushHeight(fig);
	// we update local values
	if (brush.getType() == BrushTool::MODE_ADD)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					if (terrainType == Grass)
					{
						game.removeUnitAndBuildingAndFlags(x, y, 3, Game::DEL_BUILDING | Game::DEL_UNIT);
						game.map.setNoRessource(x, y, 3);
						game.map.setUMatPos(x, y, GRASS, 1);
					}
					else if (terrainType == Sand)
					{
						game.removeUnitAndBuildingAndFlags(x, y, 1, Game::DEL_BUILDING | Game::DEL_UNIT);
						game.map.setNoRessource(x, y, 1);
						game.map.setUMatPos(x, y, SAND, 1);
					}
					else if (terrainType == Water)
					{
						game.removeUnitAndBuildingAndFlags(x, y, 3, Game::DEL_BUILDING | Game::DEL_UNIT);
						game.map.setNoRessource(x, y, 3);
						game.map.setUMatPos(x, y, WATER, 1);
					}
					else if (terrainType == Wheat)
					{
						if(game.map.isRessourceAllowed(x, y, CORN))
							game.map.setRessource(x, y, CORN, 1);
					}
					else if (terrainType == Trees)
					{
						if(game.map.isRessourceAllowed(x, y, WOOD))
							game.map.setRessource(x, y, WOOD, 1);
					}
					else if (terrainType == Stone)
					{
						if(game.map.isRessourceAllowed(x, y, STONE))
							game.map.setRessource(x, y, STONE, 1);
					}
					else if (terrainType == Algae)
					{
						if(game.map.isRessourceAllowed(x, y, ALGA))
							game.map.setRessource(x, y, ALGA, 1);
					}
					else if (terrainType == CherryTree)
					{
						if(game.map.isRessourceAllowed(x, y, CHERRY))
							game.map.setRessource(x, y, CHERRY, 1);
					}
					else if (terrainType == OrangeTree)
					{
						if(game.map.isRessourceAllowed(x, y, ORANGE))
							game.map.setRessource(x, y, ORANGE, 1);
					}
					else if (terrainType == PruneTree)
					{
						if(game.map.isRessourceAllowed(x, y, PRUNE))
							game.map.setRessource(x, y, PRUNE, 1);
					}
				}
	}
	else if (brush.getType() == BrushTool::MODE_DEL)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					if (terrainType == Sand || terrainType == Water)
					{
						game.map.setUMatPos(x, y, GRASS, 1);
						game.map.setNoRessource(x, y, 3);
					}
					else if (terrainType == Wheat)
					{
						if(game.map.isRessourceTakeable(x, y, CORN))
							game.map.setNoRessource(x, y, 1);
					}
					else if (terrainType == Trees)
					{
						if(game.map.isRessourceTakeable(x, y, WOOD))
							game.map.setNoRessource(x, y, 1);
					}
					else if (terrainType == Stone)
					{
						if(game.map.isRessourceTakeable(x, y, STONE))
							game.map.setNoRessource(x, y, 1);
					}
					else if (terrainType == Algae)
					{
						if(game.map.isRessourceTakeable(x, y, ALGA))
							game.map.setNoRessource(x, y, 1);
					}
					else if (terrainType == CherryTree || terrainType == OrangeTree || terrainType == PruneTree)
					{
						if(game.map.isRessourceTakeable(x, y, CHERRY) || game.map.isRessourceTakeable(x, y, ORANGE) || game.map.isRessourceTakeable(x, y, PRUNE))
							game.map.setNoRessource(x, y, 1);
					}
				}
	}
	else
		assert(false);
	last_placement_x=mapX;
	last_placement_y=mapY;
}


MapEditMenuScreen::MapEditMenuScreen() : OverlayScreen(globalContainer->gfx, 320, 260)
{
	addWidget(new TextButton(0, 10, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "data/gfx/gamegui", 26, 27, "menu", Toolkit::getStringTable()->getString("[load map]"), LOAD_MAP));
	addWidget(new TextButton(0, 60, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "data/gfx/gamegui", 26, 27, "menu", Toolkit::getStringTable()->getString("[save map]"), SAVE_MAP));
	addWidget(new TextButton(0, 110, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "data/gfx/gamegui", 26, 27, "menu", Toolkit::getStringTable()->getString("[open script editor]"), OPEN_SCRIPT_EDITOR, 27));
	addWidget(new TextButton(0, 160, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "data/gfx/gamegui", 26, 27, "menu", Toolkit::getStringTable()->getString("[quit the editor]"), QUIT_EDITOR));
	addWidget(new TextButton(0, 210, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "data/gfx/gamegui", 26, 27, "menu", Toolkit::getStringTable()->getString("[return to editor]"), RETURN_EDITOR, 27));
	dispatchInit();
}

void MapEditMenuScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		endValue=par1;
}

