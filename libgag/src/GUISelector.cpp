/*
	Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
	for any question or comment contact us at
	nct@ysagoon.com or nuage@ysagoon.com

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

Selector::Selector(int x, int y, Uint32 hAlign, Uint32 vAlign, unsigned count, unsigned defaultValue, unsigned size, const char *sprite, Sint32 id)
{
	this->x=x;
	this->y=y;
	this->count=count;
	this->size=size;
	assert(count>=1);
	this->w=(count-1)*size+6;
	this->h=size+4;
	this->hAlignFlag=hAlign;
	this->vAlignFlag=vAlign;
	this->value=defaultValue;

	if (sprite)
		this->sprite=sprite;
	this->id=id;
	archPtr=NULL;
}

void Selector::onSDLEvent(SDL_Event *event)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h) &&
			(event->button.button == SDL_BUTTON_LEFT))
		{
			int dx=event->button.x-x;
			value=(dx+(size>>1))/size;
			repaint();
			parent->onAction(this, VALUE_CHANGED, value, 0);
		}
	}
	else if (event->type==SDL_MOUSEMOTION)
	{
		if (isPtInRect(event->motion.x, event->motion.y, x, y, w, h) &&
			(event->motion.state&SDL_BUTTON(1)))
		{
			int dx=event->motion.x-x;
			value=(dx+(size>>1))/size;
			repaint();
			parent->onAction(this, VALUE_CHANGED, value, 0);
		}
	}
}

void Selector::internalInit(int x, int y, int w, int h)
{
	if (id>=0)
	{
		archPtr=Toolkit::getSprite(sprite.c_str());
		assert(archPtr);
	}
}

void Selector::internalRepaint(int x, int y, int w, int h)
{
	unsigned l=(count-1)*size-2;
	parent->getSurface()->drawHorzLine(x+4, y+(size>>1)+1, l, 180, 180, 180);
	parent->getSurface()->drawHorzLine(x+4, y+(size>>1)+2, l, 180, 180, 180);
	for (unsigned i=0; i<count; i++)
	{
		parent->getSurface()->drawVertLine(x+2+i*size, y+2, size, 180, 180, 180);
		parent->getSurface()->drawVertLine(x+3+i*size, y+2, size, 180, 180, 180);
	}

	if (id<0)
	{
		parent->getSurface()->drawRect(x+size*value, y, 6, size+4, 255, 255, 255);
	}
	else
	{
		parent->getSurface()->drawSprite(x+size*value, y, archPtr, id);
	}
}
