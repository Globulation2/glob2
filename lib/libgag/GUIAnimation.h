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
		Animation(int x, int y, Uint32 hAlign, Uint32 vAlign, const char *sprite, Sint32 start, Sint32 count=1, Sint32 duration=1);
		virtual ~Animation() { }
		virtual void onTimer(Uint32 tick);
		virtual void internalInit(void);
		virtual void paint(void);
	};
}

#endif
