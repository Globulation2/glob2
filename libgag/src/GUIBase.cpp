/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charri�e
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

#include <GUIBase.h>

// this function support base unicode (UCS16)
void UCS16toUTF8(Uint16 ucs16, char utf8[4])
{
	if (ucs16<0x80)
	{
		utf8[0]=ucs16;
		utf8[1]=0;
	}
	else if (ucs16<0x800)
	{
		utf8[0]=((ucs16>>6)&0x1F)|0xC0;
		utf8[1]=(ucs16&0x3F)|0x80;
		utf8[2]=0;
	}
	else if (ucs16<0xd800)
	{
		utf8[0]=((ucs16>>12)&0x0F)|0xE0;
		utf8[1]=((ucs16>>6)&0x3F)|0x80;
		utf8[2]=(ucs16&0x3F)|0x80;
		utf8[3]=0;
	}
	else
	{
		utf8[0]=0;
		fprintf(stderr, "GAG : UCS16toUTF8 : Error, can handle UTF16 characters\n");
	}
}

// this function support full unicode (UCS32)
unsigned getNextUTF8Char(unsigned char c)
{
	if (c>0xFC)
	{
		return 6;
	}
	else if (c>0xF8)
	{
		return 5;
	}
	else if (c>0xF0)
	{
		return 4;
	}
	else if (c>0xE0)
	{
		return 3;
	}
	else if (c>0xC0)
	{
		return 2;
	}
	else
	{
		return 1;
	}
}

unsigned getNextUTF8Char(const char *text, unsigned pos)
{
	unsigned next=pos+getNextUTF8Char(text[pos]);
	assert(next<=strlen(text));
	return next;
}

unsigned getPrevUTF8Char(const char *text, unsigned pos)
{
	// TODO : have a more efficient algo
	unsigned last=0, i=0;
	while (i<(unsigned)pos)
	{
		last=i;
		i+=getNextUTF8Char(text[i]);
	}
	return last;
}

Widget::Widget()
{
	visible=true;
}

Widget::~Widget()
{
	
}

void RectangularWidget::show(void)
{
	assert(parent);
	visible=true;
	repaint();
}

void RectangularWidget::hide(void)
{
	assert(parent);
	visible=false;
	repaint();
}

void RectangularWidget::setVisible(bool visible)
{
	assert(parent);
	this->visible=visible;
	repaint();
}

Screen::~Screen()
{
	for (std::vector<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); it++)
	{
		delete (*it);
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

	gfx->setClipRect();
	dispatchPaint(gfx);
	addUpdateRect(0, 0, gfx->getW(), gfx->getH());
	run=true;
	onAction(NULL, SCREEN_CREATED, 0, 0);
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
	onAction(NULL, SCREEN_DESTROYED, 0, 0);

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
	for (std::vector<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); it++)
	{
		if ((*it)->visible)
			(*it)->onTimer(tick);
	}
}

void Screen::dispatchPaint(DrawableSurface *gfx)
{
	assert(gfx);
	gfxCtx=gfx;
	gfxCtx->setClipRect();
	paint();
	for (std::vector<Widget *>::iterator it=widgets.begin(); it!=widgets.end(); it++)
	{
		if ((*it)->visible)
			(*it)->paint();
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

