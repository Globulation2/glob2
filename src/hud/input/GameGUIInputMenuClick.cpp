// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>


#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "PanelButtonHit.h"
#include "Player.h"
#include "Unit.h"

using std::shared_ptr;
using std::static_pointer_cast;

void GameGUI::handleMenuClick(int mx, int my, int button)
{
    if(globalContainer->isViewingGame() && parkForClient([&]{handleMenuClick(mx,my,button);})) return;
	if (button==SDL_BUTTON_LEFT && activateHudControlAt({globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+mx,my})) return;
	// handle minimap
	if (minimap.imageBounds().contains(GAGGUI::ui::Point{globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+mx,my}))
	{
		if (putMark)
		{
			int markx, marky;
			minimapMouseToPos(globalContainer->gfx->getW() - RIGHT_MENU_WIDTH + mx, my, &markx, &marky, false);
			enqueueOrder(shared_ptr<Order>(new MapMarkOrder(localTeamNo, markx, marky)));
			globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_NORMAL);
			putMark = false;
		}
		else
		{
			miniMapPushed=true;
			int oldViewportX = viewportX;
			int oldViewportY = viewportY;
			stopViewportMotion();
			minimapMouseToPos(globalContainer->gfx->getW() - RIGHT_MENU_WIDTH + mx, my, &viewportX, &viewportY, true);
			viewportChanged(oldViewportX, viewportX, oldViewportY, viewportY);
		}
	}
	// Check if one of the panel buttons has been clicked
	else if (my<YPOS_BASE_DEFAULT)
	{
		if (!globalContainer->isViewingGame())
		{
			const int dec = (RIGHT_MENU_WIDTH - NB_VIEWS*PANEL_BUTTON_WIDTH)/2;
			if (const auto button = panelButtonIndex(mx, dec, NB_VIEWS))
			{
				// index is a validated button in [0, NB_VIEWS), so the mask
				// shift can never be undefined.
				if (!((1 << *button) & hiddenGUIElements))
				{
					displayMode = DisplayMode(*button);
					clearSelection();
				}
			}
		}
		else
		{
			const int dec = (RIGHT_MENU_WIDTH - RDM_NB_VIEWS*PANEL_BUTTON_WIDTH)/2;
			if (const auto button = panelButtonIndex(mx, dec, RDM_NB_VIEWS))
			{
				replayDisplayMode = ReplayDisplayMode(*button);
				clearSelection();
			}
		}
	}
	else if (selectionMode==BUILDING_SELECTION)
	{
		handleMenuClickBuildingSelection(mx, my, button);
	}
	else if ((displayMode==FLAG_VIEW && !globalContainer->isViewingGame()))
	{
		int dec = (RIGHT_MENU_WIDTH - BrushTool::WIDTH)/2;
		int relY = my - YPOS_BASE_FLAG;
		if (relY > YOFFSET_BRUSH)
		{
			// set the selection
			setSelection(BRUSH_SELECTION);
			// change the brush type (forbidden, guard, clear, farm) if necessary
			if (relY < YOFFSET_BRUSH+ZONE_STRIP_HEIGHT)
			{
				const int zone = zoneStripButtonAt(mx, toolManager.zoneTypeCount());
				if (zone >= 0)
					toolManager.activateZoneTool((GameGUIToolManager::ZoneType)zone);
			}
			// anyway, update the tool
			brush.handleClick(mx-dec, relY-YOFFSET_BRUSH-ZONE_STRIP_HEIGHT);
			toolManager.activateZoneTool();
		}
		else
		{
			// Use the same hit grid as the hover preview so click and hover cannot drift.
			const auto id = pickChoiceUnderMouse(YPOS_BASE_FLAG, flagsChoiceName.size(), 3);
			if (id && flagsChoiceState[*id])
				setSelection(TOOL_SELECTION, (void*)flagsChoiceName[*id].c_str());
		}
	}

}
