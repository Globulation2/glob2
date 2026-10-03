// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2022-2023 Nathan Mills
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Minimap.h"
#include "EngineTiming.h"
#include "FixedPoint.h"
#include "Ressource.h"
#include "RessourceType.h"
#include "GlobalContainer.h"
#include "Unit.h"


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

  // Track the next row in the incremental refresh.
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
	mapW = ngame.map.getW();
	mapH = ngame.map.getH();
}

void Minimap::resizeViewport(int width)
{
	gameWidth = width;
	if (!noX && mapW) computeMinimapPositioning();
}



void Minimap::draw(const Scene &drawn, int localteam, int viewportX, int viewportY, int viewportW, int viewportH)
{
	PERF_SCOPE_TIME(Minimap);
	if (noX || !drawn.map.getW()) return; // nothing extracted yet
	scene = &drawn;
	mapW = drawn.map.getW();
	mapH = drawn.map.getH();

  // Compute the position of the minimap if it needs to be scaled & centered
	computeMinimapPositioning();

	Uint8 borderR;
	Uint8 borderG;
	Uint8 borderB;
	Uint8 borderA;
	// draw the either black or transparent border around the minimap
	if (!globalContainer->settings.translucentPanels)
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
  
  // calculate the offset for the viewport square
	offset_x = scene->entities.teams[localteam].startPosX - mapW / 2;
	offset_y = scene->entities.teams[localteam].startPosY - mapH / 2;

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
		const int rows_to_render = std::max(1, mini_h/MINIMAP_REFRESH_TICKS);
		
		refreshPixelRows(update_row, (update_row + rows_to_render) % (mini_h), localteam);
		update_row += rows_to_render;
		update_row %= (mini_h);
	}
	//Draw the surface
	globalContainer->gfx->drawSurface(gameWidth-menuWidth+xOffset, yOffset, surface);

	//Draw the viewport square, taking into account that it may
	//wrap around the sides of the minimap

	int startx, starty, endx, endy;
	convertToScreen(viewportX, viewportY, startx, starty);
	convertToScreen(viewportX + viewportW, viewportY + viewportH, endx, endy);

	// Wrapped endpoints coincide for a complete period. Use the extent to
	// distinguish a full-width/height viewport from an empty one.
	if (viewportW >= mapW) { startx = mini_x; endx = mini_x + mini_w - 1; }
	if (viewportH >= mapH) { starty = mini_y; endy = mini_y + mini_h - 1; }
	const int spanX = (endx - startx + mini_w) % mini_w;
	const int spanY = (endy - starty + mini_h) % mini_h;
	for (int i=0; i<spanX; ++i)
	{
		const int n = mini_x + (startx - mini_x + i) % mini_w;
		globalContainer->gfx->drawPixel(n, starty, 255, 255, 255);
		globalContainer->gfx->drawPixel(n, endy, 255, 255, 255);
	}
	for (int i=0; i<spanY; ++i)
	{
		const int n = mini_y + (starty - mini_y + i) % mini_h;
		globalContainer->gfx->drawPixel(startx, n, 255, 255, 255);
		globalContainer->gfx->drawPixel(endx, n, 255, 255, 255);
	}
	///The lines are out of alignment, so a single pixel in the bottom right hand of the square
	///is never drawn
	globalContainer->gfx->drawPixel(endx, endy, 255, 255, 255);

	///Draw a 1 pixel border around the minimap
	globalContainer->gfx->drawRect(gameWidth-menuWidth+xOffset-1,
	                               yOffset-1, 
	                               width+2, 
	                               height+2, 
	                               200, 200, 200);
	scene = nullptr;
}


bool Minimap::insideMinimap(int x, int y)
{
	if (noX) return false;
	computeMinimapPositioning();

	if(x > (mini_x) && x < (mini_x + mini_w)
			&& y > (mini_y) && y < (mini_y + mini_h))
		return true;
	return false;
}



void Minimap::convertToMap(int nx, int ny, int& x, int& y)
{
	if (noX) return;
	computeMinimapPositioning();

	int xpos = nx - mini_x;
	int ypos = ny - mini_y;
	x = (offset_x + (int)((float)(mapW) / (float)(mini_w) * (float)(xpos))) % mapW;
	y = (offset_y + (int)((float)(mapH) / (float)(mini_h) * (float)(ypos))) % mapH;
}



void Minimap::convertToScreen(int nx, int ny, int& x, int& y)
{
	if (noX) return;
	computeMinimapPositioning();

	int xpos = (nx - offset_x) & (mapW - 1); // map sizes are powers of two
	int ypos = (ny - offset_y) & (mapH - 1);

	x = mini_x + (int)((float)(xpos) * (float)(mini_w) / (float)(mapW)) % (mini_w);
	y = mini_y + (int)((float)(ypos) * (float)(mini_h) / (float)(mapH)) % (mini_h);
}



void Minimap::resetMinimapDrawing()
{
	update_row = -1;
}



void Minimap::setMinimapMode(MinimapMode mode)
{
	minimapMode = mode;
}



void Minimap::computeMinimapPositioning()
{
	if (noX) return;
	gameWidth = globalContainer->gfx->getW();
	
	if(mapW > mapH)
	{
	  // If the width is greater than the height, normal width but shrink the height
		mini_w = width;
		mini_h = (mapH*height) / mapW;
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
		mini_w = (mapW*width) / mapH;
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
	assert(localTeam<Team::MAX_COUNT);

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

	int pcol[3+MAX_RESOURCES];

	// get data
	int szX = mini_w;
	int decX = mini_offset_x, decY = mini_offset_y;

	// Variables for traversing each map square within a minimap square.
	// Using ?.16 fixed-point representation (gives a 2x speedup):
	const int dMx = ((scene->map.getW())<<FIXED_POINT_SHIFT_16) / (mini_w);
	const int dMy = ((scene->map.getH())<<FIXED_POINT_SHIFT_16) / (mini_h);
	const int decSPX=offset_x<<FIXED_POINT_SHIFT_16, decSPY=offset_y<<FIXED_POINT_SHIFT_16;
	bool useMapDiscovered = (minimapMode == HideFOW);

	const SceneEntities &entities = scene->entities;
	Uint32 visibleTeams = entities.teams[localTeam].me;
	if (globalContainer->isViewingGame()) visibleTeams = globalContainer->replayVisibleTeams;

	const int dy = row;
	for (int dx=0; dx<szX; dx++)
	{
		memset(pcol, 0, sizeof(pcol));
		int nCount = 0;
		int UnitOrBuildingIndex = -1;
		
		// compute
		for (int minidyFP=dMy*dy+decSPY; minidyFP<=(dMy*(dy+1))+decSPY; minidyFP+=FIXED_POINT_ONE) { // Fixed-point numbers
			int minidy = minidyFP>>FIXED_POINT_SHIFT_16;
			for (int minidxFP=dMx*dx+decSPX; minidxFP<=(dMx*(dx+1))+decSPX; minidxFP+=FIXED_POINT_ONE) // Fixed-point numbers
			{
				int minidx = minidxFP>>FIXED_POINT_SHIFT_16;
				bool seenUnderFOW = false;

				Uint16 gid=scene->map.getAirUnit(minidx, minidy);
				if (gid==NOGUID)
					gid=scene->map.getGroundUnit(minidx, minidy);
				if (gid==NOGUID)
				{
					gid=scene->map.getBuilding(minidx, minidy);
					if (gid!=NOGUID)
					{
						const SceneBuilding *building = entities.building(gid);
						if (building && (building->seenByMask & visibleTeams))
						{
							seenUnderFOW = true;
						}
					}
				}
				if (gid!=NOGUID)
				{
					int teamId=gid/Unit::MAX_COUNT;
					if (useMapDiscovered || scene->map.isFOWDiscovered(minidx, minidy, visibleTeams))
					{
						if (teamId==localTeam)
							UnitOrBuildingIndex = 0;
						else if ((entities.teams[localTeam].allies) & visibleTeams)
							UnitOrBuildingIndex = 1;
						else
							UnitOrBuildingIndex = 2;
						goto unitOrBuildingFound;
					}
					else if (seenUnderFOW)
					{
						if (teamId==localTeam)
							UnitOrBuildingIndex = 3;
						else if ((entities.teams[localTeam].allies) & visibleTeams)
							UnitOrBuildingIndex = 4;
						else
							UnitOrBuildingIndex = 5;
						goto unitOrBuildingFound;
					}
				}
				
				if (useMapDiscovered || scene->map.isMapDiscovered(minidx, minidy, visibleTeams))
				{
					// get color to add
					int pcolIndex;
					const auto& r = scene->map.getResource(minidx, minidy);
					if (r.type!=NO_RES_TYPE)
					{
						pcolIndex=r.type + 3;
					}
					else
					{
						pcolIndex=scene->map.getUMTerrain(minidx,minidy);
					}
					
					// get weight to add
					int pcolAddValue;
					if (useMapDiscovered || scene->map.isFOWDiscovered(minidx, minidy, visibleTeams))
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
			for (int i=0; i<MAX_RESOURCES; i++)
			{
				const ResourceType *rt = globalContainer->resourcesTypes.get(i);
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
