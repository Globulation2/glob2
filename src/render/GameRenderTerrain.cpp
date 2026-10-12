// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "PowerOfTwo.h"
#include <PerformanceTelemetry.h>
#include <iostream>

#include "AICastor.h"

#include <assert.h>

#include <sstream>


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


#include "Brush.h"


#include "GameRenderInternal.h"
#include "scene/SceneMap.h"
#include <SpriteDrawBatch.h>
#include <MapGeometryCache.h>
#include <RenderBatch.h>
#include <algorithm>
#include <new>
#include "terrain/TerrainCompositor.h"
#include "SoftwareTerrainCache.h"
#include "OverviewTerrainCache.h"
#include "ResourceSprites.h"

namespace
{
// Obstacle decor (boulders, hedges, rock) and resource images overlap
// neighboring tiles. Each canonical row draws its decor first, then its
// resources, so lower rows stay in front of higher ones across both layers.
// Cache ONE complete canonical row per layer, retain source traversal order
// inside texture runs, and select the exact source tile range with binary
// searches. Splitting rectangles vertically would change the painter order.
// Partial discovery uses the ordinary path below instead.
bool drawCachedResources(Uint64 mapIdentity, const SceneMap& map, int left, int top,
    int right, int bottom, int viewportX, int viewportY)
{
    const auto& presentation = ResourceSprites::resolve(map.frozenResourceRegistry(), map.frozenAssetBundle());
    auto *gfx = globalContainer->gfx;
    auto *batch = gfx->getRenderBatch();
    if (!batch) return false;
    auto *sprite = globalContainer->resources;
    const auto &compositor = globalContainer->terrainCompositor(map.frozenAssetBundle());
    auto *decorSprite = compositor.decorSprite();
    if (!decorSprite && std::any_of(compositor.catalog().materials.begin(), compositor.catalog().materials.end(),
        [](const auto &m) { return !m.decor.sprite.empty(); })) return false;
    std::vector<int> frames, decorFrames;
    GAGCore::MapGeometryCache *cache;
    try { frames.resize(map.getW()); decorFrames.resize(map.getW()); cache = &batch->geometryCache(); }
    catch (const std::bad_alloc&) { return false; }
    // Decide before emitting geometry: fallback must never redraw earlier rows.
    for (int y = top; y <= bottom; ++y)
        for (int x = 0; x < map.getW(); ++x)
        {
            const int mapY = (y + viewportY) & map.getMaskH();
            const auto& r = map.getResource(x, mapY);
            if (r.type == NO_RES_TYPE) continue;
            if (presentation.sprites[r.type] != sprite) return false;
            if (map.resourceRegistry().presentation(static_cast<ResourceId>(r.type)).frame(r.amount, x, mapY, map.tick) >= sprite->getFrameCount()) return false;
        }
    GAGCore::MapGeometryCache::Layer layer(*cache);
    for (int y = top; y <= bottom; ++y)
    {
        int mapY = (y + viewportY) & map.getMaskH();
        bool anyDecor = false;
        for (int mapX = 0; mapX < map.getW(); ++mapX)
        {
            const auto& resource = map.getResource(mapX, mapY);
            if (resource.type == NO_RES_TYPE) frames[mapX] = -1;
            else
            {
                const auto& type = map.resourceRegistry().presentation(static_cast<ResourceId>(resource.type));
                frames[mapX] = type.frame(resource.amount, mapX, mapY, map.tick);
            }
            decorFrames[mapX] = decorSprite ? compositor.decorFrame(map, mapX, mapY) : -1;
            anyDecor |= decorFrames[mapX] >= 0;
        }
        auto drawLayer = [&](GAGCore::Sprite *layerSprite, const std::vector<int> &layerFrames, int layerKey)
        {
            auto draw = [&](int first, int last, int originX, int originY)
            {
                for (int mapX = first; mapX <= last; ++mapX)
                {
                    int frame = layerFrames[mapX];
                    if (frame < 0) continue;
                    int dx = (layerSprite->getW(frame) - 32) >> 1;
                    int dy = (layerSprite->getH(frame) - 32) >> 1;
                    gfx->drawSprite((originX + mapX) * 32 - dx, originY * 32 - dy, layerSprite, frame);
                }
                gfx->finishDrawingSprite(layerSprite, 255);
            };
            for (int x = left; x <= right;)
            {
                int mapX = (x + viewportX) & map.getMaskW();
                int width = std::min(right - x + 1, map.getW() - mapX);
                bool drawn = false;
                try
                {
                    drawn = cache->draw({mapIdentity, layerKey, 0, mapY, map.getW(), 1}, layerFrames,
                        [&] { draw(0, map.getW() - 1, 0, 0); }, mapX, mapX + width - 1,
                        float((x - mapX) * 32), float(y * 32));
                }
                catch (const std::bad_alloc&) {}
                if (!drawn)
                {
                    layer.prepareFallback();
                    // Cold-cache budget exhaustion must retain the family
                    // batching path instead of reverting to one HD bind per sprite.
                    GAGCore::SpriteDrawBatch fallback(gfx, layerSprite);
                    draw(mapX, mapX + width - 1, x - mapX, y);
                }
                x += width;
            }
        };
        if (anyDecor)
            drawLayer(decorSprite, decorFrames, 2);
        drawLayer(sprite, frames, 1);
    }
    return true;
}
}

// Terrain, resource, and area rendering. Split from Game_render.cpp.


void Game::drawMapTerrain(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const SceneMap& sceneMap, int animationTime)
{
	PERF_SCOPE_TIME(Terrain);
	Uint32 visibleTeams = Team::teamNumberToMask(localTeam); // the local team's Team::me
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;

	SoftwareTerrainCache::drawUncached(sceneMap, *globalContainer->terrain, left, top, right, bot,
									   viewportX, viewportY, visibleTeams,
									   drawOptions & DRAW_WHOLE_MAP, animationTime,
									   SoftwareTerrainCache::FallbackMode::StreamPages,
									   drawOptions & DRAW_TILED_CAPTURE);
}

void Game::drawMapResources(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const SceneMap& sceneMap)
{
	PERF_SCOPE_TIME(Resources);
	Uint32 visibleTeams = Team::teamNumberToMask(localTeam); // the local team's Team::me
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;

    if ((drawOptions & DRAW_WHOLE_MAP) && drawCachedResources(sceneMap.cacheKey(), sceneMap, left, top,
            right, bot, viewportX, viewportY)) return;
    const auto& catalog = ResourceSprites::resolve(sceneMap.frozenResourceRegistry(), sceneMap.frozenAssetBundle());
    const auto &compositor = globalContainer->terrainCompositor(sceneMap.frozenAssetBundle());
    Sprite* pendingSprite = nullptr;
    const auto flush = [&] {
        if (pendingSprite) globalContainer->gfx->finishDrawingSprite(pendingSprite, 255);
        pendingSprite = nullptr;
    };

	for (int y=top; y<=bot; y++)
		for (int x=left; x<=right; x++)
			if ((drawOptions & DRAW_WHOLE_MAP) != 0 ||
				sceneMap.isMapPartiallyDiscovered(
						x+viewportX-1,
						y+viewportY-1,
						x+viewportX+1,
						y+viewportY+1,
						visibleTeams))
			{
                auto *decorSprite = compositor.decorSprite(sceneMap, x + viewportX, y + viewportY);
				if (decorSprite)
				{
					const int decor = compositor.decorFrame(sceneMap, x + viewportX, y + viewportY);
					if (decor >= 0)
					{
						if (pendingSprite != decorSprite) { flush(); pendingSprite = decorSprite; }
						globalContainer->gfx->drawSprite((x << 5) - ((decorSprite->getW(decor) - 32) >> 1),
							(y << 5) - ((decorSprite->getH(decor) - 32) >> 1), decorSprite, decor);
					}
				}
				const auto& r = sceneMap.getResource(x+viewportX, y+viewportY);
				if (r.type!=NO_RES_TYPE)
				{
					const auto& presentation = sceneMap.resourceRegistry().presentation(static_cast<ResourceId>(r.type));
					Sprite *sprite = catalog.sprites[r.type];
					const int imgid = presentation.frame(r.amount, (x+viewportX)&sceneMap.getMaskW(), (y+viewportY)&sceneMap.getMaskH(), sceneMap.tick);
					if (!sprite || imgid >= sprite->getFrameCount())
					{
						// Explicit missing-art marker; gameplay remains authoritative.
						flush();
						globalContainer->gfx->drawFilledRect((x<<5)+8, (y<<5)+8, 16, 16, 255, 0, 255);
						globalContainer->gfx->drawFilledRect((x<<5)+12, (y<<5)+12, 8, 8, 0, 0, 0);
						continue;
					}
					if (pendingSprite != sprite) { flush(); pendingSprite = sprite; }
					const int dx=(sprite->getW(imgid)-32)>>1;
					const int dy=(sprite->getH(imgid)-32)>>1;
					globalContainer->gfx->drawSprite((x<<5)-dx, (y<<5)-dy, sprite, imgid);
				}
			}
	flush();
}

void Game::drawMapOverview(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const SceneMap& sceneMap, MapRenderState& render)
{
	const Uint8 alpha = Uint8(std::clamp(render.detail.terrainOverview, 0.f, 1.f) * 255);
	if (!alpha)
		return;
	PERF_SCOPE_TIME(Terrain);
	Uint32 visibleTeams = Team::teamNumberToMask(localTeam);
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;
	constexpr int samples = TerrainVisual::Compositor::OverviewSamples;
	const int columns = right-left+1, rows = bot-top+1;
	if (!render.overview)
		render.overview = std::make_unique<GAGCore::DrawableSurface>(columns*samples, rows*samples);
	else if (render.overview->getW()!=columns*samples || render.overview->getH()!=rows*samples)
		render.overview->setRes(columns*samples, rows*samples);
	auto *pixels = render.overview->getSDLSurface();
	if (!render.overviewCache)
		render.overviewCache = std::make_unique<OverviewTerrainCache>();
	render.overviewCache->copy(sceneMap, left, top, right, bot, viewportX, viewportY,
							   visibleTeams, drawOptions & DRAW_WHOLE_MAP, pixels);
	render.overview->markPixelsChanged();
	globalContainer->gfx->drawSurface(left*32, top*32, columns*32, rows*32, render.overview.get(), alpha);
}

void Game::drawMapTerritory(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const PresentationFrame& scene, float opacity)
{
	// A wash strong enough to read over the terrain colours, inside a solid
	// border: the edge of a colour shows where a faint tint of it does not.
	opacity = std::clamp(opacity, 0.f, 1.f);
	const Uint8 alpha = Uint8(opacity * 84), borderAlpha = Uint8(opacity * 235);
	if (!alpha)
		return;
	PERF_SCOPE_TIME(Overlay);
	const SceneEntities &entities = scene.entities;
	const SceneMap &sceneMap = scene.map;
	Uint32 visibleTeams = entities.teams[localTeam].mask;
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;
	// Each cell of a coarse grid belongs to the team with the nearest building
	// within reach. Rebuilt per frame: a few hundred buildings stamp a few
	// dozen cells each, and only while the strategic view is showing.
	constexpr int Cell = 4, Reach = 10;
	const int gridW = std::max(1, sceneMap.getW()/Cell), gridH = std::max(1, sceneMap.getH()/Cell);
	static std::vector<Uint16> owner; // team in the high byte, squared distance in the low
	owner.assign(size_t(gridW)*gridH, 0xFFFF);
	for (const SnapshotBuilding &sceneBuilding : entities.buildings)
		{
			const SnapshotBuilding *building = &sceneBuilding;
			const int teamNumber = building->team;
			if (!entities.type(*building) || entities.type(*building)->isVirtual)
				continue;
			if (!(drawOptions & DRAW_WHOLE_MAP) && !(entities.teams[teamNumber].mask & visibleTeams)
				&& !(building->seenByMask & visibleTeams))
				continue;
			const int centerX = (building->posX + entities.type(*building)->width/2)/Cell;
			const int centerY = (building->posY + entities.type(*building)->height/2)/Cell;
			const int reach = Reach/Cell;
			for (int dy=-reach; dy<=reach; dy++)
				for (int dx=-reach; dx<=reach; dx++)
				{
					const int distance = dx*dx+dy*dy;
					if (distance > reach*reach)
						continue;
					Uint16 &cell = owner[size_t((centerY+dy+gridH)%gridH)*gridW + (centerX+dx+gridW)%gridW];
					if (cell==0xFFFF || distance < (cell & 0xFF))
						cell = Uint16(teamNumber << 8 | distance);
				}
		}
	const auto teamAt = [&](int x, int y) -> int
	{
		const int cellX = powerOfTwoRemainder(powerOfTwoRemainder((x+viewportX), sceneMap.getW())+sceneMap.getW(), sceneMap.getW())/Cell;
		const int cellY = powerOfTwoRemainder(powerOfTwoRemainder((y+viewportY), sceneMap.getH())+sceneMap.getH(), sceneMap.getH())/Cell;
		const Uint16 cell = owner[size_t(std::min(cellY, gridH-1))*gridW + std::min(cellX, gridW-1)];
		return cell==0xFFFF ? -1 : cell >> 8;
	};
	// Dark team colours vanish against dark ground, so every team's wash is
	// brought up to the same brightness, keeping its hue.
	const auto washColor = [&](int team, Uint8 a)
	{
		const GAGCore::Color &color = presentationColor(entities.teams[team].color);
		const int brightest = std::max({int(color.r), int(color.g), int(color.b), 1});
		const auto lift = [&](Uint8 channel) { return Uint8(std::min(255, 40 + channel * 215 / brightest)); };
		return GAGCore::Color(lift(color.r), lift(color.g), lift(color.b), a);
	};
	// The border keeps its width on screen: two points, in map pixels.
	GAGCore::GraphicContext *gfx = globalContainer->gfx;
	const int border = std::clamp(int(std::lround(2 * gfx->logicalUnitsPerPoint() / gfx->mapTransformScale())), 1, 16);
	for (int y=top; y<=bot; y++)
	{
		for (int x=left; x<=right;)
		{
			const int team = teamAt(x, y);
			int end = x+1;
			while (end<=right && teamAt(end, y)==team)
				end++;
			if (team>=0)
			{
				gfx->drawMapFill(x*32, y*32, end*32, (y+1)*32, washColor(team, alpha));
				const GAGCore::Color edge = washColor(team, borderAlpha);
				if (teamAt(x-1, y)!=team)
					gfx->drawMapFill(x*32, y*32, x*32+border, (y+1)*32, edge);
				if (teamAt(end, y)!=team)
					gfx->drawMapFill(end*32-border, y*32, end*32, (y+1)*32, edge);
			}
			x = end;
		}
		// Top and bottom edges, in runs along the row.
		for (int side=-1; side<=1; side+=2)
			for (int x=left; x<=right;)
			{
				const int team = teamAt(x, y);
				if (team<0 || teamAt(x, y+side)==team)
				{
					x++;
					continue;
				}
				int end = x+1;
				while (end<=right && teamAt(end, y)==team && teamAt(end, y+side)!=team)
					end++;
				const int edgeY = side<0 ? y*32 : (y+1)*32-border;
				gfx->drawMapFill(x*32, edgeY, end*32, edgeY+border, washColor(team, borderAlpha));
				x = end;
			}
	}
}

void Game::drawMapDebugAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, ViewState& view)
{
	const PresentationFrame& scene = view.drawnScene();
	const SceneMap& map = scene.map;
	const auto& selected = scene.entities.selectedBuilding;
	if (!selected.verbose) return;
	for (int y=top-1; y<=bot; ++y)
		for (int x=left-1; x<=right; ++x)
		{
			if (!selected.debugGradient.empty())
				globalContainer->gfx->drawString(x*32, y*32, globalContainer->littleFont,
					selected.debugGradient[map.coordToIndex(x+viewportX, y+viewportY)]);
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 192, 192, 192));
			globalContainer->gfx->drawString(x*32, y*32+16, globalContainer->littleFont, (x+viewportX)&map.getMaskW());
			globalContainer->gfx->drawString(x*32+16, y*32+8, globalContainer->littleFont, (y+viewportY)&map.getMaskH());
			globalContainer->littleFont->popStyle();
		}
}

/**
 * Draws the visible (viewport) part of the given map
 */
void Game::drawMapAreas(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, ViewState& view, const SceneMap& sceneMap, bool advanceAnimation)
{
	PERF_SCOPE_TIME(Overlay);
	int &areaAnimationTick = view.render.areaAnimationTick;

	if ((drawOptions & DRAW_AREA) != 0 && (!globalContainer->isViewingGame() || globalContainer->replayShowAreas))
	{
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, sceneMap, &SceneMap::isForbiddenInDisplayedView, areaAnimationTick, ForbiddenArea, view.render, view.displayedAreas ? &(*view.displayedAreas)[0] : nullptr);
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, sceneMap, &SceneMap::isGuardAreaInDisplayedView, areaAnimationTick, GuardArea, view.render, view.displayedAreas ? &(*view.displayedAreas)[1] : nullptr);
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, sceneMap, &SceneMap::isClearAreaInDisplayedView, areaAnimationTick, ClearingArea, view.render, view.displayedAreas ? &(*view.displayedAreas)[2] : nullptr);
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, sceneMap, &SceneMap::isFarmAreaInDisplayedView, areaAnimationTick, FarmArea, view.render, view.displayedAreas ? &(*view.displayedAreas)[3] : nullptr);
		for (int y=top; y<bot; y++)
			for (int x=left; x<right; x++)
			{
				if((drawOptions & DRAW_NO_RESOURCE_GROWTH_AREAS) != 0)
				{
					if(!sceneMap.canResourcesGrow(x+viewportX, y+viewportY))
					{
						globalContainer->gfx->drawLine((x<<5), 8+(y<<5), 32+(x<<5), 8+(y<<5), 128, 64, 0);
						globalContainer->gfx->drawLine((x<<5), 16+(y<<5), 32+(x<<5), 16+(y<<5), 128, 64, 0);
						globalContainer->gfx->drawLine((x<<5), 24+(y<<5), 32+(x<<5), 24+(y<<5), 128, 64, 0);

						if (sceneMap.canResourcesGrow(x+viewportX, y+viewportY-1))
							globalContainer->gfx->drawHorzLine((x<<5), (y<<5), 32, 255, 128, 0);
						if (sceneMap.canResourcesGrow(x+viewportX, y+viewportY+1))
							globalContainer->gfx->drawHorzLine((x<<5), 32+(y<<5), 32, 255, 128, 0);

						if (sceneMap.canResourcesGrow(x+viewportX-1, y+viewportY))
							globalContainer->gfx->drawVertLine((x<<5), (y<<5), 32, 255, 128, 0);
						if (sceneMap.canResourcesGrow(x+viewportX+1, y+viewportY))
							globalContainer->gfx->drawVertLine(32+(x<<5), (y<<5), 32, 255, 128, 0);
						}
				}
			}
		if (advanceAnimation) areaAnimationTick++;
	}
}

/**
 * Draws the visible (viewport) part of the given map
 */
void Game::drawMapArea(int left, int top, int right, int bot, int sw,
		int sh, int viewportX, int viewportY, int localTeam,
		Uint32 drawOptions, const SceneMap& map, bool (SceneMap::*mapIs)(int, int) const, int areaAnimationTick,
		AreaType areaType, const MapRenderState& render, const Utilities::BitArray* preview)
{
    const auto is=[&](int x,int y){return preview ? preview->get(map.coordToIndex(x,y)) : (map.*mapIs)(x,y);};
	Sprite* sprite;
	GAGCore::Color c;
	switch (areaType)
	{
		case ClearingArea: sprite = globalContainer->areaClearing; c = GAGCore::Color(255,255,0); break;
		case ForbiddenArea: sprite = globalContainer->areaForbidden; c = GAGCore::Color(255,0,0); break;
		case GuardArea: sprite = globalContainer->areaGuard; c = GAGCore::Color(0,0,255); break;
		case FarmArea: sprite = globalContainer->areaFarm; c = GAGCore::Color(110,240,120); break;
		default: assert(false);
	}
	// Zoomed out, the pattern is noise and a one-pixel outline is most of a
	// tile, so the zone becomes a flat tint: an area keeps its shape at any
	// scale where a line cannot. The tint steps back in the strategic view
	// unless the player is painting zones.
	const ZoomDetail &detail = render.detail;
	const int patternAlpha = int(detail.zonePattern * 255);
	const float tintStrength = render.zonesEmphasised ? 0.45f : 0.45f - 0.2f * detail.strategic;
	const GAGCore::Color tint(c.r, c.g, c.b, Uint8(detail.zoneTint * tintStrength * 255));
	const GAGCore::Color outline(c.r, c.g, c.b, Uint8(detail.zoneOutline * 255));
	for (int y=top; y<bot; y++)
	{
		for (int x=left; x<right; x++)
		{
			if (is(x+viewportX, y+viewportY))
			{
				if (tint.a)
				{
					// One fill per horizontal run of zone tiles.
					int end = x+1;
					while (end<right && is(end+viewportX, y+viewportY))
						end++;
					if (x==left || !is(x-1+viewportX, y+viewportY))
						globalContainer->gfx->drawMapFill(x*32, y*32, end*32, (y+1)*32, tint);
				}
				if (patternAlpha)
				{
					int randId = (x+viewportX) * 7919 + (y+viewportY) * 17;
					int frame = ((randId + areaAnimationTick) % (sprite->getFrameCount() * 2)) / 2;
					globalContainer->gfx->drawSprite((x<<5), (y<<5), sprite, frame, patternAlpha);
				}
				if (!outline.a)
					continue;

				if (!is(x+viewportX, y+viewportY-1))
					globalContainer->gfx->drawMapBoundary(x*32, y*32, (x+1)*32, y*32, outline, detail.zoneStrokeMaxPoints);
				if (!is(x+viewportX, y+viewportY+1))
					globalContainer->gfx->drawMapBoundary(x*32, (y+1)*32, (x+1)*32, (y+1)*32, outline, detail.zoneStrokeMaxPoints);

				if (!is(x+viewportX-1, y+viewportY))
					globalContainer->gfx->drawMapBoundary(x*32, y*32, x*32, (y+1)*32, outline, detail.zoneStrokeMaxPoints);
				if (!is(x+viewportX+1, y+viewportY))
					globalContainer->gfx->drawMapBoundary((x+1)*32, y*32, (x+1)*32, (y+1)*32, outline, detail.zoneStrokeMaxPoints);
			}
		}
	}
	globalContainer->gfx->finishDrawingSprite(sprite, patternAlpha);
}

void Game::drawMapScriptAreas(int left, int top, int right, int bot, int viewportX, int viewportY, const SceneMap& map)
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
