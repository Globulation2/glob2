// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "scene/Scene.h"
#include "ColonySkinPreview.h"
#include "IntBuildingType.h"
#include <PerformanceTelemetry.h>
#include <iostream>

#include "AICastor.h"

#include <assert.h>

#include <set>
#include <tuple>
#include <string>
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
#include "IntBuildingType.h"


// Building rendering. Split from Game_render.cpp.


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


void Game::drawMapBuilding(int x, int y, int gid, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, const Scene& scene, MapRenderState* drawnRender, ViewState* view)
{
	const SceneEntities &entities = scene.entities;
	const SceneMap &map = scene.map; // the extracted map, not Game::map
	const SceneBuilding *building = entities.building(gid);
	assert(building);
	BuildingType *type=building->type;
	const SceneTeam *team=&entities.owner(*building);

	int imgid;
	if (type->crossConnectMultiImage)
	{
		// Cross-connect grid lookup. Only non-virtual buildings have
		// crossConnectMultiImage, and non-virtual buildings can never be
		// moved by the player, so the authoritative posX/posY is correct
		// here — no pending shadow to consult.
		auto sameTypeNeighbour = [&](int nx, int ny)
		{
			Uint16 b = map.getBuilding(nx, ny);
			return (b != NOGBID)
				&& (Building::GIDtoTeam(b) == team->teamNumber)
				&& (entities.building(b)->type == type);
		};
		int add = 0;
		if (sameTypeNeighbour(building->posX, building->posY-1))               // up
			add |= (1<<3);
		if (sameTypeNeighbour(building->posX, building->posY+type->height))    // bottom
			add |= (1<<2);
		if (sameTypeNeighbour(building->posX-1, building->posY))               // left
			add |= (1<<1);
		if (sameTypeNeighbour(building->posX+type->width, building->posY))     // right
			add |= (1<<0);
		imgid = type->gameSpriteImage + add;
	}
	else
	{
		// hpMax+1 (not hpMax) so that at full HP the integer division stays strictly
		// below gameSpriteCount, leaving damageImgShift == 0 (pristine sprite). Using
		// plain hpMax would yield shift == -1 at hp == hpMax and trip the assert below.
		assert(building->hp <= building->effectiveMaxHp);
		int damageImgShift = type->gameSpriteCount - ((building->hp * type->gameSpriteCount) / (building->effectiveMaxHp+1)) - 1;
		assert(damageImgShift >= 0);
		imgid = type->gameSpriteImage + damageImgShift;
	}
	int dx, dy;


	// select buildings and set the team colors
	Sprite *buildingSprite = type->gameSpritePtr;
	dx = (type->width<<5)-buildingSprite->getW(imgid);
	dy = (type->height<<5)-buildingSprite->getH(imgid);
	auto color=team->color;
    if(view)if(const auto chosen=view->render.skinPreview().buildingColor(team->teamNumber))
        color=GAGCore::Color((*chosen>>16)&255,(*chosen>>8)&255,*chosen&255);
    buildingSprite->setBaseColor(color);

	// draw building. Zoomed far out, the sprite cross-fades to a chip in its
	// team's colour carrying an icon of what the building is for; where chips
	// would pile up the most important one stays.
	const ZoomDetail *detail = drawnRender ? &drawnRender->detail : nullptr;
	const float spriteOpacity = detail ? detail->buildingSprite : 1.f;
	// The sprite stays opaque under the chip fading in over it, and goes once
	// the chip is solid: a translucent sprite would leave the sprite batch.
	if (spriteOpacity > 0)
	{
		const bool skinned = view && type->shortTypeNum == IntBuildingType::SWARM_BUILDING
			&& !type->isBuildingSite && view->render.skinPreview().drawSwarm(
				*globalContainer->gfx, team->teamNumber, x+dx, y+dy,
				buildingSprite->getW(imgid), buildingSprite->getH(imgid));
		if (!skinned) globalContainer->gfx->drawSprite(x+dx, y+dy, buildingSprite, imgid);
	}
	if (detail && detail->buildingIcon > 0)
	{
		// Icon frames follow IntBuildingType up to the clearing flag; the market comes last.
		const int shortType = building->shortTypeNum;
		const bool wall = shortType==IntBuildingType::STONE_WALL;
		const int icon = shortType==IntBuildingType::MARKET_BUILDING ? 11 : std::clamp(shortType, 0, 10);
		const bool hurt = type->hpMax && building->hp!=building->effectiveMaxHp && !type->isBuildingSite;
		const int priority = hurt ? 200 : shortType==IntBuildingType::DEFENSE_BUILDING ? 150
			: shortType==IntBuildingType::SWARM_BUILDING ? 120 : 100;
		drawnRender->overlays.glyph(*globalContainer->gfx, x, y, x+type->width*32, y+type->height*32,
			icon, wall ? MapOverlayQueue::Tile : MapOverlayQueue::Chip, type->level, type->isBuildingSite,
			priority, color.r, color.g, color.b, detail->buildingIcon);
	}
	globalContainer->gfx->finishDrawingSprite(buildingSprite, 255);

	if ((drawOptions & DRAW_BUILDING_RECT) != 0)
	{
		int rectW=(type->width )<<5;
		int rectH=(type->height)<<5;
		globalContainer->gfx->drawRect(x, y, rectW, rectH, 255, 255, 255, 127);

		BuildingType *upgradedType=globalContainer->buildingsTypes.getLastLevel(building->typeNum);
		int upgradedRectX=x+((upgradedType->decLeft-type->decLeft)<<5);
		int upgradedRectY=y+((upgradedType->decTop-type->decTop)<<5);
		int upgradedRectW=(upgradedType->width)<<5;
		int upgradedRectH=(upgradedType->height)<<5;

		globalContainer->gfx->drawRect(upgradedRectX, upgradedRectY, upgradedRectW, upgradedRectH, 255, 255, 255, 127);
	}

	Uint32 visibleTeams = entities.teams[localTeam].me;
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;

	if (((drawOptions & DRAW_HEALTH_FOOD_BAR) != 0) && (team->sharedVisionOther & visibleTeams))
	{
		// TODO : find better color for this
		if (type->hpMax)
		{
			int maxWidth, actWidth, addDec;
			float hpRatio=(float)building->hp/(float)building->effectiveMaxHp;
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

			anchorBars(x+type->width*16, y+decy, drawnRender);
			if (building->hp!=building->effectiveMaxHp || !building->type->crossConnectMultiImage)
				drawHealthBar(x+healDecx, y+decy-4, maxWidth, actWidth, hpRatio, drawnRender);
		}

		// Attention outlasts the bars when zoomed out, as a status pip: damage, a building
		// with under half its workers, an inn without food, a tower without ammunition.
		const bool damaged = type->hpMax && building->hp!=building->effectiveMaxHp;
		const bool understaffed = building->maxUnitWorking>0 && building->unitsWorking*2<building->maxUnitWorking;
		const bool unfed = type->canFeedUnit && building->resources[WHEAT]==0;
		const bool unarmed = type->maxBullets && building->bullets==0;
		anchorBars(x+type->width*32, y, drawnRender);
		if (building->maxUnitInside>0)
			drawPointBar(x+type->width*32-4, y+1, BOTTOM_TO_TOP, building->maxUnitInside, building->unitsInside, 255, 255, 255, 2, drawnRender);
		anchorBars(x+type->width*16, y, drawnRender);
		if (building->maxUnitWorking>0)
			drawPointBar(x+type->width*16-((3*building->maxUnitWorking)>>1), y+1,LEFT_TO_RIGHT , building->maxUnitWorking, building->unitsWorking, 0, 255, 255, 255, 255, 64, 0, 2, drawnRender);

		anchorBars(x, y, drawnRender);
		if ((type->canFeedUnit) || (type->unitProductionTime))
			drawBuildingResourceBar(x+1, y+1, type, type->maxResource[WHEAT], building->resources[WHEAT], 255, 255, 120, drawnRender);

		anchorBars(x, y, drawnRender);
		if (type->maxBullets)
			drawBuildingResourceBar(x+1, y+1, type, type->maxBullets, building->bullets, 200, 200, 200, drawnRender);
		if (damaged)
			drawStatusPip(x+type->width*32-4, y+4, 255, 0, 0, drawnRender);
		else if (understaffed || unfed || unarmed)
			drawStatusPip(x+type->width*32-4, y+4, 255, 176, 0, drawnRender);
	}

	if (drawOptions & DRAW_ACCESSIBILITY)
	{
		std::ostringstream oss;
		oss << team->teamNumber;
		int accessW = globalContainer->littleFont->getStringWidth(oss.str().c_str());
		int accessH = globalContainer->littleFont->getStringHeight(oss.str().c_str());
		int accessX = x+(((type->width<<5)-accessW)>>1);
		int accessY = y+(((type->height<<5)-accessH)>>1);
		globalContainer->gfx->drawFilledRect(accessX-4, accessY, accessW+8, accessH, Color(0, 0, 0, 127));
		globalContainer->gfx->drawRect(accessX-4, accessY, accessW+8, accessH, Color(255, 255, 255, 127));
		globalContainer->gfx->drawString(accessX, accessY, globalContainer->littleFont, oss.str());
	}

	if(entities.highlightBuildingType & (1<<building->shortTypeNum))
	{
		globalContainer->gfx->drawSprite(x + buildingSprite->getW(imgid)/2 - 16, y-36, globalContainer->gamegui, 36);
	}
}


void Game::drawMapGroundBuildings(int left, int top, int right, int bot, int sw, int sh, int viewportX, int viewportY, int localTeam, Uint32 drawOptions, std::set<Uint16> *visibleBuildings, const BuildingGuiStateMap* buildingGuiState, const Scene& scene, MapRenderState* drawnRender, ViewState* view)
{
	PERF_SCOPE_TIME(GroundBuildings);
	const SceneEntities &entities = scene.entities;
	const SceneMap &map = scene.map; // the extracted map, not Game::map
	Uint32 visibleTeams = entities.teams[localTeam].me;
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;

	std::set<Uint16> drawnBuildings;
	std::set<std::tuple<Uint16, int, int>> drawnCopies;
	for (int y=top-1; y<=bot; y++)
		for (int x=left-1; x<=right; x++)
		{
			Uint16 gid=map.getBuilding(x+viewportX, y+viewportY);
			if (gid!=NOGBID) // Then this is a building
			{
				int id = Building::GIDtoID(gid);
				int team = Building::GIDtoTeam(gid);

				(void)id;
				(void)team;
				const SceneBuilding *building=entities.building(gid);
				assert(building);
				const int originX = x - ((x + viewportX - building->posX) & map.getMaskW());
				const int originY = y - ((y + viewportY - building->posY) & map.getMaskH());
				const auto copy = std::make_tuple(gid, originX, originY);
				if(drawnCopies.find(copy) == drawnCopies.end())
				{
					if (((drawOptions & DRAW_WHOLE_MAP) != 0)
						|| Building::GIDtoTeam(gid)==localTeam
						|| (building->seenByMask & visibleTeams)
						|| map.isFOWDiscovered(x+viewportX, y+viewportY, visibleTeams))
					{
						int px,py;
						const Sint32 dispX = buildingGuiState ? displayedPosX(*buildingGuiState, building->gid, building->posX) : building->posX;
						const Sint32 dispY = buildingGuiState ? displayedPosY(*buildingGuiState, building->gid, building->posY) : building->posY;
						px = originX * 32 + (dispX - building->posX) * 32;
						py = originY * 32 + (dispY - building->posY) * 32;
						drawMapBuilding(px, py, gid, viewportX, viewportY, localTeam, drawOptions, scene, drawnRender, view);
						drawnCopies.insert(copy);
						drawnBuildings.insert(building->gid);
					}
				}
			}
		}
	if(visibleBuildings)
		*visibleBuildings = drawnBuildings;
}
