// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "../render/MapCopies.h"
#include "GameGUIViewport.h"
#include "GameGUIGhostBuildingManager.h"

#include "GlobalContainer.h"
#include "render/scene/Scene.h"

namespace
{
	/// Alpha the ghost sprite is blended with, so it reads as "ordered but not
	/// built yet" against the terrain underneath.
	constexpr int GHOST_SPRITE_ALPHA = 200;
}

void GameGUIGhostBuildingManager::addBuilding(Sint32 typeNum, int x, int y)
{
	buildings.push_back(GhostBuilding{typeNum, x, y});
}



bool GameGUIGhostBuildingManager::isGhostBuilding(const PresentationFrame& scene, int x, int y, int w, int h) const
{
	if (!scene.map.getW() || !scene.map.getH()) return false;
	for(const GhostBuilding& ghost : buildings)
	{
		if (!scene.buildingTypes || ghost.typeNum < 0 || size_t(ghost.typeNum) >= scene.buildingTypes->size()) continue;
		const BuildingType *bt = &scene.buildingTypes->at(ghost.typeNum);
		// Two footprints on a torus collide only if they overlap on both axes
		// independently.
		if(wrappedRangesOverlap(x, w, ghost.x, bt->width, scene.map.getW()) &&
		   wrappedRangesOverlap(y, h, ghost.y, bt->height, scene.map.getH()))
			return true;
	}
	return false;
}



void GameGUIGhostBuildingManager::removeBuilding(int x, int y)
{
	for(unsigned i=0; i<buildings.size();)
	{
		if(buildings[i].x == x && buildings[i].y == y)
		{
			buildings.erase(buildings.begin() + i);
		}
		else
		{
			++i;
		}
	}
}



void GameGUIGhostBuildingManager::drawAll(const PresentationFrame& scene, int viewportX, int viewportY, int localTeam, int displayW, int displayH)
{
	if (localTeam < 0 || localTeam >= scene.entities.teamCount) return;
	for(const GhostBuilding& ghost : buildings)
	{
		if (!scene.buildingTypes || ghost.typeNum < 0 || size_t(ghost.typeNum) >= scene.buildingTypes->size()) continue;
		const BuildingType *bt = &scene.buildingTypes->at(ghost.typeNum);
		Sprite *sprite = bt->gameSpritePtr;
		sprite->setBaseColor(presentationColor(scene.entities.teams[localTeam].color));

		//Find position to draw. The sprite is anchored at the bottom-left of the
		//footprint: its width always matches the footprint, but it may be taller
		//(roofs, flag poles), so only Y is pulled up by the overhang.
		int spriteH = sprite->getH(bt->gameSpriteImage);
		int rectX = ((ghost.x - viewportX) & scene.map.getMaskW()) * 32;
		int rectY = (((ghost.y - viewportY) & scene.map.getMaskH()) * 32) - (spriteH - bt->height * 32);

		//Draw
		forEachMapCopy(rectX, rectY, rectX+sprite->getW(bt->gameSpriteImage)-1, rectY+spriteH-1,
			scene.map.getW()*32, scene.map.getH()*32, displayW, displayH, [&](int dx, int dy) {
				globalContainer->gfx->drawSprite(rectX+dx, rectY+dy, sprite, bt->gameSpriteImage, GHOST_SPRITE_ALPHA);
			});
	}
}
