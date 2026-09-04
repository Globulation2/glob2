// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __CURSOR_MANAGER_H
#define __CURSOR_MANAGER_H

#include <vector>

namespace GAGCore
{
	class Sprite;
	class DrawableSurface;
	struct Color;
	
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
		//! Sets the draw color of the custom cursor
		void setDrawColor(const Color& color);
		//! Sets the default draw color
		void setDefaultColor();
		//! Draw the current cursor with its current frame at a given pos
		void draw(DrawableSurface *ds, int x, int y);
	};
}

#endif
