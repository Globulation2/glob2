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

#include <GUINumber.h>
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
	
	Number::Number(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int m, const char *font)
	{
		assert(font);
		this->fontPtr=Toolkit::getFont(font);
		assert(fontPtr);
		textHeight=this->fontPtr->getStringHeight((const char *)NULL);
		
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
	
	void Number::onSDLEvent(SDL_Event *event)
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		if (event->type==SDL_MOUSEBUTTONDOWN)
		{
			if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
			{
				if (event->button.x-x<m)
				{
					// a "Less" click
					if (nth>0)
					{
						nth--;
						if (numbers.size()>0)
						{
							repaint();
							parent->onAction(this, NUMBER_ELEMENT_SELECTED, nth, 0);
						}
					}
				}
				else if (x+w-event->button.x<m)
				{
					// a "More" click
					if (nth<((int)numbers.size()-1))
					{
						nth++;
						repaint();
						parent->onAction(this, NUMBER_ELEMENT_SELECTED, nth, 0);
					}
				}
			}
		}
	}
	
	void Number::internalRepaint(int x, int y, int w, int h)
	{
		assert(parent);
		assert(parent->getSurface());
		parent->getSurface()->drawRect(x, y, w, h, 180, 180, 180);
		parent->getSurface()->drawVertLine(x+m, y, h, 255, 255, 255);
		parent->getSurface()->drawVertLine(x+w-m, y, h, 255, 255, 255);
		
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
		assert((nth>0)&&(nth<(int)numbers.size()));
		if ((nth>0)&&(nth<(int)numbers.size()))
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
