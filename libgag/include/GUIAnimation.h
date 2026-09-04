// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __GUIANIMATION_H
#define __GUIANIMATION_H

#include "GUIBase.h"
#include <string>

namespace GAGCore
{
	class Sprite;
}

namespace GAGGUI
{
	
	class Animation: public RectangularWidget
	{
	protected:
		Uint32 duration;
		Uint32 count;
		Sint32 start;
		std::string sprite;
	
		// cache, recomputed on internalInit
		GAGCore::Sprite *archPtr;
		unsigned pos, durationLeft;
	
	public:
		Animation() { duration=count=start=0; pos=durationLeft=0; archPtr=NULL; }
		Animation(int x, int y, Uint32 hAlign, Uint32 vAlign, const std::string sprite, Sint32 start, Sint32 count=1, Sint32 duration=1);
		virtual ~Animation() { }
		virtual void onTimer(Uint32 tick);
		virtual void internalInit(void);
		virtual void paint(void);
	};
}

#endif
