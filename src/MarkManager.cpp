/*
  Copyright (C) 2007 Bradley Arsenault

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

#include "MarkManager.h"
#include "Utilities.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Game.h"
#include <cmath>

Mark::Mark(int px, int py, GAGCore::Color color, int time)
  : showTicks(time), totalTime(time), px(px), py(py), color(color)
{

}



Mark::Mark()
{
}



void Mark::draw(int x, int y, float scale)
{
	showTicks -= 1;
	double ray = (sin((double)(showTicks * 2.0)/(double)(totalTime)*3.141592)*totalTime/2);
	ray = (std::abs(ray) * showTicks) / totalTime * scale;

	int pixel_ray = static_cast<int>(ray);
	int line_length = static_cast<int>(8 * scale);
	int line_pos = static_cast<int>(4 * scale);
	globalContainer->gfx->drawCircle(x, y, pixel_ray, color);
	globalContainer->gfx->drawHorzLine(x + pixel_ray-line_pos+1, y, line_length, color.r, color.g, color.b);
	globalContainer->gfx->drawHorzLine(x-pixel_ray-line_pos, y, line_length, color.r, color.g, color.b);
	globalContainer->gfx->drawVertLine(x, y+pixel_ray-line_pos+1, line_length, color.r, color.g, color.b);
	globalContainer->gfx->drawVertLine(x, y-pixel_ray-line_pos, line_length, color.r, color.g, color.b);
}



void Mark::drawInMinimap(int s, int local, int x, int y, Game& game)
{
	int mMax;
	int szX, szY;
	int decX, decY;
	int nx, ny;

	Utilities::computeMinimapData(s, game.map.getW(), game.map.getH(), &mMax, &szX, &szY, &decX, &decY);
	GameUtilities::globalCoordToLocalView(&game, local, px, py, &nx, &ny);

	nx = (nx*s)/mMax;
	ny = (ny*s)/mMax;
	nx += x + decX;
	ny += y + decY;

	draw(nx, ny, 1.0);
}



void Mark::drawInMainView(int viewportX, int viewportY, Game& game)
{
	int nx, ny;
	game.map.mapCaseToDisplayable(px, py, &nx, &ny, viewportX, viewportY);

	draw(nx, ny, 2.0);
}



MarkManager::MarkManager()
{

}



void MarkManager::drawAll(int localTeam, int minimapX, int minimapY, int minimapSize, int viewportX, int viewportY, Game& game)
{
	for(std::vector<Mark>::iterator i=marks.begin(); i!=marks.end();)
	{
		i->drawInMinimap(minimapSize, localTeam, minimapX, minimapY, game);
		i->drawInMainView(viewportX, viewportY, game);
		if(i->showTicks == 0)
			i = marks.erase(i);
		else
			i++;
	}
}



void MarkManager::addMark(const Mark& mark)
{
	marks.push_back(mark);
}

