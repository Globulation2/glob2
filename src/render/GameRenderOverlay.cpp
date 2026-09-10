// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "MapCopies.h"

#include "AICastor.h"

#include <assert.h>

#include <cmath>


#include "BuildingType.h"
#include "DatasetWriter.h"
#include "Game.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "Utilities.h"
#include "GameGUI.h"
#include "SDLCompat.h"

#include "MapEdit.h"

#include "Brush.h"
#include "Bullet.h"
#include "FertilityCalculatorDialog.h"

#include "ReplayWriter.h"

#include "GameAnimations.h"

#define BULLET_IMGID 0

// Bullets/explosions/death animations, fog of war, and overlay maps. Split from Game_render.cpp.


void Game::drawMapBulletsExplosionsDeathAnimations(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	// Let's paint the bullets and explosions
	// TODO : optimise : test only possible sectors to show bullets.

	Sprite *bulletSprite = globalContainer->bullet;

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
			int ballisticShift = 0;

			if (x<0)
				x+=mapPixW;
			if (y<0)
				y+=mapPixH;
			if ((*it)->ticksInitial)
			{
				float time = static_cast<float>((*it)->ticksLeft);
				float duration = static_cast<float>((*it)->ticksInitial);
				float speedX = static_cast<float>((*it)->speedX);
				float speedY = static_cast<float>((*it)->speedY);
				float K = static_cast<float>(sqrt(speedX * speedX + speedY * speedY));
				ballisticShift = static_cast<int>(K * ((-1.0f * time * time) / duration + time));
			}

			forEachMapCopy(x, y-ballisticShift,
				x+std::max(bulletSprite->getW(BULLET_IMGID), ballisticShift/2+bulletSprite->getW(BULLET_IMGID+1)),
				y+std::max(bulletSprite->getH(BULLET_IMGID), bulletSprite->getH(BULLET_IMGID+1)),
				mapPixW, mapPixH, sw, sh, [&](int dx, int dy) {
					globalContainer->gfx->drawSprite(x+dx, y-ballisticShift+dy, bulletSprite, BULLET_IMGID);
					globalContainer->gfx->drawSprite(x+ballisticShift/2+dx, y+dy, bulletSprite, BULLET_IMGID+1);
				});
		}
		globalContainer->gfx->finishDrawingSprite(bulletSprite, 255);
		// explosions
		for (BulletExplosion *e : animations->getExplosions(i))
		{
			if (map.isFOWDiscovered(e->x, e->y, visibleTeams))
			{
				int x, y;
				map.mapCaseToDisplayable(e->x, e->y, &x, &y, viewportX, viewportY);
				int frame = globalContainer->bulletExplosion->getFrameCount() - e->ticksLeft - 1;
				int decX = globalContainer->bulletExplosion->getW(frame)>>1;
				int decY = globalContainer->bulletExplosion->getH(frame)>>1;
				const int px = x+16-decX, py = y+16-decY;
				forEachMapCopy(px, py, px+globalContainer->bulletExplosion->getW(frame)-1,
					py+globalContainer->bulletExplosion->getH(frame)-1, mapPixW, mapPixH, sw, sh, [&](int dx, int dy) {
						globalContainer->gfx->drawSprite(px+dx, py+dy, globalContainer->bulletExplosion, frame);
					});
			}
		}
		globalContainer->gfx->finishDrawingSprite(globalContainer->bulletExplosion, 255);
		// death animations
		for (UnitDeathAnimation *a : animations->getDeathAnimations(i))
		{
			if (map.isFOWDiscovered(a->x, a->y, visibleTeams))
			{
				int x, y;
				map.mapCaseToDisplayable(a->x, a->y, &x, &y, viewportX, viewportY);
				int frame = globalContainer->deathAnimation->getFrameCount() - a->ticksLeft - 1;
				int decX = globalContainer->deathAnimation->getW(frame)>>1;
				int decY = globalContainer->deathAnimation->getH(frame)>>1;
				Team *team = a->team;

				globalContainer->deathAnimation->setBaseColor(team->color);
				const int px = x+16-decX, py = y+16-decY-frame;
				forEachMapCopy(px, py, px+globalContainer->deathAnimation->getW(frame)-1,
					py+globalContainer->deathAnimation->getH(frame)-1, mapPixW, mapPixH, sw, sh, [&](int dx, int dy) {
						globalContainer->gfx->drawSprite(px+dx, py+dy, globalContainer->deathAnimation, frame);
					});
			}
		}
		globalContainer->gfx->finishDrawingSprite(globalContainer->deathAnimation, 255);
	}
}

void Game::drawMapFogOfWar(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
{
	if ((drawOptions & DRAW_WHOLE_MAP) == 0)
	{
		// we have decrease on because we do unaligned lookup
		for (int y=top-1; y<=bot; y++)
			for (int x=left-1; x<=right; x++)
			{
				unsigned i0, i1, i2, i3;

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

void Game::computeCloudVisibility(std::valarray<unsigned char> &out, int gridW, int gridH, int originX, int originY, int cellSize, int localTeam, Uint32 drawOptions)
{
	if (out.size() != static_cast<size_t>(gridW*gridH))
		out.resize(gridW*gridH);
	if ((drawOptions & DRAW_WHOLE_MAP) != 0)
	{
		out = (unsigned char)0;
		return;
	}

	Uint32 visibleTeams = teams[localTeam]->me;
	if (globalContainer->replaying) visibleTeams = globalContainer->replayVisibleTeams;

	const unsigned char shade = (unsigned char)globalContainer->settings.cloudShadeAlpha;
	for (int y=0; y<gridH; y++)
		for (int x=0; x<gridW; x++)
		{
			//the lattice is in world pixels, the discovery maps are in cases
			int mx = (originX + x*cellSize)>>5;
			int my = (originY + y*cellSize)>>5;
			unsigned char hidden;
			if (!map.isMapDiscovered(mx, my, visibleTeams))
				hidden = 255;
			else if (!map.isFOWDiscovered(mx, my, visibleTeams))
				hidden = shade;
			else
				hidden = 0;
			out[gridW*y+x] = hidden;
		}
}

void Game::drawMapOverlayMaps(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions)
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
		switch(overlays->getOverlayType())
		{
			case OverlayArea::Starving:  overlayColor=Color(192, 0, 0);   break;
			case OverlayArea::Damage:    overlayColor=Color(192, 0, 0);   break;
			case OverlayArea::Defence:   overlayColor=Color(0, 0, 192);   break;
			case OverlayArea::Fertility: overlayColor=Color(0, 192, 128); break;
			case OverlayArea::None:      break;
		}
		///Both width and height have +2 to cover half-squares around the edge of the viewport
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
