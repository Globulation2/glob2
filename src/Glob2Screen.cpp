// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charrière

#include "Glob2Screen.h"
#include "FrontendTheme.h"
#include <GUIText.h>


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
	if (FrontendTheme::current)
		drawFrontend(gfx, widgets);
	else
		gfx->drawFilledRect(0, 0, getW(), getH(), Color(17, 35, 32));
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
	if (FrontendTheme::current)
		drawFrontend(gfx, widgets);
	else
		gfx->drawFilledRect(0, 0, getW(), getH(), Color(17, 35, 32));
}
