/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charri�e
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

#ifndef __GUIBUTTON_H
#define __GUIBUTTON_H

#include "GUIBase.h"
#include <string>

class Sprite;
class Font;

class Button: public HighlightableWidget
{
protected:
	Uint16 unicodeShortcut;
	Sint32 standardId;
	Sint32 highlightID;
	std::string sprite;

	//! cache, recomputed on internalInit
	Sprite *archPtr;

public:
	Button() { unicodeShortcut=0; highlighted=false; standardId=-1; highlightID=-1; archPtr=NULL; }
	Button(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *sprite, int standardId, int highlightID, int returnCode, Uint16 unicodeShortcut=0);
	virtual ~Button() { }

	virtual void onSDLEvent(SDL_Event *event);

protected:
	virtual void internalInit(int x, int y, int w, int h);
	virtual void internalRepaint(int x, int y, int w, int h);
};

class TextButton:public Button
{
protected:
	std::string text;
	std::string font;

	// cache, recomputed on internalInit
	Font *fontPtr;

public:
	TextButton() { fontPtr=NULL; }
	TextButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *sprite, int standardId, int highlightID, const char *font, const char *text, int retuxrnCode, Uint16 unicodeShortcut=0);
	virtual ~TextButton() { }

	void setText(const char *text);

protected:
	virtual void internalInit(int x, int y, int w, int h);
	virtual void internalRepaint(int x, int y, int w, int h);
};

class OnOffButton:public HighlightableWidget
{
protected:
	bool state;

public:
	OnOffButton() { state=false; returnCode=0; highlighted=false; }
	OnOffButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, bool startState, int returnCode);
	virtual ~OnOffButton() { }

	virtual void onSDLEvent(SDL_Event *event);
	virtual bool getState(void) { return state; }
	virtual void setState(bool newState);

protected:
	virtual void internalRepaint(int x, int y, int w, int h);
};

//! A button that can have multiple color
class ColorButton:public HighlightableWidget
{
protected:
	class Color
	{
	public:
		Color() { r=g=b=0; }
		Color(int r, int g, int b) { this->r=r; this->g=g; this->b=b; }
	public:
		Sint32 r;
		Sint32 g;
		Sint32 b;
	};

	Sint32 selColor;
	std::vector<Color> v;

public:
	ColorButton() { selColor=returnCode=0; }
	//! ColorButton constructor
	ColorButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int returnCode);
	//! ColorButton destructor
	virtual ~ColorButton() { }

	//! Process SDL event
	virtual void onSDLEvent(SDL_Event *event);
	//! Add a color to the color list
	virtual void addColor(int r, int g, int b) { v.push_back(Color(r, g, b)); }
	//! Clear the color list
	virtual void clearColors(void) { v.clear(); }
	//! Set the color selection to default
	virtual void setSelectedColor(int c=0) { selColor=c; repaint(); }
	//! Return the color sel
	virtual int getSelectedColor(void) { return selColor; }
	//! Return the number of possible colors
	virtual int getNumberOfColors(void) { return v.size(); }

protected:
	virtual void internalRepaint(int x, int y, int w, int h);
};

#endif
