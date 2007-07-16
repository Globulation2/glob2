/*
  Copyright (C) 2007 Bradley Arsenault
  
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "Minimap.h"
#include "Ressource.h"
#include "RessourceType.h"
#include "RessourcesTypes.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include <iostream>


using namespace GAGCore;

Minimap::Minimap(int px, int py, int size, int border, MinimapMode minimap_mode)
  : px(px), py(py), size(size), border(border), minimap_mode(minimap_mode)
{
	colors.push_back(Color(10, 240, 20)); //Self
	colors.push_back(Color(220, 200, 20)); //Ally
	colors.push_back(Color(220, 25, 30)); //Enemy
	colors.push_back(Color((10*3)/5, (240*3)/5, (20*3)/5)); //Self FOW
	colors.push_back(Color((220*3)/5, (200*3)/5, (20*3)/5)); //Ally FOW
	colors.push_back(Color((220*3)/5, (25*3)/5, (30*3)/5)); //Enemy FOW
	colors.push_back(Color(0, 0, 0)); //Hidden
	colors.push_back(Color(0, 40, 120)); //Water
	colors.push_back(Color(170, 170, 0)); //Sand
	colors.push_back(Color(0, 90, 0)); //Grass
	colors.push_back(Color(0, (40*3) / 4, (120*3)/5)); //Water FOW
	colors.push_back(Color((170*3)/5, (170*3)/5, 0)); //Sand FOW
	colors.push_back(Color(0, (90*3)/5, 0)); //Grass FOW
	for(int i=0; i<MAX_RESSOURCES; ++i)
	{
		RessourceType *rt = globalContainer->ressourcesTypes.get(i);
		colors.push_back(Color(rt->minimapR, rt->minimapG, rt->minimapB));
	}
	for(int i=0; i<MAX_RESSOURCES; ++i)
	{
		RessourceType *rt = globalContainer->ressourcesTypes.get(i);
		colors.push_back(Color((rt->minimapR*3)/5, (rt->minimapG*3)/5, (rt->minimapB*3)/5));
	}
	update_row = -1;
	surface=new DrawableSurface(size - border * 2, size - border * 2);
}


Minimap::~Minimap()
{
	if (surface)
		delete surface;
}

void Minimap::setGame(Game& ngame)
{
	game = &ngame;
	colorMap.resize(game->map.getW() * game->map.getH());
}



void Minimap::draw(int localteam, int viewportX, int viewportY, int viewportW, int viewportH)
{
	computeMinimapPositioning();

	Uint8 borderR;
	Uint8 borderG;
	Uint8 borderB;
	Uint8 borderA;
	// draw the either black or transparent border arround the minimap
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
	{
		borderR = 0;
		borderG = 0;
		borderB = 0;
		borderA = Color::ALPHA_OPAQUE;
	}
	else
	{
		borderR = 0;
		borderG = 0;
		borderB = 40;
		borderA = 180;
	}
	globalContainer->gfx->drawFilledRect(px, py, size, border, borderR, borderG, borderB, borderA);
	globalContainer->gfx->drawFilledRect(px, py + size - border, size, border, borderR, borderG, borderB, borderA);
	globalContainer->gfx->drawFilledRect(px, py + border, border, size - border*2, borderR, borderG, borderB, borderA);
	globalContainer->gfx->drawFilledRect(px + size - border, py + border, border, size-border*2, borderR, borderG, borderB, borderA);

	///Draw a 1 pixel hilight arround the minimap
	globalContainer->gfx->drawRect(px + border - 1, py + border - 1, size - border * 2 + 2, size - border * 2 + 2, 200, 200, 200);

	offset_x = game->teams[localteam]->startPosX - game->map.getW() / 2;
	offset_y = game->teams[localteam]->startPosY - game->map.getH() / 2;

	///What row the scan-line is to be drawn at
	int line_row = 0;

	//Render the colorMap and blit the surface
	if(update_row == -1)
	{
		renderAllRows(localteam);
		update_row = 0;
		refreshPixelRows(0, mini_h);
	}
	else
	{
		///Render four rows at a time
		const int rows_to_render = mini_h/25;
		
		int first = (int)((float)(update_row) * (float)(game->map.getH()) / (float)(mini_h));
		int last  = (int)((float)(update_row + rows_to_render + 1) * (float)(game->map.getH()) / (float)(mini_h));
		
		for(int r=first; r<=last; ++r)
		{
			renderRow(r % (game->map.getH()), localteam);
		}
		
		refreshPixelRows(update_row, (update_row + rows_to_render) % (mini_h));
		update_row += rows_to_render;
		update_row %= (mini_h);
		line_row = update_row;
	}
	//Draw the surface
	globalContainer->gfx->drawSurface(px + border, py + border, surface);

	//Draw the viewport square, taking into account that it may
	//wrap arround the sides of the minimap

	int startx, starty, endx, endy;
	convertToScreen(viewportX, viewportY, startx, starty);
	convertToScreen(viewportX + viewportW, viewportY + viewportH, endx, endy);

	for (int n=startx; n!=endx;)
	{
		globalContainer->gfx->drawPixel(n, starty, 255, 255, 255);
		globalContainer->gfx->drawPixel(n, endy, 255, 255, 255);
		
		n+=1;
		if(n == (mini_x + mini_w))
			n = mini_x;
	}
	for (int n=starty; n!=endy;)
	{
		globalContainer->gfx->drawPixel(startx, n, 255, 255, 255);
		globalContainer->gfx->drawPixel(endx, n, 255, 255, 255);
		n+=1;
		if(n == (mini_y + mini_h))
			n = mini_y;
	}
	///The lines are out of alignment, so a single pixel in the bottom right hand of the square
	///is never drawn
	globalContainer->gfx->drawPixel(endx, endy, 255, 255, 255);

	///Draw the line that shows where the minimap is currently updating
	if(minimap_mode == HideFOW)
		globalContainer->gfx->drawHorzLine(mini_x, mini_y + line_row , mini_w, 100, 100, 100);
}



void Minimap::renderAllRows(int localteam)
{
	for(int y=0; y<game->map.getH(); ++y)
	{
		renderRow(y, localteam);
	}
}



bool Minimap::insideMinimap(int x, int y)
{
	if(x > (mini_x) && x < (mini_x + mini_w)
			&& y > (mini_y) && y < (mini_y + mini_h))
		return true;
	return false;
}



void Minimap::convertToMap(int nx, int ny, int& x, int& y)
{
	int xpos = nx - mini_x;
	int ypos = ny - mini_y;
	x = (offset_x + (int)((float)(game->map.getW()) / (float)(mini_w) * (float)(xpos))) % game->map.getW();
	y = (offset_y + (int)((float)(game->map.getH()) / (float)(mini_h) * (float)(ypos))) % game->map.getH();
}



void Minimap::convertToScreen(int nx, int ny, int& x, int& y)
{
	int xpos = game->map.normalizeX(nx - offset_x);
	int ypos = game->map.normalizeY(ny - offset_y);

	x = mini_x + (int)((float)(xpos) * (float)(mini_w) / (float)(game->map.getW())) % (mini_w);
	y = mini_y + (int)((float)(ypos) * (float)(mini_h) / (float)(game->map.getH())) % (mini_h);
}



void Minimap::computeMinimapPositioning()
{
	int msize = size - border*2;
	if(game->map.getW() > game->map.getH())
	{
		mini_w = msize;
		mini_h = (game->map.getH() * msize) / game->map.getW();
		mini_offset_x = 0;
		mini_offset_y = (msize - mini_h)/2;
		mini_x = px + border + mini_offset_x;
		mini_y = py + border + mini_offset_y;
	}
	else
	{
		mini_w = (game->map.getW() * msize) / game->map.getH();
		mini_h = msize;
		mini_offset_x = (msize - mini_w)/2;
		mini_offset_y = 0;
		mini_x = px + border + mini_offset_x;
		mini_y = py + border + mini_offset_y;
	}
}



void Minimap::refreshPixelRows(int start, int end)
{
	for(int y=start; y!=end;)
	{
		for(int x=0; x<(mini_w); ++x)
		{
			surface->drawPixel(mini_offset_x + x, mini_offset_y + y, getColor(x, y));
		}
		
		y++;
		if(y == end)
			break;
		if(y == mini_h)
			y = 0;
	}
}



void Minimap::renderRow(int y, int localteam)
{
	Uint32 localMask = game->teams[localteam]->me;
	Uint32 allyMask = game->teams[localteam]->allies;
	for(int x=0; x<game->map.getW(); ++x)
	{
		if(minimap_mode == HideFOW && !game->map.isMapDiscovered(offset_x + x, offset_y + y, localMask))
		{
			colorMap[position(x, y)] = Hidden;
		}
		else
		{
			bool isVisible = game->map.isFOWDiscovered(offset_x + x, offset_y + y, localMask);
			if(minimap_mode == ShowFOW)
				isVisible=true;
			Uint16 gid = game->map.getAirUnit(offset_x + x, offset_y + y);
			if(gid == NOGUID)
			{
				gid = game->map.getGroundUnit(offset_x + x, offset_y + y);
			}
			if(gid != NOGUID && isVisible)
			{
				//If a unit is detected, use its color
				Team* owner = game->teams[Unit::GIDtoTeam(gid)];
				if(owner == game->teams[localteam])
				{
					colorMap[position(x, y)] = Self;
				}
				else if(owner->me & allyMask)
				{
					colorMap[position(x, y)] = Ally;
				}
				else
				{
					colorMap[position(x, y)] = Enemy;
				}
			}
			else
			{
				gid = game->map.getBuilding(offset_x + x, offset_y + y);
				if(gid != NOGBID)
				{
					//If building is detected, use its color
					Building* b = game->teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
					
					if(isVisible)
					{
						if(b->owner == game->teams[localteam])
						{
							colorMap[position(x, y)] = Self;
						}
						else if(b->owner->me & allyMask)
						{
							colorMap[position(x, y)] = Ally;
						}
						else
						{
							colorMap[position(x, y)] = Enemy;
						}
					}
					else if(b->seenByMask & localMask)
					{
						if(b->owner == game->teams[localteam])
						{
							colorMap[position(x, y)] = SelfFOW;
						}
						else if(b->owner->me & allyMask)
						{
							colorMap[position(x, y)] = AllyFOW;
						}
						else
						{
							colorMap[position(x, y)] = EnemyFOW;
						}
					}
				}
				else
				{
					if(isVisible)
					{
						int ressource = game->map.getRessource(offset_x + x, offset_y + y).type;
						if(ressource != NO_RES_TYPE)
						{
							colorMap[position(x, y)] = ColorMode(int(RessourceColorStart) + ressource);
						}
						else
						{
							colorMap[position(x, y)] = (ColorMode)(TerrainWater + game->map.getUMTerrain((int)offset_x + x,(int)offset_y + y));
						}
					}
					else
					{
						int ressource = game->map.getRessource(offset_x + x, offset_y + y).type;
						if(ressource != NO_RES_TYPE)
						{
							colorMap[position(x, y)] = ColorMode(int(RessourceColorStart + MAX_RESSOURCES) + ressource);
						}
						else
						{
							colorMap[position(x, y)] = (ColorMode)(TerrainWaterFOW + game->map.getUMTerrain((int)offset_x + x,(int)offset_y + y));
						}
					}
				}
			}
		}
	}
}



GAGCore::Color Minimap::getColor(int xpos, int ypos)
{
	int startx = (int)((float)(xpos) * (float)(game->map.getW()) / (float)(mini_w));
	int endx = 	(int)((float)(xpos + 1) * (float)(game->map.getW()) / (float)(mini_w));
	int starty = (int)((float)(ypos) * (float)(game->map.getH()) / (float)(mini_h));
	int endy = 	(int)((float)(ypos + 1) * (float)(game->map.getH()) / (float)(mini_h));

	int count = 0;
	Uint16 r=0;
	Uint16 g=0;
	Uint16 b=0;

	for(int x=startx; x<=endx; ++x)
	{
		for(int y=starty; y<=endy; ++y)
		{
			ColorMode mode = colorMap[position(game->map.normalizeX(x), game->map.normalizeY(y))];
			if(mode < Hidden)
			{
				return colors[mode];
			}
			else
			{
				r += colors[mode].r;
				g += colors[mode].g;
				b += colors[mode].b;
			}
			count+= 1;
		}
	}

	return Color(r / count, g / count, b / count);
}



