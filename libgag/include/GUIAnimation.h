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

#ifndef __GUIANIMATION_H
#define __GUIANIMATION_H

#include "GUIBase.h"

class Animation: public RectangularWidget
{
protected:
	CLASSDEF(Animation)
		BASECLASS(RectangularWidget)
	MEMBERS
		ITEM(Uint32, duration)
		ITEM(Uint32, count)
		ITEM(Sint32, start)
		ITEM(std::string, sprite)
	CLASSEND;

	// cache, recomputed on internalInit
	Sprite *archPtr;
	unsigned pos, durationLeft;

public:
	Animation() { duration=count=start=0; pos=durationLeft=0; archPtr=NULL; }
	Animation(int x, int y, Uint32 hAlign, Uint32 vAlign, const char *sprite, Sint32 start, Sint32 count=1, Sint32 duration=1);
	virtual ~Animation() { }
	virtual void onTimer(Uint32 tick);

protected:
	virtual void internalInit(int x, int y, int w, int h);
	virtual void internalRepaint(int x, int y, int w, int h);
};

#endif
