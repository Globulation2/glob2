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

#ifndef __GUI_RATIO_H
#define __GUI_RATIO_H

#include "GUIBase.h"

class Ratio: public RectangularWidget
{
	CLASSDEF(Ratio)
		BASECLASS(RectangularWidget)
	MEMBERS
	CLASSEND;

public:
	Ratio() { font=NULL; }
	Ratio(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int size, int value, const char *font);
	virtual ~Ratio();

	virtual void onTimer(Uint32 tick);
	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(void);

	void set(int value);
	int getMax(void);
	int get(void);

	void setScale(float start, float ratio);
protected:
	virtual void repaint(void);
	virtual void internalPaint(void);

protected:
	int textHeight;
	const Font *font;

	//! This is the wheight of the scrool bar
	int size;
	//! The current value of the scrool bar
	int value;
	int oldValue;
	//! The max value of the scrool bar
	int max;

	//! If scrool bar is pressed
	bool pressed;
	//! The mouse position when button was pressed
	int px, py;
	//! The value before the button was pressed
	int pValue;

	float start;
	float ratio;

	bool needRefresh;
};

#endif

