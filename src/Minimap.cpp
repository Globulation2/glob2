/*
  Copyright (C) 2007 Bradley Arsenault
  
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#include "Minimap.h"
#include "Ressource.h"
#include "RessourceType.h"
#include "RessourcesTypes.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include <iostream>


using namespace GAGCore;


// Creates a minimap of specified values
// nox - no graphics
// menuWidth - width of the menu the minimap is placed in
// gameWidth - width of the game screen
// xOffset - offset from the left side of the menu
// yOffset - offset from the top
// width & height - size of the minimap graphic
// minimapMode - to draw fog of war, or not to draw!

Minimap::Minimap(bool nox, int menuWidth, int gameWidth, int xOffset, int yOffset, int width, int height, MinimapMode minimapMode)
	: noX(nox), menuWidth(menuWidth), gameWidth(gameWidth), xOffset(xOffset), yOffset(yOffset), width(width), height(height), minimapMode(minimapMode)
{
	if (nox) return;

  // since the update loop goes by row store that row so we gan draw the gray "radar" line 
	update_row = -1;
	// The actual minimap picture to be drawn to.
	surface=new DrawableSurface(width, height);
}


Minimap::~Minimap()
{
	if (noX) return;
	if (surface)
		delete surface;
}

void Minimap::setGame(Game& ngame)
{
	if (noX) return;
	game = &ngame;
}



void Minimap::draw(int localteam, int viewportX, int viewportY, int viewportW, int viewportH)
{
	if (noX) return;

  // Compute the position of the minimap if it needs to be scaled & centered
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
	
	// Fill the 4 sides of the menu around the minimap with the color above
	// left side
	globalContainer->gfx->drawFilledRect(gameWidth-menuWidth, 0, xOffset, height+yOffset, borderR, borderG, borderB, borderA);
	// right side
	globalContainer->gfx->drawFilledRect(gameWidth-menuWidth+xOffset+width, 0, menuWidth-xOffset-width, height+yOffset, borderR, borderG, borderB, borderA);
	// top side
	globalContainer->gfx->drawFilledRect(gameWidth-menuWidth+xOffset, 0, width, yOffset, borderR, borderG, borderB, borderA);
	// bottom side not needed, because the menu draws up to it
	//globalContainer->gfx->drawFilledRect(gameWidth-menuWidth+xOffset, yOffset+height, width, 0, borderR, borderG, borderB, borderA);

	///Draw a 1 pixel border arround the minimap
	globalContainer->gfx->drawRect(gameWidth-menuWidth+xOffset-1,
	                               yOffset-1, 
	                               width+2, 
	                               height+2, 
	                               200, 200, 200);
  
  // calculate the offset for the viewport square
	offset_x = game->teams[localteam]->startPosX - game->map.getW() / 2;
	offset_y = game->teams[localteam]->startPosY - game->map.getH() / 2;

	///What row the scan-line ("radar") is to be drawn at
	int line_row = 0;

	//Render the colorMap and blit the surface
	if(update_row == -1)
	{
	  // clear the minimap by drawing a black rect over it
		surface->drawFilledRect(0, 0, width, height, 0, 0, 0, Color::ALPHA_OPAQUE);
		update_row = 0;
		refreshPixelRows(0, mini_h, localteam);
	}
	else
	{
		///Render 1/25th of the rows at a time
		const int rows_to_render = std::max(1, mini_h/25);
		
		refreshPixelRows(update_row, (update_row + rows_to_render) % (mini_h), localteam);
		update_row += rows_to_render;
		update_row %= (mini_h);
		line_row = update_row;
	}
	//Draw the surface
	globalContainer->gfx->drawSurface(gameWidth-menuWidth+xOffset, yOffset, surface);

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
	if(minimapMode == HideFOW)
		globalContainer->gfx->drawHorzLine(mini_x, mini_y + line_row , mini_w, 100, 100, 100);
}


bool Minimap::insideMinimap(int x, int y)
{
	if (noX) return false;

	if(x > (mini_x) && x < (mini_x + mini_w)
			&& y > (mini_y) && y < (mini_y + mini_h))
		return true;
	return false;
}



void Minimap::convertToMap(int nx, int ny, int& x, int& y)
{
	if (noX) return;

	int xpos = nx - mini_x;
	int ypos = ny - mini_y;
	x = (offset_x + (int)((float)(game->map.getW()) / (float)(mini_w) * (float)(xpos))) % game->map.getW();
	y = (offset_y + (int)((float)(game->map.getH()) / (float)(mini_h) * (float)(ypos))) % game->map.getH();
}



void Minimap::convertToScreen(int nx, int ny, int& x, int& y)
{
	if (noX) return;

	int xpos = game->map.normalizeX(nx - offset_x);
	int ypos = game->map.normalizeY(ny - offset_y);

	x = mini_x + (int)((float)(xpos) * (float)(mini_w) / (float)(game->map.getW())) % (mini_w);
	y = mini_y + (int)((float)(ypos) * (float)(mini_h) / (float)(game->map.getH())) % (mini_h);
}



void Minimap::resetMinimapDrawing()
{
	update_row = -1;
}
	


void Minimap::computeMinimapPositioning()
{
	if (noX) return;
	
	if(game->map.getW() > game->map.getH())
	{
	  // If the width is greater than the height, normal width but shrink the height
		mini_w = width;
		mini_h = (game->map.getH()*height) / game->map.getW();
		// Once the minimap has been scaled, center it on the minimap
		mini_offset_x = 0;
		mini_offset_y = (height-mini_h)/2;
		// Now set the position of it on the whole screen
		mini_x = gameWidth-menuWidth+xOffset+mini_offset_x;
		mini_y = yOffset + mini_offset_y;
	}
	else
	{
	  // Height is greater than width
		mini_w = (game->map.getW()*width) / game->map.getH();
		mini_h = height;
		// Center it..
		mini_offset_x = (width - mini_w)/2;
		mini_offset_y = 0;
		// And set the position for the screen!
		mini_x = gameWidth-menuWidth+xOffset+mini_offset_x;
		mini_y = yOffset + mini_offset_y;
	}
}



void Minimap::refreshPixelRows(int start, int end, int localteam)
{
	if (noX) return;

	for(int y=start; y!=end;)
	{
		computeColors(y, localteam);
		
		y++;
		if(y == end)
			break;
		if(y == mini_h)
			y = 0;
	}
}



void Minimap::computeColors(int row, int localTeam)
{
	if (noX) return;

	assert(localTeam>=0);
	assert(localTeam<32);

	const int terrainColor[3][3] = {
		{ 0, 40, 120 }, // Water
		{ 170, 170, 0 }, // Sand
		{ 0, 90, 0 }, // Grass
	};

	const int buildingsUnitsColor[6][3] = {
		{ 10, 240, 20 }, // self
		{ 220, 200, 20 }, // ally
		{ 220, 25, 30 }, // enemy
		{ (10*3)/5, (240*3)/5, (20*3)/5 }, // self FOW
		{ (220*3)/5, (200*3)/5, (20*3)/5 }, // ally FOW
		{ (220*3)/5, (25*3)/5, (30*3)/5 }, // enemy FOW
	};

	int pcol[3+MAX_RESSOURCES];

	// get data
	int szX = mini_w;
	int decX = mini_offset_x, decY = mini_offset_y;

	// Variables for traversing each map square within a minimap square.
	// Using ?.16 fixed-point representation (gives a 2x speedup):
	const int dMx = ((game->map.getW())<<16) / (mini_w);
	const int dMy = ((game->map.getH())<<16) / (mini_h);
	const int decSPX=offset_x<<16, decSPY=offset_y<<16;
	bool useMapDiscovered = minimapMode == ShowFOW;

	const int dy = row;
	for (int dx=0; dx<szX; dx++)
	{
		memset(pcol, 0, sizeof(pcol));
		int nCount = 0;
		int UnitOrBuildingIndex = -1;
		
		// compute
		for (int minidyFP=dMy*dy+decSPY; minidyFP<=(dMy*(dy+1))+decSPY; minidyFP+=(1<<16)) { // Fixed-point numbers
			int minidy = minidyFP>>16;
			for (int minidxFP=dMx*dx+decSPX; minidxFP<=(dMx*(dx+1))+decSPX; minidxFP+=(1<<16)) // Fixed-point numbers
			{
				int minidx = minidxFP>>16;
				bool seenUnderFOW = false;

				Uint16 gid=game->map.getAirUnit(minidx, minidy);
				if (gid==NOGUID)
					gid=game->map.getGroundUnit(minidx, minidy);
				if (gid==NOGUID)
				{
					gid=game->map.getBuilding(minidx, minidy);
					if (gid!=NOGUID)
					{
						if (game->teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)]->seenByMask & game->teams[localTeam]->me)
						{
							seenUnderFOW = true;
						}
					}
				}
				if (gid!=NOGUID)
				{
					int teamId=gid/1024;
					if (useMapDiscovered || game->map.isFOWDiscovered(minidx, minidy, game->teams[localTeam]->me))
					{
						if (teamId==localTeam)
							UnitOrBuildingIndex = 0;
						else if ((game->teams[localTeam]->allies) & (game->teams[teamId]->me))
							UnitOrBuildingIndex = 1;
						else
							UnitOrBuildingIndex = 2;
						goto unitOrBuildingFound;
					}
					else if (seenUnderFOW)
					{
						if (teamId==localTeam)
							UnitOrBuildingIndex = 3;
						else if ((game->teams[localTeam]->allies) & (game->teams[teamId]->me))
							UnitOrBuildingIndex = 4;
						else
							UnitOrBuildingIndex = 5;
						goto unitOrBuildingFound;
					}
				}
				
				if (useMapDiscovered || game->map.isMapDiscovered(minidx, minidy, game->teams[localTeam]->me))
				{
					// get color to add
					int pcolIndex;
					Ressource r=game->map.getRessource(minidx, minidy);
					if (r.type!=NO_RES_TYPE)
					{
						pcolIndex=r.type + 3;
					}
					else
					{
						pcolIndex=game->map.getUMTerrain(minidx,minidy);
					}
					
					// get weight to add
					int pcolAddValue;
					if (useMapDiscovered || game->map.isFOWDiscovered(minidx, minidy, game->teams[localTeam]->me))
						pcolAddValue=5;
					else
						pcolAddValue=3;

					pcol[pcolIndex]+=pcolAddValue;
				}

				nCount++;
			}
		}

		// Yes I know, this is *ugly*, but this piece of code *needs* speedup
		unitOrBuildingFound:

		int r, g, b;
		if (UnitOrBuildingIndex >= 0)
		{
			r = buildingsUnitsColor[UnitOrBuildingIndex][0];
			g = buildingsUnitsColor[UnitOrBuildingIndex][1];
			b = buildingsUnitsColor[UnitOrBuildingIndex][2];
			UnitOrBuildingIndex = -1;
		}
		else
		{
			nCount*=5;

			int lr, lg, lb;
			lr = lg = lb = 0;
			for (int i=0; i<3; i++)
			{
				lr += pcol[i]*terrainColor[i][0];
				lg += pcol[i]*terrainColor[i][1];
				lb += pcol[i]*terrainColor[i][2];
			}
			for (int i=0; i<MAX_RESSOURCES; i++)
			{
				RessourceType *rt = globalContainer->ressourcesTypes.get(i);
				lr += pcol[i+3]*(rt->minimapR);
				lg += pcol[i+3]*(rt->minimapG);
				lb += pcol[i+3]*(rt->minimapB);
			}

			r = lr/nCount;
			g = lg/nCount;
			b = lb/nCount;
		}
		surface->drawPixel(dx+decX, dy+decY, r, g, b, Color::ALPHA_OPAQUE);
	}
}

