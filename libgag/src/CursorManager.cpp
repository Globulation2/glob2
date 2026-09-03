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

#include <CursorManager.h>
#include <GraphicContext.h>
#include <Toolkit.h>

namespace GAGCore
{
	CursorManager::CursorManager()
	{
		nextType = currentType = CURSOR_NORMAL;
		currentFrame = 0;
		packedDrawColor = 0;
		activeCursor = NULL;
		activeType = CURSOR_NORMAL;
		activeFrame = 0;
		activePackedColor = 0;
		activeValid = false;
	}

	CursorManager::~CursorManager()
	{
		releaseNativeCursor();
	}

	void CursorManager::releaseNativeCursor(void)
	{
		if (activeCursor)
		{
			SDL_FreeCursor(activeCursor);
			activeCursor = NULL;
			activeValid = false;
		}
	}

	void CursorManager::load(void)
	{
		cursors.clear();
		cursors.push_back(Toolkit::getSprite("data/gfx/cursor/normal"));
		cursors.push_back(Toolkit::getSprite("data/gfx/cursor/click"));
		cursors.push_back(Toolkit::getSprite("data/gfx/cursor/direction_lu"));
		cursors.push_back(Toolkit::getSprite("data/gfx/cursor/direction_u"));
		cursors.push_back(Toolkit::getSprite("data/gfx/cursor/direction_ru"));
		cursors.push_back(Toolkit::getSprite("data/gfx/cursor/direction_r"));
		cursors.push_back(Toolkit::getSprite("data/gfx/cursor/direction_rd"));
		cursors.push_back(Toolkit::getSprite("data/gfx/cursor/direction_d"));
		cursors.push_back(Toolkit::getSprite("data/gfx/cursor/direction_ld"));
		cursors.push_back(Toolkit::getSprite("data/gfx/cursor/direction_l"));
		cursors.push_back(Toolkit::getSprite("data/gfx/cursor/wait"));
		cursors.push_back(Toolkit::getSprite("data/gfx/cursor/mark"));
		setDefaultColor();
		activeValid = false;
	}
	
	void CursorManager::nextTypeFromMouse(DrawableSurface *ds, int x, int y, bool button)
	{
		// if we are in a user mode, we don't change it
		if (nextType > CURSOR_LEFT)
			return;
		
		// button override directions
		const int limit = 20;
		if (button)
		{
			nextType = CURSOR_CLICK;
			return;
		}
		
		// cursor change if near end of screen
		int w = ds->getW();
		int h = ds->getH();
		if (x<limit)
		{
			if (y<limit)
				nextType = CURSOR_LEFT_UP;
			else if (y>h-limit)
				nextType = CURSOR_LEFT_DOWN;
			else
				nextType = CURSOR_LEFT;
		}
		else if (x>w-limit)
		{
			if (y<limit)
				nextType = CURSOR_RIGHT_UP;
			else if (y>h-limit)
				nextType = CURSOR_RIGHT_DOWN;
			else
				nextType = CURSOR_RIGHT;
		}
		else
		{
			if (y<limit)
				nextType = CURSOR_UP;
			else if (y>h-limit)
				nextType = CURSOR_DOWN;
			else
				nextType = CURSOR_NORMAL;
		}
	}
	
	void CursorManager::setNextType(CursorType type)
	{
		nextType = type;
	}
	
	
	void CursorManager::setDrawColor(const Color& color)
	{
		for(unsigned i = 0; i<cursors.size(); ++i)
		{
			cursors[i]->setBaseColor(color);
		}
		packedDrawColor = color.pack();
	}



	void CursorManager::setDefaultColor()
	{
		setDrawColor(Color(255, 0, 0));
	}



	void CursorManager::update(void)
	{
		if (currentFrame >= cursors[static_cast<int>(currentType)]->getFrameCount())
		{
			currentType = nextType;
			currentFrame = 0;
		}

		// nothing to do if the native cursor already matches this state
		if (activeValid && activeType == currentType && activeFrame == currentFrame
			&& activePackedColor == packedDrawColor)
		{
			currentFrame++;
			return;
		}

		Sprite *sprite = cursors[static_cast<int>(currentType)];
		int w = sprite->getW(currentFrame);
		int h = sprite->getH(currentFrame);
		DrawableSurface frame(w, h);
		frame.drawSprite(0, 0, sprite, currentFrame);
		SDL_Cursor *newCursor = SDL_CreateColorCursor(frame.getSDLSurface(), w>>1, h>>1);
		if (newCursor)
		{
			SDL_SetCursor(newCursor);
			if (activeCursor)
				SDL_FreeCursor(activeCursor);
			activeCursor = newCursor;
			activeType = currentType;
			activeFrame = currentFrame;
			activePackedColor = packedDrawColor;
			activeValid = true;
		}
		currentFrame++;
	}
}
