/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __GUITEXTINPUT_H
#define __GUITEXTINPUT_H

#include "GUIBase.h"
#include <string>

namespace GAGCore
{
	class Font;
}

namespace GAGGUI
{
	class TextInput: public HighlightableWidget
	{
	protected:
		std::string font;
		std::string text;
		bool activated;
		size_t cursPos;
		size_t maxLength;
		bool password;
	
		// cache, recomputed at least on paint
		GAGCore::Font *fontPtr;
		unsigned textDep;
		int cursorScreenPos;
		std::string pwd;
	
	public:
		TextInput(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font, const char *text="", bool activated=false, size_t maxLength=0, bool password=false);
		virtual ~TextInput() { }
	
		virtual void onTimer(Uint32 tick);
		virtual void onSDLEvent(SDL_Event *event);
		virtual void init(void);
		virtual void paint(void);
		void setText(const char *newText);
		const char *getText(void) { return text.c_str(); }
		void deactivate(void) { activated=false; recomputeTextInfos(); }
		
	protected:
		void recomputeTextInfos(void);
	};
}

#endif 
