// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charrière

#include "Glob2Screen.h"
#include "FrontendTheme.h"
#include <GUIText.h>
#include <algorithm>
#include "GlobalContainer.h"
#include "DynamicClouds.h"


namespace
{
void drawFrontend(DrawableSurface* surface, const std::set<Widget*>& widgets)
{
	// Most menus use the centered 640x480 canvas. Expand for full-window
	// lists/charts, while keeping centered ALIGN_FILL titles inside the common canvas.
	SDL_Rect content{(surface->getW()-640)/2,(surface->getH()-480)/2,640,480};
	for(auto* widget:widgets)
	{
		if(!widget->visible) continue;
		auto* rectangle=dynamic_cast<RectangularWidget*>(widget);
		if(!rectangle) continue;
		SDL_Rect bounds=rectangle->getScreenRect();
		if(dynamic_cast<Text*>(widget) && bounds.w==surface->getW())
		{
			bounds.x=(surface->getW()-640)/2;
			bounds.w=640;
		}
		SDL_UnionRect(&content,&bounds,&content);
	}
	FrontendTheme::current->background(surface,true,&content);
}
}

Glob2Screen::Glob2Screen()
{
}

Glob2Screen::~Glob2Screen()
{

}

int Glob2Screen::execute(DrawableSurface* surface, int stepLength)
{
	FrontendScope scope;
	return Screen::execute(surface, stepLength);
}

void Glob2Screen::paint(void)
{
	if (FrontendTheme::current && Style::style == FrontendTheme::current)
	{
		drawFrontend(gfx,widgets);
		return;
	}
	static int time = 0;
	time++;
	randomSeed = 1;

	// grass
	for (int y = 0; y < getH(); y += 32)
		for (int x = 0; x < getW(); x += 32)
			gfx->drawSprite(x, y, globalContainer->terrain, getNextTerrain());
	dynamic_cast<GraphicContext*>(gfx)->finishDrawingSprite(globalContainer->terrain, 255);

	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
	{
		static DynamicClouds ds(&globalContainer->settings);
		ds.compute(0, 0, getW(), getH(), time, (getW()+31)/32, (getH()+31)/32);
		ds.render(globalContainer->gfx, getW(), getH(), DynamicClouds::SHADOW);
		ds.render(globalContainer->gfx, getW(), getH(), DynamicClouds::CLOUD);
	}
}

unsigned Glob2Screen::getNextTerrain(void)
{
	randomSeed = randomSeed * 69069;
	return ((randomSeed >> 16) & 0xF);
}




Glob2TabScreen::Glob2TabScreen(bool fullScreen, bool longerButtons)
	: TabScreen(fullScreen, longerButtons)
{
}

Glob2TabScreen::~Glob2TabScreen()
{

}

int Glob2TabScreen::execute(DrawableSurface* surface, int stepLength)
{
	FrontendScope scope;
	return Screen::execute(surface, stepLength);
}

void Glob2TabScreen::paint(void)
{
	if (FrontendTheme::current && Style::style == FrontendTheme::current)
	{
		drawFrontend(gfx,widgets);
		return;
	}
	static int time = 0;
	time++;
	randomSeed = 1;

	// grass
	for (int y = 0; y < getH(); y += 32)
		for (int x = 0; x < getW(); x += 32)
			gfx->drawSprite(x, y, globalContainer->terrain, getNextTerrain());
	dynamic_cast<GraphicContext*>(gfx)->finishDrawingSprite(globalContainer->terrain, 255);

	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
	{
		static DynamicClouds ds(&globalContainer->settings);
		ds.compute(0, 0, getW(), getH(), time, (getW()+31)/32, (getH()+31)/32);
		ds.render(globalContainer->gfx, getW(), getH(), DynamicClouds::SHADOW);
		ds.render(globalContainer->gfx, getW(), getH(), DynamicClouds::CLOUD);
	}
}

unsigned Glob2TabScreen::getNextTerrain(void)
{
	randomSeed = randomSeed * 69069;
	return ((randomSeed >> 16) & 0xF);
}
