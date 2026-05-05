// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "GameGUIGhostBuildingManager.h"

#include "GlobalContainer.h"
#include "Game.h"

GameGUIGhostBuildingManager::GameGUIGhostBuildingManager(Game& game)
	: game(game)
{

}



void GameGUIGhostBuildingManager::addBuilding(const std::string& type, int x, int y)
{
	buildings.push_back(std::make_tuple(type, x, y));
}



bool GameGUIGhostBuildingManager::isGhostBuilding(int x, int y, int w, int h)
{
	for(int px = 0; px < w; ++px)
	{
		for(int py = 0; py < h; ++py)
		{
			int lx = (x + px + game.map.getW()) % game.map.getW();
			int ly = (y + py + game.map.getH()) % game.map.getH();
			for(unsigned i=0; i<buildings.size(); ++i)
			{
				int bx = std::get<1>(buildings[i]);
				int by = std::get<2>(buildings[i]);

				std::string building = std::get<0>(buildings[i]);
				int typeNum = globalContainer->buildingsTypes.getTypeNum(building, 0, true);
				if(typeNum == -1)
					typeNum = globalContainer->buildingsTypes.getTypeNum(building, 0, false);
				BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
				
				for(int dx=0; dx<bt->width; ++dx)
				{
					for(int dy=0; dy<bt->height; ++dy)
					{
						int nx = (bx + dx + game.map.getW()) % game.map.getW();
						int ny = (by + dy + game.map.getH()) % game.map.getH();
						if(lx == nx && ly == ny)
						{
							return true;
						}
					}
				}
			}
	}
	}
	return false;
}



void GameGUIGhostBuildingManager::removeBuilding(int x, int y)
{
	for(unsigned i=0; i<buildings.size();)
	{
		if(std::get<1>(buildings[i]) == x && std::get<2>(buildings[i]) == y)
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
	for(unsigned i=0; i<buildings.size(); ++i)
	{
		std::string building = std::get<0>(buildings[i]);
		int px = std::get<1>(buildings[i]);
		int py = std::get<2>(buildings[i]);

		int typeNum = globalContainer->buildingsTypes.getTypeNum(building, 0, true);
		if(typeNum == -1)
			typeNum = globalContainer->buildingsTypes.getTypeNum(building, 0, false);

		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		Sprite *sprite = bt->gameSpritePtr;
		sprite->setBaseColor(game.teams[localTeam]->color);

		//Find position to draw
		int batW = (bt->width) * 32;
		int batH = sprite->getH(bt->gameSpriteImage);
		int batX = (((px-viewportX)&(game.map.wMask)) * 32)-(batW-(bt->width * 32));
		int batY = (((py-viewportY)&(game.map.hMask)) * 32)-(batH-(bt->height * 32));

		//Draw
		globalContainer->gfx->drawSprite(batX, batY, sprite, bt->gameSpriteImage, 200);
	}
}


