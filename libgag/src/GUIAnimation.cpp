// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <GUIAnimation.h>
#include <Toolkit.h>
#include <GraphicContext.h>
#include <assert.h>

using namespace GAGCore;

namespace GAGGUI
{
	Animation::Animation(int x, int y, Uint32 hAlign, Uint32 vAlign, const std::string sprite, Sint32 start, Sint32 count, Sint32 duration)
	{
		this->x=x;
		this->y=y;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
		this->start=start;
		this->count=count;
		this->duration=duration;
		pos=start;
		durationLeft=duration;
	
		assert(sprite.length());
		this->sprite=sprite;
		archPtr=Toolkit::getSprite(sprite);
		assert(archPtr);
	
		this->w=archPtr->getW(start);
		this->h=archPtr->getH(start);
	}
	
	void Animation::internalInit(void)
	{
		archPtr=Toolkit::getSprite(sprite.c_str());
		assert(archPtr);
		pos=start;
		durationLeft=duration;
	}
	
	void Animation::paint(void)
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		assert(parent);
		assert(parent->getSurface());
		
		int dW=(w-archPtr->getW(pos))>>1;
		int dH=(h-archPtr->getH(pos))>>1;
		parent->getSurface()->drawSprite(x+dW, y+dH, archPtr, pos);
	}
	
	void Animation::onTimer(Uint32 tick)
	{
		if (count>1)
		{
			if (--durationLeft==0)
			{
				pos++;
				if (pos==start+count)
					pos=start;
				durationLeft=duration;
			}
		}
	}
}
