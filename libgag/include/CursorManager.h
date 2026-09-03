// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <vector>
#include "SDL.h"

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
		//! one native cursor per (type, frame), built on first use and owned by us
		std::vector<std::vector<SDL_Cursor *> > nativeCursors;
		//! the scale the cached cursors were built at
		float cacheScale;
		//! (type, frame) of the cursor currently installed via SDL_SetCursor
		CursorType activeType;
		int activeFrame;
		bool activeValid;
		//! build the native cursor for a (type, frame) at the given scale
		SDL_Cursor *createNativeCursor(CursorType type, int frame, float scale);

	public:
		//! Constructor, set default values
		CursorManager();
		//! Frees the native cursor, if any
		~CursorManager();
		//! Releases the native cursors, which reverts to the system one. Must be called before SDL_Quit();
		//! ~CursorManager() runs too late for that (after ~GraphicContext()'s body).
		void releaseNativeCursor(void);
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
		//! Advance the cursor animation and, if the type or frame changed,
		//! install the native cursor for it via SDL_SetCursor. The OS then
		//! moves and composites it independent of our own render/tick rate.
		//! scale is GraphicContext::drawableScale(), so the cursor matches
		//! the rest of a scaled-up window instead of rendering at 1x.
		void update(float scale);
	};
}
