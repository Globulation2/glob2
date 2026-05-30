// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <algorithm>
#include <iostream>

#include <SDL_keycode.h>

#include <FileManager.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <TextStream.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "GameGUIKeyActions.h"
#include "GameGUILoadSave.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "SoundMixer.h"
#include "Unit.h"
#include "VoiceRecorder.h"

using std::shared_ptr;
using std::static_pointer_cast;

void GameGUI::minimapMouseToPos(int mx, int my, int *cx, int *cy, bool forScreenViewport)
{
	minimap.convertToMap(mx, my, *cx, *cy);

	///when for the screen viewport, center
	if (forScreenViewport)
	{
		*cx-=((globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)>>6);
		*cy-=((globalContainer->gfx->getH())>>6);
	}

}

void GameGUI::handleMouseMotion(int mx, int my, int button)
{
	const int scrollZoneWidth = 10;
	game.mouseX=mouseX=mx;
	game.mouseY=mouseY=my;

	int oldViewportX = viewportX;
	int oldViewportY = viewportY;

	if (miniMapPushed)
	{
		minimapMouseToPos(mx, my, &viewportX, &viewportY, true);
	}
	else
	{
		if (mx<scrollZoneWidth)
			viewportSpeedX=-1;
		else if ((mx>globalContainer->gfx->getW()-scrollZoneWidth) )
			viewportSpeedX=1;
		else
			viewportSpeedX=0;

		if (my<scrollZoneWidth)
			viewportSpeedY=-1;
		else if (my>globalContainer->gfx->getH()-scrollZoneWidth)
			viewportSpeedY=1;
		else
			viewportSpeedY=0;
	}

	if (panPushed)
	{
		// handle paning
		int dx = (mx-panMouseX)>>1;
		int dy = (my-panMouseY)>>1;
		viewportX = (panViewX+dx)&game.map.getMaskW();
		viewportY = (panViewY+dy)&game.map.getMaskH();
	}

	moveParticles(oldViewportX, viewportX, oldViewportY, viewportY);

	dragStep(mx, my, button);
}

void GameGUI::handleMapClick(int mx, int my, int button)
{
	if (selectionMode==TOOL_SELECTION)
	{
		toolManager.handleMouseDown(mx, my, localTeamNo, viewportX, viewportY);

	}
	else if (selectionMode==BRUSH_SELECTION)
	{
		toolManager.handleMouseDown(mx, my, localTeamNo, viewportX, viewportY);
	}
	else if (putMark)
	{
		int markx, marky;
		game.map.displayToMapCaseAligned(mx, my, &markx, &marky, viewportX, viewportY);
		orderQueue.push_back(shared_ptr<Order>(new MapMarkOrder(localTeamNo, markx, marky)));
		globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_NORMAL);
		putMark = false;
	}
	else
	{
		int mapX, mapY;
		game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY, viewportX, viewportY);
		selectionPushedPosX=mapX;
		selectionPushedPosY=mapY;
		// check for flag first
		for (std::list<Building *>::iterator virtualIt=localTeam->virtualBuildings.begin();
				virtualIt!=localTeam->virtualBuildings.end(); ++virtualIt)
			{
				Building *b=*virtualIt;
				if ((displayedPosX(*b)==mapX) && (displayedPosY(*b)==mapY))
				{
					setSelection(BUILDING_SELECTION, b);
					selectionPushed=true;
					return;
				}
			}
		// then for unit
		if (game.mouseUnit)
		{
			// a unit is selected:
			setSelection(UNIT_SELECTION, game.mouseUnit);
			selectionPushed = true;
			// handle dump of unit characteristics
			if ((SDL_GetModState() & KMOD_SHIFT) != 0)
			{
				OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("unit.dump.txt"));
				if (stream->isEndOfStream())
				{
					std::cerr << "Can't dump unit to file unit.dump.txt" << std::endl;
				}
				else
				{
					std::cerr << "Dump unit " << game.mouseUnit->gid << " memory" << std::endl;
					game.mouseUnit->save(stream);
					game.mouseUnit->saveCrossRef(stream);
					if (game.mouseUnit->attachedBuilding)
					{
						game.mouseUnit->attachedBuilding->save(stream);
						game.mouseUnit->attachedBuilding->saveCrossRef(stream);
					}
				}
				delete stream;
			}
		}
		else
		{
			// then for building
			Uint16 gbid=game.map.getBuilding(mapX, mapY);
			if (gbid != NOGBID)
			{
				int buildingTeam=Building::GIDtoTeam(gbid);
				// we can select for view buildings that are in shared vision, or any building in replay mode
				if ((buildingTeam==localTeamNo)
					|| game.map.isFOWDiscovered(mapX, mapY, localTeam->me)
					|| (game.map.isMapDiscovered(mapX, mapY, localTeam->me) && (game.teams[buildingTeam]->allies&(1<<localTeamNo)))
					|| globalContainer->replaying )
				{
					setSelection(BUILDING_SELECTION, gbid);
					selectionPushed=true;
					// showUnitWorkingToBuilding=true;
					// handle dump of building characteristics
					if ((SDL_GetModState() & KMOD_SHIFT) != 0)
					{
						OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("building.dump.txt"));
						if (stream->isEndOfStream())
						{
							std::cerr << "Can't dump unit to file building.dump.txt" << std::endl;
						}
						else
						{
							Building* selBuild=selectionBuilding();
							std::cerr << "Dump building " << selBuild->gid << " memory" << std::endl;
							selBuild->save(stream);
							selBuild->saveCrossRef(stream);
						}
						delete stream;
					}
				}
			}
			else
			{
				// and ressource
				if (game.map.isRessource(mapX, mapY) && game.map.isMapDiscovered(mapX, mapY, localTeam->me))
				{
					setSelection(RESSOURCE_SELECTION, mapY*game.map.getW()+mapX);
					selectionPushed=true;
				}
				else
				{
					if (selectionMode == RESSOURCE_SELECTION)
						clearSelection();
				}
			}
		}
	}
}

void GameGUI::handleReplayProgressBarClick(int mx, int my, int button)
{
	// Check the play, pause and fast-forward buttons
	if (globalContainer->replaying)
	{
		int x = REPLAY_BAR_WIDTH - REPLAY_PROGRESS_BAR_X_OFFSET - REPLAY_PROGRESS_BAR_CAP_WIDTH;
		int y = REPLAY_BAR_Y + REPLAY_PROGRESS_BAR_Y_OFFSET;
		int inc = REPLAY_PROGRESS_BAR_BUTTON_WIDTH;

		if (my >= y && my <= y+20)
		{
			if (mx >= x-3*inc && mx <= x-2*inc)
			{
				// Play
				gamePaused = false;
				globalContainer->replayFastForward = false;
			}
			if (mx > x-2*inc && mx <= x-inc)
			{
				// Pause
				gamePaused = true;
			}
			if (mx > x-inc && mx <= x)
			{
				// Fast-forward
				gamePaused = false;
				globalContainer->replayFastForward = true;
			}
		}
	}
}
