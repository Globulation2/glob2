/*
  Copyright (C) 2007 Bradley Arsenault

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

#ifndef __GUIKeySelector_h
#define __GUIKeySelector_h

#include "GUIBase.h"
#include "GraphicContext.h"
#include <string>
#include "KeyPress.h"

namespace GAGGUI
{
	//! This widget is a simple widget for selecting a key
	class KeySelector: public HighlightableWidget
	{
	public:
		//Construct with given cordinates
		KeySelector(int x, int y, Uint32 hAlign, Uint32 vAlign, const std::string font, int w=0, int h=0);
		//Construct with tooltip
		KeySelector(int x, int y, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string& tooltip, const std::string &tooltipFont, int w=0, int h=0);
		//Destructor
		virtual ~KeySelector() { }

		///Paint the widget
		virtual void paint(void);

		///Gets the current key
		KeyPress getKey();

		///Sets the key
		void setKey(const KeyPress& key);
	protected:
		///Constructs this widget
		void constructor(int x, int y, Uint32 hAlign, Uint32 vAlign, const std::string font, int w, int h);
		///Handles events, records the key
		virtual void onSDLKeyDown(SDL_Event *event);
		///Handles press events
		virtual void onSDLMouseButtonDown(SDL_Event *event);

		///The font
		GAGCore::Font *fontPtr;
		std::string font;
		///Font style
		GAGCore::Font::Style style;
		///The current key
		KeyPress key;
		///the current text
		std::string text;
		///Returns whether its activated
		bool activated;
		///Times the blinking of the waiting for text
		Uint16 blinkTimer;
		///Tells whether the text is in the visible stage of a blink
		bool blinkVisible;
	};
};


#endif
