/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#ifndef __GUIBASE_H
#define __GUIBASE_H

#include "GAGSys.h"
#include "GraphicContext.h"
#include <vector>

//! if defined, widget added twice are handeld correctly
#define ENABLE_MULTIPLE_ADD_WIDGET

// transform an ucs 16 unicode char to an utf8 one
void UCS16toUTF8(Uint16 ucs16, char utf8[4]);

// return the number of char to go to the next utf8 one in the string
unsigned getNextUTF8Char(unsigned char c);

// return pos of the next UTF8 char in text
unsigned getNextUTF8Char(const char *text, unsigned pos);

// return pos of the previous UTF8 char in text
unsigned getPrevUTF8Char(const char *text, unsigned pos);

//! Widget reacts to SDL_Event and produce Action
enum Action
{
	SCREEN_CREATED,	// after first draw
	SCREEN_DESTROYED,	// after endExectue

	BUTTON_GOT_MOUSEOVER,
	BUTTON_LOST_MOUSEOVER,
	BUTTON_PRESSED,
	BUTTON_RELEASED,
	BUTTON_SHORTCUT,
	BUTTON_STATE_CHANGED,

	TEXT_CURSOR_MOVED,
	TEXT_MODIFIED,
	TEXT_ACTIVATED,
	TEXT_VALIDATED,
	TEXT_CANCELED,
	TEXT_SET,

	LIST_ELEMENT_SELECTED,
	
	NUMBER_ELEMENT_SELECTED,
	RATIO_CHANGED
};

class Screen;

//! A widget is a GUI block element
class Widget
{
public:
	//! Widget constructor
	Widget();
	//! Widget destructor
	virtual ~Widget();

	//! Method called for each timer's tick
	virtual void onTimer(Uint32 tick) { }
	//! Method called for each SDL_Event
	virtual void onSDLEvent(SDL_Event *event) { }
	//! Drawing methode of the widget, called on a clean screen and doesn't need to do any addUpdateRect()
	virtual void paint(void)=0;

	//! if the widget is visible it receive paint event, timer event and SDL event. Otherwise it receive no events.
	bool visible;

protected:
	friend class Screen;
	//! Return true if (px,py) is in the rect (x,y)-(x+w,y+h)
	bool isPtInRect(int px, int py, int x, int y, int w, int h) { if ((px>x) && (py>y) && (px<x+w) && (py<y+h)) return true; else return false; }
	//! Screen that contains the widget
	Screen *parent;
};

//! The parent for all standards widgets like Button, texts, etc...
class RectangularWidget:public Widget
{
public:
	//! RectangularWidget destructor
	virtual ~RectangularWidget() { }

	//! Show the widget
	virtual void show(void);
	//! Hide the widget
	virtual void hide(void);
	//! Show or hide the widget, depending on visible
	virtual void setVisible(bool visible);

protected:

	//! Repaint the widget in its environment
	virtual void repaint(void)=0;

protected:
	//! Coordinates and size
	int x, y, w, h;
};

//! The screen is the widget container and has a background
class Screen
{
public:
	virtual ~Screen();

	//! Method called for each timer's tick
	virtual void onTimer(Uint32 tick) { }
	//! Method called for each SDL_Event
	virtual void onSDLEvent(SDL_Event *event) { }
	//! Method called when a widget produces an Action
	virtual void onAction(Widget *source, Action action, int par1, int par2)=0;
	//! Full screen paint, call paint(0, 0, gfx->getW(), gfx->getH())
	virtual void paint();
	//! Real paint methode, the screen draws itself here
	virtual void paint(int x, int y, int w, int h)=0;
	//! Wrapper methode, call paint(x, y, w, h)
	virtual void paint(SDL_Rect *r) { paint(r->x, r->y, r->w, r->h); }

	//! Run the screen until someone call endExecute(returnCode). Return returnCode
	int execute(DrawableSurface *gfx, int stepLength);
	//! Call this methode to stop the execution of the screen
	void endExecute(int returnCode);
	//! Next update will update the whole screen
	void addUpdateRect();
	//! Next update will include the (x,y)-(x+w,y+h) rect
	void addUpdateRect(int x, int y, int w, int h);
	//! Add widget, added widget are garbage collected
	void addWidget(Widget *widget);
	//! Remove widget, note that removed widget are not garbage collected
	void removeWidget(Widget *widget);
	//! Call onSDLEvent on each widget after having called onSDLEvent on the screen itself
	void dispatchEvents(SDL_Event *event);
	//! Call onTimer on each widget after having called onTimer on the screen itself
	void dispatchTimer(Uint32 tick);
	//! Call paint on each widget after having called paint on the screen itself. Do a full update after
	void dispatchPaint(DrawableSurface *gfx);
	//! Merge all addUpdateRect calls and do the resulting update
	void repaint(DrawableSurface *gfx);
	//! Return the associated drawable surface
	DrawableSurface *getSurface(void) { return gfxCtx; }
	//! Return the width of the screen
	int getW(void) { if (gfxCtx) return gfxCtx->getW(); else return 0; }
	//! Return the height of the screen
	int getH(void) { if (gfxCtx) return gfxCtx->getH(); else return 0; }

protected:
	//! true while execution is running
	bool run;
	//! the return code
	int returnCode;
	//! the widgets
	std::vector<Widget *> widgets;
	//! the rectangle to be updated on repaint
	std::vector<SDL_Rect> updateRects;
	//! the associated drawable surface
	DrawableSurface *gfxCtx;
};


//! Base class used for screen that don't take full frame and/or are non-blocking
class OverlayScreen:public Screen
{
public:
	//! Int to say when we have finished
	int endValue;
	//! Displacement from top-left corner of screen
	int decX, decY;

public:
	//! Constructor, take the context in which the overlay must be create and it's size in x and y
	OverlayScreen(GraphicContext *parentCtx, int w, int h);
	//! Destructor
	virtual ~OverlayScreen();

	//! Call thisinstead of dispatch event
	virtual void translateAndProcessEvent(SDL_Event *event);
	//! Paint a part of the background of screen
	virtual void paint(int x, int y, int w, int h);
};


#endif 
