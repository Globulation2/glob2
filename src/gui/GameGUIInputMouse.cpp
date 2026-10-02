// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <iostream>
#include <algorithm>

#include <SDL_keycode.h>

#include <FileManager.h>
#include <Stream.h>
#include <TextStream.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUITouch.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "InGameTouchTheme.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"

using std::shared_ptr;
using std::static_pointer_cast;

void GameGUI::minimapMouseToPos(int mx, int my, int *cx, int *cy, bool forScreenViewport)
{
	minimap.convertToMap(mx, my, *cx, *cy);

	///when for the screen viewport, center
	if (forScreenViewport)
	{
		camera.originX=*cx*32.0-camera.visibleW()/2;
		camera.originY=*cy*32.0-camera.visibleH()/2;
		camera.normalize();
		*cx=camera.tileX();*cy=camera.tileY();
	}

}

void GameGUI::handleMouseMotion(int mx, int my, int button)
{
	const int scrollZoneWidth = 10;
	mouseX=mx;mouseY=my;
	updateCamera();

	int oldViewportX = viewportX;
	int oldViewportY = viewportY;

	if (miniMapPushed)
	{
		stopViewportMotion();
		minimapMouseToPos(mx, my, &viewportX, &viewportY, true);
	}
	else
	{
		if (mapPanPushed)
			viewportSpeedX=viewportSpeedY=0;
		else if (mx<scrollZoneWidth)
			viewportSpeedX=-1;
		else if ((mx>globalContainer->gfx->getW()-scrollZoneWidth) )
			viewportSpeedX=1;
		else
			viewportSpeedX=0;

		if (mapPanPushed)
			viewportSpeedY=0;
		else if (my<scrollZoneWidth)
			viewportSpeedY=-1;
		else if (my>globalContainer->gfx->getH()-scrollZoneWidth)
			viewportSpeedY=1;
		else
			viewportSpeedY=0;
	}

	if (panPushed || mapPanPushed)
	{
		// handle panning
		const int direction = mapPanPushed ? -1 : 1;
		camera.originX += direction*(mx-panMouseX)/camera.zoom;
        camera.originY += direction*(my-panMouseY)/camera.zoom;
        camera.normalize();viewportX=camera.tileX();viewportY=camera.tileY();
        panMouseX=mx;panMouseY=my;
	}

	viewportChanged(oldViewportX, viewportX, oldViewportY, viewportY);

	dragStep(mx, my, button);
}

double GameGUI::flagReachAt(double screenX, double screenY) const
{
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	const double edge = std::min({screenX, screenY, gfx->getW() - screenX, gfx->getH() - screenY}) / unit;
	const double toward = std::clamp(1 - edge / InGameTouchTheme::flagReachEdgeBand, 0.0, 1.0);
	return InGameTouchTheme::flagReach + (InGameTouchTheme::flagReachEdge - InGameTouchTheme::flagReach) * toward;
}

Building *GameGUI::flagAt(int mx, int my, double reachPoints)
{
	int mapX, mapY;
	game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY, viewportX, viewportY);
	for (Building *flag : localTeam->virtualBuildings)
		if (displayedPosX(*flag)==mapX && displayedPosY(*flag)==mapY)
			return flag;
	if (reachPoints <= 0)
		return nullptr;
	// Screen points whatever the zoom, measured to the flag's tile centre across
	// the map's wrap.
	const double radius = reachPoints * globalContainer->gfx->logicalUnitsPerPoint() / camera.zoom;
	double nearestDistance = radius * radius;
	Building *nearest = nullptr;
	const double worldX = mx + viewportX * 32., worldY = my + viewportY * 32.;
	const auto wrappedDistance = [](double delta, double period)
	{
		return MapCamera::wrap(delta + period / 2, period) - period / 2;
	};
	for (auto *flag : localTeam->virtualBuildings)
	{
		const double dx = wrappedDistance(worldX - (displayedPosX(*flag) * 32. + 16),
										   game.map.getW() * 32.);
		const double dy = wrappedDistance(worldY - (displayedPosY(*flag) * 32. + 16),
										   game.map.getH() * 32.);
		const double distance = dx * dx + dy * dy;
		if (distance < nearestDistance ||
			(distance == nearestDistance && nearest && flag->gid < nearest->gid))
		{
			nearest = flag;
			nearestDistance = distance;
		}
	}
	return nearest;
}

void GameGUI::handleMapClick(int mx, int my, int button)
{
	updateCamera();
	if (!torusView.active() && (!camera.contains(mx,my) || my<16)) return;
	const double flagReach=flagReachAt(mx, my);
	if (!torusView.active()) {mx=mapMouseX(mx);my=mapMouseY(my);}
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
		if (Building *flag=flagAt(mx, my, 0))
		{
			setSelection(BUILDING_SELECTION, flag);
			selectionPushed=true;
			return;
		}
        // Keep exact flag hits above units/buildings as before. The extra
        // touch-only selection halo claims otherwise empty ground, so it cannot
        // steal direct clicks from a neighbouring building or unit.
        Unit *mouseUnit = game.resolveUnit(view.mouseUnit);
        if (touch->usesHUD() && !torusView.active() && !mouseUnit &&
            game.map.getBuilding(mapX, mapY) == NOGBID)
        {
            if (Building *nearest=flagAt(mx, my, flagReach))
            {
                setSelection(BUILDING_SELECTION, nearest);
                // A forgiving selection click must not move the flag onto the
                // neighbouring tile on mouse-up. Touch drags move flags through
                // GameGUITouch, which grabs them before the map pans.
                selectionPushed = false;
                return;
            }
        }
		// then for unit
		if (mouseUnit)
		{
			// a unit is selected:
			setSelection(UNIT_SELECTION, mouseUnit);
			selectionPushed = true;
			// handle dump of unit characteristics
			if ((inputState.modifiers() & KMOD_SHIFT) != 0)
			{
				OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("unit.dump.txt"));
				if (stream->isEndOfStream())
				{
					std::cerr << "Can't dump unit to file unit.dump.txt" << std::endl;
				}
				else
				{
					std::cerr << "Dump unit " << mouseUnit->gid << " memory" << std::endl;
					mouseUnit->save(stream);
					mouseUnit->saveCrossRef(stream);
					if (mouseUnit->attachedBuilding)
					{
						mouseUnit->attachedBuilding->save(stream);
						mouseUnit->attachedBuilding->saveCrossRef(stream);
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
					|| globalContainer->isViewingGame() )
				{
					setSelection(BUILDING_SELECTION, gbid);
					selectionPushed=true;
					// showUnitWorkingToBuilding=true;
					// handle dump of building characteristics
					if ((inputState.modifiers() & KMOD_SHIFT) != 0)
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
				// and resource
				if (game.map.isResource(mapX, mapY) && game.map.isMapDiscovered(mapX, mapY, localTeam->me))
				{
					setSelection(RESOURCE_SELECTION, mapY*game.map.getW()+mapX);
					selectionPushed=true;
				}
				else
				{
					if (selectionMode == RESOURCE_SELECTION)
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
