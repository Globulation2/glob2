/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __GUITEXT_H
#define __GUITEXT_H

#include "GUIBase.h"

//! This widget is a simple text widget
class Text: public RectangularWidget
{
protected:
	CLASSDEF(Text)
		BASECLASS(RectangularWidget)
	MEMBERS
		ITEM(std::string, font)
		ITEM(std::string, text)
		ITEM(Uint8, cr)
		ITEM(Uint8, cg)
		ITEM(Uint8, cb)
		ITEM(Uint8, ca)
		ITEM(bool, keepW)
		ITEM(bool, keepH)
	CLASSEND;

	// cache, recomputed at least on paint
	Font *fontPtr;

public:
	Text() { cr=cg=cb=ca=0; }
	Text(int x, int y, Uint32 hAlign, Uint32 vAlign, const char *font, const char *text="", int w=0, int h=0);
	virtual ~Text() { }
	virtual const char *getText() const { return text.c_str();}
	virtual void setText(const char *newText, ...);
	virtual void setColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a = DrawableSurface::ALPHA_OPAQUE);

protected:
	virtual void internalInit(int x, int y, int w, int h);
	virtual void internalRepaint(int x, int y, int w, int h);
};

#endif
