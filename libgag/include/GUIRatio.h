// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
		Ratio(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int size, int value, const std::string font);
		Ratio(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int size, int value, const std::string font, const std::string& tooltip, const std::string &tooltipFont);
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
