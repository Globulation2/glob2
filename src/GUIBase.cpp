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

#include "GUIBase.h"

Screen::~Screen()
{
	{
		for (std::vector<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); it++)
		{
			delete (*it);
		}
	}
}

void Screen::paint()
{
	assert(gfxCtx);
	paint(0, 0, gfxCtx->getW(), gfxCtx->getH());
}

int Screen::execute(DrawableSurface *gfx, int stepLength)
{
	Uint32 frameStartTime;
	Sint32 frameWaitTime;

	dispatchPaint(gfx);
	addUpdateRect(0, 0, gfx->getW(), gfx->getH());
	run=true;
	while (run)
	{
		// get first timer
		frameStartTime=SDL_GetTicks();

		// send timer
		dispatchTimer(frameStartTime);

		// send events
		SDL_Event lastMouseMotion, windowEvent, event;
		bool hadLastMouseMotion=false;
		bool wasWindowEvent=false;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
				case SDL_QUIT:
				{
					run=false;
					returnCode=-1;
					break;
				}
				break;
				case SDL_MOUSEMOTION:
				{
					hadLastMouseMotion=true;
					lastMouseMotion=event;
				}
				break;
				case SDL_ACTIVEEVENT:
				{
					windowEvent=event;
					wasWindowEvent=true;
				}
				default:
				{
					dispatchEvents(&event);
				}
				break;
			}
		}
		if (hadLastMouseMotion)
			dispatchEvents(&lastMouseMotion);
		if (wasWindowEvent)
			dispatchEvents(&windowEvent);

		// redraw
		repaint(gfx);

		// wait timer
		frameWaitTime=SDL_GetTicks()-frameStartTime;
		frameWaitTime=stepLength-frameWaitTime;
		if (frameWaitTime>0)
			SDL_Delay(frameWaitTime);
	}

	return returnCode;
}

void Screen::endExecute(int returnCode)
{
	run=false;
	this->returnCode=returnCode;
}

void Screen::addUpdateRect()
{
	assert(gfxCtx);
	updateRects.clear();
	addUpdateRect(0, 0, gfxCtx->getW(), gfxCtx->getH());
}

void Screen::addUpdateRect(int x, int y, int w, int h)
{
	SDL_Rect r;
	r.x=x;
	r.y=y;
	r.w=w;
	r.h=h;
	updateRects.push_back(r);
}

void Screen::addWidget(Widget *widget)
{
	assert(widget);
	widget->parent=this;
	// this option enable or disable the multiple add check
#ifdef ENABLE_MULTIPLE_ADD_WIDGET
	bool already=false;
	for (std::vector<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); it++)
		if ((*it)==widget)
		{
			already=true;
			break;
		}
	if (!already)
#endif
		widgets.push_back(widget);
}

void Screen::removeWidget(Widget *widget)
{
	assert(widget);
	assert(widget->parent==this);
	for (std::vector<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); it++)
		if ((*it)==widget)
		{
			widgets.erase(it);
			break;
		}
}

void Screen::dispatchEvents(SDL_Event *event)
{
	onSDLEvent(event);
	for (std::vector<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); it++)
	{
		if ((*it)->visible)
			(*it)->onSDLEvent(event);
	}
}

void Screen::dispatchTimer(Uint32 tick)
{
	onTimer(tick);
	{
		for (std::vector<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); it++)
		{
			if ((*it)->visible)
				(*it)->onTimer(tick);
		}
	}
}

void Screen::dispatchPaint(DrawableSurface *gfx)
{
	assert(gfx);
	gfxCtx=gfx;
	gfxCtx->setClipRect();
	paint();
	{
		for (std::vector<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); it++)
		{
			if ((*it)->visible)
				(*it)->paint(gfx);
		}
	}
}

void Screen::repaint(DrawableSurface *gfx)
{
	if (updateRects.size()>0)
	{
		SDL_Rect *rects=new SDL_Rect[updateRects.size()];
		{
			for (unsigned int i=0; i<updateRects.size(); i++)
				rects[i]=updateRects[i];
		}
		gfx->updateRects(rects, updateRects.size());
		delete[] rects;
		updateRects.clear();
	}
}

