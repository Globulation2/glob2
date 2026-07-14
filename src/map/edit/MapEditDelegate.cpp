// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include "GameGUILoadSave.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include <sstream>
#include "Utilities.h"
#include "FertilityCalculatorDialog.h"
#include "SDLCompat.h"

void MapEdit::delegateMenu(SDL_Event& event)
{
	if(showingMenuScreen)
	{
			menuScreen->translateAndProcessEvent(&event);
		switch (menuScreen->endValue)
		{
			case MapEditMenuScreen::LOAD_MAP:
			{
				performAction("close menu screen");
				performAction("open load screen");
			}
			break;
			case MapEditMenuScreen::SAVE_MAP:
			{
				performAction("close menu screen");
				performAction("open save screen");
			}
			break;
			case MapEditMenuScreen::OPEN_SCRIPT_EDITOR:
			{
				performAction("close menu screen");
				performAction("open scenario editor");
			}
			break;
			case MapEditMenuScreen::OPEN_TEAMS_EDITOR:
			{
				performAction("close menu screen");
				performAction("open teams editor");
			}
			break;
			case MapEditMenuScreen::RETURN_EDITOR:
			{
				performAction("close menu screen");
			}
			break;
			case MapEditMenuScreen::QUIT_EDITOR:
			{
				performAction("close menu screen");
				performAction("quit editor");
			}
			break;
		}
	}
	if(showingLoad)
	{
		loadSaveScreen->translateAndProcessEvent(&event);
		switch (loadSaveScreen->endValue)
		{
			case LoadSaveScreen::OK:
			{
				load(loadSaveScreen->getFileName());
				performAction("close load screen");
			}
			break;
			case LoadSaveScreen::CANCEL:
			{
				performAction("close load screen");
			}
			break;
		}
	}
	if(showingSave)
	{
		loadSaveScreen->translateAndProcessEvent(&event);
		switch (loadSaveScreen->endValue)
		{
			case LoadSaveScreen::OK:
			{
				save(loadSaveScreen->getFileName(), loadSaveScreen->getName());
				performAction("close save screen");
			}
			case LoadSaveScreen::CANCEL:
			{
				performAction("close save screen");
			}
		}
	}
	if(showingScriptEditor)
	{
		scriptEditor->translateAndProcessEvent(&event);
		switch(scriptEditor->endValue)
		{
			case ScriptEditorScreen::OK:
			case ScriptEditorScreen::CANCEL:
			{
				performAction("close scenario editor");
			}
		}
	}
	if(showingTeamsEditor)
	{
		teamsEditor->translateAndProcessEvent(&event);
		switch(teamsEditor->endValue)
		{
			case ScriptEditorScreen::OK:
			case ScriptEditorScreen::CANCEL:
			{
				performAction("close teams editor");
			}
		}
	}
	if(isShowingAreaName)
	{
		areaName->translateAndProcessEvent(&event);
		switch(areaName->endValue)
		{
			case AskForTextInput::OK:
			case AskForTextInput::CANCEL:
			{
				performAction("close area name");
			}
		}
	}
}

void MapEdit::handleMapScroll()
{
	xSpeed = 0;
	ySpeed = 0;
	int scrollAreaWidth=10; // if the cursor is that close to the border the viewport will scroll

	SDL_PumpEvents();
	const Uint8 *keystate = SDL_GetKeyboardState(NULL);
	SDL_Keymod modState = SDL_GetModState();
	int xMotion = 1;
	int yMotion = 1;
	/* We check that only Control is held to avoid accidentally
		matching window manager bindings for switching windows
		and/or desktops. */
	if (!(modState & (KMOD_ALT|KMOD_SHIFT)))
	{
		/* It violates good abstraction principles that I
			have to do the calculations in the next two
			lines.  There should be methods that abstract
			these computations. */
		if ((modState & KMOD_CTRL))
		{
			/* We move by half screens if Control is held while
				the arrow keys are held.  So we shift by 6
				instead of 5.  (If we shifted by 5, it would be
				good to subtract 1 so that there would be a small
				overlap between what is viewable both before and
				after the motion.) */
			xMotion = ((globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)>>6);
			yMotion = ((globalContainer->gfx->getH())>>6);
		}
		else
		{
			/* We move the screen by one square at a time if CTRL key
				is not being help */
			xMotion = 1;
			yMotion = 1;
		}
	}
	else if (modState)
	{
		/* Probably some keys held down as part of window
			manager operations. */
		xMotion = 0;
		yMotion = 0; 
	}
	if (
			keystate[SDL_SCANCODE_UP] ||
			keystate[SDL_SCANCODE_KP_7] ||
			keystate[SDL_SCANCODE_KP_8] ||
			keystate[SDL_SCANCODE_KP_9] ||
			mouseY<scrollAreaWidth)
	{
		ySpeed += -yMotion;
	}
	if (
			keystate[SDL_SCANCODE_DOWN] ||
			keystate[SDL_SCANCODE_KP_1] || 
			keystate[SDL_SCANCODE_KP_2] || 
			keystate[SDL_SCANCODE_KP_3] ||
			globalContainer->gfx->getH()-mouseY<scrollAreaWidth)
	{
		ySpeed += yMotion;
	}
	if (
			keystate[SDL_SCANCODE_LEFT] || 
			keystate[SDL_SCANCODE_KP_1] || 
			keystate[SDL_SCANCODE_KP_4] || 
			keystate[SDL_SCANCODE_KP_7] ||
			mouseX<scrollAreaWidth)
	{
		xSpeed += -xMotion;
	}
	if (
			keystate[SDL_SCANCODE_RIGHT] || 
			keystate[SDL_SCANCODE_KP_3] || 
			keystate[SDL_SCANCODE_KP_6] || 
			keystate[SDL_SCANCODE_KP_9] ||
			globalContainer->gfx->getW()-mouseX<scrollAreaWidth)
	{
		xSpeed += xMotion;
	}
	updateCoordinatesLabel();
}

void MapEdit::updateCoordinatesLabel()
{
	std::ostringstream s;
	int x;
	int y;
	if (panelMode==Terrain) //terrain has a slightly different coordinates system
		game.map.displayToMapCaseAligned(mouseX+(terrainType>TerrainSelector::Water ? 0 : 16), mouseY+(terrainType>TerrainSelector::Water ? 0 : 16), &x, &y,  viewportX, viewportY);
	else
		game.map.displayToMapCaseAligned(mouseX, mouseY, &x, &y, viewportX, viewportY);
	s << "X: " << x << " Y: " << y;
	mapCoordinatesLabel->setLabel(s.str());
}

