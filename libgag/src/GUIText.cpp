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

#include <GUIText.h>
#include <stdarg.h>
#include <SupportFunctions.h>
#include <assert.h>

Text::Text(int x, int y, Font *font, const char *text, int w, int h)
{
	this->x=x;
	this->y=y;
	this->font=font;
	this->text=NULL;
	this->w=w;
	this->h=h;
	cr = 255;
	cg = 255;
	cb = 255;
	ca = DrawableSurface::ALPHA_OPAQUE;
	if (text)
	{
		int textLength=strlen(text);
		this->text=new char[textLength+1];
		strncpy(this->text, text, textLength+1);
	}
	else
		this->text=NULL;
}

void Text::setText(const char *newText, ...)
{
	assert(parent);
	assert(font);
	assert(newText);
	
	va_list arglist;
	char output[1024];
	int upW, upH, nW, nH;

	// handle printf-like outputs
	va_start(arglist, newText);
	vsnprintf(output, 1024, newText, arglist);
	va_end(arglist);
	output[1023]=0;

	// erase old
	if (w)
		nW=upW=w;
	else
	{
		upW=font->getStringWidth(text)+5;
		nW=font->getStringWidth(output);
	}
	if (h)
		nH=upH=h;
	else
	{
		upH=font->getStringHeight(text);
		nH=font->getStringHeight(output);
	}
	parent->paint(x, y, upW, upH);

	// copy text
	int textLength=strlen(output)+1;
	if (this->text)
		delete[] this->text;
	this->text=new char[textLength];
	strcpy(this->text, output);

	// draw new
	paint();
	parent->addUpdateRect(x, y, MAX(nW, upW), MAX(nH, upH));
	parent->onAction(this, TEXT_SET, 0, 0);
}

void Text::setColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	cr = r;
	cg = g;
	cb = b;
	ca = a;
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

		font->pushColor(cr, cg, cb, ca);
		parent->getSurface()->drawString(x+wDec, y+hDec, font, "%s", text);
		font->popColor();
	}
}

void Text::repaint(void)
{
	int upW, upH;
	assert(font);
	
	DrawableSurface *s=parent->getSurface();
	int rx=x, ry=y, rw=s->getW()-x, rh=s->getH()-y;
	
	if (w)
	{
		upW=w;
	}
	else
	{
		assert(font);
		upW=font->getStringWidth(text)+2;
	}
	
	if (h)
	{
		upH=h;
	}
	else
	{
		assert(font);
		upH=font->getStringHeight(text);
	}
	
	if (w || h)
	{
		SDL_Rect r = {0, 0, s->getW(), s->getH()};
		GAG::rectClipRect(rx, ry, rw, rh, r);
		s->setClipRect(rx, ry, rw, rh);
	}
	
	parent->paint(x-1, y, upW, upH);
	paint();
	
	if (w || h)
		s->setClipRect();
	
	parent->addUpdateRect(x-1, y, upW, upH);
}
