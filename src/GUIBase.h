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

#ifndef __GUIBASE_H
#define __GUIBASE_H

#include "Header.h"
#include "GraphicContext.h"
#include <vector>

//! if defined, widget added twice are handeld correctly
#define ENABLE_MULTIPLE_ADD_WIDGET

enum Action
{
	BUTTON_GOT_MOUSEOVER,
	BUTTON_LOST_MOUSEOVER,
	BUTTON_PRESSED,
	BUTTON_RELEASED,
	BUTTON_STATE_CHANGED,
	TEXT_CURSOR_MOVED,
	TEXT_MODIFFIED,
	TEXT_ACTIVATED,
	TEXT_VALIDATED,
	TEXT_SET,
	LIST_ELEMENT_SELECTED
};

class Screen;

class Widget
{
public:
	Widget();
	virtual ~Widget();

	virtual void onTimer(Uint32 tick) { }
	virtual void onSDLEvent(SDL_Event *event) { }
	virtual void paint(DrawableSurface *gfx)=0;

	//! if the widget is visible it receive paint event, timer event and SDL event. Otherwise it receive no events.
	bool visible;

protected:
	friend class Screen;
	bool isPtInRect(int px, int py, int x, int y, int w, int h) { if ((px>x) && (py>y) && (px<x+w) && (py<y+h)) return true; else return false; }

	Screen *parent;
};

class Screen
{
public:
	virtual ~Screen();

	virtual void onTimer(Uint32 tick) { }
	virtual void onSDLEvent(SDL_Event *event) { }
	virtual void onAction(Widget *source, Action action, int par1, int par2)=0;
	virtual void paint();
	virtual void paint(int x, int y, int w, int h)=0;
	virtual void paint(SDL_Rect *r) { paint(r->x, r->y, r->w, r->h); }

	int execute(DrawableSurface *gfx, int stepLength);
	void endExecute(int returnCode);
	void addUpdateRect();
	void addUpdateRect(int x, int y, int w, int h);
	//! Add widget, added widget are garbage collected
	void addWidget(Widget *widget);
	//! Remove widget, note that removed widget are not garbage collected
	void removeWidget(Widget *widget);
	void dispatchEvents(SDL_Event *event);
	void dispatchTimer(Uint32 tick);
	void dispatchPaint(DrawableSurface *gfx);
	void repaint(DrawableSurface *gfx);
	DrawableSurface *getSurface(void) { return gfxCtx; }
	int getW(void) { if (gfxCtx) return gfxCtx->getW(); else return 0; }
	int getH(void) { if (gfxCtx) return gfxCtx->getH(); else return 0; }

protected:
	bool run;
	int returnCode;
	std::vector<Widget *> widgets;
	std::vector<SDL_Rect> updateRects;
	DrawableSurface *gfxCtx;
};

#endif 
