// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "GameGUIGhostBuildingManager.h"

#include "GlobalContainer.h"
#include "Game.h"

namespace
{
	/// Alpha the ghost sprite is blended with, so it reads as "ordered but not
	/// built yet" against the terrain underneath.
	constexpr int GHOST_SPRITE_ALPHA = 200;
}

GameGUIGhostBuildingManager::GameGUIGhostBuildingManager(Game& game)
	: game(game)
{

}



void GameGUIGhostBuildingManager::addBuilding(Sint32 typeNum, int x, int y)
{
	buildings.push_back(GhostBuilding{typeNum, x, y});
}



bool GameGUIGhostBuildingManager::isGhostBuilding(int x, int y, int w, int h)
{
	for(const GhostBuilding& ghost : buildings)
	{
		const BuildingType *bt = globalContainer->buildingsTypes.get(ghost.typeNum);
		// Two footprints on a torus collide only if they overlap on both axes
		// independently.
		if(wrappedRangesOverlap(x, w, ghost.x, bt->width, game.map.getW()) &&
		   wrappedRangesOverlap(y, h, ghost.y, bt->height, game.map.getH()))
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



void GameGUIGhostBuildingManager::drawAll(int viewportX, int viewportY, int localTeam)
{
	for(const GhostBuilding& ghost : buildings)
	{
		BuildingType *bt = globalContainer->buildingsTypes.get(ghost.typeNum);
		Sprite *sprite = bt->gameSpritePtr;
		sprite->setBaseColor(game.teams[localTeam]->color);

		//Find position to draw. The sprite is anchored at the bottom-left of the
		//footprint: its width always matches the footprint, but it may be taller
		//(roofs, flag poles), so only Y is pulled up by the overhang.
		int spriteH = sprite->getH(bt->gameSpriteImage);
		int batX = ((ghost.x - viewportX) & game.map.wMask) * Map::TILE_PX;
		int batY = (((ghost.y - viewportY) & game.map.hMask) * Map::TILE_PX) - (spriteH - bt->height * Map::TILE_PX);

		//Draw
		globalContainer->gfx->drawSprite(batX, batY, sprite, bt->gameSpriteImage, GHOST_SPRITE_ALPHA);
	}
}
