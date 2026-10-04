// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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

namespace
{
// Terrain fits its tile, so rectangular chunks preserve painter order. Split
// at canonical 32-tile and torus boundaries; camera motion only changes the
// translation, and visibility is included in the exact cached frame vector.
template<class Describe>
bool drawCachedTerrain(const void *mapIdentity, Sprite *sprite, int left, int top,
    int right, int bottom, int viewportX, int viewportY, int maskW, int maskH,
    Describe describe)
{
    auto *gfx = globalContainer->gfx;
    auto *batch = gfx->getRenderBatch();
    if (!batch) return false;
    std::vector<int> frames;
    GAGCore::MapGeometryCache *cache;
    try { frames.reserve(32 * 32); cache = &batch->geometryCache(); }
    catch (const std::bad_alloc&) { return false; }
    GAGCore::MapGeometryCache::Layer layer(*cache);
    for (int y = top; y <= bottom;)
    {
        int mapY = (y + viewportY) & maskH;
        int height = std::min({bottom - y + 1, 32 - mapY % 32, maskH + 1 - mapY});
        for (int x = left; x <= right;)
        {
            int mapX = (x + viewportX) & maskW;
            int width = std::min({right - x + 1, 32 - mapX % 32, maskW + 1 - mapX});
            frames.clear();
            for (int dy = 0; dy < height; ++dy)
                for (int dx = 0; dx < width; ++dx)
                    frames.push_back(describe(mapX + dx, mapY + dy));
            auto draw = [&](int originX, int originY)
            {
                std::size_t index = 0;
                for (int dy = 0; dy < height; ++dy)
                    for (int dx = 0; dx < width; ++dx)
                    {
                        int frame = frames[index++];
                        if (frame >= 0) gfx->drawSprite((originX + dx) * 32, (originY + dy) * 32, sprite, frame);
                    }
                gfx->finishDrawingSprite(sprite, 255);
            };
            bool drawn = false;
            try
            {
                drawn = cache->draw({mapIdentity, 0, mapX, mapY, width, height}, frames,
                    [&] { draw(0, 0); }, -1, -1, float(x * 32), float(y * 32));
            }
            catch (const std::bad_alloc&) {} // std::function construction can fail before entering draw().
            if (!drawn) { layer.prepareFallback(); draw(x, y); }
            x += width;
        }
        y += height;
    }
    return true;
}

// Resource images overlap neighboring tiles. Cache ONE complete canonical row,
// retain source traversal order inside texture runs, and select the exact source
// tile range with binary searches. Splitting rectangles vertically would change
// the painter order. Partial discovery uses the ordinary path below instead.
bool drawCachedResources(const void *mapIdentity, const SceneMap& map, int left, int top,
    int right, int bottom, int viewportX, int viewportY)
{
    auto *gfx = globalContainer->gfx;
    auto *batch = gfx->getRenderBatch();
    if (!batch) return false;
    auto *sprite = globalContainer->resources;
    std::vector<int> frames;
    GAGCore::MapGeometryCache *cache;
    try { frames.resize(map.getW()); cache = &batch->geometryCache(); }
    catch (const std::bad_alloc&) { return false; }
    GAGCore::MapGeometryCache::Layer layer(*cache);
    for (int y = top; y <= bottom; ++y)
    {
        int mapY = (y + viewportY) & map.getMaskH();
        for (int mapX = 0; mapX < map.getW(); ++mapX)
        {
            const auto& resource = map.getResource(mapX, mapY);
            if (resource.type == NO_RES_TYPE) frames[mapX] = -1;
            else
            {
                const auto *type = globalContainer->resourcesTypes.get(resource.type);
                frames[mapX] = type->gfxId + resource.variety * type->sizesCount + resource.amount - (type->eternal ? 0 : 1);
            }
        }
        auto draw = [&](int first, int last, int originX, int originY)
        {
            for (int mapX = first; mapX <= last; ++mapX)
            {
                int frame = frames[mapX];
                if (frame < 0) continue;
                int dx = (sprite->getW(frame) - 32) >> 1;
                int dy = (sprite->getH(frame) - 32) >> 1;
                gfx->drawSprite((originX + mapX) * 32 - dx, originY * 32 - dy, sprite, frame);
            }
            gfx->finishDrawingSprite(sprite, 255);
        };
        for (int x = left; x <= right;)
        {
            int mapX = (x + viewportX) & map.getMaskW();
            int width = std::min(right - x + 1, map.getW() - mapX);
            bool drawn = false;
            try
            {
                drawn = cache->draw({mapIdentity, 1, 0, mapY, map.getW(), 1}, frames,
                    [&] { draw(0, map.getW() - 1, 0, 0); }, mapX, mapX + width - 1,
                    float((x - mapX) * 32), float(y * 32));
            }
            catch (const std::bad_alloc&) {}
            if (!drawn)
            {
                layer.prepareFallback();
                // Cold-cache budget exhaustion must retain the resource-family
                // batching path instead of reverting to one HD bind per sprite.
                GAGCore::SpriteDrawBatch fallback(gfx, sprite);
                draw(mapX, mapX + width - 1, x - mapX, y);
            }
            x += width;
        }
    }
    return true;
}
}

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
	PERF_SCOPE_TIME(Water);
	// Tile size of the data/gfx/water sprite, in pixels.
	static const int WATER_TILE_SIZE = 512;
	int waterStartX = -(((viewportX<<5)+time/2) % WATER_TILE_SIZE);
	int waterStartY = -((viewportY<<5) % WATER_TILE_SIZE);
	for (int y=waterStartY; y<sh; y += WATER_TILE_SIZE)
		for (int x=waterStartX; x<sw; x += WATER_TILE_SIZE)
			globalContainer->gfx->drawSprite(x, y, globalContainer->terrainWater, 0);
	globalContainer->gfx->finishDrawingSprite(globalContainer->terrainWater, 255);
}

void Game::drawMapTerrain(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const SceneMap& sceneMap)
{
	PERF_SCOPE_TIME(Terrain);
	Uint32 visibleTeams = Team::teamNumberToMask(localTeam); // the local team's Team::me
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;

    if (drawCachedTerrain(sceneMap.cacheKey(), globalContainer->terrain, left, top, right, bot,
            viewportX, viewportY, sceneMap.getMaskW(), sceneMap.getMaskH(), [&](int x, int y)
            {
                bool visible = (drawOptions & DRAW_WHOLE_MAP) ||
                    sceneMap.isMapPartiallyDiscovered(x - 1, y - 1, x + 1, y + 1, visibleTeams);
                int frame = sceneMap.getTerrain(x, y);
                return visible && frame < 256 ? frame : -1; // Water is animated separately.
            })) return;

	// we draw the terrains, eventually with debug rects:
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
				// draw terrain
				int id=sceneMap.getTerrain(x+viewportX, y+viewportY);
				Sprite *sprite;
				if (id<Map::TERRAIN_TILE_END)
				{
					sprite=globalContainer->terrain;
				}
				else
				{
					assert(false); // Now there shouldn't be any more resources on "terrain".
					sprite=globalContainer->resources;
					id-=272;
				}
				if ((id < 256) || (id >= 256+16))
					globalContainer->gfx->drawSprite(x<<5, y<<5, sprite, id);
			}
	globalContainer->gfx->finishDrawingSprite(globalContainer->terrain, 255);
}

void Game::drawMapResources(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const SceneMap& sceneMap)
{
	PERF_SCOPE_TIME(Resources);
	Uint32 visibleTeams = Team::teamNumberToMask(localTeam); // the local team's Team::me
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;

    if ((drawOptions & DRAW_WHOLE_MAP) && drawCachedResources(sceneMap.cacheKey(), sceneMap, left, top,
            right, bot, viewportX, viewportY)) return;
    GAGCore::SpriteDrawBatch batch(globalContainer->gfx, globalContainer->resources);

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
				const auto& r = sceneMap.getResource(x+viewportX, y+viewportY);
				if (r.type!=NO_RES_TYPE)
				{
					Sprite *sprite=globalContainer->resources;
					int type=r.type;
					int amount=r.amount;
					int variety=r.variety;
					const ResourceType *rt=globalContainer->resourcesTypes.get(type);
					int imgid=rt->gfxId+(variety*rt->sizesCount)+amount;
					if (!rt->eternal)
						imgid--;
					int dx=(sprite->getW(imgid)-32)>>1;
					int dy=(sprite->getH(imgid)-32)>>1;
					assert(type>=0);
					assert(type<(int)globalContainer->resourcesTypes.size());
					assert(amount>=0);
					assert(amount<=rt->sizesCount);
					assert(variety>=0);
					assert(variety<rt->varietiesCount);
					globalContainer->gfx->drawSprite((x<<5)-dx, (y<<5)-dy, sprite, imgid);
				}
			}
	globalContainer->gfx->finishDrawingSprite(globalContainer->resources, 255);
}

void Game::drawMapOverview(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const SceneMap& sceneMap, MapRenderState& render)
{
	const Uint8 alpha = Uint8(std::clamp(render.detail.terrainOverview, 0.f, 1.f) * 255);
	if (!alpha)
		return;
	PERF_SCOPE_TIME(Terrain);
	Uint32 visibleTeams = Team::teamNumberToMask(localTeam);
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;
	// Averages of the lit terrain artwork, indexed by undermap type (water,
	// sand, grass), so the cross-fade from the detailed tiles keeps its hue.
	static const Uint8 terrainColor[3][3] = {{70, 50, 191}, {182, 168, 48}, {30, 113, 30}};
	const auto colorOf = [&](int x, int y) -> Uint32
	{
		const int terrain = std::clamp(sceneMap.getUMTerrain(x+viewportX, y+viewportY), 0, 2);
		int r = terrainColor[terrain][0], g = terrainColor[terrain][1], b = terrainColor[terrain][2];
		const auto &resource = sceneMap.getResource(x+viewportX, y+viewportY);
		if (resource.type != NO_RES_TYPE && ((drawOptions & DRAW_WHOLE_MAP) != 0 ||
			sceneMap.isMapPartiallyDiscovered(x+viewportX-1, y+viewportY-1, x+viewportX+1, y+viewportY+1, visibleTeams)))
		{
			// A field of resources reads as its minimap colour over the ground it grows on.
			const ResourceType *rt = globalContainer->resourcesTypes.get(resource.type);
			r = (r + 3*rt->minimapR) / 4;
			g = (g + 3*rt->minimapG) / 4;
			b = (b + 3*rt->minimapB) / 4;
		}
		return Uint32(r) << 16 | Uint32(g) << 8 | Uint32(b);
	};
	// One pixel per tile, stretched over the map in a single draw: thousands of
	// translucent rectangles a frame cost more than the terrain they cover.
	const int columns = right-left+1, rows = bot-top+1;
	if (!render.overview)
		render.overview = std::make_unique<GAGCore::DrawableSurface>(columns, rows);
	else if (render.overview->getW()!=columns || render.overview->getH()!=rows)
		render.overview->setRes(columns, rows);
	for (int y=top; y<=bot; y++)
		for (int x=left; x<=right; x++)
		{
			const Uint32 color = colorOf(x, y);
			render.overview->drawPixel(x-left, y-top, Uint8(color >> 16), Uint8(color >> 8), Uint8(color), 255);
		}
	globalContainer->gfx->drawSurface(left*32, top*32, columns*32, rows*32, render.overview.get(), alpha);
}

void Game::drawMapTerritory(int left, int top, int right, int bot, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const Scene& scene, float opacity)
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
	Uint32 visibleTeams = entities.teams[localTeam].me;
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;
	// Each cell of a coarse grid belongs to the team with the nearest building
	// within reach. Rebuilt per frame: a few hundred buildings stamp a few
	// dozen cells each, and only while the strategic view is showing.
	constexpr int Cell = 4, Reach = 10;
	const int gridW = std::max(1, sceneMap.getW()/Cell), gridH = std::max(1, sceneMap.getH()/Cell);
	static std::vector<Uint16> owner; // team in the high byte, squared distance in the low
	owner.assign(size_t(gridW)*gridH, 0xFFFF);
	for (const SceneBuilding &sceneBuilding : entities.buildings)
		{
			const SceneBuilding *building = &sceneBuilding;
			const int teamNumber = building->team;
			if (!building->type || building->type->isVirtual)
				continue;
			if (!(drawOptions & DRAW_WHOLE_MAP) && !(entities.teams[teamNumber].me & visibleTeams)
				&& !(building->seenByMask & visibleTeams))
				continue;
			const int centerX = (building->posX + building->type->width/2)/Cell;
			const int centerY = (building->posY + building->type->height/2)/Cell;
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
		const int cellX = ((x+viewportX)%sceneMap.getW()+sceneMap.getW())%sceneMap.getW()/Cell;
		const int cellY = ((y+viewportY)%sceneMap.getH()+sceneMap.getH())%sceneMap.getH()/Cell;
		const Uint16 cell = owner[size_t(std::min(cellY, gridH-1))*gridW + std::min(cellX, gridW-1)];
		return cell==0xFFFF ? -1 : cell >> 8;
	};
	// Dark team colours vanish against dark ground, so every team's wash is
	// brought up to the same brightness, keeping its hue.
	const auto washColor = [&](int team, Uint8 a)
	{
		const GAGCore::Color &color = entities.teams[team].color;
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
	const Scene& scene = view.drawnScene();
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
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, sceneMap, &SceneMap::isForbiddenInDisplayedView, areaAnimationTick, ForbiddenArea, view.render);
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, sceneMap, &SceneMap::isGuardAreaInDisplayedView, areaAnimationTick, GuardArea, view.render);
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, sceneMap, &SceneMap::isClearAreaInDisplayedView, areaAnimationTick, ClearingArea, view.render);
		drawMapArea(left, top, right, bot, sw, sh, viewportX, viewportY, localTeam, drawOptions, sceneMap, &SceneMap::isFarmAreaInDisplayedView, areaAnimationTick, FarmArea, view.render);
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
		AreaType areaType, const MapRenderState& render)
{
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
			if ((map.*mapIs)(x+viewportX, y+viewportY))
			{
				if (tint.a)
				{
					// One fill per horizontal run of zone tiles.
					int end = x+1;
					while (end<right && (map.*mapIs)(end+viewportX, y+viewportY))
						end++;
					if (x==left || !(map.*mapIs)(x-1+viewportX, y+viewportY))
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

				if (!(map.*mapIs)(x+viewportX, y+viewportY-1))
					globalContainer->gfx->drawMapBoundary(x*32, y*32, (x+1)*32, y*32, outline, detail.zoneStrokeMaxPoints);
				if (!(map.*mapIs)(x+viewportX, y+viewportY+1))
					globalContainer->gfx->drawMapBoundary(x*32, (y+1)*32, (x+1)*32, (y+1)*32, outline, detail.zoneStrokeMaxPoints);

				if (!(map.*mapIs)(x+viewportX-1, y+viewportY))
					globalContainer->gfx->drawMapBoundary(x*32, y*32, x*32, (y+1)*32, outline, detail.zoneStrokeMaxPoints);
				if (!(map.*mapIs)(x+viewportX+1, y+viewportY))
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
