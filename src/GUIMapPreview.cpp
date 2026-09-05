// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <StringTable.h>
#include <Toolkit.h>
#include <GUIStyle.h>
using namespace GAGCore;

#include "GUIMapPreview.h"

MapPreview::MapPreview(int x, int y, Uint32 hAlign, Uint32 vAlign)
{
	this->x = x;
	this->y = y;
	this->hAlignFlag = hAlign;
	this->vAlignFlag = vAlign;
	this->w = PreviewSize;
	this->h = PreviewSize;
	surface = NULL;
}

MapPreview::MapPreview(int x, int y, Uint32 hAlign, Uint32 vAlign, const std::string &tooltip, const std::string &tooltipFont)
	: RectangularWidget(tooltip, tooltipFont)
{
	this->x = x;
	this->y = y;
	this->hAlignFlag = hAlign;
	this->vAlignFlag = vAlign;
	this->w = PreviewSize;
	this->h = PreviewSize;
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
	MapThumbnail *n = new MapThumbnail();
	n->loadFromMap(mapName);
	setMapThumbnail(*n);
	delete n;
}



void MapPreview::setMapThumbnail(const MapThumbnail &nthumbnail)
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
	int sx, sy, sw, sh;
	getScreenPos(&sx, &sy, &sw, &sh);

	assert(parent);
	assert(parent->getSurface());
	DrawableSurface *target = parent->getSurface();

	// getScreenPos returns the layout area; ALIGN_FILL means "centre the
	// fixed PreviewSize tile within the available area on that axis".
	if (hAlignFlag == ALIGN_FILL)
		sx += (sw - PreviewSize) / 2;
	if (vAlignFlag == ALIGN_FILL)
		sy += (sh - PreviewSize) / 2;

	if (surface)
	{
		target->drawSurface(sx, sy, surface);
	}
	else
	{
		Font *standardFont = Toolkit::getFont("standard");
		assert(standardFont);
		const std::string line0 = Toolkit::getStringTable()->getString("[GUIMapPreview text 0]");
		const std::string line1 = Toolkit::getStringTable()->getString("[GUIMapPreview text 1]");
		const int line0Width = standardFont->getStringWidth(line0);
		const int line1Width = standardFont->getStringWidth(line1);
		const int lineHeight = standardFont->getStringHeight(line0);
		const int centerY    = sy + PreviewSize / 2;
		target->drawString(sx + (PreviewSize - line0Width) / 2, centerY - lineHeight, standardFont, line0);
		target->drawString(sx + (PreviewSize - line1Width) / 2, centerY,              standardFont, line1);
	}
	Style::style->drawFrame(target, sx, sy, PreviewSize, PreviewSize, Color::ALPHA_TRANSPARENT);
}
