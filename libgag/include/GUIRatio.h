/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef __GUI_RATIO_H
#define __GUI_RATIO_H

#include "GUIBase.h"
#include <string>

namespace GAGCore
{
	class Font;
}

namespace GAGGUI
{
	class Ratio : public HighlightableWidget
	{
	protected:
		int textHeight;
		GAGCore::Font *fontPtr;
		std::string font;
	
		//! This is the wheight of the scrool bar
		int size;
		//! The current value of the scrool bar
		int value;
		int oldValue;
		//! The max value of the scrool bar
		int max;
	
		//! If scrool bar is pressed
		bool pressed;
		//! The mouse position when button was pressed
		int px, py;
		//! The value before the button was pressed
		int pValue;
	
		float start;
		float ratio;
	
		bool valueUpdated;

	public:
		Ratio() { fontPtr=NULL; }
		Ratio(const std::string& tooltip, const std::string &tooltipFont) : HighlightableWidget(tooltip, tooltipFont) { fontPtr=NULL; }
		Ratio(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int size, int value, const char *font);
		Ratio(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int size, int value, const char *font, const std::string& tooltip, const std::string &tooltipFont);
		virtual ~Ratio();
	
		virtual void onTimer(Uint32 tick);
		virtual void internalInit(void);
		virtual void paint(void);
	
		void set(int newValue) {value=newValue;};
		int getMax(void);
		int get(void);
	
		void setScale(float start, float ratio);
		

	protected:
		virtual void onSDLMouseButtonDown(SDL_Event *event);
		virtual void onSDLMouseButtonUp(SDL_Event *event);
		virtual void onSDLMouseMotion(SDL_Event *event);
	};
}

#endif
