/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "GUIMapPreview.h"
#include "GlobalContainer.h"

MapPreview::MapPreview(int x, int y, const char *mapName)
{
	this->x=x;
	this->y=y;
	this->gfx=NULL;
	setThumbnailNameFromMapName(mapName);
}

void MapPreview::paint(DrawableSurface *gfx)
{
	this->gfx=gfx;
	repaint();
}

void MapPreview::setMapThumbnail(const char *mapName)
{
	setThumbnailNameFromMapName(mapName);
	repaint();
}

void MapPreview::setThumbnailNameFromMapName(const char *name)
{
	if (name)
		snprintf(thumbnailName, 256, "%s.tn", name);
	else
		thumbnailName[0]=0;
}

void MapPreview::repaint(void)
{
	assert(gfx);
	if (thumbnailName[0]!=0)
	{
		SDL_RWops *stream=globalContainer->fileManager.open(thumbnailName, "rb", false);
		if (stream)
		{
			Uint8 colors[7][3] = {
				{ 0, 40, 120 }, // water
				{ 170, 170, 0 }, // sand
				{ 0, 90, 0 }, // grass
				{ 0, 60, 0 }, // wood
				{ 211, 207, 167 }, // corn
				{ 104, 112, 124 }, // stone
				{ 41, 157, 165 } }; // seaweed

			Uint8 thumbData[128*128];
			SDL_RWread(stream, thumbData, 128*128, 1);
			SDL_RWclose(stream);
			int dx, dy, i;
			Uint8 pixel;
			int accR, accG, accB, accI;
			for (dy=0; dy<128; dy++)
			{
				for (dx=0; dx<128; dx++)
				{
					accR=0;
					accG=0;
					accB=0;
					accI=0;
					pixel=thumbData[dy*128+dx];
					for (i=0; i<7; i++)
					{
						if (pixel&0x1)
						{
							accR+=colors[i][0];
							accG+=colors[i][1];
							accB+=colors[i][2];
							accI++;
						}
						pixel>>=1;
					}
					assert(accI);
					gfx->drawPixel(x+dx, y+dy, (Uint8)(accR/accI), (Uint8)(accG/accI), (Uint8)(accB/accI));
				}
			}
			parent->addUpdateRect(x, y, 128, 128);
			return;
		}
	}
	parent->paint(x, y, 128, 128);
	gfx->drawLine(x, y, x+128, y+128, 255, 0, 0);
	gfx->drawLine(x+128, y, x, y+128, 255, 0, 0);
	parent->addUpdateRect(x, y, 128, 128);
}