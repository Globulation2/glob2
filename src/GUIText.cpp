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

#include "GUIText.h"

Text::Text(int x, int y,const Font *font, const char *text, int w, int h)
{
	this->x=x;
	this->y=y;
	this->font=font;
	this->text=text;
	this->w=w;
	this->h=h;
}

void Text::paint(DrawableSurface *gfx)
{
	int wDec, hDec;

	if (w)
		wDec=(w-font->getStringWidth(text))>>1;
	else
		wDec=0;

	if (h)
		hDec=(h-font->getStringHeight(text))>>1;
	else
		hDec=0;

	gfx->drawString(x+wDec, y+hDec, font, text);
}