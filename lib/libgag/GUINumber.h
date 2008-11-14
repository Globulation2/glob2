/*
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
		Number(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int m, const char *font);
		Number(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int m, const char *font, const std::string& tooltip, const std::string &tooltipFont);
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
	};
}

#endif

