/*
 * GAG GUI base file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
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

int Screen::execute(GraphicContext *gfx, int stepLength)
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
		SDL_Event lastMouseMotion, event;
		bool hadLastMouseMotion=false;
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
				default:
				{
					dispatchEvents(&event);
				}
				break;
			}
		}
		if (hadLastMouseMotion)
			dispatchEvents(&lastMouseMotion);

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
	widget->parent=this;
	widgets.push_back(widget);
}

void Screen::dispatchEvents(SDL_Event *event)
{
	onSDLEvent(event);
	for (std::vector<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); it++)
	{
		(*it)->onSDLEvent(event);
	}
}

void Screen::dispatchTimer(Uint32 tick)
{
	onTimer(tick);
	{
		for (std::vector<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); it++)
		{
			(*it)->onTimer(tick);
		}
	}
}

void Screen::dispatchPaint(GraphicContext *gfx)
{
	gfxCtx=gfx;
	paint();
	{
		for (std::vector<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); it++)
		{
			(*it)->paint(gfx);
		}
	}
}

void Screen::repaint(GraphicContext *gfx)
{
	if (updateRects.size()>0)
	{
		SDL_Rect *rects=new SDL_Rect[updateRects.size()];
		{
			for (unsigned int i=0; i<updateRects.size(); i++)
				rects[i]=updateRects[i];
		}
		SDL_UpdateRects(((SDLGraphicContext *)gfx)->screen, updateRects.size(), rects);
		delete[] rects;
		updateRects.clear();
	}
}

