/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

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

#include "GUIList.h"
#include <functional>
#include <algorithm>

List::List(int x, int y, int w, int h, const Font *font)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->font=font;
	textHeight=font->getStringHeight(NULL);
	nth=-1;
}

List::~List()
{
	for (std::vector<char *>::iterator it=strings.begin(); it!=strings.end(); ++it)
	{
		delete[] (*it);
	}
}

void List::clear(void)
{
	for (std::vector<char *>::iterator it=strings.begin(); it!=strings.end(); ++it)
	{
		delete[] (*it);
	}
	strings.clear();
	nth=-1;
}

void List::onSDLEvent(SDL_Event *event)
{
	if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
		{
			int id=event->button.y-y-2;
			id/=textHeight;
			if ((id>=0) &&(id<(int)strings.size()))
			{
				nth=id;
				repaint();
				parent->onAction(this, LIST_ELEMENT_SELECTED, id, 0);
			}
		}
	}
}

void List::internalPaint(void)
{
	int nextSize=textHeight;
	int yPos=y+2;
	int i=0;
	assert(parent);
	assert(parent->getSurface());
	parent->getSurface()->drawRect(x, y, w, h, 180, 180, 180);
	while ((nextSize<h-4) && ((unsigned)i<strings.size()))
	{
		parent->getSurface()->drawString(x+2, yPos, font, strings[i]);
		if (i==nth)
			parent->getSurface()->drawRect(x+1, yPos-1, w-2, textHeight, 170, 170, 240);
		nextSize+=textHeight;
		i++;
		yPos+=textHeight;
	}
}

void List::paint(void)
{
	if (visible)
		internalPaint();
}

void List::repaint(void)
{
	assert(parent);
	parent->paint(x, y, w, h);
	if (visible)
		internalPaint();
	parent->addUpdateRect(x, y, w, h);
}

void List::addText(const char *text, int pos)
{
	if ((pos>=0) && (pos<(int)strings.size()))
	{
		int textLength=strlen(text);
		char *newText=new char[textLength+1];
		strncpy(newText, text, textLength+1);
		strings.insert(strings.begin()+pos, newText);
	}
}

struct strcasecmp_functor : public std::binary_function<char *, char *, bool>
{
	bool operator()(char * x, char * y) { return (strcasecmp(x, y)<0); }
};

void List::sort(void)
{
	std::sort(strings.begin(), strings.end(), strcasecmp_functor());
}

void List::addText(const char *text)
{
	int textLength=strlen(text);
	char *newText=new char[textLength+1];
	strncpy(newText, text, textLength+1);
	strings.push_back(newText);
}

void List::removeText(int pos)
{
	if ((pos>=0) && (pos<(int)strings.size()))
	{
		char *text=strings[pos];
		delete[] text;
		strings.erase(strings.begin()+pos);
		if (pos<nth)
			nth--;
	}
}

char *List::getText(int pos) const
{
	if ((pos>=0) && (pos<(int)strings.size()))
	{
		return strings[pos];
	}
	else
		return NULL;
}

char *List::get(void) const
{
	return getText(nth);
}

int List::getNth(void) const
{
	return nth;
}

void List::setNth(int nth)
{
	assert((nth>=0)&&(nth<(int)strings.size()));
	if ((nth>=0)&&(nth<(int)strings.size()))
		this->nth=nth;
}
