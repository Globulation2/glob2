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

#ifndef __GUITEXTINPUT_H
#define __GUITEXTINPUT_H

#include "GUIBase.h"

class TextInput: public RectangularWidget
{
protected:
	CLASSDEF(TextInput)
		BASECLASS(RectangularWidget)
	MEMBERS
		ITEM(std::string, font)
		ITEM(std::string, text)
		ITEM(bool, activated)
		ITEM(Uint32, cursPos)
	CLASSEND;

	// cache, recomputed at least on paint
	Font *fontPtr;
	unsigned textDep;
	int cursorScreenPos;

public:
	TextInput();
	TextInput(int x, int y, int w, int h, const char *font, const char *text, bool activated, unsigned textLength=512);
	virtual ~TextInput() { }

	virtual void onTimer(Uint32 tick);
	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(void);
	virtual void setText(const char *newText);
	virtual const char *getText(void) { return text.c_str(); }

protected:
	virtual void repaint(void);
	void recomputeTextInfos(void);
};

#endif 
