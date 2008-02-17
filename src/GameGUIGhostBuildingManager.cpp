/*
  Copyright (C) 2008 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "GameGUIGhostBuildingManager.h"

#include "GlobalContainer.h"

GameGUIGhostBuildingManager::GameGUIGhostBuildingManager(Game& game)
	: game(game)
{

}



void GameGUIGhostBuildingManager::addBuilding(const std::string& type, int x, int y)
{
	buildings.push_back(boost::make_tuple(type, x, y));
}



void GameGUIGhostBuildingManager::removeBuilding(int x, int y)
{
	for(int i=0; i<buildings.size();)
	{
		if(buildings[i].get<1>() == x && buildings[i].get<2>() == y)
		{
			buildings.erase(buildings.begin() + i);
		}
		else
		{
			++i;
		}
	}
}



void GameGUIGhostBuildingManager::drawAll(int viewportX, int viewportY)
{
	for(int i=0; i<buildings.size(); ++i)
	{
		std::string building = buildings[i].get<0>();
		int px = buildings[i].get<1>();
		int py = buildings[i].get<2>();

		int typeNum = globalContainer->buildingsTypes.getTypeNum(building, 0, true);
		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		Sprite *sprite = bt->gameSpritePtr;

		//Find position to draw
		int batW = (bt->width) * 32;
		int batH = sprite->getH(bt->gameSpriteImage);
		int batX = (((px-viewportX)&(game.map.wMask)) * 32);
		int batY = (((py-viewportY)&(game.map.hMask)) * 32)-(batH-(bt->height * 32));

		//Draw
		globalContainer->gfx->drawSprite(batX, batY, sprite, bt->gameSpriteImage, 200);
	}
}


