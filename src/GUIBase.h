/*
 * GAG GUI base file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __GUIBASE_H
#define __GUIBASE_H

#include "Header.h"
#include "GraphicContext.h"
#include <vector>

enum Action
{
	BUTTON_GOT_MOUSEOVER,
	BUTTON_LOST_MOUSEOVER,
	BUTTON_PRESSED,
	BUTTON_RELEASED,
	BUTTON_STATE_CHANGED,
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
	virtual ~Widget() { }

	virtual void onTimer(Uint32 tick) { }
	virtual void onSDLEvent(SDL_Event *event) { }
	virtual void paint(DrawableSurface *gfx)=0;

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
	void addWidget(Widget *widget);
	void dispatchEvents(SDL_Event *event);
	void dispatchTimer(Uint32 tick);
	void dispatchPaint(DrawableSurface *gfx);
	void repaint(DrawableSurface *gfx);
	DrawableSurface *getSurface(void) { return gfxCtx; }

protected:
	bool run;
	int returnCode;
	std::vector<Widget *> widgets;
	std::vector<SDL_Rect> updateRects;
	DrawableSurface *gfxCtx;
};

#endif 
