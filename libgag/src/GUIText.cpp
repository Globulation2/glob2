/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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
#include <Toolkit.h>
#include <GraphicContext.h>
#include <algorithm>

Text::Text(int x, int y, Uint32 hAlign, Uint32 vAlign, const char *font, const char *text, int w, int h)
{
	this->x=x;
	this->y=y;
	this->hAlignFlag=hAlign;
	this->vAlignFlag=vAlign;

	this->font=font;
	this->text=text;

	internalInit(0, 0, 0, 0);
	assert(fontPtr);
	assert(text);
	if ((w) || (hAlignFlag==ALIGN_FILL))
	{
		this->w=w;
		keepW=true;
	}
	else
	{
		this->w=fontPtr->getStringWidth(text);
		keepW=false;
	}

	if ((h) || (vAlignFlag==ALIGN_FILL))
	{
		this->h=h;
		keepH=true;
	}
	else
	{
		this->h=fontPtr->getStringHeight(text);
		keepH=false;
	}

	cr = 255;
	cg = 255;
	cb = 255;
	ca = DrawableSurface::ALPHA_OPAQUE;
}

void Text::setText(const char *newText)
{
/*	va_list arglist;
	char output[1024];

	// handle printf-like outputs
	va_start(arglist, newText);
	vsnprintf(output, 1024, newText, arglist);
	va_end(arglist);
	output[1023]=0;
*/
	if (this->text != newText)
	{
		if (!keepW)
			w = std::max<int>(w, fontPtr->getStringWidth(newText));
		if (!keepH)
			h = std::max<int>(h, fontPtr->getStringHeight(newText));
		
		// copy text
		this->text = newText;
	
		repaint();
	
		if (!keepW)
			w = fontPtr->getStringWidth(newText);
		if (!keepH)
			h = fontPtr->getStringHeight(newText);
		parent->onAction(this, TEXT_SET, 0, 0);
	}
}

void Text::setColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	cr = r;
	cg = g;
	cb = b;
	ca = a;
}

void Text::internalRepaint(int x, int y, int w, int h)
{
	int wDec, hDec;

	if (hAlignFlag==ALIGN_FILL)
		wDec=(w-fontPtr->getStringWidth(text.c_str()))>>1;
	else
		wDec=0;

	if (vAlignFlag==ALIGN_FILL)
		hDec=(h-fontPtr->getStringHeight(text.c_str()))>>1;
	else
		hDec=0;

	Font::Style style = fontPtr->getStyle();
	style.r = cr;
	style.g = cg;
	style.b = cb;
	style.a = ca;
	fontPtr->pushStyle(style);
	parent->getSurface()->drawString(x+wDec, y+hDec, fontPtr, text.c_str());
	fontPtr->popStyle();
}

void Text::internalInit(int x, int y, int w, int h)
{
	fontPtr = Toolkit::getFont(font.c_str());
	assert(fontPtr);
}
