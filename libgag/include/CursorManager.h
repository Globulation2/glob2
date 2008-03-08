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

#ifndef __CURSOR_MANAGER_H
#define __CURSOR_MANAGER_H

#include <vector>

namespace GAGCore
{
	class Sprite;
	class DrawableSurface;
	
	//! A support class to manage cursors
	class CursorManager
	{
	public:
		//! A cursor type, i.e. a sprite that represent an action
		enum CursorType
		{
			// basic types, computed by nextTypeFromMouse
			CURSOR_NORMAL = 0,
			CURSOR_CLICK = 1,
			CURSOR_LEFT_UP = 2,
			CURSOR_UP = 3,
			CURSOR_RIGHT_UP = 4,
			CURSOR_RIGHT = 5,
			CURSOR_RIGHT_DOWN = 6,
			CURSOR_DOWN = 7,
			CURSOR_LEFT_DOWN = 8,
			CURSOR_LEFT = 9,
			// user types, sta unchanged by nextTypeFromMouse
			CURSOR_WAIT = 10,
			CURSOR_MARK = 11,
			CURSOR_COUNT
		};
	
	protected:
		//! a vector of loaded cursors
		std::vector<Sprite *> cursors;
		//! current cursor type
		CursorType currentType;
		//! next cursor type
		CursorType nextType;
		//! the current frame of cursor sprite.
		int currentFrame;
		
	public:
		//! Constructor, set default values
		CursorManager();
		//! Load the cursor sprites
		void load(void);
		//! Select the next type given the mouse position
		void nextTypeFromMouse(DrawableSurface *ds, int x, int y, bool button);
		//! Manually set the next type
		void setNextType(CursorType type);
		//! Draw the current cursor with its current frame at a given pos
		void draw(DrawableSurface *ds, int x, int y);
	};
}

#endif
