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
#include "UnitSkin.h"
#include "FertilityCalculatorDialog.h"

#include "ReplayWriter.h"

// Unit rendering. Split from Game_render.cpp.


void Game::drawUnit(int x, int y, Uint16 gid, int viewportX, int viewportY, int screenW, int screenH, int localTeam, Uint32 drawOptions, ViewState& view)
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
	const UnitSkin &skin = g_unitSkins[unit->typeNum];
	imgid=skin.startImage[unit->action];
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
	Sprite *unitSprite = skin.sprite;
	unitSprite->setBaseColor(teams[team]->color);
	int decX = (unitSprite->getW(imgid)-32)>>1;
	int decY = (unitSprite->getH(imgid)-32)>>1;
	globalContainer->gfx->drawSprite(px-decX, py-decY, unitSprite, imgid);

	// draw selection
	if (unit==view.selectedUnit)
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

	if ((px<view.mouseX)&&((px+32)>view.mouseX)&&(py<view.mouseY)&&((py+32)>view.mouseY)&&(((drawOptions & DRAW_WHOLE_MAP) != 0) ||(map.isFOWDiscovered(x+viewportX, y+viewportY, visibleTeams))||(Unit::GIDtoTeam(gid)==localTeam)))
		view.mouseUnit=unit;

	if ((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0 )
	{
		drawPointBar(px+1, py+25, LEFT_TO_RIGHT, 10, (unit->hungry*10)/Unit::HUNGRY_MAX, 80, 179, 223);

		float hpRatio=(float)unit->hp/(float)unit->performance[HP];
		drawHealthBar(px+1, py+25+3, 10, 1+(int)(9*hpRatio), hpRatio);

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


void Game::drawMapGroundUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, ViewState& view)
{
	//Reset the mouse unit to NULL, as this time arround there may not be a unit
	//under the mouse pointer
	view.mouseUnit=NULL;
	for (int y=top-1; y<=bot; y++)
		for (int x=left-1; x<=right; x++)
		{
			Uint16 gid=map.getGroundUnit(x+viewportX, y+viewportY);
			if (gid!=NOGUID)
				drawUnit(x, y, gid, viewportX, viewportY, (sw>>5), (sh>>5), localTeam, drawOptions, view);
		}
}


void Game::drawMapAirUnits(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, ViewState& view)
{
	for (int y=top-1; y<=bot; y++)
		for (int x=left-1; x<=right; x++)
		{
			Uint16 gid=map.getAirUnit(x+viewportX, y+viewportY);
			if (gid!=NOGUID)
				drawUnit(x, y, gid, viewportX, viewportY, (sw>>5), (sh>>5), localTeam, drawOptions, view);
		}
}


void Game::drawUnitPathLines(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, ViewState& view)
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
	if(view.selectedUnit != NULL)
	{
		drawUnitPathLine(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, view.selectedUnit);
	}
}



void Game::drawUnitPathLine(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, Unit* unit)
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



void Game::drawUnitOffScreen(int sx, int sy, int sw, int sh, int viewportX, int viewportY, Unit* unit, Uint32 drawOptions)
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
