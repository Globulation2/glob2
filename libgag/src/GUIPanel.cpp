/*
  Copyright (C) 2008 Bradley Arsenault

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

#include "GUIPanel.h"



namespace GAGGUI
{
	Panel::Panel(int x, int y, int w, int h)
	{
		this->x=x;
		this->y=y;
		this->w=w;
		this->h=h;
	}
	
	
	Panel::~Panel()
	{
		for(std::vector<Widget*>::iterator i = widgets.begin(); i!=widgets.end(); ++i)
		{
			delete *i;
		}
	}

	///Adds a widget
	void Panel::addWidget(Widget* widget)
	{
		widgets.push_back(widget);
	}

	void Panel::onTimer(Uint32 tick)
	{
		for(std::vector<Widget*>::iterator i = widgets.begin(); i!=widgets.end(); ++i)
		{
			if((*i)->visible)
			{
				(*i)->onTimer(tick);
			}
		}
	}
	void Panel::onSDLOtherEvent(SDL_Event *event)
	{
		for(std::vector<Widget*>::iterator i = widgets.begin(); i!=widgets.end(); ++i)
		{
			if((*i)->visible)
			{
				(*i)->onSDLOtherEvent(event);
			}
		}
	}
	void Panel::paint(void)
	{
		for(std::vector<Widget*>::iterator i = widgets.begin(); i!=widgets.end(); ++i)
		{
			if((*i)->visible)
			{
				(*i)->paint();
			}
		}
	}
	void Panel::internalInit(void)
	{
		for(std::vector<Widget*>::iterator i = widgets.begin(); i!=widgets.end(); ++i)
		{
			if((*i)->visible)
			{
				(*i)->internalInit();
			}
		}
	}
	void Panel::onSDLActive(SDL_Event *event)
	{
		for(std::vector<Widget*>::iterator i = widgets.begin(); i!=widgets.end(); ++i)
		{
			if((*i)->visible)
			{
				(*i)->onSDLActive(event);
			}
		}
	}
	void Panel::onSDLKeyDown(SDL_Event *event)
	{
		for(std::vector<Widget*>::iterator i = widgets.begin(); i!=widgets.end(); ++i)
		{
			if((*i)->visible)
			{
				(*i)->onSDLKeyDown(event);
			}
		}
	}
	void Panel::onSDLKeyUp(SDL_Event *event)
	{
		for(std::vector<Widget*>::iterator i = widgets.begin(); i!=widgets.end(); ++i)
		{
			if((*i)->visible)
			{
				(*i)->onSDLKeyUp(event);
			}
		}
	}
	void Panel::onSDLMouseMotion(SDL_Event *event)
	{
		for(std::vector<Widget*>::iterator i = widgets.begin(); i!=widgets.end(); ++i)
		{
			if((*i)->visible)
			{
				(*i)->onSDLMouseMotion(event);
			}
		}
	}
	void Panel::onSDLMouseButtonUp(SDL_Event *event)
	{
		for(std::vector<Widget*>::iterator i = widgets.begin(); i!=widgets.end(); ++i)
		{
			if((*i)->visible)
			{
				(*i)->onSDLMouseButtonUp(event);
			}
		}
	}
	void Panel::onSDLMouseButtonDown(SDL_Event *event)
	{
		for(std::vector<Widget*>::iterator i = widgets.begin(); i!=widgets.end(); ++i)
		{
			if((*i)->visible)
			{
				(*i)->onSDLMouseButtonDown(event);
			}
		}
	}
	void Panel::onSDLVideoExpose(SDL_Event *event)
	{
		for(std::vector<Widget*>::iterator i = widgets.begin(); i!=widgets.end(); ++i)
		{
			if((*i)->visible)
			{
				(*i)->onSDLVideoExpose(event);
			}
		}
	}
};


