/*
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

#include <GUISelector.h>
#include <Toolkit.h>
#include <GraphicContext.h>
#include <assert.h>
#include <algorithm>

using namespace GAGCore;

namespace GAGGUI
{
	Selector::Selector(int x, int y, Uint32 hAlign, Uint32 vAlign, unsigned width, unsigned defaultValue, unsigned maxValue, bool taper, unsigned step, const char *sprite, Sint32 id)
	{
		this->x=x;
		this->y=y;
		this->maxValue=maxValue;
		this->taper=taper;
		this->step=step;
		this->w=width;
		this->h=10;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
		this->value=defaultValue;
		this->dragging = false;
	
		if (sprite)
			this->sprite=sprite;
		this->id=id;
		archPtr=NULL;
	}
	
	Selector::Selector(int x, int y, Uint32 hAlign, Uint32 vAlign, unsigned width, const std::string &tooltip, const std::string &tooltipFont, unsigned defaultValue, unsigned maxValue, bool taper, unsigned step, const char *sprite, Sint32 id)
		: RectangularWidget(tooltip, tooltipFont)
	{
		this->x=x;
		this->y=y;
		this->maxValue=maxValue;
		this->taper=taper;
		this->step=step;
		this->w=width;
		this->h=10;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
		this->value=defaultValue;
		this->dragging = false;
	
		if (sprite)
			this->sprite=sprite;
		this->id=id;
		archPtr=NULL;
	}
	
	void Selector::clipValue(int v)
	{
		if (v>=static_cast<int>(maxValue))
			value=maxValue;
		else if (v<0)
			value=0;
		else
			value=static_cast<unsigned>(v);
		
		value = step * ((value + step/2) / step);
	}
	
	void Selector::onSDLMouseButtonDown(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONDOWN);
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h) &&
			(event->button.button == SDL_BUTTON_LEFT))
		{
			dragging = true;
			int dx=event->button.x-x-3;
			int v=dx*static_cast<int>(maxValue)/(w-4);
			clipValue(v);
			parent->onAction(this, VALUE_CHANGED, value, 0);
		}
	}
		
	void Selector::onSDLMouseMotion(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEMOTION);
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		if (dragging)
		{
			int dx=event->button.x-x-3;
			int v=dx*static_cast<int>(maxValue)/(w-4);
			clipValue(v);
			parent->onAction(this, VALUE_CHANGED, value, 0);
		}
	}
	
	void Selector::onSDLMouseButtonUp(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONUP);
		if (event->button.button == SDL_BUTTON_LEFT)
		{
			dragging = false;
		}
	}
	
	void Selector::internalInit(void)
	{
		if (id>=0)
		{
			archPtr=Toolkit::getSprite(sprite.c_str());
			assert(archPtr);
		}
	}
 
 
	void Selector::paint(void)
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		assert(parent);
		assert(parent->getSurface());
	
		///Taper the line to show a definitive larger end on the right
		if(taper)
		{
			parent->getSurface()->drawLine(x+4, y+(h>>1)+1, x+w-3, y+(h>>1)-1, 180, 180, 180);
			parent->getSurface()->drawLine(x+4, y+(h>>1)+1, x+w-3, y+(h>>1), 180, 180, 180);
			parent->getSurface()->drawLine(x+4, y+(h>>1)+1, x+w-3, y+(h>>1)+1, 180, 180, 180);
			parent->getSurface()->drawLine(x+4, y+(h>>1)+2, x+w-3, y+(h>>1)+2, 180, 180, 180);
			parent->getSurface()->drawLine(x+4, y+(h>>1)+2, x+w-3, y+(h>>1)+3, 180, 180, 180);
			parent->getSurface()->drawLine(x+4, y+(h>>1)+2, x+w-3, y+(h>>1)+4, 180, 180, 180);
		}
		else
		{
			parent->getSurface()->drawHorzLine(x+4, y+(h>>1)+1, w-4, 180, 180, 180);
			parent->getSurface()->drawHorzLine(x+4, y+(h>>1)+2, w-4, 180, 180, 180);
		}
		parent->getSurface()->drawVertLine(x+2, y+2, h, 180, 180, 180);
		parent->getSurface()->drawVertLine(x+3, y+2, h, 180, 180, 180);
		parent->getSurface()->drawVertLine(x+w-1, y+2, h, 180, 180, 180);
		parent->getSurface()->drawVertLine(x+w-2, y+2, h, 180, 180, 180);
	
		if (id<0)
		{
			parent->getSurface()->drawRect(x+(w-4)*value/maxValue, y, 6, h+4, 255, 255, 255);
		}
		else
		{
			parent->getSurface()->drawSprite(x+(w-4)*value/maxValue, y, archPtr, id);
		}
	}
}
