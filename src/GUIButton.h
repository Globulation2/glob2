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

class Button: public Widget
{
public:
	Button(int x, int y, int w, int h, Sprite *arch, int standardId, int highlightID, int returnCode);
	virtual ~Button() { }

	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(DrawableSurface *gfx);
	virtual void repaint(void);

protected:
	int x, y, w, h;
	Sprite *arch;
	int standardId, highlightID, returnCode;
	bool highlighted;
	DrawableSurface *gfx;
};

class TextButton:public Button
{
public:
	TextButton(int x, int y, int w, int h, Sprite *arch, int standardId, int highlightID, const Font *font, const char *text, int returnCode);
	virtual ~TextButton() { delete[] text; }

	virtual void paint(DrawableSurface *gfx);
	virtual void repaint(void);

	void setText(const char *text);

protected:
	char *text;
	const Font *font;
	int decX, decY;
};

class OnOffButton:public Widget
{
public:
	OnOffButton(int x, int y, int w, int h, bool startState, int returnCode);
	virtual ~OnOffButton() { }

	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(DrawableSurface *gfx);
	virtual bool getState(void) { return state; }
	virtual void setState(bool newState);

protected:
	virtual void internalPaint(void);
	virtual void repaint(void);

protected:
	int x, y, w, h;
	bool state;
	int returnCode;
	bool highlighted;
	DrawableSurface *gfx;
};


#endif 
