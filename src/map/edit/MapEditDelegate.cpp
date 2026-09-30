// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include "gui/LoadSaveDialog.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "PhoneEditor.h"
#include "ScriptEditorScreen.h"
#include <sstream>
#include "Utilities.h"
#include "SDLCompat.h"

bool MapEdit::hasDialog() const
{
	return showingMenuScreen || showingLoad || showingSave || showingScriptEditor || showingTeamsEditor || isShowingAreaName;
}

Glob2UI::InGameDialog *MapEdit::activeDialog() const
{
	if (showingMenuScreen)
		return menuScreen.get();
	if (showingLoad || showingSave)
		return loadSaveScreen.get();
	if (showingScriptEditor)
		return scriptEditor->fileDialog() ? static_cast<Glob2UI::InGameDialog *>(scriptEditor->fileDialog()) : scriptEditor.get();
	if (showingTeamsEditor)
		return teamsEditor.get();
	if (isShowingAreaName)
		return areaName.get();
	return nullptr;
}

void MapEdit::attachDialog(Glob2UI::InGameDialog &dialog)
{
	if (!globalContainer->runNoX)
		dialog.attach(*globalContainer->gfx);
}

void MapEdit::drawDialog()
{
	if (auto *dialog = activeDialog())
	{
		globalContainer->gfx->setClipRect();
		dialog->draw(SDL_GetTicks());
	}
}

// Route an event to the open dialog and act once it completes. A user event
// carries no input and only polls for completion.
void MapEdit::delegateMenu(SDL_Event& event)
{
	auto *dialog = activeDialog();
	if (dialog && event.type != SDL_USEREVENT)
		dialog->event(event);
	if(showingMenuScreen && menuScreen->finished())
	{
		switch (menuScreen->result())
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
	if(showingLoad && loadSaveScreen->finished())
	{
		switch (loadSaveScreen->result())
		{
			case LoadSaveDialog::OK:
			{
				requestLoad(loadSaveScreen->getFileName());
				performAction("close load screen");
			}
			break;
			case LoadSaveDialog::CANCEL:
			{
				performAction("close load screen");
			}
			break;
		}
	}
	if(showingSave && loadSaveScreen->finished())
	{
		switch (loadSaveScreen->result())
		{
			case LoadSaveDialog::OK:
			{
                if(!*loadSaveScreen->getName()) {loadSaveScreen->showSaveFailure();break;}
                pendingSaveFilename = loadSaveScreen->getFileName();
                pendingSaveName = loadSaveScreen->getName();
                fertilityRequested = true;
                loadSaveScreen->resume();
            }
            break;
			case LoadSaveDialog::CANCEL:
			{
                doQuitAfterLoadSave = false;
				performAction("close save screen");
			}
		}
	}
	if(showingScriptEditor)
	{
		if (scriptEditor->fileDialog() && scriptEditor->fileDialog()->finished())
			scriptEditor->finishFileDialog();
		else if (scriptEditor->finished())
			performAction("close scenario editor");
	}
	if(showingTeamsEditor && teamsEditor->finished())
	{
		performAction("close teams editor");
	}
	if(isShowingAreaName && areaName->finished())
	{
		performAction("close area name");
	}
}

void MapEdit::handleMapScroll()
{
	xSpeed = 0;
	ySpeed = 0;
	int scrollAreaWidth=10; // if the cursor is that close to the border the viewport will scroll

	if (!inputState.hasFocus()) return;
	const Uint8 *keystate = inputState.keyboard();
	SDL_Keymod modState = inputState.modifiers();
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
		game.map.displayToMapCaseAligned(mapMouseX(mouseX), mapMouseY(mouseY), &x, &y, viewportX, viewportY);
	s << "X: " << x << " Y: " << y;
	mapCoordinatesLabel->setLabel(s.str());
}

