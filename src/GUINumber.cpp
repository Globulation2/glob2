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

#include "GUINumber.h"
#include "GlobalContainer.h"

Number::Number(int x, int y, int w, int h, int m, const Font *font)
{
	if (!font)
		font=globalContainer->littleFontGreen;
	this->font=font;
	textHeight=font->getStringHeight(NULL);
	
	this->x=x;
	this->y=y;
	this->w=w;
	if (h<1)
		h=textHeight;
	this->h=h;
	if (m<1)
		m=h;
	this->m=m;
	nth=0;
	assert(w>textHeight*2);
}

Number::~Number()
{
	// Let's sing.
}

void Number::onSDLEvent(SDL_Event *event)
{
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

void Number::internalPaint(void)
{
	assert(parent);
	assert(parent->getSurface());
	parent->getSurface()->drawRect(x, y, w, h, 180, 180, 180);
	parent->getSurface()->drawVertLine(x+m, y, h, 255, 255, 255);
	parent->getSurface()->drawVertLine(x+w-m, y, h, 255, 255, 255);
	
	assert(nth<(int)numbers.size());
	int dy=(h-textHeight)/2;
	if (nth<(int)numbers.size())
	{
		// We center the string
		char s[256];
		snprintf(s, 256, "%d", numbers[nth]);
		int tw=font->getStringWidth(s);
		parent->getSurface()->drawString(x+m+(w-2*m-tw)/2, y+dy, font, "%d", numbers[nth]);
	}
	int dx1=(m-font->getStringWidth("-"))/2;
	parent->getSurface()->drawString(x+dx1, y+dy, font, "-");
	int dx2=(m-font->getStringWidth("+"))/2;
	parent->getSurface()->drawString(x+dx2+w-m, y+dy, font, "+");
}

void Number::paint(void)
{
	if (visible)
		internalPaint();
}

void Number::repaint(void)
{
	assert(parent);
	parent->paint(x, y, w, h);
	if (visible)
		internalPaint();
	parent->addUpdateRect(x, y, w, h);
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
