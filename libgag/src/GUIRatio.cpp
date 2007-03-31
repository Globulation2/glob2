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

#include <GUIRatio.h>
#include <Toolkit.h>
#include <GraphicContext.h>
#include <assert.h>
#include <sstream>

using namespace GAGCore;

namespace GAGGUI
{
	Ratio::Ratio(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int size, int value, const char *font)
	{
		assert(font);
		
		this->x=x;
		this->y=y;
		this->w=w;
		this->h=h;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
	
		this->size=size;
		this->value=value;
		oldValue=value;
		
		this->font=font;
		
		max=w-size-1;
		assert(value<max);
		valueUpdated=true;
		pressed=false;
		
		start=0.0;
		ratio=1.0;
	}
	
	Ratio::Ratio(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int size, int value, const char *font, const std::string& tooltip, const std::string &tooltipFont)
		: HighlightableWidget(tooltip, tooltipFont)
	{
		assert(font);
		
		this->x=x;
		this->y=y;
		this->w=w;
		this->h=h;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
	
		this->size=size;
		this->value=value;
		oldValue=value;
		
		this->font=font;
		
		max=w-size-1;
		assert(value<max);
		valueUpdated=true;
		pressed=false;
		
		start=0.0;
		ratio=1.0;
	}
	
	Ratio::~Ratio()
	{
		// Let's sing.
	}
	
	void Ratio::onTimer(Uint32 tick)
	{
		if (valueUpdated)
		{
			parent->onAction(this, RATIO_CHANGED, value, 0);
			valueUpdated = false;
		}
	}
		
	void Ratio::onSDLMouseButtonDown(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONDOWN);
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		if (isPtInRect(event->button.x, event->button.y, x+value, y, x+value+size, h))
		{
			pressed=true;
			px=event->button.x;
			py=event->button.y;
			pValue=value;
		}
	}
	
	void Ratio::onSDLMouseButtonUp(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONUP);
		pressed=false;
	}
	
	void Ratio::onSDLMouseMotion(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEMOTION);
		HighlightableWidget::onSDLMouseMotion(event);
		
		if(!pressed) return;
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		int dx=event->motion.x-px;
		int dy=event->motion.y-py;
		if (abs(dy)>h)
			dx=0;
		value=pValue+dx;
		if (value<0)
			value=0;
		else if (value>max)
			value=max;
		if (oldValue!=value)
		{
			oldValue=value;
			valueUpdated=true;
		}
		
	}
	
	void Ratio::internalInit(void)
	{
		fontPtr = Toolkit::getFont(font.c_str());
		assert(fontPtr);
		textHeight = fontPtr->getStringHeight((const char *)NULL);
		assert(textHeight > 0);
	}
	
	void Ratio::paint(void)
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		assert(parent);
		assert(parent->getSurface());
		
		HighlightableWidget::paint();
		
		if (pressed)
			parent->getSurface()->drawFilledRect(x+value+1, y+1, size-2, h-2, ColorTheme::highlightColor);
		else
			parent->getSurface()->drawRect(x+value+1, y+1, size-2, h-2, ColorTheme::frameColor);
		//Style::style->drawFrame(parent->getSurface(), x+value+1, y+1, size-2, h-2, Color::ALPHA_TRANSPARENT);
	
		// We center the string
		std::stringstream g;
		g << get();
		int tw=fontPtr->getStringWidth(g.str().c_str());
		parent->getSurface()->drawString(x+value+1+(size-2-tw)/2, y+1+(h-2-textHeight)/2, fontPtr, g.str().c_str());
	}
	
	int Ratio::getMax(void)
	{
		return (int)(max+ratio*(float)value);
	}
	
	int Ratio::get(void)
	{
		return (int)(start+ratio*(float)value);
	}
	
	void Ratio::setScale(float start, float ratio)
	{
		this->start=start;
		this->ratio=ratio;
	}
}
