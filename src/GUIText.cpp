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

#include "GUIText.h"

Text::Text(int x, int y, const Font *font, const char *text, int w, int h)
{
	this->x=x;
	this->y=y;
	this->font=font;
	this->text=NULL;
	this->w=w;
	this->h=h;
	if (text)
	{
		int textLength=strlen(text);
		this->text=new char[textLength+1];
		strncpy(this->text, text, textLength+1);
	}
	else
		this->text=NULL;
}

void Text::setText(const char *newText)
{
	int upW, upH, nW, nH;

	assert(parent);
	assert(font);
	assert(newText);
	
	// erase old
	if (w)
		nW=upW=w;
	else
	{
		upW=font->getStringWidth(text)+5;
		nW=font->getStringWidth(newText);
	}
	if (h)
		nH=upH=h;
	else
	{
		upH=font->getStringHeight(text);
		nH=font->getStringHeight(newText);
	}
	parent->paint(x, y, upW, upH);

	// copy text
	int textLength=strlen(newText);
	if (this->text)
		delete[] this->text;
	this->text=new char[textLength+1];
	strncpy(this->text, newText, textLength+1);

	// draw new
	paint();
	parent->addUpdateRect(x, y, MAX(nW, upW), MAX(nH, upH));
	parent->onAction(this, TEXT_SET, 0, 0);
}

void Text::paint(void)
{
	assert(parent);
	assert(parent->getSurface());
	if (visible)
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

		parent->getSurface()->drawString(x+wDec, y+hDec, font, text);
	}
}

void Text::repaint(void)
{
	int upW, upH;
	assert(font);
	if (w)
		upW=w;
	else
	{
		assert(font);
		upW=font->getStringWidth(text)+2;
	}
	if (h)
		upH=h;
	else
	{
		assert(font);
		upH=font->getStringHeight(text);
	}
	parent->paint(x-1, y, upW, upH);
	paint();
	parent->addUpdateRect(x-1, y, upW, upH);
}
