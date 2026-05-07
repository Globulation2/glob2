// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
	void Text::constructor(int x, int y, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string text, int w, int h)
	{
		this->x=x;
		this->y=y;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
	
		this->font=font;
		this->text=text;
	
		internalInit();
		assert(fontPtr);
		
		// If w or h is specified it means that we want the text left/top aligned in a box that is not related to the length of this->text
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
	
	void Text::internalInit(void)
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
	
		
		fontPtr->pushStyle(style);
		
		if (hAlignFlag==ALIGN_FILL)
			wDec=(w-fontPtr->getStringWidth(text.c_str()))>>1;
		else
			wDec=0;
	
		if (vAlignFlag==ALIGN_FILL)
			hDec=(h-fontPtr->getStringHeight(text.c_str()))>>1;
		else
			hDec=0;
	
		parent->getSurface()->drawString(x+wDec, y+hDec, fontPtr, text.c_str());
		fontPtr->popStyle();
	}
	
	void Text::setText(const std::string newText)
	{
		if (this->text != newText)
		{
			// copy text
			this->text = newText;
		
			if ((!keepW) || (!keepH))
			{
				fontPtr->pushStyle(style);
				if (!keepW)
					w = fontPtr->getStringWidth(newText);
				if (!keepH)
					h = fontPtr->getStringHeight(newText);
				fontPtr->popStyle();
			}
			parent->onAction(this, TEXT_SET, 0, 0);
		}
	}
	
	void Text::setStyle(Font::Style style)
	{
		this->style = style;
	}
}
