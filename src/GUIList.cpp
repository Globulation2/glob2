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

List::List(int x, int y, int w, int h, const Font *font)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->font=font;
	textHeight=font->getStringHeight(NULL);
}

List::~List()
{
	for (std::vector<char *>::iterator it=strings.begin(); it!=strings.end(); ++it)
	{
		delete (*it);
	}
}

void List::clear(void)
{
	for (std::vector<char *>::iterator it=strings.begin(); it!=strings.end(); ++it)
	{
		delete (*it);
	}
	strings.clear();
}

void List::onSDLEvent(SDL_Event *event)
{
	if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if (isPtInRect(event->motion.x, event->motion.y, x, y, w, h))
		{
			int id=event->button.y-y-2;
			id/=textHeight;
			if ((id>=0) &&(id<(int)strings.size()))
				parent->onAction(this, LIST_ELEMENT_SELECTED, id, 0);
		}
	}
}

void List::internalPaint(void)
{
	int nextSize=textHeight;
	int yPos=y+2;
	int i=0;
	gfx->drawRect(x, y, w, h, 180, 180, 180);
	while ((nextSize<h-4) && ((unsigned)i<strings.size()))
	{
		gfx->drawString(x+2, yPos, font, strings[i]);
		nextSize+=textHeight;
		i++;
		yPos+=textHeight;
	}
}

void List::paint(DrawableSurface *gfx)
{
	this->gfx=gfx;
	internalPaint();
}

void List::repaint(void)
{
	parent->paint(x, y, w, h);
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
	}
}

/*const*/ char *List::getText(int pos)
{
	if ((pos>=0) && (pos<(int)strings.size()))
	{
		return strings[pos];
	}
	else
		return NULL;
}
