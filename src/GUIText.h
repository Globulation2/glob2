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

#ifndef __GUITEXT_H
#define __GUITEXT_H

#include "GUIBase.h"

//! This widget is a simple text widget
class Text: public RectangularWidget
{
protected:
	Font *font;
	char *text;
	Uint8 cr, cg, cb, ca;

public:
	Text(int x, int y, Font *font, const char *text="", int w=0, int h=0);
	virtual ~Text() { if (this->text) delete[] this->text; }
	virtual void setText(const char *newText);
	virtual void setColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a = DrawableSurface::ALPHA_OPAQUE);
	virtual void paint(void);

protected:
	virtual void repaint(void);
};

#endif
