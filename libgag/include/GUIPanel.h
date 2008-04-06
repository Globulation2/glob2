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

#ifndef GUIPanel_h
#define GUIPanel_h

#include "GUIBase.h"

namespace GAGGUI
{
	///A panel is simply a container for other widgets
	class Panel : public RectangularWidget
	{
	public:
		///Constructs a panel
		Panel(int x, int y, int w, int h);
	
		~Panel();
	
		///Adds a widget
		void addWidget(Widget* widget);
	
		virtual void onTimer(Uint32 tick);	
		virtual void onSDLOtherEvent(SDL_Event *event);
		virtual void paint(void);
		virtual void internalInit(void);
	protected:
		virtual void onSDLActive(SDL_Event *event);
		virtual void onSDLKeyDown(SDL_Event *event);
		virtual void onSDLKeyUp(SDL_Event *event);
		virtual void onSDLMouseMotion(SDL_Event *event);
		virtual void onSDLMouseButtonUp(SDL_Event *event);
		virtual void onSDLMouseButtonDown(SDL_Event *event);
		virtual void onSDLVideoExpose(SDL_Event *event);
		std::vector<Widget*> widgets;
	};
};

#endif
