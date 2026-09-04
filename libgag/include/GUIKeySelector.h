// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
