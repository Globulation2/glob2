// SPDX-License-Identifier: GPL-3.0-or-later

#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "Unit.h"

void GameGUI::toggleTorusView()
{
	if (!torusView.available() || typingInputScreen || inGameMenu != IGM_NONE || scrollableText)
		return;
	if (torusPointerDown)
		toolManager.finishPointerGesture(localTeamNo);
	torusPointerDown = false;
	torusView.toggle();
	selectionPushed = panPushed = miniMapPushed = false;
	viewportSpeedX = viewportSpeedY = 0;
}

// Adapt the rendered surface to the normal tool coordinate system, retaining
// sub-tile precision for even-sized buildings and wrapping at the map seams.
bool GameGUI::torusMapPointer(int x, int y, int &mx, int &my) const
{
	int px, py;
	if (!torusView.pick(x, y, px, py))
		return false;
	mx = (px - viewportX * 32) & (game.map.getW() * 32 - 1);
	my = (py - viewportY * 32) & (game.map.getH() * 32 - 1);
	return true;
}

bool GameGUI::handleTorusPointer(const SDL_Event &event)
{
	if (event.type != SDL_MOUSEBUTTONDOWN && event.type != SDL_MOUSEBUTTONUP)
		return false;
	if (event.button.button != SDL_BUTTON_LEFT)
		return false;
	bool onMap = event.button.x >= 0 && event.button.y >= 16 &&
		event.button.x < globalContainer->gfx->getW() - RIGHT_MENU_WIDTH;
	if (!onMap && !torusPointerDown)
		return false;
	int mx, my;
	bool hit = onMap && torusMapPointer(event.button.x, event.button.y, mx, my);
	view.mouseX = mouseX = event.button.x;
	view.mouseY = mouseY = event.button.y;
	if (event.type == SDL_MOUSEBUTTONDOWN)
	{
		torusPointerDown = hit;
		if (hit)
		{
			// The atlas renderer's mouse hit refers to a previous capture.
			// Resolve units at the picked map cell, with normal visibility rules.
			view.mouseUnit = NULL;
			int x = ((mx >> 5) + viewportX) & game.map.getMaskW();
			int y = ((my >> 5) + viewportY) & game.map.getMaskH();
			Uint16 gid = game.map.getAirUnit(x, y);
			if (gid == NOGUID)
				gid = game.map.getGroundUnit(x, y);
			if (gid != NOGUID &&
				(Unit::GIDtoTeam(gid) == localTeamNo || game.map.isFOWDiscovered(x, y, localTeam->me) ||
				 globalContainer->replaying))
				view.mouseUnit = game.teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];
			handleMapClick(mx, my, SDL_BUTTON_LEFT);
		}
	}
	else
	{
		if (torusPointerDown)
		{
			if (hit && selectionMode == BUILDING_SELECTION && selectionPushed &&
				selectionBuilding()->type->isVirtual)
				moveFlag(mx, my, true);
			else if (selectionMode == BRUSH_SELECTION || selectionMode == TOOL_SELECTION)
			{
				if (hit)
					toolManager.handleMouseUp(mx, my, localTeamNo, viewportX, viewportY);
				else
					toolManager.finishPointerGesture(localTeamNo);
			}
		}
		torusPointerDown = false;
		miniMapPushed = selectionPushed = panPushed = false;
	}
	return true;
}

void GameGUI::drawTorusMapOverlay(int originX, int originY)
{
	if (globalContainer->replaying)
		return;
	ghostManager.drawAll(originX, originY, localTeamNo);
	int px, py;
	if ((selectionMode == TOOL_SELECTION || selectionMode == BRUSH_SELECTION) &&
		torusView.pick(mouseX, mouseY, px, py))
	{
		int mx = (px - originX * 32) & (game.map.getW() * 32 - 1);
		int my = (py - originY * 32) & (game.map.getH() * 32 - 1);
		toolManager.drawTool(mx, my, localTeamNo, originX, originY);
	}
	if (selectionMode == BUILDING_SELECTION)
	{
		Building *b = selectionBuilding();
		int x, y;
		game.map.buildingPosToCursor(displayedPosX(*b), displayedPosY(*b), b->type->width, b->type->height, &x, &y,
		                             originX, originY);
		globalContainer->gfx->drawCircle(x, y, b->type->width * 16, 0, 0, 190);
	}
}
