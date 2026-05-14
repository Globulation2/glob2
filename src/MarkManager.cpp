// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

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



void Mark::draw(int x, int y, float scale) const
{
	double ray = (sin((double)(showTicks * 2.0)/(double)(totalTime)*3.141592)*totalTime/2);
	ray = (std::abs(ray) * showTicks) / totalTime * scale;

	int pixel_ray = static_cast<int>(ray);
	int line_length = static_cast<int>(MARK_LINE_LENGTH_PX * scale);
	int line_pos = static_cast<int>(MARK_LINE_OFFSET_PX * scale);
	globalContainer->gfx->drawCircle(x, y, pixel_ray, color);
	globalContainer->gfx->drawHorzLine(x + pixel_ray-line_pos+1, y, line_length, color.r, color.g, color.b);
	globalContainer->gfx->drawHorzLine(x-pixel_ray-line_pos, y, line_length, color.r, color.g, color.b);
	globalContainer->gfx->drawVertLine(x, y+pixel_ray-line_pos+1, line_length, color.r, color.g, color.b);
	globalContainer->gfx->drawVertLine(x, y-pixel_ray-line_pos, line_length, color.r, color.g, color.b);
}



void Mark::drawInMinimap(int s, int local, int x, int y, Game& game) const
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



void Mark::drawInMainView(int viewportX, int viewportY, Game& game) const
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
		i->tick();
		if(i->expired())
		{
			i = marks.erase(i);
			continue;
		}
		i->drawInMinimap(minimapSize, localTeam, minimapX, minimapY, game);
		i->drawInMainView(viewportX, viewportY, game);
		++i;
	}
}



void MarkManager::addMark(const Mark& mark)
{
	marks.push_back(mark);
}

