/*
	Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
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
#include <assert.h>

Selector::Selector(int x, int y, Uint32 hAlign, Uint32 vAlign, unsigned count, unsigned size, unsigned defaultValue, const char *sprite, Sint32 id)
{
	this->x=x;
	this->y=y;
	this->size=size;
	assert(count>=1);
	this->w=(count-1)*size+6;
	this->h=size+4;
	this->hAlignFlag=hAlign;
	this->vAlignFlag=vAlign;

	if (sprite)
		this->sprite=sprite;
	this->id=id;
	archPtr=NULL;
}

void Selector::onSDLEvent(SDL_Event *event)
{

}

void Selector::internalInit(int x, int y, int w, int h)
{

}

void Selector::internalRepaint(int x, int y, int w, int h)
{

}
