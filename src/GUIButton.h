/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef __GUIBUTTON_H
#define __GUIBUTTON_H

#include "GUIBase.h"

class Button: public RectangularWidget
{
public:
	Button(int x, int y, int w, int h, Sprite *arch, int standardId, int highlightID, int returnCode, Uint16 unicodeShortcut=0);
	virtual ~Button() { }

	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(void);

protected:
	//! Repaint method, call parent->paint(), internalPaint() and parent->addUpdateRect()
	virtual void repaint(void);

protected:
	Sprite *arch;
	int standardId, highlightID, returnCode;
	Uint16 unicodeShortcut;
	bool highlighted;
};

class TextButton:public Button
{
public:
	TextButton(int x, int y, int w, int h, Sprite *arch, int standardId, int highlightID, const Font *font, const char *text, int returnCode, Uint16 unicodeiShortcut=0);
	virtual ~TextButton() { if (text) delete[] text; }

	virtual void paint(void);
	
	void setText(const char *text);

protected:
	//! Repaint method, call parent->paint(), internalPaint() and parent->addUpdateRect()
	virtual void repaint(void);
	//! Set text internally, withouh calling repaint
	void internalSetText(const char *text);

protected:
	char *text;
	const Font *font;
	int decX, decY;
};

class OnOffButton:public RectangularWidget
{
public:
	OnOffButton(int x, int y, int w, int h, bool startState, int returnCode);
	virtual ~OnOffButton() { }

	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(void);
	virtual bool getState(void) { return state; }
	virtual void setState(bool newState);

protected:
	//! Repaint method, call parent->paint(), internalPaint() and parent->addUpdateRect()
	virtual void repaint(void);
	//! Internal paint method, call by paint and repaint
	virtual void internalPaint(void);

protected:
	bool state;
	int returnCode;
	bool highlighted;
};

//! A button that can have multiple color
class ColorButton:public RectangularWidget
{
public:
	//! ColorButton constructor
	ColorButton(int x, int y, int w, int h, int returnCode);
	//! ColorButton destructor
	virtual ~ColorButton() { }

	//! Process SDL event
	virtual void onSDLEvent(SDL_Event *event);
	//! Inital paint call, parent is ok and no addUpdateRect is needed.
	virtual void paint(void);
	//! Add a color to the color list
	virtual void addColor(int r, int g, int b) { vr.push_back(r); vg.push_back(g); vb.push_back(b); }
	//! Clear the color list
	virtual void clearColors(void) { vr.clear(); vg.clear(); vb.clear(); selColor=0; }
	//! Set the color selection to default
	virtual void setSelectedColor(int c=0) { selColor=c; repaint(); }
	//! Return the color sel
	virtual int getSelectedColor(void) { return selColor; }
	//! Return the number of possible colors
	virtual int getNumberOfColors(void) { return vr.size(); }

protected:
	//! Repaint method, call parent->paint(), internalPaint() and parent->addUpdateRect()
	virtual void repaint(void);
	//! Internal paint method, call by paint and repaint
	virtual void internalPaint(void);

protected:
	int selColor;
	std::vector<int> vr, vg, vb;
	int returnCode;
	bool highlighted;
};

#endif
