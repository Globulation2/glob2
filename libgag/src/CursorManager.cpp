// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <CursorManager.h>
#include <GraphicContext.h>
#include <Toolkit.h>
#include <algorithm>
#include <memory>

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
		activeScale = 0.0f;
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



	void CursorManager::update(float scale)
	{
		if (currentFrame >= cursors[static_cast<int>(currentType)]->getFrameCount())
		{
			currentType = nextType;
			currentFrame = 0;
		}

		// nothing to do if the native cursor already matches this state
		if (activeValid && activeType == currentType && activeFrame == currentFrame
			&& activePackedColor == packedDrawColor && activeScale == scale)
		{
			currentFrame++;
			return;
		}

		Sprite *sprite = cursors[static_cast<int>(currentType)];
		int w = sprite->getW(currentFrame);
		int h = sprite->getH(currentFrame);
		DrawableSurface frame(w, h);
		frame.drawSprite(0, 0, sprite, currentFrame);

		// A native cursor renders at its own pixel size regardless of window
		// scaling, unlike the old blit-into-the-logical-framebuffer approach
		// which inherited GraphicContext::nextFrame()'s scaled blit for free.
		// Scale the composited image ourselves so the cursor still matches
		// the rest of a scaled-up window (nearest-neighbour, matching how the
		// rest of the engine scales -- see DrawableSurface.cpp's GL upload
		// and GraphicContext::nextFrame()'s software SDL_BlitScaled).
		SDL_Surface *cursorSurface = frame.getSDLSurface();
		std::unique_ptr<DrawableSurface> scaled;
		int sw = w, sh = h;
		if (scale > 0.0f && scale != 1.0f)
		{
			sw = std::max(1, static_cast<int>(w * scale + 0.5f));
			sh = std::max(1, static_cast<int>(h * scale + 0.5f));
			scaled = std::make_unique<DrawableSurface>(sw, sh);
			SDL_SetSurfaceBlendMode(cursorSurface, SDL_BLENDMODE_NONE);
			SDL_Rect dst = {0, 0, sw, sh};
			SDL_BlitScaled(cursorSurface, NULL, scaled->getSDLSurface(), &dst);
			cursorSurface = scaled->getSDLSurface();
		}

		SDL_Cursor *newCursor = SDL_CreateColorCursor(cursorSurface, sw>>1, sh>>1);
		if (newCursor)
		{
			SDL_SetCursor(newCursor);
			if (activeCursor)
				SDL_FreeCursor(activeCursor);
			activeCursor = newCursor;
			activeType = currentType;
			activeFrame = currentFrame;
			activePackedColor = packedDrawColor;
			activeScale = scale;
			activeValid = true;
		}
		currentFrame++;
	}
}
