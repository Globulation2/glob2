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

#include "NetMessage.h"

#include "ReplayWriter.h"

#define BULLET_IMGID 0

#define MIN_MAX_PRESIGE 500
#define TEAM_MAX_PRESTIGE 150

// Map and unit/building rendering. Split out of Game.cpp.


void Game::drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, int secondActLength, Uint8 r, Uint8 g, Uint8 b, Uint8 r2, Uint8 g2, Uint8 b2, int barWidth)
{
	assert(maxLength>=0);
	assert(maxLength<65536);
	assert(actLength<=maxLength);

	if ((orientation==LEFT_TO_RIGHT) || (orientation==RIGHT_TO_LEFT))
	{
		/*globalContainer->gfx->drawHorzLine(x, y, maxLength*3+1, 32, 32, 32);
		globalContainer->gfx->drawHorzLine(x, y+barWidth+1, maxLength*3+1, 32, 32, 32);
		for (int i=0; i<maxLength+1; i++)
			globalContainer->gfx->drawVertLine(x+i*3, y+1, barWidth, 32, 32, 32);
		*/
		globalContainer->gfx->drawFilledRect(x, y, maxLength*3+1, barWidth+2, 0, 0, 0);

		if (orientation==LEFT_TO_RIGHT)
		{
			int i;
			for (i=0; i<actLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r, g, b);
			for (; i<secondActLength+actLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r2, g2, b2);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawRect(x+i*3, y, 4, barWidth+2, r/3, g/3, b/3);
		}
		else
		{
			int i;
			for (i=0; i<maxLength-secondActLength-actLength; i++)
				globalContainer->gfx->drawRect(x+i*3, y, 4, barWidth+2, r/3, g/3, b/3);
			for (; i<maxLength-actLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r2, g2, b2);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r, g, b);
		}
	}
	else if ((orientation==BOTTOM_TO_TOP) || (orientation==TOP_TO_BOTTOM))
	{
		/*globalContainer->gfx->drawVertLine(x, y, maxLength*3+1, 32, 32, 32);
		globalContainer->gfx->drawVertLine(x+barWidth+1, y, maxLength*3+1, 32, 32, 32);
		for (int i=0; i<maxLength+1; i++)
			globalContainer->gfx->drawHorzLine(x+1, y+i*3, barWidth, 32, 32, 32);
		*/
		globalContainer->gfx->drawFilledRect(x, y, barWidth+2, maxLength*3+1, 0, 0, 0);

		if (orientation==TOP_TO_BOTTOM)
		{
			int i;
			for (i=0; i<actLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r, g, b);
			for (; i<secondActLength+actLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r2, g2, b2);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawRect(x, y+i*3, 4, barWidth+2, r/3, g/3, b/3);
		}
		else
		{
			int i;
			for (i=0; i<maxLength-secondActLength-actLength; i++)
				globalContainer->gfx->drawRect(x, y+i*3, 4, barWidth+2, r/3, g/3, b/3);
			for (; i<maxLength-actLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r2, g2, b2);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r, g, b);
		}
	}
	else
		assert(false);
}

void Game::drawUnit(int x, int y, Uint16 gid, int viewportX, int viewportY, int screenW, int screenH, int localTeam, Uint32 drawOptions)
{
	int id=Unit::GIDtoID(gid);
	int team=Unit::GIDtoTeam(gid);
	Unit *unit=teams[team]->myUnits[id];
	assert(unit);
	if (!unit)
	{
		globalContainer->gfx->drawRect((x<<5)+1, (y<<5)+1, 30, 30, 255, 255, 0);
		return;
	}
	int dx=unit->dx;
	int dy=unit->dy;

	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	if ((drawOptions & DRAW_WHOLE_MAP) == 0)
		if ((!map.isFOWDiscovered(x+viewportX, y+viewportY, visibleTeams))&&(!map.isFOWDiscovered(x+viewportX-dx, y+viewportY-dy, visibleTeams)))
			return;

	int imgid;
	assert(unit->action>=0);
	assert(unit->action<NB_MOVE);
	imgid=unit->skin->startImage[unit->action];
	int px, py;
	map.mapCaseToDisplayable(unit->posX, unit->posY, &px, &py, viewportX, viewportY);
	int deltaLeft=255-unit->delta;
	if (unit->action<BUILD)
	{
		px-=(unit->dx*deltaLeft)>>3;
		py-=(unit->dy*deltaLeft)>>3;
	}
	else
	{
		// TODO : if looks ugly, do something intelligent here
	}

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

	// draw unit
	Sprite *unitSprite = unit->skin->sprite;
	unitSprite->setBaseColor(teams[team]->color);
	int decX = (unitSprite->getW(imgid)-32)>>1;
	int decY = (unitSprite->getH(imgid)-32)>>1;
	globalContainer->gfx->drawSprite(px-decX, py-decY, unitSprite, imgid);

	// draw selection
	if (unit==selectedUnit)
	{
		globalContainer->gfx->drawCircle(px+16, py+16, 16, 0, 0, 255);
		if (unit->owner->teamNumber == localTeam)
			globalContainer->gfx->drawCircle(px+16, py+16, 16, 0, 0, 190);
		else if ((teams[localTeam]->allies) & (unit->owner->me))
			globalContainer->gfx->drawCircle(px+16, py+16, 16, 255, 196, 0);
		else
			globalContainer->gfx->drawCircle(px+16, py+16, 16, 190, 0, 0);
	}

	// draw xp animation
	if (unit->levelUpAnimation)
	{
		std::ostringstream oss;
		oss << unit->experienceLevel;
		globalContainer->standardFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 242, 131, 14));
		globalContainer->gfx->drawString(px + 16 - (globalContainer->standardFont->getStringWidth(oss.str().c_str()) >> 1), py - 16 - 2 *( LEVEL_UP_ANIMATION_FRAME_COUNT - unit->levelUpAnimation), globalContainer->standardFont, oss.str(), 0, (255*unit->levelUpAnimation) / LEVEL_UP_ANIMATION_FRAME_COUNT);
		globalContainer->standardFont->popStyle();
	}

	// draw magic animation
	if (unit->magicActionAnimation)
	{
		if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		{
			globalContainer->gfx->drawSprite(px+16-(globalContainer->magiceffect->getW(0)>>1), py+16-(globalContainer->magiceffect->getH(0)>>1), globalContainer->magiceffect, 0);
		}
		else
		{
			unsigned alpha = (unit->magicActionAnimation * 255) / MAGIC_ACTION_ANIMATION_FRAME_COUNT;
			if (globalContainer->gfx->canDrawStretchedSprite())
			{
				int stretchW = ((MAGIC_ACTION_ANIMATION_FRAME_COUNT - unit->magicActionAnimation) * globalContainer->magiceffect->getW(0)) / (MAGIC_ACTION_ANIMATION_FRAME_COUNT * 2);
				int stretchH = ((MAGIC_ACTION_ANIMATION_FRAME_COUNT - unit->magicActionAnimation) * globalContainer->magiceffect->getH(0)) / (MAGIC_ACTION_ANIMATION_FRAME_COUNT * 2);
				globalContainer->gfx->drawSprite(px+16-stretchW, py+16-stretchH, stretchW*2, stretchH*2, globalContainer->magiceffect, 0, alpha);
			}
			else
			{
				globalContainer->gfx->drawSprite(px+16-(globalContainer->magiceffect->getW(0)>>1), py+16-(globalContainer->magiceffect->getH(0)>>1), globalContainer->magiceffect, 0, alpha);
			}
		}
	}

	if ((px<mouseX)&&((px+32)>mouseX)&&(py<mouseY)&&((py+32)>mouseY)&&(((drawOptions & DRAW_WHOLE_MAP) != 0) ||(map.isFOWDiscovered(x+viewportX, y+viewportY, visibleTeams))||(Unit::GIDtoTeam(gid)==localTeam)))
		mouseUnit=unit;

	if ((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0 )
	{
		drawPointBar(px+1, py+25, LEFT_TO_RIGHT, 10, (unit->hungry*10)/Unit::HUNGRY_MAX, 80, 179, 223);

		float hpRatio=(float)unit->hp/(float)unit->performance[HP];
		if (hpRatio>0.6)
			drawPointBar(px+1, py+25+3, LEFT_TO_RIGHT, 10, 1+(int)(9*hpRatio), 78, 187, 78);
		else if (hpRatio>0.3)
			drawPointBar(px+1, py+25+3, LEFT_TO_RIGHT, 10, 1+(int)(9*hpRatio), 255, 255, 0);
		else
			drawPointBar(px+1, py+25+3, LEFT_TO_RIGHT, 10, 1+(int)(9*hpRatio), 255, 0, 0);

		if ((unit->performance[HARVEST]) && (unit->carriedRessource>=0))
			globalContainer->gfx->drawSprite(px+24, py, globalContainer->ressourceMini, unit->carriedRessource);
		globalContainer->gfx->finishDrawingSprite(globalContainer->ressourceMini, 255);
	}

	if (drawOptions & DRAW_ACCESSIBILITY)
	{
		std::ostringstream oss;
		oss << unit->owner->teamNumber;
		int accessW = globalContainer->littleFont->getStringWidth(oss.str().c_str());
		int accessH = globalContainer->littleFont->getStringHeight(oss.str().c_str());
		int accessX = px+((32-accessW)>>1);
		int accessY = py+((32-accessH)>>1);
		globalContainer->gfx->drawFilledRect(accessX-4, accessY, accessW+8, accessH, Color(0, 0, 0, 127));
		globalContainer->gfx->drawRect(accessX-4, accessY, accessW+8, accessH, Color(255, 255, 255, 127));
		globalContainer->gfx->drawString(accessX, accessY, globalContainer->littleFont, oss.str());
	}
	if(highlightUnitType & (1<<unit->typeNum))
	{
		globalContainer->gfx->drawSprite(px, py-decY-32, globalContainer->gamegui, 36);
	}
}

struct BuildingPosComp
{
	bool operator () (Building * const & a, Building * const & b)
	{
		if (a->posY != b->posY)
			return a->posY < b->posY;
		else
			return a->posX < b->posX;
	}
};

inline void Game::drawMapWater(int sw, int sh, int viewportX, int viewportY, int time)
{
	int waterStartX = -(((viewportX<<5)+time/2) % 512);
	int waterStartY = -((viewportY<<5) % 512);
	for (int y=waterStartY; y<sh; y += 512)
		for (int x=waterStartX; x<sw; x += 512)
			globalContainer->gfx->drawSprite(x, y, globalContainer->terrainWater, 0);
	globalContainer->gfx->finishDrawingSprite(globalContainer->terrainWater, 255);
}

inline void Game::drawMapTerrain(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
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

inline void Game::drawMapRessources(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
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

inline void Game::drawMapGroundUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	//Reset the mouse unit to NULL, as this time arround there may not be a unit
	//under the mouse pointer
	mouseUnit=NULL;
	for (int y=top-1; y<=bot; y++)
		for (int x=left-1; x<=right; x++)
		{
			Uint16 gid=map.getGroundUnit(x+viewportX, y+viewportY);
			if (gid!=NOGUID)
				drawUnit(x, y, gid, viewportX, viewportY, (sw>>5), (sh>>5), localTeam, drawOptions);
		}
}

inline void Game::drawMapDebugAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	if (false)
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
	if (false)
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
	if (false)
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

inline void Game::drawMapBuilding(int x, int y, int gid, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	Building *building = teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
	assert(building);
	BuildingType *type=building->type;
	Team *team=building->owner;

	int imgid;
	if (type->crossConnectMultiImage)
	{
		int add = 0;
		Uint16 b;
		// Up
		b = map.getBuilding(building->posXLocal, building->posYLocal-1);
		if ((b != NOGBID) &&
			(Building::GIDtoTeam(b) == team->teamNumber) && (teams[Building::GIDtoTeam(b)]->myBuildings[Building::GIDtoID(b)]->type == type))
			add |= (1<<3);
		// Bottom
		b = map.getBuilding(building->posXLocal, building->posYLocal+building->type->height);
		if ((b != NOGBID) &&
			(Building::GIDtoTeam(b) == team->teamNumber) && (teams[Building::GIDtoTeam(b)]->myBuildings[Building::GIDtoID(b)]->type == type))
			add |= (1<<2);
		// Left
		b = map.getBuilding(building->posXLocal-1, building->posYLocal);
		if ((b != NOGBID) &&
			(Building::GIDtoTeam(b) == team->teamNumber) && (teams[Building::GIDtoTeam(b)]->myBuildings[Building::GIDtoID(b)]->type == type))
			add |= (1<<1);
		// Right
		b = map.getBuilding(building->posXLocal+building->type->width, building->posYLocal);
		if ((b != NOGBID) &&
			(Building::GIDtoTeam(b) == team->teamNumber) && (teams[Building::GIDtoTeam(b)]->myBuildings[Building::GIDtoID(b)]->type == type))
			add |= (1<<0);
		imgid = type->gameSpriteImage + add;
	}
	else
	{
		// FIXME : why building->hp is > type->hpMax ?
		int hp = std::min(building->hp, type->hpMax);
		int damageImgShift = type->gameSpriteCount - ((hp * type->gameSpriteCount) / (type->hpMax+1)) - 1;
		assert(damageImgShift >= 0);
		imgid = type->gameSpriteImage + damageImgShift;
	}
//	int x, y;
	int dx, dy;

//	map.mapCaseToDisplayable(building->posXLocal, building->posYLocal, &x, &y, viewportX, viewportY);

	// select buildings and set the team colors
	Sprite *buildingSprite = type->gameSpritePtr;
	dx = (type->width<<5)-buildingSprite->getW(imgid);
	dy = (type->height<<5)-buildingSprite->getH(imgid);
	buildingSprite->setBaseColor(team->color);

	// draw building
	globalContainer->gfx->drawSprite(x+dx, y+dy, buildingSprite, imgid);
	globalContainer->gfx->finishDrawingSprite(buildingSprite, 255);

	if ((drawOptions & DRAW_BUILDING_RECT) != 0)
	{
		int batW=(type->width )<<5;
		int batH=(type->height)<<5;
		int typeNum=building->typeNum;
		globalContainer->gfx->drawRect(x, y, batW, batH, 255, 255, 255, 127);

		BuildingType *lastbt=globalContainer->buildingsTypes.get(typeNum);
		int lastTypeNum=typeNum;
		int max=0;
		while(lastbt->nextLevel>=0)
		{
			lastTypeNum=lastbt->nextLevel;
			lastbt=globalContainer->buildingsTypes.get(lastTypeNum);
			if (max++>200)
			{
				printf("GameGUI: Error: nextLevelTypeNum architecture is broken.\n");
				assert(false);
				break;
			}
		}
		int exBatX=x+((lastbt->decLeft-type->decLeft)<<5);
		int exBatY=y+((lastbt->decTop-type->decTop)<<5);
		int exBatW=(lastbt->width)<<5;
		int exBatH=(lastbt->height)<<5;

		globalContainer->gfx->drawRect(exBatX, exBatY, exBatW, exBatH, 255, 255, 255, 127);
	}

	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	if (((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0) && (building->owner->sharedVisionOther & visibleTeams))
	{
		//int unitDecx=(building->type->width*16)-((3*building->maxUnitInside)>>1);
		// TODO : find better color for this
		// health
		if (type->hpMax)
		{
			int maxWidth, actWidth, addDec;
			float hpRatio=(float)building->hp/(float)type->hpMax;
			if (type->width==1)
			{
				maxWidth=8;
				actWidth=1+(int)(7.0f*hpRatio);
				addDec=2;
			}
			else
			{
				maxWidth=16;
				actWidth=1+(int)(15.0f*hpRatio);
				addDec=7;
			}
			int decy=(type->height*32);
			int healDecx=(type->width-(maxWidth>>3))*16+addDec;

			if (building->hp!=type->hpMax || !building->type->crossConnectMultiImage)
			{
				if (hpRatio>0.6)
					drawPointBar(x+healDecx, y+decy-4, LEFT_TO_RIGHT, maxWidth, actWidth, 78, 187, 78);
				else if (hpRatio>0.3)
					drawPointBar(x+healDecx, y+decy-4, LEFT_TO_RIGHT, maxWidth, actWidth, 255, 255, 0);
				else
					drawPointBar(x+healDecx, y+decy-4, LEFT_TO_RIGHT, maxWidth, actWidth, 255, 0, 0);
			}
		}

		// units
		if (building->maxUnitInside>0)
			drawPointBar(x+type->width*32-4, y+1, BOTTOM_TO_TOP, building->maxUnitInside, (signed)building->unitsInside.size(), 255, 255, 255);
		if (building->maxUnitWorking>0)
			drawPointBar(x+type->width*16-((3*building->maxUnitWorking)>>1), y+1,LEFT_TO_RIGHT , building->maxUnitWorking, (signed)building->unitsWorking.size(), 0, 255, 255, 255, 255, 64, 0);

		// food (for inns)
		if ((type->canFeedUnit) || (type->unitProductionTime))
		{
			// compute bar size, prevent oversize
			int bDiv=1;
			assert(type->height!=0);
			while ( ((type->maxRessource[CORN]*3+1)/bDiv)>((type->height*32)-10))
				bDiv++;
			drawPointBar(x+1, y+1, BOTTOM_TO_TOP, type->maxRessource[CORN]/bDiv, building->ressources[CORN]/bDiv, 255, 255, 120, 1+bDiv);
		}

		// bullets (for defence towers)
		if (type->maxBullets)
		{
			// compute bar size, prevent oversize
			int bDiv=1;
			assert(type->height!=0);
			while ( ((type->maxBullets*3+1)/bDiv)>((type->height*32)-10))
				bDiv++;
			drawPointBar(x+1, y+1, BOTTOM_TO_TOP, type->maxBullets/bDiv, building->bullets/bDiv, 200, 200, 200, 1+bDiv);
		}
	}

	if (drawOptions & DRAW_ACCESSIBILITY)
	{
		std::ostringstream oss;
		oss << building->owner->teamNumber;
		int accessW = globalContainer->littleFont->getStringWidth(oss.str().c_str());
		int accessH = globalContainer->littleFont->getStringHeight(oss.str().c_str());
		int accessX = x+(((type->width<<5)-accessW)>>1);
		int accessY = y+(((type->height<<5)-accessH)>>1);
		globalContainer->gfx->drawFilledRect(accessX-4, accessY, accessW+8, accessH, Color(0, 0, 0, 127));
		globalContainer->gfx->drawRect(accessX-4, accessY, accessW+8, accessH, Color(255, 255, 255, 127));
		globalContainer->gfx->drawString(accessX, accessY, globalContainer->littleFont, oss.str());
	}

	if(highlightBuildingType & (1<<building->shortTypeNum))
	{
		globalContainer->gfx->drawSprite(x + buildingSprite->getW(imgid)/2 - 16, y-36, globalContainer->gamegui, 36);
	}
}


inline void Game::drawMapGroundBuildings(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, std::set<Building*> *visibleBuildings)
{
	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	std::set<Building*> drawnBuildings;
	for (int y=top-1; y<=bot; y++)
		for (int x=left-1; x<=right; x++)
		{
			Uint16 gid=map.getBuilding(x+viewportX, y+viewportY);
			if (gid!=NOGBID) // Then this is a building
			{
				//globalContainer->gfx->drawRect(x<<5, y<<5, 32, 32, 255, 128, 0);
				//globalContainer->gfx->drawRect(2+(x<<5), 2+(y<<5), 28, 28, 255, 128, 0);

				int id = Building::GIDtoID(gid);
				int team = Building::GIDtoTeam(gid);

				Building *building=teams[team]->myBuildings[id];
				if(drawnBuildings.find(building)==drawnBuildings.end())
				{
					assert(building); // if this fails, and unwanted garbage-UID is on the ground.
					if (((drawOptions & DRAW_WHOLE_MAP) != 0)
						|| Building::GIDtoTeam(gid)==localTeam
						|| (building->seenByMask & visibleTeams)
						|| map.isFOWDiscovered(x+viewportX, y+viewportY, visibleTeams))
					{
						int px,py;
						map.mapCaseToDisplayable(building->posXLocal, building->posYLocal, &px, &py, viewportX, viewportY);
					 	drawMapBuilding(px, py, gid, viewportX, viewportY, localTeam, drawOptions);
						drawnBuildings.insert(building);
					}
				}
			}
		}
	if(visibleBuildings)
		*visibleBuildings = drawnBuildings;
}
/**
 * Draws the visible (viewport) part of the given map
 */
inline void Game::drawMapAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	static int areaAnimationTick = 0;

	if ((drawOptions & DRAW_AREA) != 0 && (!globalContainer->replaying || globalContainer->replayShowAreas))
	{
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, &map, &Map::isForbiddenLocal, areaAnimationTick, ForbiddenArea);
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, &map, &Map::isGuardAreaLocal, areaAnimationTick, GuardArea);
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, &map, &Map::isClearAreaLocal, areaAnimationTick, ClearingArea);
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
inline void Game::drawMapArea(int left, int top, int right, int bot, int sw,
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

inline void Game::drawMapAirUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	for (int y=top-1; y<=bot; y++)
		for (int x=left-1; x<=right; x++)
		{
			Uint16 gid=map.getAirUnit(x+viewportX, y+viewportY);
			if (gid!=NOGUID)
				drawUnit(x, y, gid, viewportX, viewportY, (sw>>5), (sh>>5), localTeam, drawOptions);
		}
}

inline void Game::drawMapScriptAreas(int left, int top, int right, int bot, int viewportX, int viewportY)
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

inline void Game::drawMapBulletsExplosionsDeathAnimations(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	// Let's paint the bullets and explosions
	// TODO : optimise : test only possible sectors to show bullets.

	Sprite *bulletSprite = globalContainer->bullet;
	// FIXME : have team in bullets to have the correct color

	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	int mapPixW=(map.getW())<<5;
	int mapPixH=(map.getH())<<5;

	for (int i=0; i<(map.getSectorW()*map.getSectorH()); i++)
	{
		Sector *s=map.getSector(i);
		// bullets
		for (std::list<Bullet *>::iterator it=s->bullets.begin();it!=s->bullets.end();it++)
		{
			int x=(*it)->px-(viewportX<<5);
			int y=(*it)->py-(viewportY<<5);
			int balisticShift = 0;

			if (x<0)
				x+=mapPixW;
			if (y<0)
				y+=mapPixH;
			if ((*it)->ticksInitial)
			{
				float x = static_cast<float>((*it)->ticksLeft);
				float T = static_cast<float>((*it)->ticksInitial);
				float speedX = static_cast<float>((*it)->speedX);
				float speedY = static_cast<float>((*it)->speedX);
				float K = static_cast<float>(sqrt(speedX * speedX + speedY * speedY));
				balisticShift = static_cast<int>(K * ((-1.0f * x * x) / T + x));
			}

			//printf("px=(%d, %d) vp=(%d, %d)\n", (*it)->px, (*it)->py, viewportX, viewportY);
			if ( (x<=sw) && (y<=sh) )
			{
				globalContainer->gfx->drawSprite(x, y-balisticShift, bulletSprite, BULLET_IMGID);
				globalContainer->gfx->drawSprite(x+(balisticShift>>1), y, bulletSprite, BULLET_IMGID+1);
			}
		}
		globalContainer->gfx->finishDrawingSprite(bulletSprite, 255);
		// explosions
		for (std::list<BulletExplosion *>::iterator it=s->explosions.begin();it!=s->explosions.end();it++)
		{
			if (map.isFOWDiscovered((*it)->x, (*it)->y, visibleTeams))
			{
				int x, y;
				map.mapCaseToDisplayable((*it)->x, (*it)->y, &x, &y, viewportX, viewportY);
				int frame = globalContainer->bulletExplosion->getFrameCount() - (*it)->ticksLeft - 1;
				int decX = globalContainer->bulletExplosion->getW(frame)>>1;
				int decY = globalContainer->bulletExplosion->getH(frame)>>1;
				globalContainer->gfx->drawSprite(x+16-decX, y+16-decY, globalContainer->bulletExplosion, frame);
			}
		}
		globalContainer->gfx->finishDrawingSprite(globalContainer->bulletExplosion, 255);
		// death animations
		for (std::list<UnitDeathAnimation *>::iterator it=s->deathAnimations.begin();it!=s->deathAnimations.end();++it)
		{
			if (map.isFOWDiscovered((*it)->x, (*it)->y, visibleTeams))
			{
				int x, y;
				map.mapCaseToDisplayable((*it)->x, (*it)->y, &x, &y, viewportX, viewportY);
				int frame = globalContainer->deathAnimation->getFrameCount() - (*it)->ticksLeft - 1;
				int decX = globalContainer->deathAnimation->getW(frame)>>1;
				int decY = globalContainer->deathAnimation->getH(frame)>>1;
				Team *team = (*it)->team;

				globalContainer->deathAnimation->setBaseColor(team->color);
				globalContainer->gfx->drawSprite(x+16-decX, y+16-decY-frame, globalContainer->deathAnimation, frame);
			}
		}
		globalContainer->gfx->finishDrawingSprite(globalContainer->deathAnimation, 255);
	}
}

inline void Game::drawMapFogOfWar(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	if ((drawOptions & DRAW_WHOLE_MAP) == 0)
	{
		// we have decrease on because we do unalign lookup
		for (int y=top-1; y<=bot; y++)
			for (int x=left-1; x<=right; x++)
			{
				unsigned i0, i1, i2, i3;

				/*if ( (!map.isMapDiscovered(x+viewportX, y+viewportY, teams[localTeam]->me)))
				{
					globalContainer->gfx->drawFilledRect(x<<5, y<<5, 32, 32, 10, 10, 10);
				}
				else if ( (!map.isFOW(x+viewportX, y+viewportY, teams[localTeam]->me)))
				{
					globalContainer->gfx->drawSprite(x<<5, y<<5, globalContainer->terrainShader, 0);
				}*/

				Uint32 visibleTeams = teams[localTeam]->me;
				if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

				// first draw black
				i0=!map.isMapDiscovered(x+viewportX+1, y+viewportY+1, visibleTeams) ? 1 : 0;
				i1=!map.isMapDiscovered(x+viewportX, y+viewportY+1, visibleTeams) ? 1 : 0;
				i2=!map.isMapDiscovered(x+viewportX+1, y+viewportY, visibleTeams) ? 1 : 0;
				i3=!map.isMapDiscovered(x+viewportX, y+viewportY, visibleTeams) ? 1 : 0;
				unsigned blackValue = i0 + (i1<<1) + (i2<<2) + (i3<<3);
				if (blackValue==15)
					globalContainer->gfx->drawFilledRect((x<<5)+16, (y<<5)+16, 32, 32, 0, 0, 0);
				else if (blackValue)
					globalContainer->gfx->drawSprite((x<<5)+16, (y<<5)+16, globalContainer->terrainBlack, blackValue);

				// then if it isn't full black, draw shade
				if (blackValue!=15)
				{
					i0=!map.isFOWDiscovered(x+viewportX+1, y+viewportY+1, visibleTeams) ? 1 : 0;
					i1=!map.isFOWDiscovered(x+viewportX, y+viewportY+1, visibleTeams) ? 1 : 0;
					i2=!map.isFOWDiscovered(x+viewportX+1, y+viewportY, visibleTeams) ? 1 : 0;
					i3=!map.isFOWDiscovered(x+viewportX, y+viewportY, visibleTeams) ? 1 : 0;
					unsigned shadeValue = i0 + (i1<<1) + (i2<<2) + (i3<<3);

					if (shadeValue==15)
						globalContainer->gfx->drawFilledRect((x<<5)+16, (y<<5)+16, 32, 32, 0, 0, 0, 127);
					else if (shadeValue)
						globalContainer->gfx->drawSprite((x<<5)+16, (y<<5)+16, globalContainer->terrainShader, shadeValue);
				}
			}
		globalContainer->gfx->finishDrawingSprite(globalContainer->terrainBlack, 255);
		globalContainer->gfx->finishDrawingSprite(globalContainer->terrainShader, 255);
	}
}

inline void Game::drawMapOverlayMaps(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	if(drawOptions & DRAW_OVERLAY)
	{
		OverlayArea* overlays;
		if(gui)
			overlays=&gui->overlay;
		else if(edit)
			overlays=&edit->overlay;
		else assert(false);
		int overlayMax=overlays->getMaximum();
		Color overlayColor;
		if(overlays->getOverlayType() == OverlayArea::Starving)
			overlayColor=Color(192, 0, 0);
		if(overlays->getOverlayType() == OverlayArea::Damage)
			overlayColor=Color(192, 0, 0);
		if(overlays->getOverlayType() == OverlayArea::Defence)
			overlayColor=Color(0, 0, 192);
		if(overlays->getOverlayType() == OverlayArea::Fertility)
			overlayColor=Color(0, 192, 128);
		///Both width and height have +2 to cover half-squares arround the edge of the viewport
		int width = (right - left) + 2;
		int height = (bot - top) + 2;

		overlayAlphas.resize(width * height);
		for (int y=0; y<height; y++)
		{
			for (int x=0; x<width; x++)
			{
				Uint32 visibleTeams = teams[localTeam]->me;
				if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

				int rx=(x+viewportX-1+map.getW())%map.getW();
				int ry=(y+viewportY-1+map.getH())%map.getH();
				if(!edit && !map.isMapDiscovered(rx, ry, visibleTeams))
					continue;
				if(overlays->getValue(rx, ry))
				{
					const int value_c=overlays->getValue(rx, ry);
					const int alpha_c=int(float(200)/float(overlayMax) * float(value_c));
					overlayAlphas[width * y + x] = alpha_c;
				}
			}
		}

		///This is to correct OpenGL's blending not beeing offset correctly to line up with the map tiles
		if(globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
			globalContainer->gfx->drawAlphaMap(overlayAlphas, width, height, -16, -16, 32, 32, overlayColor);
		else
			globalContainer->gfx->drawAlphaMap(overlayAlphas, width, height, -32, -32, 32, 32, overlayColor);
	}
}



inline void Game::drawUnitPathLines(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	if ((drawOptions & DRAW_PATH_LINE) != 0)
	{
		for(int i=0; i<Unit::MAX_COUNT; ++i)
		{
			Unit *unit=teams[localTeam]->myUnits[i];
			if (unit)
			{
				drawUnitPathLine(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, unit);
			}
		}
	}
	if(selectedUnit != NULL)
	{
		drawUnitPathLine(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, selectedUnit);
	}
}



inline void Game::drawUnitPathLine(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, Unit* unit)
{
	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	if(unit->owner->sharedVisionOther & visibleTeams)
	{
		if (unit->validTarget)
		{
			if(isOnScreen(left,top,right,bot,viewportX,viewportY,unit->posX,unit->posY) || isOnScreen(left,top,right,bot,viewportX,viewportY,unit->targetX,unit->targetY))
			{
				int px, py;
				map.mapCaseToDisplayableVector(unit->posX, unit->posY, &px, &py, viewportX, viewportY, sw, sh);
				int deltaLeft=255-unit->delta;
				if (unit->action<BUILD)
				{
					px-=(unit->dx*deltaLeft)>>3;
					py-=(unit->dy*deltaLeft)>>3;
				}


				int lsx, lsy, ldx, ldy;
				map.mapCaseToDisplayableVector(unit->targetX, unit->targetY, &ldx, &ldy, viewportX, viewportY, sw, sh);
				lsx=px+16;
				lsy=py+16;
				if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
					globalContainer->gfx->drawLine(lsx, lsy, ldx+16, ldy+16, 250, 250, 250);
				else
					globalContainer->gfx->drawLine(lsx, lsy, ldx+16, ldy+16, 250, 250, 250, 128);
			}
		}
	}
}



inline void Game::drawUnitOffScreen(int sx, int sy, int sw, int sh, int viewportX, int viewportY, Unit* unit, Uint32 drawOptions)
{
	// Get the direction to the unit
	int px, py;
	map.mapCaseToDisplayableVector(unit->posX, unit->posY, &px, &py, viewportX, viewportY, sw, sh);
	int deltaLeft=255-unit->delta;
	if (unit->action<BUILD)
	{
		px-=(unit->dx*deltaLeft)>>3;
		py-=(unit->dy*deltaLeft)>>3;
	}

	// To get the center of the unit
	px+=16;
	py+=16;

	// Place the internal box dimensions
	int i_sx = sx + 20;
	int i_sy = sy + 20;
	int i_sw = sw - 40;
	int i_sh = sh - 40;

	// The units draw position releative to the center of the internal square
	int rel_cx = px - i_sx - i_sw/2;
	int rel_cy = py - i_sy - i_sh/2;
	if(rel_cx == 0)
		rel_cx = 1;
	if(rel_cy == 0)
		rel_cy = 1;

	//globalContainer->gfx->drawLine(sx + sw/2, sy + sh/2, px, py, Color::white);

	// Decide which edge of the screen the box is on, and compute its center cordinates
	int bx = 0;
	int by = 0;
	float slope = float(rel_cy) / float(rel_cx);
	float angle = atan2f(float(rel_cy), float(rel_cx));
	float screen=float(i_sh) / float(i_sw);
	if(rel_cx > 0 && std::abs(slope) <= std::abs(screen))
	{
		bx = i_sx + i_sw;
		by = i_sy + (i_sh/2) + int(slope * float(i_sw/2));
	}
	else if(rel_cx < 0 && std::abs(slope) <= std::abs(screen))
	{
		bx = i_sx;
		by = i_sy + (i_sh/2) - int(slope * float(i_sw/2));
	}
	else if(rel_cy > 0 && std::abs(slope) >= std::abs(screen))
	{
		bx = i_sx + (i_sw/2) + int(float(i_sh/2) / slope);
		by = i_sy + i_sh;
	}
	else if(rel_cy < 0 && std::abs(slope) >= std::abs(screen))
	{
		bx = i_sx + (i_sw/2) - int(float(i_sh/2) / slope);
		by = i_sy;
	}

	bx -= 20;
	by -= 20;

	// draw unit's image
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

	// Draw the code
	//globalContainer->gfx->drawFilledRect(bx, by, 40, 40, 0,0,0,128);
	//globalContainer->gfx->drawCircle(bx+20, by+20, 20, Color::white);
	Color transpWhite = Color(255, 255, 255, 192);
	globalContainer->gfx->drawLine(
		bx+20+cosf(angle)*5,
		by+20+sinf(angle)*5,
		bx+20+cosf(angle)*17,
		by+20+sinf(angle)*17,
		Color::white);
	globalContainer->gfx->drawLine(
		bx+20+cosf(angle)*17,
		by+20+sinf(angle)*17,
		bx+20+cosf(angle-M_PI/6)*10,
		by+20+sinf(angle-M_PI/6)*10,
		Color::white);
	globalContainer->gfx->drawLine(
		bx+20+cosf(angle)*17,
		by+20+sinf(angle)*17,
		bx+20+cosf(angle+M_PI/6)*10,
		by+20+sinf(angle+M_PI/6)*10,
		Color::white);
	globalContainer->gfx->drawSprite(bx+decX+4, by+decY+4, unitSprite, imgid, 160);
}


float Game::interpolateValues(float a, float b, float x)
{
	float ft = 3.141592653f * x;
	float f = (1.0f - std::cos(ft)) * 0.5f;
	return  a*(1.0-f) + b*f;
}



inline bool Game::isOnScreen(int left, int top, int right, int bot, int viewportX, int viewportY, int x, int y)
{

	left += viewportX;
	right += viewportX;
	top += viewportY;
	bot += viewportY;

	if((x >= left-1 && x <= right) || (x+map.getW() >= left-1 && x+map.getW() <= right))
	{
		if((y >= top-1 && y <= bot) || (y+map.getH() >= top-1 && y+map.getH() <= bot))
		{
			return true;
		}
	}
	return false;
}



void Game::drawMap(int sx, int sy, int sw, int sh, int rightMargin, int topMargin, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, std::set<Building*> *visibleBuildings)
{
	static int time = 0;
	static DynamicClouds ds(&globalContainer->settings);
	int left=(sx>>5);
	int top=(sy>>5);
	int right=((sx+sw+31)>>5);
	int bot=((sy+sh+31)>>5);

	time++;
	drawMapWater(sw, sh, viewportX, viewportY, time);
	drawMapTerrain(left, top, right, bot, viewportX, viewportY, localTeam, drawOptions);
	drawMapRessources(left, top, right, bot, viewportX, viewportY, localTeam, drawOptions);
	drawMapGroundUnits(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	drawMapDebugAreas(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	drawMapGroundBuildings(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, visibleBuildings);
	drawMapAirUnits(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	if((drawOptions & DRAW_SCRIPT_AREAS) != 0)
		drawMapScriptAreas(left, top, right, bot, viewportX, viewportY);
	drawMapBulletsExplosionsDeathAnimations(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);

	// compute and draw cloud shadow if we are in high quality
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
	{
		ds.compute(viewportX, viewportY, sw, sh, time);
		ds.render(globalContainer->gfx, sw, sh, DynamicClouds::SHADOW);
	}

	drawMapFogOfWar(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	drawMapAreas(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	drawMapOverlayMaps(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);
	drawUnitPathLines(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions);

	// draw cloud overlay if we are in high quality
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
		ds.render(globalContainer->gfx, sw, sh, DynamicClouds::CLOUD);

	// Draw units that are off the screen for the selected building

	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	if(selectedBuilding != NULL && (selectedBuilding->owner->sharedVisionOther & visibleTeams))
	{
		for(std::list<Unit*>::iterator i = selectedBuilding->unitsWorking.begin(); i!=selectedBuilding->unitsWorking.end(); ++i)
		{
			Unit* unit = *i;
			if(!isOnScreen(left, top, right, bot, viewportX, viewportY, unit->posX, unit->posY))
			{
				drawUnitOffScreen(0, topMargin, sw - rightMargin, sh-topMargin, viewportX, viewportY, unit, drawOptions);
			}
		}
	}

	// we look on the whole map for buildings
	// TODO : increase speed, do not count on graphic clipping
	if (!globalContainer->replaying || globalContainer->replayShowFlags)
	{
		// In replays we want to show the flags of all players, so we build a list of whose buildings to show
		std::list<Team *> teamsToShow;

		if (!globalContainer->replaying)
		{
			// Only add the local team
			teamsToShow.push_back(teams[localTeam]);
		}
		else
		{
			// Add all teams
			for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
			{
				teamsToShow.push_back(teams[i]);
			}
		}

		// now cycle through all added teams
		for (std::list<Team *>::iterator teamsIt=teamsToShow.begin(); teamsIt!=teamsToShow.end(); ++teamsIt)
		{
			for (std::list<Building *>::iterator virtualIt=(*teamsIt)->virtualBuildings.begin();
				virtualIt!=(*teamsIt)->virtualBuildings.end(); ++virtualIt)
			{
				Building *building=*virtualIt;
				BuildingType *type=building->type;

				int team = building->owner->teamNumber;

				int imgid = type->gameSpriteImage;

				int x, y;
				map.mapCaseToDisplayable(building->posXLocal, building->posYLocal, &x, &y, viewportX, viewportY);

				// all flags are hued:
				Sprite *buildingSprite = type->gameSpritePtr;
				buildingSprite->setBaseColor(teams[team]->color);
				globalContainer->gfx->drawSprite(x, y, buildingSprite, imgid);

				// flag circle:
				if (((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0) || (building==selectedBuilding))
					globalContainer->gfx->drawCircle(x+16, y+16, 16+(32*building->unitStayRange), 0, 0, 255);

				// FIXME : ugly copy past
				if ((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0)
				{
					int decy=(type->height*32);
					int healDecx=(type->width-2)*16+1;
					//int unitDecx=(building->type->width*16)-((3*building->maxUnitInside)>>1);

					// TODO : find better color for this
					// health
					if (type->hpMax)
					{
						float hpRatio=(float)building->hp/(float)type->hpMax;
						if (hpRatio>0.6)
							drawPointBar(x+healDecx+6, y+decy-4, LEFT_TO_RIGHT, 16, 1+(int)(15.0f*hpRatio), 78, 187, 78);
						else if (hpRatio>0.3)
							drawPointBar(x+healDecx+6, y+decy-4, LEFT_TO_RIGHT, 16, 1+(int)(15.0f*hpRatio), 255, 255, 0);
						else
							drawPointBar(x+healDecx+6, y+decy-4, LEFT_TO_RIGHT, 16, 1+(int)(15.0f*hpRatio), 255, 0, 0);
					}

					// units

					if (building->maxUnitInside>0)
						drawPointBar(x+type->width*32-4, y+1, BOTTOM_TO_TOP, building->maxUnitInside, (signed)building->unitsInside.size(), 255, 255, 255);
					if (building->maxUnitWorking>0)
						drawPointBar(x+type->width*16-((3*building->maxUnitWorking)>>1), y+1,LEFT_TO_RIGHT , building->maxUnitWorking, (signed)building->unitsWorking.size(), 255, 255, 255);

					// food
					if ((type->canFeedUnit) || (type->unitProductionTime))
					{
						// compute bar size, prevent oversize
						int bDiv=1;
						assert(type->height!=0);
						while ( ((type->maxRessource[CORN]*3+1)/bDiv)>((type->height*32)-10))
							bDiv++;
						drawPointBar(x+1, y+1, BOTTOM_TO_TOP, type->maxRessource[CORN]/bDiv, building->ressources[CORN]/bDiv, 255, 255, 120, 1+bDiv);
					}
				}
			}
		}
	}

	if (false)
		for (int y=top-1; y<=bot; y++)
			for (int x=left-1; x<=right; x++)
				for (int pi=0; pi<gameHeader.getNumberOfPlayers(); pi++)
					if (players[pi] && players[pi]->ai && players[pi]->ai->implementitionID==AI::CASTOR)
					{
						AICastor *ai=(AICastor *)players[pi]->ai->aiImplementation;
						//Uint8 *gradient=ai->wheatCareMap[1];
						Uint8 *gradient=ai->hydratationMap;
						//Uint8 *gradient=ai->enemyWarriorsMap;
						//Uint8 *gradient=map.forbiddenGradient[1][0];
						//Uint8 *gradient=map.ressourcesGradient[0][CORN][0];

						assert(gradient);
						size_t addr=((x+viewportX)&map.wMask)+map.w*((y+viewportY)&map.hMask);
						Uint8 value=gradient[addr];
						if (value)
							globalContainer->gfx->drawString((x<<5), (y<<5), globalContainer->littleFont, value);

						/*Uint8 *gradient2=ai->wheatCareMap[1];
						assert(gradient2);
						Uint8 value2=gradient2[addr];
						if (value2)
							globalContainer->gfx->drawString((x<<5), (y<<5)+10, globalContainer->littleFont, value2);*/
						break;
					}
}
