// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

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
		virtual void onSDLTextInput(SDL_Event *event);
	};
}
