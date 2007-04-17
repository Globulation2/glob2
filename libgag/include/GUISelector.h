/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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
		Uint32 maxValue;
		Uint32 step;
		Uint32 value;
		bool dragging;
		Sint32 id;
		std::string sprite;
	
		//! cache, recomputed on internalInit
		GAGCore::Sprite *archPtr;
	
	public:
		Selector(int x, int y, Uint32 hAlign, Uint32 vAlign, unsigned width, unsigned defaultValue=0, unsigned maxValue=16, unsigned step=1, const char *sprite=NULL, Sint32 id=-1);
		Selector(int x, int y, Uint32 hAlign, Uint32 vAlign, unsigned width, const std::string& tooltip, const std::string &tooltipFont, unsigned defaultValue=0, unsigned maxValue=16, unsigned step=1, const char *sprite=NULL, Sint32 id=-1);
		virtual ~Selector() { }
	
		virtual void internalInit(void);
		virtual void paint(void);
		virtual Uint32 getValue(void) { return value; }
		virtual void setValue(Uint32 v) { clipValue(v); }
	
	protected:
		void clipValue(int v);
		virtual void onSDLMouseButtonDown(SDL_Event *event);
		virtual void onSDLMouseMotion(SDL_Event *event);
		virtual void onSDLMouseButtonUp(SDL_Event *event);
	};
}

#endif
