/*
  Copyright (C) 2007 Bradley Arsenault
  
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#include "GUIKeySelector.h"
#include "Toolkit.h"
#include <string>
#include "StringTable.h"

using namespace GAGCore;

namespace GAGGUI
{
	KeySelector::KeySelector(int x, int y, Uint32 hAlign, Uint32 vAlign, const char *font, int w, int h)
	{
		constructor(x, y, hAlign, vAlign, font, w, h);
	}
	
	
	
	KeySelector::KeySelector(int x, int y, Uint32 hAlign, Uint32 vAlign, const char *font, const std::string& tooltip, const std::string &tooltipFont, int w, int h)
	: HighlightableWidget(tooltip, tooltipFont)
	{
		constructor(x, y, hAlign, vAlign, font, w, h);
	}
	
	
	
	void KeySelector::paint(void)
	{
		HighlightableWidget::paint();
		
		if(blinkVisible)
		{
			int wDec, hDec;
			int x, y, w, h;
			getScreenPos(&x, &y, &w, &h);
			
			assert(parent);
			assert(parent->getSurface());
			
			fontPtr->pushStyle(style);
			
			wDec=(w-fontPtr->getStringWidth(text.c_str()))/2;
			hDec=(h-fontPtr->getStringHeight(text.c_str()))/2;
		
			parent->getSurface()->drawString(x+wDec, y+hDec, fontPtr, text.c_str());
			fontPtr->popStyle();
		}

		blinkTimer-=1;
		
		if(blinkTimer == 1)
		{
			blinkVisible = !blinkVisible;
			blinkTimer = 12;
		}
	}
	
	
	
	KeyPress KeySelector::getKey()
	{
		return key;
	}
	
	
	
	void KeySelector::setKey(const KeyPress& nkey)
	{
		key = nkey;
		this->text = nkey.getTranslated();
	}
	
	
	
	void KeySelector::constructor(int x, int y, Uint32 hAlign, Uint32 vAlign, const char *font, int w, int h)
	{
		this->x=x;
		this->y=y;
		this->w=w;
		this->h=h;
	
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
	
		this->font = font;
		this->activated = false;
		
		key = KeyPress();
		
		fontPtr = Toolkit::getFont(font);
		
		this->text = key.getTranslated();
		
		blinkTimer = 0;
		blinkVisible = true;
		
		if ((w) || (hAlignFlag==ALIGN_FILL))
		{
			this->w=w;
		}
		else
		{
			this->w=fontPtr->getStringWidth(text.c_str());
		}
	
		if ((h) || (vAlignFlag==ALIGN_FILL))
		{
			this->h=h;
		}
		else
		{
			this->h=fontPtr->getStringHeight(text.c_str());
		}
	}
	
	
	
	void KeySelector::onSDLMouseButtonDown(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONDOWN);
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
		{
			activated=true;
			text = Toolkit::getStringTable()->getString("[waiting for key]");
			this->blinkTimer=12;
			this->blinkVisible = false;
		}
	}
	
	
	
	void KeySelector::onSDLKeyDown(SDL_Event *event)
	{
		if (activated)
		{
			key = KeyPress(event->key.keysym, true);
			activated=false;
			
			this->text = key.getTranslated();
			this->blinkVisible = true;
			this->blinkTimer = 0;
			
			parent->onAction(this, KEY_CHANGED, 0, 0);
		}
	}
};


