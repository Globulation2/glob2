/*
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

#include <FileManager.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <Stream.h>
#include <StreamFilter.h>
#include <BinaryStream.h>
#include <GUIStyle.h>
using namespace GAGCore;

#include "GUIMapPreview.h"
#include "Map.h"
#include "Utilities.h"

MapPreview::MapPreview(int x, int y, Uint32 hAlign, Uint32 vAlign)
{
	this->x = x;
	this->y = y;
	this->hAlignFlag = hAlign;
	this->vAlignFlag = vAlign;
	this->w = 128;
	this->h = 128;
	surface = NULL;
}

MapPreview::MapPreview(int x, int y, Uint32 hAlign, Uint32 vAlign, const std::string &tooltip, const std::string &tooltipFont)
	: RectangularWidget(tooltip, tooltipFont)
{
	this->x = x;
	this->y = y;
	this->hAlignFlag = hAlign;
	this->vAlignFlag = vAlign;
	this->w = 128;
	this->h = 128;
	surface = NULL;
}



MapPreview::~MapPreview()
{
	if(surface)
		delete surface;
}



std::string MapPreview::getMethode(void)
{
	return Toolkit::getStringTable()->getString("[handmade map]");
}



bool MapPreview::isThumbnailLoaded()
{
	return thumbnail.isLoaded();
}


void MapPreview::setMapThumbnail(const std::string& mapName)
{
	MapThumbnail n;
	n.loadFromMap(mapName);
	setMapThumbnail(n);
}



void MapPreview::setMapThumbnail(MapThumbnail nthumbnail)
{
	thumbnail = nthumbnail;
	if(surface)
	{
		delete surface;
	}
	if(thumbnail.isLoaded())
	{
		surface = new DrawableSurface(128, 128);
		thumbnail.loadIntoSurface(surface);
	}
	else
	{
		surface = NULL;
	}
}



void MapPreview::paint(void)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);
	
	assert(parent);
	assert(parent->getSurface());
	
	if (hAlignFlag == ALIGN_FILL)
		x += (w-128)>>1;
	if (vAlignFlag == ALIGN_FILL)
		y += (h-128)>>1;
	
	if (surface)
	{
		parent->getSurface()->drawSurface(x, y, surface);
	}
	else
	{
		/*parent->getSurface()->drawLine(x, y, x+127, y+127, 255, 0, 0);
		parent->getSurface()->drawLine(x+127, y, x, y+127, 255, 0, 0);*/
		/*parent->getSurface()->drawRect(x, y, 128, 128, ColorTheme::frontColor);*/
		Font *standardFont = Toolkit::getFont("standard");
		assert(standardFont);
		std::string line0 = Toolkit::getStringTable()->getString("[GUIMapPreview text 0]");
		std::string line1 = Toolkit::getStringTable()->getString("[GUIMapPreview text 1]");
		int sw0 = standardFont->getStringWidth(line0);
		int sw1 = standardFont->getStringWidth(line1);
		int sh = standardFont->getStringHeight(line0);
		parent->getSurface()->drawString(x+((128-sw0)>>1), y+64-sh, standardFont, line0);
		parent->getSurface()->drawString(x+((128-sw1)>>1), y+64, standardFont, line1);
	}
	Style::style->drawFrame(parent->getSurface(), x, y, 128, 128, Color::ALPHA_TRANSPARENT);
}
