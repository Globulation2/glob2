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

#ifndef __GUISELECTOR_H
#define __GUISELECTOR_H

#include "GUIBase.h"
#include <string>

namespace GAGCore
{
	class Sprite;
}

namespace GAGGUI
{
	class Selector: public RectangularWidget
	{
	protected:
		Uint32 count;
		Uint32 markStep;
		Uint32 size;
		Uint32 value;
		Sint32 id;
		std::string sprite;
	
		//! cache, recomputed on internalInit
		GAGCore::Sprite *archPtr;
	
	public:
		Selector() { count=0; value=0; id=0; archPtr=NULL; }
		Selector(int x, int y, Uint32 hAlign, Uint32 vAlign, unsigned count, unsigned markStep=1, unsigned defaultValue=0, unsigned size=16, const char *sprite=NULL, Sint32 id=-1);
		virtual ~Selector() { }
	
		virtual void onSDLEvent(SDL_Event *event);
		virtual void init(void);
		virtual void paint(void);
		virtual Uint32 getValue(void) { return value; }
		virtual void setValue(Uint32 v) { this->value=v; }
		virtual Uint32 getCount(void) { return count; }
	
	protected:
		void clipValue(int v);
	};
}

#endif
