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

using namespace GAGCore;

namespace GAGGUI
{
	Text::Text(int x, int y, Uint32 hAlign, Uint32 vAlign, const char *font, const char *text, int w, int h)
	{
		this->x=x;
		this->y=y;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
	
		this->font=font;
		this->text=text;
	
		init();
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
	}
	
	void Text::init(void)
	{
		fontPtr = Toolkit::getFont(font.c_str());
		assert(fontPtr);
	}
	
	void Text::paint(void)
	{
		int wDec, hDec;
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		assert(parent);
		assert(parent->getSurface());
	
		if (hAlignFlag==ALIGN_FILL)
			wDec=(w-fontPtr->getStringWidth(text.c_str(), style.shape))>>1;
		else
			wDec=0;
	
		if (vAlignFlag==ALIGN_FILL)
			hDec=(h-fontPtr->getStringHeight(text.c_str(), style.shape))>>1;
		else
			hDec=0;
	
		parent->getSurface()->pushFontStyle(fontPtr, style);
		parent->getSurface()->drawString(x+wDec, y+hDec, fontPtr, text.c_str());
		parent->getSurface()->popFontStyle(fontPtr);
	}
	
	void Text::setText(const char *newText)
	{
		if (this->text != newText)
		{
			// copy text
			this->text = newText;
		
			if (!keepW)
				w = fontPtr->getStringWidth(newText);
			if (!keepH)
				h = fontPtr->getStringHeight(newText);
			parent->onAction(this, TEXT_SET, 0, 0);
		}
	}
	
	void Text::setStyle(Font::Style style)
	{
		this->style = style;
	}
}
