// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __GUI_NUMBER_H
#define __GUI_NUMBER_H

#include "GUIBase.h"
#include <vector>
#include <string>

namespace GAGCore
{
	class Font;
}

namespace GAGGUI
{
	class Number: public HighlightableWidget
	{
	protected:
		Sint32 nth;
		Sint32 m;
		std::vector<int> numbers;
		std::string font;
	
		// cache, recomputed at least on paint
		GAGCore::Font *fontPtr;
		int textHeight;
	
	public:
		Number();
		Number(const std::string& tooltip, const std::string &tooltipFont);
		Number(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int m, const std::string font);
		Number(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int m, const std::string font, const std::string& tooltip, const std::string &tooltipFont);
		virtual ~Number();
	
		virtual void internalInit(void);
		virtual void paint(void);
	
		void add(int number);
		void clear(void);
		void setNth(int nth);
		void set(int number);
		int getNth(void);
		int get(void);
	protected:
		virtual void onSDLMouseButtonDown(SDL_Event *event);
		virtual void onSDLMouseWheel(SDL_Event* event);
	};
}

#endif

