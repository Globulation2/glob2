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

#ifndef __GUILIST_H
#define __GUILIST_H

#include "GUIBase.h"
#include <vector>

class List: public RectangularWidget
{
public:
	List(int x, int y, int w, int h, const Font *font);
	virtual ~List();

	virtual void onTimer(Uint32 tick) { }
	virtual void onSDLEvent(SDL_Event *event);
	virtual void paint(void);

	void addText(const char *text, int pos);
	void addText(const char *text);
	void removeText(int pos);
	void clear(void);
	char *getText(int pos) const;
	char *get(void) const;
	//! Call this after all add has been done
	void commit(void) { repaint(); }
	void sort(void);
	
	int getNth(void) const;
	void setNth(int nth);

protected:
	virtual void repaint(void);
	virtual void internalPaint(void);

protected:
	int textHeight;
	const Font *font;
	std::vector<char *> strings;
	int nth;
};

#endif

