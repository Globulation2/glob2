#include <climits>
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "scene/Scene.h"
#include <PerformanceTelemetry.h>
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
#include <SDL3/SDL.h>

#include "MapEdit.h"

#include "Brush.h"
#include "Bullet.h"

#include "ReplayWriter.h"

#include "GameAnimations.h"

#define BULLET_IMGID 0

// Bullets/explosions/death animations, fog of war, and overlay maps. Split from Game_render.cpp.


void Game::drawMapBulletsExplosionsDeathAnimations(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const Scene& scene)
{
	const SceneEntities &entities = scene.entities;
	const SceneMap &map = scene.map; // the extracted map, not Game::map
	PERF_SCOPE_TIME(Effects);
	// Let's paint the bullets and explosions
	// TODO : optimise : test only possible sectors to show bullets.

	Sprite *bulletSprite = globalContainer->bullet;

	Uint32 visibleTeams = entities.teams[localTeam].me;
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;

	int mapPixW=(map.getW())<<5;
	int mapPixH=(map.getH())<<5;

	for (const SceneSectorEffects &sector : entities.sectors)
	{
		// bullets
		for (const SceneBullet &bullet : sector.bullets)
		{
			const SceneBullet *it = &bullet;
			int x=it->px-(viewportX<<5);
			int y=it->py-(viewportY<<5);
			int ballisticShift = 0;

			if (x<0)
				x+=mapPixW;
			if (y<0)
				y+=mapPixH;
			if (it->ticksInitial)
			{
				float time = static_cast<float>(it->ticksLeft);
				float duration = static_cast<float>(it->ticksInitial);
				float speedX = static_cast<float>(it->speedX);
				float speedY = static_cast<float>(it->speedY);
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
		for (const SceneExplosion &explosion : sector.explosions)
		{
			const SceneExplosion *e = &explosion;
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
		for (const SceneDeathAnimation &death : sector.deaths)
		{
			const SceneDeathAnimation *a = &death;
			if (map.isFOWDiscovered(a->x, a->y, visibleTeams))
			{
				int x, y;
				map.mapCaseToDisplayable(a->x, a->y, &x, &y, viewportX, viewportY);
				int frame = globalContainer->deathAnimation->getFrameCount() - a->ticksLeft - 1;
				int decX = globalContainer->deathAnimation->getW(frame)>>1;
				int decY = globalContainer->deathAnimation->getH(frame)>>1;
				globalContainer->deathAnimation->setBaseColor(entities.teams[a->team].color);
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

void Game::drawMapFogOfWar(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const Scene& scene)
{
	const SceneMap &sceneMap = scene.map;
	PERF_SCOPE_TIME(RenderFog);
	if ((drawOptions & DRAW_WHOLE_MAP) == 0)
	{
		// we have decrease on because we do unaligned lookup
		Uint32 visibleTeams = scene.entities.teams[localTeam].me;
		if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;
		for (int y=top-1; y<=bot; y++)
		{
			// Whole squares of black or shade join into one fill per horizontal
			// run. In the software renderer, plain per-tile rectangles at a
			// fractional zoom leave gaps and overlaps that show as a grid, so
			// the fill snaps to pixels there; see drawMapTileFill.
			int blackStart = INT_MIN, shadeStart = INT_MIN;
			const auto flush = [&](int &start, int end, const GAGCore::Color &color)
			{
				if (start != INT_MIN)
					globalContainer->gfx->drawMapTileFill((start<<5)+16, (y<<5)+16, (end<<5)+16, (y<<5)+48, color);
				start = INT_MIN;
			};
			const GAGCore::Color black(0, 0, 0), shade(0, 0, 0, 127);
			for (int x=left-1; x<=right; x++)
			{
				unsigned i0, i1, i2, i3;

				// first draw black
				i0=!sceneMap.isMapDiscovered(x+viewportX+1, y+viewportY+1, visibleTeams) ? 1 : 0;
				i1=!sceneMap.isMapDiscovered(x+viewportX, y+viewportY+1, visibleTeams) ? 1 : 0;
				i2=!sceneMap.isMapDiscovered(x+viewportX+1, y+viewportY, visibleTeams) ? 1 : 0;
				i3=!sceneMap.isMapDiscovered(x+viewportX, y+viewportY, visibleTeams) ? 1 : 0;
				unsigned blackValue = i0 + (i1<<1) + (i2<<2) + (i3<<3);
				if (blackValue==15)
				{
					if (blackStart == INT_MIN)
						blackStart = x;
					flush(shadeStart, x, shade);
					continue;
				}
				flush(blackStart, x, black);
				if (blackValue)
					globalContainer->gfx->drawMapTileSprite((x<<5)+16, (y<<5)+16, 32, globalContainer->terrainBlack, blackValue);

				// then if it isn't full black, draw shade
				i0=!sceneMap.isFOWDiscovered(x+viewportX+1, y+viewportY+1, visibleTeams) ? 1 : 0;
				i1=!sceneMap.isFOWDiscovered(x+viewportX, y+viewportY+1, visibleTeams) ? 1 : 0;
				i2=!sceneMap.isFOWDiscovered(x+viewportX+1, y+viewportY, visibleTeams) ? 1 : 0;
				i3=!sceneMap.isFOWDiscovered(x+viewportX, y+viewportY, visibleTeams) ? 1 : 0;
				unsigned shadeValue = i0 + (i1<<1) + (i2<<2) + (i3<<3);

				if (shadeValue==15)
				{
					if (shadeStart == INT_MIN)
						shadeStart = x;
					continue;
				}
				flush(shadeStart, x, shade);
				if (shadeValue)
					globalContainer->gfx->drawMapTileSprite((x<<5)+16, (y<<5)+16, 32, globalContainer->terrainShader, shadeValue);
			}
			flush(blackStart, right+1, black);
			flush(shadeStart, right+1, shade);
		}
		globalContainer->gfx->finishDrawingSprite(globalContainer->terrainBlack, 255);
		globalContainer->gfx->finishDrawingSprite(globalContainer->terrainShader, 255);
	}
}

void Game::drawMapOverlayMaps(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, ViewState& view)
{
	std::valarray<unsigned char> &overlayAlphas = view.render.overlayAlphas;
	PERF_SCOPE_TIME(Overlay);
	if(drawOptions & DRAW_OVERLAY)
	{
		const OverlayArea* overlays = view.drawnScene().overlay.get();
		if (!overlays && edit)
			overlays=&edit->overlay;
		if (!overlays)
			return;
		int overlayMax=overlays->getMaximum();
		const Color overlayColor = OverlayArea::colorOf(overlays->getOverlayType());
		///Both width and height have +2 to cover half-squares around the edge of the viewport
		int width = (right - left) + 2;
		int height = (bot - top) + 2;

		overlayAlphas.resize(width * height);
		for (int y=0; y<height; y++)
		{
			for (int x=0; x<width; x++)
			{
				Uint32 visibleTeams = view.drawnScene().entities.teams[localTeam].me;
				if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;

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
