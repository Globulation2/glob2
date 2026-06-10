// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <iostream>
#include <fstream>

#include "AICastor.h"
#include "AINicowar.h"

#include <assert.h>
#include <string.h>

#include <set>
#include <string>
#include <functional>
#include <algorithm>
#include <sstream>
#include <cmath>

#include <FileManager.h>
#include <GraphicContext.h>

#include "BuildingType.h"
#include "DatasetWriter.h"
#include "Game.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Order.h"
#include "Unit.h"
#include "UnitSkin.h"
#include "Integrity.h"
#include "Utilities.h"
#include "GameGUI.h"
#include "SDLCompat.h"

#include "MapEdit.h"

#include "Brush.h"
#include "DynamicClouds.h"
#include "Bullet.h"
#include "TextStream.h"
#include "FertilityCalculatorDialog.h"

#include "ReplayWriter.h"

#include "GameRenderInternal.h"

// Terrain, resource, and area rendering. Split from Game_render.cpp.


// TODO: WATER_TILE_SIZE is hardcoded to the dimensions of data/gfx/water and would
// silently break if that asset is ever resized. Could be replaced with
// terrainWater->getW(0) / getH(0), but that relies on the sprite being loaded with
// a valid frame 0, and nothing here or at the load site (GlobalContainer::load)
// validates that. If terrainWater fails to load or reports zero size, water tiles
// silently fail to render -- oceans and lakes look visibly broken but the game
// otherwise plays normally, with no log or crash to flag the asset problem.
// The right fix is asset validation at load time (covering ~30 sprites loaded the
// same way in GlobalContainer::load), not a per-render guard here.
void Game::drawMapWater(int sw, int sh, int viewportX, int viewportY, int time)
{
	// Tile size of the data/gfx/water sprite, in pixels.
	static const int WATER_TILE_SIZE = 512;
	int waterStartX = -(((viewportX<<5)+time/2) % WATER_TILE_SIZE);
	int waterStartY = -((viewportY<<5) % WATER_TILE_SIZE);
	for (int y=waterStartY; y<sh; y += WATER_TILE_SIZE)
		for (int x=waterStartX; x<sw; x += WATER_TILE_SIZE)
			globalContainer->gfx->drawSprite(x, y, globalContainer->terrainWater, 0);
	globalContainer->gfx->finishDrawingSprite(globalContainer->terrainWater, 255);
}

void Game::drawMapTerrain(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	// we draw the terrains, eventually with debug rects:
	for (int y=top; y<=bot; y++)
		for (int x=left; x<=right; x++)
			if (
					map.isMapPartiallyDiscovered(
							x+viewportX-1,
							y+viewportY-1,
							x+viewportX+1,
							y+viewportY+1,
							visibleTeams) ||
				((drawOptions & DRAW_WHOLE_MAP) != 0))
			{
				// draw terrain
				int id=map.getTerrain(x+viewportX, y+viewportY);
				Sprite *sprite;
				if (id<272)
				{
					sprite=globalContainer->terrain;
				}
				else
				{
					assert(false); // Now there shouldn't be any more ressources on "terrain".
					sprite=globalContainer->ressources;
					id-=272;
				}
				if ((id < 256) || (id >= 256+16))
					globalContainer->gfx->drawSprite(x<<5, y<<5, sprite, id);
			}
	globalContainer->gfx->finishDrawingSprite(globalContainer->terrain, 255);
}

void Game::drawMapRessources(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	for (int y=top; y<=bot; y++)
		for (int x=left; x<=right; x++)
			if (
				map.isMapPartiallyDiscovered(
						x+viewportX-1,
						y+viewportY-1,
						x+viewportX+1,
						y+viewportY+1,
						visibleTeams) ||
				((drawOptions & DRAW_WHOLE_MAP) != 0))
			{
				const auto& r = map.getRessource(x+viewportX, y+viewportY);
				if (r.type!=NO_RES_TYPE)
				{
					Sprite *sprite=globalContainer->ressources;
					int type=r.type;
					int amount=r.amount;
					int variety=r.variety;
					const RessourceType *rt=globalContainer->ressourcesTypes.get(type);
					int imgid=rt->gfxId+(variety*rt->sizesCount)+amount;
					if (!rt->eternal)
						imgid--;
					int dx=(sprite->getW(imgid)-32)>>1;
					int dy=(sprite->getH(imgid)-32)>>1;
					assert(type>=0);
					assert(type<(int)globalContainer->ressourcesTypes.size());
					assert(amount>=0);
					assert(amount<=rt->sizesCount);
					assert(variety>=0);
					assert(variety<rt->varietiesCount);
					globalContainer->gfx->drawSprite((x<<5)-dx, (y<<5)-dy, sprite, imgid);
				}
			}
	globalContainer->gfx->finishDrawingSprite(globalContainer->ressources, 255);
}

void Game::drawMapDebugAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	if (DEBUG_RENDER_GRADIENTS)
		for (int y=top-1; y<=bot; y++)
			for (int x=left-1; x<=right; x++)
			{
				//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, ((AICastor *)players[1]->ai->aiImplementation)->wheatCareMap[0][(x+viewportX)+(y+viewportY)*map.w]);
				//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, ((AICastor *)players[1]->ai->aiImplementation)->notGrassMap[(x+viewportX)+(y+viewportY)*map.w]);
//				globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, map.guardAreasGradient[0][1][(x+viewportX)+(y+viewportY)*map.w]);
//				globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, ((Nicowar::AINicowar*)players[3]->ai->aiImplementation)->getGradientManager().getGradient(Nicowar::Gradient::VillageCenter, Nicowar::Gradient::Resource).getHeight(x+viewportX, y+viewportY));
				//((AICastor *)players[0].ai->aiImplementation)->wheatCareMap
			}
				//if (map.getForbidden(x+viewportX, y+viewportY))
				//{
					//if (!map.isFreeForGroundUnit(x+viewportX, y+viewportY, 1, 1))
					//	globalContainer->gfx->drawRect(x<<5, y<<5, 32, 32, 255, 16, 32);
					//globalContainer->gfx->drawRect(2+(x<<5), 2+(y<<5), 28, 28, 255, 16, 32);
					//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, map.getGradient(1, 5, 0, x+viewportX, y+viewportY));
					//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, map.getGradient(0, STONE, 1, x+viewportX, y+viewportY));
					//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, map.forbiddenGradient[0][0][(x+viewportX)+(y+viewportY)*map.w]);
					//globalContainer->gfx->drawString((x<<5), (y<<5)+16, globalContainer->littleFont, ((x+viewportX)&(map.getMaskW())));
					//globalContainer->gfx->drawString((x<<5)+16, (y<<5)+8, globalContainer->littleFont, ((y+viewportY)&(map.getMaskH())));
				//}

	// We draw debug area:
	if (DEBUG_RENDER_GRADIENTS)
	{
		assert(teams[0]);
		Building *b=selectedBuilding;
		if (b)
			for (int y=top-1; y<=bot; y++)
				for (int x=left-1; x<=right; x++)
				{
					//if (map.warpDistMax(b->posX, b->posY, x+viewportX, y+viewportY)<16)
					{
						//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, "%d", map.getGradient(0, 6, 1, x+viewportX, y+viewportY));
						//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, "%d", map.warpDistMax(b->posX, b->posY, x+viewportX, y+viewportY));
						//int lx=(x+viewportX-b->posX+15+32)&31;
						//int ly=(y+viewportY-b->posY+15+32)&31;
						//globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, b->localGradient[1][lx+ly*32]);
						if(b->globalGradient[1])
							globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, b->globalGradient[1][(x+viewportX) + (y+viewportY)*map.w]);
						//globalContainer->gfx->drawString((x<<5), (y<<5)+10, globalContainer->littleFont, lx);
						//globalContainer->gfx->drawString((x<<5)+16, (y<<5)+10, globalContainer->littleFont, ly);
						//globalContainer->gfx->drawString((x<<5), (y<<5)+16, globalContainer->littleFont, "%d", x+viewportX);
						//globalContainer->gfx->drawString((x<<5)+16, (y<<5)+16, globalContainer->littleFont, "%d", y+viewportY);
						//globalContainer->gfx->drawString((x<<5), (y<<5)+16, globalContainer->littleFont, "%d", x+viewportX-b->posX+16);
						//globalContainer->gfx->drawString((x<<5)+16, (y<<5)+16, globalContainer->littleFont, "%d", y+viewportY-b->posY+16);
					}
				}
	}

	// We draw debug area:
	if (DEBUG_RENDER_GRADIENTS)
		if (selectedUnit && selectedUnit->verbose)
		{
			//assert(teams[0]);
			Building *b=selectedUnit->attachedBuilding;
			//b=teams[0]->myBuildings[21];
			//if (teams[0]->virtualBuildings.size())
			//	b=*teams[0]->virtualBuildings.begin();
			if (b && b->localRessources[1])
				for (int y=top-1; y<=bot; y++)
					for (int x=left-1; x<=right; x++)
						if (map.warpDistMax(b->posX, b->posY, x+viewportX, y+viewportY)<16)
						{
							int lx=(x+viewportX-b->posX+15)&31;
							int ly=(y+viewportY-b->posY+15)&31;
							globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, b->localRessources[1][lx+ly*32]);
						}
		}

	// We draw debug area:
	//if (selectedUnit && selectedUnit->verbose)
	if (selectedBuilding && selectedBuilding->verbose)
	{
		//Building *b=NULL;
		Building *b=selectedBuilding;
		//Building *b=selectedUnit->attachedBuilding;

		//assert(teams[0]);
		//Building *b=teams[0]->myBuildings[0];
		//if (teams[0]->virtualBuildings.size())
		//	b=*teams[0]->virtualBuildings.begin();

		int w=map.getW();
		if (b)
			for (int y=top-1; y<=bot; y++)
				for (int x=left-1; x<=right; x++)
				{
					if (b->verbose==1 || b->verbose==2)
					{
						if (b->globalGradient[b->verbose&1])
							globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont,
								b->globalGradient[b->verbose&1][((x+viewportX)&(map.getMaskW()))+((y+viewportY)&(map.getMaskH()))*w]);
					}
					else if ((b->verbose==3 || b->verbose==4) && map.isInLocalGradient(x+viewportX, y+viewportY, b->posX, b->posY))
					{
						int lx=(x+viewportX-b->posX+15)&31;
						int ly=(y+viewportY-b->posY+15)&31;
						if (!b->dirtyLocalGradient[b->verbose&1])
							globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, b->localGradient[b->verbose&1][lx+ly*32]);
					}

					globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 192, 192, 192));
					globalContainer->gfx->drawString((x<<5), (y<<5)+16, globalContainer->littleFont, (x+viewportX+map.getW())&(map.getMaskW()));
					globalContainer->gfx->drawString((x<<5)+16, (y<<5)+8, globalContainer->littleFont, (y+viewportY+map.getH())&(map.getMaskH()));
					globalContainer->littleFont->popStyle();
				}

	}
}

/**
 * Draws the visible (viewport) part of the given map
 */
void Game::drawMapAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	static int areaAnimationTick = 0;

	if ((drawOptions & DRAW_AREA) != 0 && (!globalContainer->replaying || globalContainer->replayShowAreas))
	{
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, &map, &Map::isForbiddenInDisplayedView, areaAnimationTick, ForbiddenArea);
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, &map, &Map::isGuardAreaInDisplayedView, areaAnimationTick, GuardArea);
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, &map, &Map::isClearAreaInDisplayedView, areaAnimationTick, ClearingArea);
		for (int y=top; y<bot; y++)
			for (int x=left; x<right; x++)
			{
				if((drawOptions & DRAW_NO_RESSOURCE_GROWTH_AREAS) != 0)
				{
					if(!map.canRessourcesGrow(x+viewportX, y+viewportY))
					{
						globalContainer->gfx->drawLine((x<<5), 8+(y<<5), 32+(x<<5), 8+(y<<5), 128, 64, 0);
						globalContainer->gfx->drawLine((x<<5), 16+(y<<5), 32+(x<<5), 16+(y<<5), 128, 64, 0);
						globalContainer->gfx->drawLine((x<<5), 24+(y<<5), 32+(x<<5), 24+(y<<5), 128, 64, 0);
//						globalContainer->gfx->drawLine((x<<5), 32+(y<<5), 32+(x<<5), 32+(y<<5), 128, 64, 0);

						if (map.canRessourcesGrow(x+viewportX, y+viewportY-1))
							globalContainer->gfx->drawHorzLine((x<<5), (y<<5), 32, 255, 128, 0);
						if (map.canRessourcesGrow(x+viewportX, y+viewportY+1))
							globalContainer->gfx->drawHorzLine((x<<5), 32+(y<<5), 32, 255, 128, 0);

						if (map.canRessourcesGrow(x+viewportX-1, y+viewportY))
							globalContainer->gfx->drawVertLine((x<<5), (y<<5), 32, 255, 128, 0);
						if (map.canRessourcesGrow(x+viewportX+1, y+viewportY))
							globalContainer->gfx->drawVertLine(32+(x<<5), (y<<5), 32, 255, 128, 0);
						}
				}
			}
		areaAnimationTick++;
	}
}

/**
 * Draws the visible (viewport) part of the given map
 */
void Game::drawMapArea(int left, int top, int right, int bot, int sw,
		int sh, int viewportX, int viewportY, int localTeam,
		Uint32 drawOptions, Map * map, bool (Map::*mapIs)(int, int) const, int areaAnimationTick,
		AreaType areaType)
{
	Sprite* sprite;
	GAGCore::Color c;
	switch (areaType)
	{
		case ClearingArea: sprite = globalContainer->areaClearing; c = GAGCore::Color(255,255,0); break;
		case ForbiddenArea: sprite = globalContainer->areaForbidden; c = GAGCore::Color(255,0,0); break;
		case GuardArea: sprite = globalContainer->areaGuard; c = GAGCore::Color(0,0,255); break;
		default: assert(false);
	}
	for (int y=top; y<bot; y++)
	{
		for (int x=left; x<right; x++)
		{
			if ((map->*mapIs)(x+viewportX, y+viewportY))
			{
				int randId = (x+viewportX) * 7919 + (y+viewportY) * 17;
				int frame = ((randId + areaAnimationTick) % (sprite->getFrameCount() * 2)) / 2;
				globalContainer->gfx->drawSprite((x<<5), (y<<5), sprite, frame);

				if (!(map->*mapIs)(x+viewportX, y+viewportY-1))
					globalContainer->gfx->drawHorzLine((x<<5), (y<<5), 32, c);
				if (!(map->*mapIs)(x+viewportX, y+viewportY+1))
					globalContainer->gfx->drawHorzLine((x<<5), 32+(y<<5), 32, c);

				if (!(map->*mapIs)(x+viewportX-1, y+viewportY))
					globalContainer->gfx->drawVertLine((x<<5), (y<<5), 32, c);
				if (!(map->*mapIs)(x+viewportX+1, y+viewportY))
					globalContainer->gfx->drawVertLine(32+(x<<5), (y<<5), 32, c);
			}
		}
	}
	globalContainer->gfx->finishDrawingSprite(sprite, 255);
}

void Game::drawMapScriptAreas(int left, int top, int right, int bot, int viewportX, int viewportY)
{
	for (int y=top; y<bot; y++)
		for (int x=left; x<right; x++)
		{
			std::stringstream str;
			for(int n=0; n<9; ++n)
			{
				if(map.isPointSet(n, x+viewportX, y+viewportY))
				{
					str.str("");
					str<<n+1;
					globalContainer->gfx->drawString((x<<5)+(n%3)*10, (y<<5)+(n/3)*10, globalContainer->littleFont, str.str());

					globalContainer->gfx->drawHorzLine((x<<5), (y<<5), 32, 64, 255, 255);
					globalContainer->gfx->drawHorzLine((x<<5), 32+(y<<5), 32, 64, 255, 255);

					globalContainer->gfx->drawVertLine((x<<5), (y<<5), 32, 64, 255, 255);
					globalContainer->gfx->drawVertLine(32+(x<<5), (y<<5), 32, 64, 255, 255);
				}
			}
		}
}
