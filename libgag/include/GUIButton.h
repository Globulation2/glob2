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

#ifndef __GUIBUTTON_H
#define __GUIBUTTON_H

#include "GUIBase.h"
#include <string>

namespace GAGCore
{
	class Sprite;
	class Font;
}

namespace GAGGUI
{
	class Button: public HighlightableWidget
	{
	protected:
		Uint16 unicodeShortcut;
		
	public:
		Button() {  }
		Button(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int returnCode, Uint16 unicodeShortcut=0);
		Button(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int returnCode, const std::string& tooltip, const std::string &font, Uint16 unicodeShortcut=0);
		virtual ~Button() { }
	
	protected:
		virtual void onSDLKeyDown(SDL_Event *event);
		virtual void onSDLMouseButtonDown(SDL_Event *event);
		virtual void onSDLMouseButtonUp(SDL_Event *event);
	};
	
	class TextButton:public Button
	{
	protected:
		std::string text;
		std::string font;
	
		// cache, recomputed on internalInit
		GAGCore::Font *fontPtr;
	
	public:
		TextButton() { fontPtr=NULL; }
		TextButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font, const char *text, int returnCode,
		const std::string& tooltip, const std::string &tooltipFont, Uint16 unicode=0);
		TextButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font, const char *text, int returxnCode, Uint16 unicodeShortcut=0);
		virtual ~TextButton() { }
		virtual void internalInit(void);
		virtual void paint(void);
	
		void setText(const char *text);
		void setText(const std::string &text) { setText(text.c_str()); }
	};
	
	class OnOffButton:public HighlightableWidget
	{
	protected:
		bool state;
	
	public:
		OnOffButton() { state=false; returnCode=0; }
		OnOffButton(const std::string &tooltip, const std::string &tooltipFont) :HighlightableWidget(tooltip, tooltipFont) { state=false; returnCode=0; }
		OnOffButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, bool startState, int returnCode);
		OnOffButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, bool startState, int returnCode, const std::string &tooltip, const std::string &tooltipFont);
		virtual ~OnOffButton() { }
	
		virtual void paint(void);
		virtual bool getState(void) { return state; }
		virtual void setState(bool newState);
	protected:
		virtual void onSDLMouseButtonDown(SDL_Event *event);
		virtual void onSDLMouseButtonUp(SDL_Event *event);
	};
	
	//! A button that can have multiple color
	class ColorButton:public HighlightableWidget
	{
	protected:
		Sint32 selColor;
		std::vector<GAGCore::Color> v;
		bool isClickable;
	
	public:
		ColorButton() { selColor=returnCode=0; isClickable=true; }
		//! ColorButton constructor
		ColorButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int returnCode);
		//! With a tooltip
		ColorButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string& tooltip, const std::string &tooltipFont, int returnCode);
		//! ColorButton destructor
		virtual ~ColorButton() { }
	
		virtual void paint(void);
		//! Add a color to the color list
		virtual void addColor(const GAGCore::Color& color) { v.push_back(color); }
		//! Clear the color list
		virtual void clearColors(void) { v.clear(); }
		//! Set the color selection to default
		virtual void setSelectedColor(int c=0) { selColor=c; }
		//! Return the color sel
		virtual int getSelectedColor(void) { return selColor; }
		//! Return the number of possible colors
		virtual size_t getNumberOfColors(void) { return v.size(); }
		//! Makes it so that nothing occurs on click
		virtual void setClickable(bool enabled) { isClickable = enabled; }
	protected:
		virtual void onSDLMouseButtonUp(SDL_Event *event);
		virtual void onSDLMouseButtonDown(SDL_Event *event);
	};
	
	//! A button that can have multiple texts
	class MultiTextButton:public TextButton
	{
	protected:
		std::vector<std::string> texts;
		unsigned textIndex;
	
	public:
		MultiTextButton() { textIndex=0; returnCode=0; }
		MultiTextButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font, const char *text, int retuxrnCode, Uint16 unicodeShortcut=0);
		MultiTextButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font, const char *text, int retuxrnCode, const std::string& tooltip, const std::string &tooltipFont, Uint16 unicodeShortcut=0);
		virtual ~MultiTextButton() { }
	
		
		void addText(const char *s);
		void addText(const std::string &s) { addText(s.c_str()); }
		const std::string &getText(void) const { return texts.at(textIndex); }
		void clearTexts(void);
		void setIndex(int i);
		void setIndexFromText(const std::string &s);
		int getIndex(void) const { return textIndex; }
		size_t getCount(void) const { return texts.size(); }
	protected:
		virtual void onSDLMouseButtonDown(SDL_Event *event);
		virtual void onSDLMouseButtonUp(SDL_Event *event);
	};
}

#endif
