// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "MapCopies.h"

#include "AICastor.h"
#include "AINicowar.h"

#include <algorithm>
#include <assert.h>
#include <string.h>

#include <set>


#include "BuildingType.h"
#include "DatasetWriter.h"
#include "Game.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "Utilities.h"
#include <SDL3/SDL.h>


#include "Brush.h"
#include "DynamicClouds.h"
#include <OpaqueRectangleBatch.h>
#include <RenderBatch.h>


#include "GameRenderInternal.h"
#include "SoftwareTerrainCache.h"
#include "scene/SceneExtract.h"
#include "PerformanceTelemetry.h"

// Map rendering orchestrator and shared helpers. Split from Game_render.cpp.


void Game::drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, int secondActLength, Uint8 r, Uint8 g, Uint8 b, Uint8 r2, Uint8 g2, Uint8 b2, int barWidth)
{
	assert(maxLength>=0);
	assert(maxLength<65536);
	// Live counts may exceed the displayed capacity. Bound both sections;
	// drawing a status bar must not abort gameplay or spill outside the bar.
	actLength = std::clamp(actLength, 0, maxLength);
	secondActLength = std::clamp(secondActLength, 0, maxLength - actLength);
	if (drawnRender)
	{
		drawnRender->overlays.bar(x, y, orientation==TOP_TO_BOTTOM || orientation==BOTTOM_TO_TOP,
			orientation==RIGHT_TO_LEFT || orientation==BOTTOM_TO_TOP, maxLength, actLength,
			secondActLength, r, g, b, r2, g2, b2, barWidth);
		return;
	}
	GAGCore::OpaqueRectangleBatch rectangles(globalContainer->gfx);

	if ((orientation==LEFT_TO_RIGHT) || (orientation==RIGHT_TO_LEFT))
	{
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


void Game::anchorBars(int x, int y, bool exception)
{
	if (drawnRender)
		drawnRender->overlays.anchor(*globalContainer->gfx, x, y,
			exception ? drawnRender->detail.barException : drawnRender->detail.barAll);
}


void Game::drawStatusPip(int x, int y, Uint8 r, Uint8 g, Uint8 b)
{
	if (drawnRender)
		drawnRender->overlays.pip(*globalContainer->gfx, x, y, r, g, b, drawnRender->detail.statusPip);
}


void Game::drawHealthBar(int x, int y, int maxLength, int actLength, float hpRatio)
{
	if (hpRatio > 0.6f)
		drawPointBar(x, y, LEFT_TO_RIGHT, maxLength, actLength, 78, 187, 78);
	else if (hpRatio > 0.3f)
		drawPointBar(x, y, LEFT_TO_RIGHT, maxLength, actLength, 255, 255, 0);
	else
		drawPointBar(x, y, LEFT_TO_RIGHT, maxLength, actLength, 255, 0, 0);
}


void Game::drawBuildingResourceBar(int x, int y, BuildingType* type, int maxValue, int currentValue, Uint8 r, Uint8 g, Uint8 b)
{
	// Shrink the bar (3px per unit + 1) until it fits within the building's height minus 10px of padding.
	int bDiv = 1;
	assert(type->height != 0);
	// A constant-size bar is larger against a zoomed-out building; fit what shows.
	const double fit = drawnRender
		? std::min(1.0, globalContainer->gfx->mapTransformScale() / drawnRender->detail.overlayScale) : 1.0;
	while (((maxValue * 3 + 1) / bDiv) > int(((type->height * 32) - 10) * fit) && bDiv < maxValue)
		bDiv++;
	drawPointBar(x, y, BOTTOM_TO_TOP, maxValue / bDiv, currentValue / bDiv, r, g, b, 1 + bDiv);
}



bool Game::isOnScreen(int left, int top, int right, int bot, int viewportX, int viewportY, int x, int y)
{

	left += viewportX;
	right += viewportX;
	top += viewportY;
	bot += viewportY;

	if((x >= left-1 && x <= right) || (x+map.getW() >= left-1 && x+map.getW() <= right))
	{
		if ((y >= top - 1 && y <= bot) || (y + map.getH() >= top - 1 && y + map.getH() <= bot))
		{
			return true;
		}
	}
	return false;
}

namespace
{
bool drawPreparedWater(const GameRenderFrame &frame, const SoftwareTerrainCache &cache, int time)
{
	if (frame.left != 0 || frame.top != 0)
		return false;

	auto *water = frame.water.nativeFrame(0);
	if (water)
	{
		PERF_SCOPE_TIME(Water);
		const int startX = -(((frame.viewportX << 5) + time / 2) % 512);
		const int startY = -((frame.viewportY << 5) % 512);
        // Include the original pass's overshoot outside the logical viewport.
        // Fractional transforms can bring those pixels back inside the target.
        const SDL_Rect bounds{startX, startY,
            ((frame.width - startX + 511) / 512) * 512,
            ((frame.height - startY + 511) / 512) * 512};
        const auto regions = cache.waterRegions(bounds);
        for (int y = startY; y < frame.height; y += 512)
            for (int x = startX; x < frame.width; x += 512)
            {
                const SDL_Rect tile{x, y, 512, 512};
                // Keep the complete source mapping: cropping before scaling
                // would restart nearest-neighbor sampling at coverage edges.
                if (std::any_of(regions.begin(), regions.end(), [&](const SDL_Rect &region) {
                    return SDL_HasRectIntersection(&tile, &region);
                })) frame.target.drawSurface(x, y, water);
            }
		return true;
	}

	return false;
}
} // namespace

void Game::drawMap(int sx, int sy, int sw, int sh, int rightMargin, int topMargin, int viewportX,
				   int viewportY, int localTeam, ViewState &view, Uint32 drawOptions,
				   std::set<Uint16> *visibleBuildings,
				   const BuildingGuiStateMap *buildingGuiState, bool animationsPaused,
				   int cloudGridLimit)
{
    GAGCore::FrameDrawBatch frameBatch(globalContainer->gfx);
	// Frozen while paused, so the water and the clouds hold still with the rest.
	int &time = view.render.animationTime;
	// Draw the scene the simulation published, else extract one now (serial callers).
	if (!view.scene)
	{
		SceneRequest request;
		request.localTeam = localTeam;
		request.selectedBuilding = refOf(view.selectedBuilding);
		request.selectedUnit = refOf(view.selectedUnit);
		extractScene(*this, request, view.render.ownScene);
	}
	const Scene &scene = view.scene ? *view.scene : view.render.ownScene;
	int left = (sx >> 5);
	int top = (sy >> 5);
	int right = ((sx + sw + 31) >> 5);
	int bot = ((sy + sh + 31) >> 5);

	if (!animationsPaused)
		time++;
	view.render.detail = ZoomDetail::forView(globalContainer->gfx->mapTransformScale(),
		globalContainer->gfx->logicalUnitsPerPoint(), globalContainer->settings.adaptiveZoomDetail);
	// Queue constant-size overlays for this frame, and draw whatever the later
	// passes queued however drawMap returns.
	struct OverlayPass
	{
		Game &game;
		MapRenderState &render;
		OverlayPass(Game &game, MapRenderState &render, bool queue) : game(game), render(render)
		{
			game.drawnRender = queue ? &render : nullptr;
		}
		void flush() { render.overlays.flush(*globalContainer->gfx, globalContainer->mapIcons, render.detail.overlayScale, globalContainer->gfx->logicalUnitsPerPoint()); }
		~OverlayPass()
		{
			flush();
			game.drawnRender = nullptr;
		}
	} overlayPass(*this, view.render, globalContainer->settings.adaptiveZoomDetail);
	GameRenderFrame frame{*globalContainer->gfx,
						  *globalContainer->terrain,
						  *globalContainer->terrainWater,
						  left,
						  top,
						  right,
						  bot,
						  sw,
						  sh,
						  viewportX,
						  viewportY,
						  localTeam,
						  drawOptions,
						  globalContainer->isViewingGame() ? globalContainer->replayVisibleTeams
														   : scene.entities.teams[localTeam].me,
						  !(globalContainer->gfx->getOptionFlags() &
							(GraphicContext::USEGPU | GraphicContext::PORTABLEGPU))};
	// Prepare coverage before water, keeping scene ordering independent of the
	// cache's storage policy. Discovery uses exactly the uncached terrain rule.
	// Native opaque tile copies beat blending mixed-alpha chunks. Cache only
	// transformed CPU passes, where batching and per-pixel opaque copies help.
	const bool cacheEligible = frame.software && frame.target.hasPortableRenderer();
	SoftwareTerrainCache *softwareTerrainCache = nullptr;
	if (cacheEligible)
	{
		try
		{
			softwareTerrainCache = &view.render.terrainCache(scene.map.identity());
		}
		catch (const std::bad_alloc &)
		{ /* Keep the uncached renderer available under memory pressure. */
		}
	}
	// Far enough out the flat overview covers the terrain completely, so the
	// detailed water, terrain and resource passes underneath are not drawn.
	const bool overviewOnly = view.render.detail.terrainOverview >= 1;
	bool cached = !overviewOnly &&
		softwareTerrainCache &&
		softwareTerrainCache->prepare(scene.map, frame.terrain, frame.left, frame.top, frame.right,
									  frame.bottom, frame.viewportX, frame.viewportY,
									  frame.visibleTeams, frame.options & DRAW_WHOLE_MAP);
	bool coveredWater = false;
	try
	{
		if (cached)
			coveredWater = drawPreparedWater(frame, *softwareTerrainCache, time);
	}
	catch (const std::bad_alloc &)
	{
		cached = false;
	}
	if (overviewOnly)
		;
	else if (!coveredWater)
		drawMapWater(sw, sh, viewportX, viewportY, time);
	if (overviewOnly)
		;
	else if (cached)
	{
		PERF_SCOPE_TIME(Terrain);
		softwareTerrainCache->draw(frame.target);
	}
	else
		drawMapTerrain(left, top, right, bot, viewportX, viewportY, localTeam, drawOptions, scene.map);

	// Pass adapters keep the two coordinate conventions in one place. Individual
	// layers still own their visibility decisions and their original draw order.
	const auto tilePass = [&](auto method, auto &&...state)
	{
		(this->*method)(frame.left, frame.top, frame.right, frame.bottom, frame.viewportX,
						frame.viewportY, frame.localTeam, frame.options, state...);
	};
	const auto scenePass = [&](auto method, auto &&...state)
	{
		(this->*method)(frame.left, frame.top, frame.right, frame.bottom, frame.width, frame.height,
						frame.viewportX, frame.viewportY, frame.localTeam, frame.options, state...);
	};

	if (!overviewOnly)
		tilePass(&Game::drawMapResources, scene.map);
	tilePass(&Game::drawMapOverview, scene.map, view.render);
	tilePass(&Game::drawMapTerritory, scene, view.render.detail.strategic);
	scenePass(&Game::drawMapGroundUnits, view, scene);
	scenePass(&Game::drawMapDebugAreas, view);
	scenePass(&Game::drawMapGroundBuildings, visibleBuildings, buildingGuiState, scene);
	scenePass(&Game::drawMapAirUnits, view, scene);
	// Bars sit above every unit and building, and under the fog like them.
	overlayPass.flush();
	if ((drawOptions & DRAW_SCRIPT_AREAS) != 0)
		drawMapScriptAreas(left, top, right, bot, viewportX, viewportY);

	scenePass(&Game::drawMapBulletsExplosionsDeathAnimations, scene);

	// Compute once for the independently selected cloud layers.
	if (globalContainer->settings.cloudShadows || (globalContainer->settings.clouds && !(drawOptions & DRAW_NO_CLOUD_LAYER)))
	{
		view.render.clouds().compute(viewportX, viewportY, sw, sh, time, map.getW(), map.getH(),
		           globalContainer->settings.clouds && !(drawOptions & DRAW_NO_CLOUD_LAYER), cloudGridLimit);
		if (globalContainer->settings.cloudShadows)
			view.render.clouds().render(globalContainer->gfx, sw, sh, DynamicClouds::SHADOW);
	}

	scenePass(&Game::drawMapFogOfWar, scene);
	scenePass(&Game::drawMapAreas, view, scene.map);
	scenePass(&Game::drawMapOverlayMaps, view);

	scenePass(&Game::drawUnitPathLines, view, scene);


	// Draw clouds above the world independently of shadows.
	if (!(drawOptions & DRAW_NO_CLOUD_LAYER) && globalContainer->settings.clouds)
		view.render.clouds().render(globalContainer->gfx, sw, sh, DynamicClouds::CLOUD);

	// Draw units that are off the screen for the selected building

	const SceneEntities &entities = scene.entities;
	Uint32 visibleTeams = entities.teams[localTeam].me;
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;

	const SceneBuilding *selectedBuilding = entities.building(entities.selectedBuilding.ref.gid);
	if(selectedBuilding && entities.isSelected(*selectedBuilding) && (entities.owner(*selectedBuilding).sharedVisionOther & visibleTeams))
	{
		for (Uint16 worker : entities.selectedBuilding.unitsWorking)
		{
			const SceneUnit *unit = entities.unit(worker);
			if(unit && !isOnScreen(left, top, right, bot, viewportX, viewportY, unit->posX, unit->posY))
			{
				drawUnitOffScreen(0, topMargin, sw - rightMargin, sh-topMargin, viewportX, viewportY, *unit, drawOptions, scene, view.render.unitMotion);
			}
		}
	}


	// we look on the whole map for buildings
	// TODO : increase speed, do not count on graphic clipping
	if (!globalContainer->isViewingGame() || globalContainer->replayShowFlags)
	{
		// In replays we want to show the flags of all players, so we build a list of whose buildings to show
		std::vector<int> teamsToShow;

		if (!globalContainer->isViewingGame())
		{
			// Only add the local team
			teamsToShow.push_back(localTeam);
		}
		else
		{
			// Add all teams
			for (int i=0; i<entities.teamCount; i++)
			{
				teamsToShow.push_back(i);
			}
		}

		// now cycle through all added teams
		for (int shownTeam : teamsToShow)
		{
			for (Uint16 flagGid : entities.virtualBuildings[shownTeam])
			{
				const SceneBuilding *building=entities.building(flagGid);
				BuildingType *type=building->type;

				int team = building->team;

				int imgid = type->gameSpriteImage;

				int x, y;
				const Sint32 dispX = buildingGuiState ? displayedPosX(*buildingGuiState, building->gid, building->posX) : building->posX;
				const Sint32 dispY = buildingGuiState ? displayedPosY(*buildingGuiState, building->gid, building->posY) : building->posY;
				x = ((dispX-viewportX)&map.getMaskW())*32;
				y = ((dispY-viewportY)&map.getMaskH())*32;

				const int radius = 16 + 32*building->unitStayRange;
				Sprite *sprite = type->gameSpritePtr;
				forEachMapCopy(std::min(x,x+16-radius), std::min(y,y+16-radius),
					std::max(x+sprite->getW(imgid),x+16+radius), std::max(y+sprite->getH(imgid),y+16+radius),
					map.getW()*32, map.getH()*32, sw, sh, [&](int dx, int dy) {
					const int x = ((dispX-viewportX)&map.getMaskW())*32 + dx;
					const int y = ((dispY-viewportY)&map.getMaskH())*32 + dy;
					// all flags are hued:
					Sprite *buildingSprite = type->gameSpritePtr;
					buildingSprite->setBaseColor(entities.teams[team].color);
					// A flag is the player's command, so it turns into an icon
					// of constant size sooner than a building does.
					const float flagIcon = drawnRender ? view.render.detail.flagIcon : 0.f;
					if (flagIcon < 1)
						globalContainer->gfx->drawSprite(x, y, buildingSprite, imgid);
					if (flagIcon > 0)
						view.render.overlays.glyph(*globalContainer->gfx, x, y, x+32, y+32,
							std::clamp(int(building->shortTypeNum), 0, 10), MapOverlayQueue::Disc, 0, false, 180,
							entities.teams[team].color.r, entities.teams[team].color.g, entities.teams[team].color.b, flagIcon);

					// flag circle:
					if (((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0) || entities.isSelected(*building))
						globalContainer->gfx->drawCircle(x+16, y+16, 16+(32*building->unitStayRange), 0, 0, 255);

					if ((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0)
					{
						int decy=(type->height*32);
						int healDecx=(type->width-2)*16+1;

						// TODO : find better color for this
						if (type->hpMax)
						{
							float hpRatio=(float)building->hp/(float)building->effectiveMaxHp;
							anchorBars(x+type->width*16, y+decy, building->hp!=building->effectiveMaxHp);
							drawHealthBar(x+healDecx+6, y+decy-4, 16, 1+(int)(15.0f*hpRatio), hpRatio);
						}

						anchorBars(x+type->width*32, y);
						if (building->maxUnitInside>0)
							drawPointBar(x+type->width*32-4, y+1, BOTTOM_TO_TOP, building->maxUnitInside, building->unitsInside, 255, 255, 255);
						anchorBars(x+type->width*16, y);
						if (building->maxUnitWorking>0)
							drawPointBar(x+type->width*16-((3*building->maxUnitWorking)>>1), y+1,LEFT_TO_RIGHT , building->maxUnitWorking, building->unitsWorking, 255, 255, 255);

						anchorBars(x, y);
						if ((type->canFeedUnit) || (type->unitProductionTime))
							drawBuildingResourceBar(x+1, y+1, type, type->maxResource[WHEAT], building->resources[WHEAT], 255, 255, 120);
					}
				});
			}
		}
	}



	if (DEBUG_RENDER_GRADIENTS)
		for (int y=top-1; y<=bot; y++)
			for (int x=left-1; x<=right; x++)
				for (int pi=0; pi<gameHeader.getNumberOfPlayers(); pi++)
					if (players[pi] && players[pi]->ai && players[pi]->ai->implementationID==AI::CASTOR)
					{
						AICastor *ai=(AICastor *)players[pi]->ai->aiImplementation;
						//Uint8 *gradient=ai->wheatCareMap[1];
						Uint8 *gradient=ai->hydratationMap;
						//Uint8 *gradient=ai->enemyWarriorsMap;
						//Uint8 *gradient=map.forbiddenGradient[1][0];
						//Uint8 *gradient=map.resourcesGradient[0][WHEAT][0];

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
