/*
  Copyright (C) 2007 Bradley Arsenault

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

Minimap::Minimap(int px, int py, int size, int border)
  : px(px), py(py), size(size), border(border)
{
	colors.push_back(Color(0, 40, 120)); //Water
	colors.push_back(Color(170, 170, 0)); //Sand
	colors.push_back(Color(0, 90, 0)); //Grass
	colors.push_back(Color(10, 240, 20)); //Self
	colors.push_back(Color(220, 200, 20)); //Ally
	colors.push_back(Color(220, 25, 30)); //Enemy
	colors.push_back(Color((10*3)/5, (240*3)/5, (20*3)/5)); //Self FOW
	colors.push_back(Color((220*3)/5, (200*3)/5, (20*3)/5)); //Ally FOW
	colors.push_back(Color((220*3)/5, (25*3)/5, (30*3)/5)); //Enemy FOW
	colors.push_back(Color(0, 0, 0)); //Enemy FOW
	for(int i=0; i<MAX_RESSOURCES; ++i)
	{
		RessourceType *rt = globalContainer->ressourcesTypes.get(i);
		colors.push_back(Color(rt->minimapR, rt->minimapG, rt->minimapB));
	}
	row = -1;
	surface=new DrawableSurface(size, size);
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

	///Draw a 1 pixel line arround the minimap
	globalContainer->gfx->drawRect(px + border - 1, py + border - 1, size - border * 2 + 2, size - border * 2 + 2, 200, 200, 200);

	offset_x = game->map.getW() / 2 - game->teams[localteam]->startPosX;
	offset_y = game->map.getW() / 2 - game->teams[localteam]->startPosY;

	//Render the colorMap and blit the surface
	
	if(row == -1)
	{
		renderAllRows(localteam);
		row = 0;
		refreshPixelRows(0, size - border * 2);
	}
	else
	{
		row += 1;
		row %= game->map.getH();
		renderRow(row, localteam);

		row += 1;
		row %= game->map.getH();
		renderRow(row, localteam);

		row += 1;
		row %= game->map.getH();
		renderRow(row, localteam);
		
		int lower_pixel_row = int((float)(row-3) / (float)(game->map.getH()) * (float)(size - border*2));
		lower_pixel_row = (lower_pixel_row + (size - border*2)) % (size - border*2);
		int upper_pixel_row = int((float)(row+1) / (float)(game->map.getH()) * (float)(size - border*2));
		upper_pixel_row = (upper_pixel_row + (size - border*2)) % (size - border*2);
		
		refreshPixelRows(lower_pixel_row, upper_pixel_row);
	}
	//Draw the surface
	globalContainer->gfx->drawSurface(px, py, surface);

	//Draw the viewport square, taking into account that it may
	//wrap arround the sides of the minimap
	viewportX += offset_x;
	viewportY += offset_y;

	int startx, starty, endx, endy;
	convertToScreen(viewportX, viewportY, startx, starty);
	convertToScreen(viewportX + viewportW, viewportY + viewportH, endx, endy);
	

	for (int n=startx; n!=endx;)
	{
		globalContainer->gfx->drawPixel(n, starty, 255, 255, 255);
		globalContainer->gfx->drawPixel(n, endy, 255, 255, 255);
		
		n+=1;
		if(n == (px + size - border))
			n = px + border;
	}
	
	///The end lines are out of alignment, so a single pixel in the bottom right hand of the square
	///is never drawn
	globalContainer->gfx->drawPixel(endx, endy, 255, 255, 255);
	
	for (int n=starty; n!=endy;)
	{
		globalContainer->gfx->drawPixel(startx, n, 255, 255, 255);
		globalContainer->gfx->drawPixel(endx, n, 255, 255, 255);
		n+=1;
		if(n == (py + size - border))
			n = py + border;
	}
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
	if(x > (px + border) && x < (px + size - border)
			&& y > (py + border) && y < (py + size - border))
		return true;
}



void Minimap::convertToMap(int nx, int ny, int& x, int& y)
{
	int xpos = nx - px - border;
	int ypos = ny - py - border;
	x = (offset_x + (int)((float)(game->map.getW()) / (float)(size - border*2) * (float)(xpos))) % game->map.getW();
	y = (offset_y + (int)((float)(game->map.getH()) / (float)(size - border*2) * (float)(ypos))) % game->map.getH();
}


void Minimap::convertToScreen(int nx, int ny, int& x, int& y)
{
	int xpos = game->map.normalizeX(nx - offset_x);
	int ypos = game->map.normalizeY(ny - offset_y);

	x = px + border + (int)((float)(xpos) * (float)(size - border*2) / (float)(game->map.getW())) % (size - border*2);
	y = py + border + (int)((float)(ypos) * (float)(size - border*2) / (float)(game->map.getH())) % (size - border*2);
}

void Minimap::refreshPixelRows(int start, int end)
{
	for(int y=start; y!=end;)
	{
		for(int x=0; x<(size-border*2); ++x)
		{
			surface->drawPixel(x + border, y + border, getColor(double(x) / double(size-border*2), double(y) / double(size - border*2)));
		}
		
		y++;
		if(y == end)
			break;
		if(y == (size - border * 2))
			y = 0;
	}
}



void Minimap::renderRow(int y, int localteam)
{
	Uint32 localMask = game->teams[localteam]->me;
	Uint32 allyMask = game->teams[localteam]->allies;
	for(int x=0; x<game->map.getW(); ++x)
	{
		if(! game->map.isMapDiscovered(offset_x + x, offset_y + y, localMask))
			colorMap[position(x, y)] = Hidden;
		else
		{
			bool isVisible = game->map.isFOWDiscovered(offset_x + x, offset_y + y, localMask);
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
					colorMap[position(x, y)] = Self;
				else if(owner->me & allyMask)
					colorMap[position(x, y)] = Ally;
				else
					colorMap[position(x, y)] = Enemy;
			}
			else
			{
				gid = game->map.getBuilding(offset_x + x, offset_y + y);
				if(gid != NOGBID)
				{
					//If building is detected, use its color
					Building* b = game->teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
					if(b->seenByMask & localMask)
					{
						if(b->owner == game->teams[localteam])
							colorMap[position(x, y)] = Self;
						else if(b->owner->me & allyMask)
							colorMap[position(x, y)] = AllyFOW;
						else
							colorMap[position(x, y)] = EnemyFOW;
					}
					else if(isVisible)
					{
						if(b->owner == game->teams[localteam])
							colorMap[position(x, y)] = Self;
						else if(b->owner->me & allyMask)
							colorMap[position(x, y)] = Ally;
						else
							colorMap[position(x, y)] = Enemy;
					}
				}
				else
				{
					int ressource = game->map.getRessource(offset_x + x, offset_y + y).type;
					if(ressource != NO_RES_TYPE)
						colorMap[position(x, y)] = ColorMode(int(RessourceColorStart) + ressource);
					else if(game->map.isWater(offset_x + x, offset_y + y))
						colorMap[position(x, y)] = TerrainWater;
					else if(game->map.isSand(offset_x + x, offset_y + y))
						colorMap[position(x, y)] = TerrainSand;
					else if(game->map.isGrass(offset_x + x, offset_y + y))
						colorMap[position(x, y)] = TerrainGrass;
				}
			}
		}
	}
}



GAGCore::Color Minimap::getColor(double xpos, double ypos)
{
	
	int lower_x = game->map.normalizeX(int(xpos * double(game->map.getW())));
	int upper_x = game->map.normalizeX(lower_x+1);
	double mu_x = (xpos * double(game->map.getW())) - lower_x;
	
	int lower_y = game->map.normalizeY(int(ypos * double(game->map.getH())));
	int upper_y = game->map.normalizeY(lower_y+1);
	double mu_y = (ypos * double(game->map.getH())) - lower_y;
	
	Color u_l = colors[colorMap[position(lower_x, lower_y)]];
	Color u_r = colors[colorMap[position(upper_x, lower_y)]];
	Color d_l = colors[colorMap[position(lower_x, upper_y)]];
	Color d_r = colors[colorMap[position(upper_x, upper_y)]];

	//interpolate each color in three dimensions
	int final_r = interpolate(mu_y, interpolate(mu_x, u_l.r, u_r.r), interpolate(mu_x, d_l.r, d_r.r));
	int final_g = interpolate(mu_y, interpolate(mu_x, u_l.g, u_r.g), interpolate(mu_x, d_l.g, d_r.g));
	int final_b = interpolate(mu_y, interpolate(mu_x, u_l.b, u_r.b), interpolate(mu_x, d_l.b, d_r.b));
	
	return Color(final_r, final_g, final_b);
}


int Minimap::interpolate(double mu, int y1, int y2)
{
   return int(y1*(1-mu)+y2*mu);
}



