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

#ifndef __GUITEXTINPUT_H
#define __GUITEXTINPUT_H

#include "GUIBase.h"
#include <string>
#include <iostream>
#include <sstream>

namespace GAGCore
{
	class Font;
}

namespace GAGGUI
{
	class TextInput: public HighlightableWidget
	{
		// STATUS nct 20060315 : clean
	protected:
		std::string font;
		std::string text;
		bool activated;
		size_t cursPos;
		size_t maxLength;
		bool password;
		std::vector<std::string> autocompletableWord;
	
		// cache, recomputed at least on paint
		GAGCore::Font *fontPtr;
		unsigned textDep;
		int cursorScreenPos;
		std::string pwd;
	
	public:
		// constructor / destructor
		TextInput(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string text="", bool activated=false, size_t maxLength=0, bool password=false) { constructor(x, y, w, h, hAlign, vAlign, font, text, activated, maxLength, password); }
		//! With a tooltip
		TextInput(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string& tooltip, const std::string &tooltipFont, const std::string &text="", bool activated=false, size_t maxLength=0, bool password=false) : HighlightableWidget(tooltip, tooltipFont)
		 { constructor(x, y, w, h, hAlign, vAlign, font, text.c_str(), activated, maxLength, password); }
		TextInput(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string& tooltip, const std::string &tooltipFont, const std::string text="", bool activated=false, size_t maxLength=0, bool password=false) : HighlightableWidget(tooltip, tooltipFont)
		 { constructor(x, y, w, h, hAlign, vAlign, font, text, activated, maxLength, password); }
		virtual ~TextInput() { }
	
		// methods inherited from widget
		virtual void onTimer(Uint32 tick);
		virtual void internalInit(void);
		virtual void paint(void);
		
		// text setter / getter
		void setText(const std::string &newText);
		template<typename T>
		void setText(T from)
		{
			std::ostringstream oss;
			oss << from;
			setText(oss.str());
		}
		
		const std::string &getText(void) { return text; }
		template<typename T>
		T getText(void)
		{
			std::istringstream iss(getText());
			T v;
			iss >> v;
			return v;
		}
		
		// cursor / activation
		void setCursorPos(size_t pos){ cursPos = pos;};
		void deactivate(void) { activated = false; recomputeTextInfos(); }
		void activate(void) { activated = true; recomputeTextInfos(); }
		
		// autocompletion
		void addAutoCompletableWord(const std::string &word);
		void removeAutoCompletableWord(const std::string &word);
		bool getAutoCompleteSuggestion(const std::string & word, std::vector<std::string> & wordlist);
		std::string getAutoComplete(const std::string & word, int n);
		
		bool isActivated(void) { return activated; } 

	protected:
		void constructor(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string text, bool activated, size_t maxLength, bool password);
		void recomputeTextInfos(void);
		virtual void onSDLKeyDown(SDL_Event *event);
		virtual void onSDLMouseButtonDown(SDL_Event *event);
	};
}

#endif
