/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#ifndef __GUI_NUMBER_H
#define __GUI_NUMBER_H

#include "GUIBase.h"
#include <vector>

class Number: public RectangularWidget
{
public:
	Number(int x, int y, int w, int h, int m, const Font *font);
	virtual ~Number();

	virtual void onTimer(Uint32 tick) { }
	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(void);

	void add(int number);
	void clear(void);
	void setNth(int nth);
	void set(int number);
	int getNth(void);
	int get(void);

protected:
	virtual void repaint(void);
	virtual void internalPaint(void);

protected:
	int textHeight;
	const Font *font;
	std::vector<int> numbers;
	int nth;
	int m;
};

#endif

