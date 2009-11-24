/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <GUINumber.h>
#include <GUIStyle.h>
#include <assert.h>
#include <Toolkit.h>
#include <GraphicContext.h>
#include <sstream>

using namespace GAGCore;

namespace GAGGUI
{
	Number::Number()
	{
		x = y = w = h = m = nth = 0;
	}
	
	Number::Number(const std::string &tooltip, const std::string &tooltipFont)
		:HighlightableWidget(tooltip, tooltipFont)
	{
		x = y = w = h = m = nth = 0;
	}
	
	Number::Number(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int m, const std::string font)
	{
		assert(font.size()>=0);
		this->font = font;
		this->x=x;
		this->y=y;
		this->w=w;
		this->h=h;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
	
		if (m<1)
			m=h;
		this->m=m;
		nth=0;
	}
	
	Number::Number(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int m, const std::string font, const std::string &tooltip, const std::string &tooltipFont)
		: HighlightableWidget(tooltip, tooltipFont)
	{
		assert(font.size()>=0);
		this->font = font;
		this->x=x;
		this->y=y;
		this->w=w;
		this->h=h;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
	
		if (m<1)
			m=h;
		this->m=m;
		nth=0;
	}
	
	Number::~Number()
	{
		// Let's sing.
	}
	
	void Number::onSDLMouseButtonDown(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONDOWN);
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		bool inc = false;
		bool dec = false;
		
		
		// We can't use isOnWidget since x, y, w, h are needed
		// out of the test
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
		{
			if(event->button.button == SDL_BUTTON_LEFT)
			{
				if (event->button.x-x<m)
				{
					dec = true;
				}
				else if (x+w-event->button.x<m)
				{
					inc = true;
				}
			
			}
			else if(event->button.button == SDL_BUTTON_WHEELUP)
			{
				inc = true;
			}
			else if(event->button.button == SDL_BUTTON_WHEELDOWN)
			{
				dec = true;
			}
		}
		
		if (dec)
		{
			// a "Less" click
			if (nth>0)
			{
				nth--;
				if (numbers.size()>0)
				{
					parent->onAction(this, NUMBER_ELEMENT_SELECTED, nth, 0);
				}
			}
		}
		else if (inc)
		{
			// a "More" click
			if (nth<((int)numbers.size()-1))
			{
				nth++;
				parent->onAction(this, NUMBER_ELEMENT_SELECTED, nth, 0);
			}
		}
	}
	
	void Number::internalInit(void)
	{
		this->fontPtr=Toolkit::getFont(font.c_str());
		assert(fontPtr);
		textHeight=this->fontPtr->getStringHeight("");
		assert(textHeight > 0);
	}
	
	void Number::paint(void)
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		assert(parent);
		assert(parent->getSurface());
		
		parent->getSurface()->drawLine(x+m, y, x+m, y+h, Style::style->frameColor);
		parent->getSurface()->drawLine(x+w-m, y, x+w-m, y+h, Style::style->frameColor);
		
		assert(nth>=0);
		assert(nth<(int)numbers.size());
		int dy=(h-textHeight)/2;
		if (nth<(int)numbers.size())
		{
			// We center the string
			std::stringstream s;
			s << numbers[nth];
			int tw=fontPtr->getStringWidth(s.str().c_str());
			parent->getSurface()->drawString(x+m+(w-2*m-tw)/2, y+dy, fontPtr, s.str().c_str());

		}
		int dx1=(m-fontPtr->getStringWidth("-"))/2;
		parent->getSurface()->drawString(x+dx1, y+dy, fontPtr, "-");
		int dx2=(m-fontPtr->getStringWidth("+"))/2;
		parent->getSurface()->drawString(x+dx2+w-m, y+dy, fontPtr, "+");
		
		HighlightableWidget::paint();
	}
	
	void Number::add(int number)
	{
		numbers.push_back(number);
	}
	
	void Number::clear(void)
	{
		numbers.clear();
	}
	
	void Number::setNth(int nth)
	{
		assert((nth>=0)&&(nth<(int)numbers.size()));
		if ((nth>=0)&&(nth<(int)numbers.size()))
			this->nth=nth;
	}
	
	void Number::set(int number)
	{
		for (int i=0; i<(int)numbers.size(); i++)
			if (numbers[i]==number)
			{
				nth=i;
				break;
			}
	}
	
	int Number::getNth(void)
	{
		return nth;
	}
	
	int Number::get(void)
	{
		assert(nth<(int)numbers.size());
		if (nth<(int)numbers.size())
			return numbers[nth];
		else
			return 0;
	}
}
