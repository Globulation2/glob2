// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "MapCopies.h"
#include "unit/render/ColonySkinPreview.h"


#include <algorithm>
#include <assert.h>
#include <string.h>

#include <set>


#include "BuildingType.h"
#include "building/hud/BuildingPresentation.h"
#include "DatasetWriter.h"
#include "Game.h"
#include "MapDrawer.h"
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
#include "terrain/DirectTerrain.h"
#include "unit/render/UnitMotion.h"
#include "UnitEffects.h"
#include "scene/SceneExtract.h"
#include "PerformanceTelemetry.h"

// Map rendering orchestrator and shared helpers. Split from Game_render.cpp.


void MapDrawer::drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, int secondActLength, Uint8 r, Uint8 g, Uint8 b, Uint8 r2, Uint8 g2, Uint8 b2, int barWidth, MapRenderState* drawnRender, float opacity)
{
	assert(maxLength>=0);
	assert(maxLength<65536);
	// Live counts may exceed the displayed capacity. Bound both sections;
	// drawing a status bar must not abort gameplay or spill outside the bar.
	actLength = std::clamp(actLength, 0, maxLength);
	secondActLength = std::clamp(secondActLength, 0, maxLength - actLength);
	// Queued bars take their opacity from the last anchorBars, which already
	// includes the entity's own.
	if (drawnRender)
	{
		drawnRender->overlays.bar(x, y, orientation==TOP_TO_BOTTOM || orientation==BOTTOM_TO_TOP,
			orientation==RIGHT_TO_LEFT || orientation==BOTTOM_TO_TOP, maxLength, actLength,
			secondActLength, r, g, b, r2, g2, b2, barWidth);
		return;
	}
	// Drawn directly, a bar fading with its entity (as a unit fading into the fog)
	// is translucent, and a fully faded one is not drawn at all.
	const Uint8 alpha = Uint8(std::lround(std::clamp(opacity, 0.f, 1.f) * 255));
	if (!alpha)
		return;
	GAGCore::OpaqueRectangleBatch rectangles(globalContainer->gfx);

	if ((orientation==LEFT_TO_RIGHT) || (orientation==RIGHT_TO_LEFT))
	{
		globalContainer->gfx->drawFilledRect(x, y, maxLength*3+1, barWidth+2, 0, 0, 0, alpha);

		if (orientation==LEFT_TO_RIGHT)
		{
			int i;
			for (i=0; i<actLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r, g, b, alpha);
			for (; i<secondActLength+actLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r2, g2, b2, alpha);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawRect(x+i*3, y, 4, barWidth+2, r/3, g/3, b/3, alpha);
		}
		else
		{
			int i;
			for (i=0; i<maxLength-secondActLength-actLength; i++)
				globalContainer->gfx->drawRect(x+i*3, y, 4, barWidth+2, r/3, g/3, b/3, alpha);
			for (; i<maxLength-actLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r2, g2, b2, alpha);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawFilledRect(x+i*3+1, y+1, 2, barWidth, r, g, b, alpha);
		}
	}
	else if ((orientation==BOTTOM_TO_TOP) || (orientation==TOP_TO_BOTTOM))
	{
		globalContainer->gfx->drawFilledRect(x, y, barWidth+2, maxLength*3+1, 0, 0, 0, alpha);

		if (orientation==TOP_TO_BOTTOM)
		{
			int i;
			for (i=0; i<actLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r, g, b, alpha);
			for (; i<secondActLength+actLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r2, g2, b2, alpha);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawRect(x, y+i*3, 4, barWidth+2, r/3, g/3, b/3, alpha);
		}
		else
		{
			int i;
			for (i=0; i<maxLength-secondActLength-actLength; i++)
				globalContainer->gfx->drawRect(x, y+i*3, 4, barWidth+2, r/3, g/3, b/3, alpha);
			for (; i<maxLength-actLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r2, g2, b2, alpha);
			for (; i<maxLength; i++)
				globalContainer->gfx->drawFilledRect(x+1, y+i*3+1, barWidth, 2, r, g, b, alpha);
		}
	}
	else
		assert(false);
}


void MapDrawer::anchorBars(int x, int y, MapRenderState* drawnRender, float opacity)
{
	if (drawnRender)
		drawnRender->overlays.anchor(*globalContainer->gfx, x, y,
			drawnRender->detail.barAll * opacity);
}


void MapDrawer::drawStatusPip(int x, int y, MapOverlayQueue::StatusIcon icon, MapRenderState* drawnRender, float opacity, bool beside)
{
	if (drawnRender)
		drawnRender->overlays.pip(*globalContainer->gfx, x, y, icon, drawnRender->detail.statusPip * opacity, beside);
}


void MapDrawer::drawHealthBar(int x, int y, int maxLength, int actLength, float hpRatio, MapRenderState* drawnRender, float opacity)
{
	if (hpRatio > 0.6f)
		drawPointBar(x, y, LEFT_TO_RIGHT, maxLength, actLength, 78, 187, 78, 2, drawnRender, opacity);
	else if (hpRatio > 0.3f)
		drawPointBar(x, y, LEFT_TO_RIGHT, maxLength, actLength, 255, 255, 0, 2, drawnRender, opacity);
	else
		drawPointBar(x, y, LEFT_TO_RIGHT, maxLength, actLength, 255, 0, 0, 2, drawnRender, opacity);
}


void MapDrawer::drawBuildingResourceBar(int x, int y, const BuildingType* type, int maxValue, int currentValue, Uint8 r, Uint8 g, Uint8 b, MapRenderState* drawnRender)
{
	// Shrink the bar (3px per unit + 1) until it fits within the building's height minus 10px of padding.
	int bDiv = 1;
	assert(type->height != 0);
	// A constant-size bar is larger against a zoomed-out building; fit what shows.
	const double fit = drawnRender
		? std::min(1.0, globalContainer->gfx->mapTransformScale() / drawnRender->detail.overlayScale) : 1.0;
	while (((maxValue * 3 + 1) / bDiv) > int(((type->height * 32) - 10) * fit) && bDiv < maxValue)
		bDiv++;
	drawPointBar(x, y, BOTTOM_TO_TOP, maxValue / bDiv, currentValue / bDiv, r, g, b, 1 + bDiv, drawnRender);
}



bool MapDrawer::isOnScreen(int left, int top, int right, int bot, int viewportX, int viewportY, int x, int y, const SceneMap& map)
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

void MapDrawer::prepareSceneMapFrame(const PresentationFrame &scene, int localTeam, ViewState &view, Uint32 drawOptions, bool paused)
{
    view.render.skinPreview().setVisible(globalContainer->settings.showColonySkins);
    view.render.skinPreview().poll();
    view.render.animationTime += view.render.animationClock.advance(paused);
    const Uint32 visibleTeams = globalContainer->isViewingGame() ? globalContainer->replayVisibleTeams
        : scene.entities.teams[localTeam].mask;
    if (globalContainer->settings.smoothFog && !(drawOptions & Game::DRAW_WHOLE_MAP))
        view.render.fogFade.update(scene.map, visibleTeams, scene.tick,
            scene.tick + unitMotionFraction(scene, SDL_GetTicks()));
    else if (view.render.fogFade.active()) view.render.fogFade.reset();
}

void MapDrawer::finishMapCapture(ViewState &view, Uint32 options, bool paused)
{
    finishMapCapture(view.render,options,paused);
}
void MapDrawer::finishMapCapture(MapRenderState &render, Uint32 options, bool paused)
{
    if ((options & Game::DRAW_AREA) && (!globalContainer->isViewingGame() || globalContainer->replayShowAreas))
        render.areaAnimationTick += render.areaClock.advance(paused);
}

void MapDrawer::drawMap(int sx, int sy, int sw, int sh, int rightMargin, int topMargin, int viewportX,
				   int viewportY, int localTeam, ViewState &view, Uint32 drawOptions,
				   std::set<Uint16> *visibleBuildings,
				   const BuildingGuiStateMap *buildingGuiState, bool animationsPaused,
				   int cloudGridLimit, bool preparedCapture)
{
    if (!view.scene) throw std::logic_error("Drawing requires a published presentation frame");
	drawSceneMap(*view.scene, sx, sy, sw, sh,
		rightMargin, topMargin, viewportX, viewportY, localTeam, view, drawOptions,
		visibleBuildings, buildingGuiState, animationsPaused, cloudGridLimit, preparedCapture);
}

void MapDrawer::drawSceneMap(const PresentationFrame& scene, int sx, int sy, int sw, int sh,
	int rightMargin, int topMargin, int viewportX, int viewportY, int localTeam,
	ViewState& view, Uint32 drawOptions, std::set<Uint16>* visibleBuildings,
	const BuildingGuiStateMap* buildingGuiState, bool animationsPaused, int cloudGridLimit, bool preparedCapture)
{
    drawSceneMapRegion(scene,sx,sy,sw,sh,rightMargin,topMargin,viewportX,viewportY,localTeam,
        view,drawOptions,visibleBuildings,buildingGuiState,animationsPaused,cloudGridLimit,
        preparedCapture,sw,sh);
}

void MapDrawer::drawSceneMapRegion(const PresentationFrame& scene, int sx, int sy, int sw, int sh,
	int rightMargin, int topMargin, int viewportX, int viewportY, int localTeam,
	ViewState& view, Uint32 drawOptions, std::set<Uint16>* visibleBuildings,
	const BuildingGuiStateMap* buildingGuiState, bool animationsPaused, int cloudGridLimit, bool preparedCapture, int targetWidth, int targetHeight)
{
    if (!preparedCapture)
        prepareSceneMapFrame(scene, localTeam, view, drawOptions, animationsPaused);
	const PresentationFrame* previous = view.scene;
	view.scene = &scene;
	struct RestoreScene { ViewState& view; const PresentationFrame* previous; ~RestoreScene() { view.scene = previous; } } restore{view, previous};
	GAGCore::FrameDrawBatch frameBatch(globalContainer->gfx);
	const SceneMap& map = scene.map;
	int& time = view.render.animationTime;
	int left = (sx >> 5);
	int top = (sy >> 5);
	int right = ((sx + sw + 31) >> 5);
	int bot = ((sy + sh + 31) >> 5);

	view.render.detail = ZoomDetail::forView(globalContainer->gfx->mapTransformScale(),
		globalContainer->gfx->logicalUnitsPerPoint(), globalContainer->settings.adaptiveZoomDetail,
		view.render.minimumZoom);
	// Queue constant-size overlays for this frame, and draw whatever the later
	// passes queued however drawMap returns.
	MapRenderState* drawnRender = globalContainer->settings.adaptiveZoomDetail ? &view.render : nullptr;
	struct OverlayPass
	{
		MapRenderState &render;
		void flush() { render.overlays.flush(*globalContainer->gfx, globalContainer->mapIcons, globalContainer->statusIcons, render.detail.overlayScale, globalContainer->gfx->logicalUnitsPerPoint()); }
		~OverlayPass()
		{
			// Drawing may throw (for example on allocation failure). Discard
			// unfinished overlays instead of drawing during stack unwinding.
			render.overlays.bars.clear(); render.overlays.pips.clear();
			render.overlays.markers.clear(); render.overlays.glyphs.clear();
		}
	} overlayPass{view.render};
	GameRenderFrame frame{*globalContainer->gfx,
						  *globalContainer->terrain,
						  left,
						  top,
						  right,
						  bot,
						  targetWidth,
						  targetHeight,
						  viewportX,
						  viewportY,
						  localTeam,
						  drawOptions,
						  globalContainer->isViewingGame() ? globalContainer->replayVisibleTeams
														   : scene.entities.teams[localTeam].mask};
    view.render.skinPreview().prepare(frame.target, scene, left, top, right, bot,
        viewportX, viewportY, localTeam, frame.visibleTeams, drawOptions & Game::DRAW_WHOLE_MAP,
        view.render.unitMotion, view.render.detail.unitSprite > 0, view.render.detail.buildingSprite > 0, &view.render.fogFade);
	// Unit effects at buildings go with the unit sprites, are building particles
	// for the detail setting, and only accelerated renderers draw them: index this
	// frame's by unit, and the producing swarms' glows by swarm.
	view.render.unitEffects.clear();
	view.render.swarmGlows.clear();
	view.render.unitOverlays.clear();
	if (view.render.detail.unitSprite > 0 && globalContainer->settings.buildingParticles
		&& frame.target.rendererCapabilities().accelerated)
		for (const SceneSectorEffects &sector : scene.entities.sectors)
			for (const SceneUnitEffect &effect : sector.unitEffects)
			{
				view.render.unitEffects[effect.unit] = effect;
				if (effect.kind != Uint8(UnitEffect::Kind::Spawn))
					continue;
				const float glow = UnitEffect::swarmGlow(effect.age + view.render.unitMotion);
				if (glow > 0)
				{
					float &swarm = view.render.swarmGlows[effect.building];
					swarm = std::max(swarm, glow);
				}
			}
	// The overview fully replaces detailed terrain at the far end of zoom.
	bool overviewOnly = view.render.detail.terrainOverview >= 1;
	// Browser builds drive Legacy GL through the WebGL2 bridge, which has no
	// direct pass: their views compose pages, native until HD artwork streams in.
	const bool legacy = frame.target.isLegacyGLScreen() && DirectTerrain::legacyPassAvailable();
	bool direct = false;
	bool terrainBackdrop = false;
	const bool hdArtwork = globalContainer->settings.highResolutionArtwork;
	const auto drawBackdrop = [&]
	{
		const auto alpha = view.render.detail.terrainOverview;
		view.render.detail.terrainOverview = 1;
		try
		{
			drawMapOverview(frame.left, frame.top, frame.right, frame.bottom,
				frame.viewportX, frame.viewportY, frame.localTeam, frame.options, scene.map, view.render);
		}
		catch (const std::bad_alloc &)
		{
			view.render.detail.terrainOverview = alpha;
			return;
		}
		view.render.detail.terrainOverview = alpha;
		terrainBackdrop = true;
	};
	if (legacy && !overviewOnly)
	{
		// Legacy GL draws terrain in one direct pass at every zoom, in games,
		// the editor and captures. The editor, captures and the menu colony's
		// first frame complete their view before drawing; elsewhere cells whose masks are still building draw
		// a straight split between their corners, and the overview shows until the map's tables
		// are ready.
		PERF_SCOPE_TIME(Terrain);
		const bool complete = scene.editor || (frame.options & (Game::DRAW_TILED_CAPTURE | Game::DRAW_COMPLETE_TERRAIN));
		try
		{
			auto &terrain = view.render.directTerrain();
			if (terrain.needsBackdrop(frame.target, scene.map))
				drawBackdrop();
			if (terrain.prepare(frame.target, scene.map, frame.left, frame.top, frame.right, frame.bottom,
				frame.viewportX, frame.viewportY, frame.visibleTeams, frame.options & Game::DRAW_WHOLE_MAP, time, complete))
			{
				terrain.drawPrepared(frame.target, frame.viewportX, frame.viewportY,
					frame.options & Game::DRAW_WHOLE_MAP, time);
				direct = true;
			}
		}
		catch (const std::bad_alloc &) { /* The overview remains. */ }
		if (!direct && !terrainBackdrop)
			drawBackdrop();
	}
	else if (!overviewOnly && !hdArtwork && !scene.editor && !(frame.options & Game::DRAW_TILED_CAPTURE))
	{
		// Programmable devices draw native direct terrain where they can.
		PERF_SCOPE_TIME(Terrain);
		try
		{
			direct = view.render.directTerrain().draw(frame.target, scene.map, frame.left, frame.top,
				frame.right, frame.bottom, frame.viewportX, frame.viewportY, frame.visibleTeams,
				frame.options & Game::DRAW_WHOLE_MAP, time);
		}
		catch (const std::bad_alloc &) { /* Pages remain available. */ }
	}
	// Software, programmable-device and browser Legacy GL renderers compose pages.
	SoftwareTerrainCache *pagesForView = nullptr;
	if (!direct && !overviewOnly && !legacy)
	{
		try
		{
			pagesForView = &view.render.terrainCache(scene.map.identity());
		}
		catch (const std::bad_alloc &)
		{ /* Keep the uncached renderer available under memory pressure. */ }
	}
	const bool cached = pagesForView && pagesForView->prepare(scene.map, frame.terrain,
		frame.left, frame.top, frame.right, frame.bottom, frame.viewportX, frame.viewportY,
		frame.visibleTeams, frame.options & Game::DRAW_WHOLE_MAP, time,
		frame.options & Game::DRAW_TILED_CAPTURE);
	if (cached)
	{
		PERF_SCOPE_TIME(Terrain);
		pagesForView->draw(frame.target);
	}
	else if (!overviewOnly && !direct && !terrainBackdrop && !legacy)
		drawMapTerrain(left, top, right, bot, viewportX, viewportY, localTeam, drawOptions, scene.map, time);

    const auto *atlas = view.render.existingDirectTerrain();
    const auto *pages = view.render.existingTerrainCache();
    const auto textureBytes = (atlas ? atlas->textureBytes() : 0) +
        (pages ? pages->residentTextureBytes() : 0);
    view.render.peakTerrainTextureBytes = std::max(view.render.peakTerrainTextureBytes, textureBytes);

	// Pass adapters keep the two coordinate conventions in one place. Individual
	// layers still own their visibility decisions and their original draw order.
	const auto tilePass = [&](auto method, auto &&...state)
	{
		method(frame.left, frame.top, frame.right, frame.bottom, frame.viewportX,
						frame.viewportY, frame.localTeam, frame.options, state...);
	};
	const auto scenePass = [&](auto method, auto &&...state)
	{
		method(frame.left, frame.top, frame.right, frame.bottom, frame.width, frame.height,
						frame.viewportX, frame.viewportY, frame.localTeam, frame.options, state...);
	};

	if (!overviewOnly)
		tilePass(&MapDrawer::drawMapResources, scene.map, &view.render);
	if (!terrainBackdrop || direct || cached)
		tilePass(&MapDrawer::drawMapOverview, scene.map, view.render);
	tilePass(&MapDrawer::drawMapTerritory, scene, view.render.detail.strategic);
	scenePass(&MapDrawer::drawMapGroundUnits, view, scene);
	scenePass(&MapDrawer::drawMapDebugAreas, view);
	scenePass(&MapDrawer::drawMapGroundBuildings, visibleBuildings, buildingGuiState, scene, drawnRender, &view);
	scenePass(&MapDrawer::drawMapAirUnits, view, scene);
	// Bars sit above every unit and building, and under the fog like them.
	overlayPass.flush();
	if ((drawOptions & Game::DRAW_SCRIPT_AREAS) != 0)
		drawMapScriptAreas(left, top, right, bot, viewportX, viewportY, scene.map);

	// Bullets, explosions and death animations are unit-sized effects, and go
	// with the unit sprites once units are only markers.
	if (view.render.detail.unitSprite > 0)
	{
		scenePass(&MapDrawer::drawMapBulletsExplosionsDeathAnimations, scene);
		scenePass(&MapDrawer::drawMapUnitEffects, view, scene);
	}

	// Compute once for the independently selected cloud layers.
	if (globalContainer->settings.cloudShadows || (globalContainer->settings.clouds && !(drawOptions & Game::DRAW_NO_CLOUD_LAYER)))
	{
		view.render.clouds().compute(viewportX, viewportY, sw, sh, time, map.getW(), map.getH(),
		           globalContainer->settings.clouds && !(drawOptions & Game::DRAW_NO_CLOUD_LAYER), cloudGridLimit);
		if (globalContainer->settings.cloudShadows)
			view.render.clouds().render(globalContainer->gfx, sw, sh, DynamicClouds::SHADOW);
	}

	scenePass(&MapDrawer::drawMapFogOfWar, view.render, scene);
	scenePass(&MapDrawer::drawMapAreas, view, scene.map, !preparedCapture);
	scenePass(&MapDrawer::drawMapOverlayMaps, view);

	scenePass(&MapDrawer::drawUnitPathLines, view, scene);


	// Draw clouds above the world independently of shadows.
	if (!(drawOptions & Game::DRAW_NO_CLOUD_LAYER) && globalContainer->settings.clouds)
		view.render.clouds().render(globalContainer->gfx, sw, sh, DynamicClouds::CLOUD);

	// Draw units that are off the screen for the selected building

	const SceneEntities &entities = scene.entities;
	Uint32 visibleTeams = entities.teams[localTeam].mask;
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;

	const SnapshotBuilding *selectedBuilding = entities.building(entities.selectedBuilding.ref.gid);
	if(!preparedCapture && selectedBuilding && entities.isSelected(*selectedBuilding) && (entities.owner(*selectedBuilding).otherVision & visibleTeams))
	{
		for (UnitRef worker : entities.selectedBuilding.unitsWorking)
		{
			const SnapshotUnit *unit = entities.unit(worker.gid);
			if(unit && unit->identity==worker && !isOnScreen(left, top, right, bot, viewportX, viewportY, unit->posX, unit->posY, map))
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
			for (BuildingRef flag : entities.virtualBuildings[shownTeam])
			{
				const SnapshotBuilding *building=entities.building(flag.gid);
                if (!building || building->identity!=flag) continue;
				const BuildingType *type=entities.type(*building);

				int team = building->team;

				const int imgid = buildingSpriteFrame(*type, building->hp, building->maxHp, entities.connections(*building));

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
					buildingSprite->setBaseColor(presentationColor(entities.teams[team].color));
					// A flag is the player's command, so it turns into an icon
					// of constant size sooner than a building does.
					const float flagIcon = drawnRender ? view.render.detail.flagIcon : 0.f;
					if (flagIcon < 1)
						globalContainer->gfx->drawSprite(x, y, buildingSprite, imgid);
					if (flagIcon > 0)
						view.render.overlays.glyph(*globalContainer->gfx, x, y, x+32, y+32,
							type->presentation.iconFrame, type->presentation.iconTile ? MapOverlayQueue::Tile : MapOverlayQueue::Disc, 0, false, type->presentation.iconPriority,
							entities.teams[team].color.r, entities.teams[team].color.g, entities.teams[team].color.b, flagIcon);

					// flag circle:
					if (((drawOptions & Game::DRAW_HEALTH_FOOD_BAR) != 0) || entities.isSelected(*building))
						globalContainer->gfx->drawCircle(x+16, y+16, 16+(32*building->unitStayRange), 0, 0, 255);

					if ((drawOptions & Game::DRAW_HEALTH_FOOD_BAR) != 0)
					{
						int decy=(type->height*32);
						int healDecx=(type->width-2)*16+1;

						// TODO : find better color for this
						if (type->hpMax)
						{
							float hpRatio=(float)building->hp/(float)building->maxHp;
							anchorBars(x+type->width*16, y+decy, drawnRender);
							drawHealthBar(x+healDecx+6, y+decy-4, 16, 1+(int)(15.0f*hpRatio), hpRatio, drawnRender);
						}

						anchorBars(x+type->width*32, y, drawnRender);
						if (building->maxUnitInside>0)
							drawPointBar(x+type->width*32-4, y+1, BOTTOM_TO_TOP, building->maxUnitInside, building->inside.count, 255, 255, 255, 2, drawnRender);
						anchorBars(x+type->width*16, y, drawnRender);
						if (building->maxUnitWorking>0)
							drawPointBar(x+type->width*16-((3*building->maxUnitWorking)>>1), y+1,LEFT_TO_RIGHT , building->maxUnitWorking, building->working.count, 255, 255, 255, 2, drawnRender);

						anchorBars(x, y, drawnRender);
						if (const int resource=buildingResourceBarResource(*type,entities.materials(*building)); resource>=0)
							drawBuildingResourceBar(x+1, y+1, type, type->maxMaterial[resource], entities.materials(*building)[resource], 255, 255, 120, drawnRender);
					}
				});
			}
		}
	}



	overlayPass.flush();

}
