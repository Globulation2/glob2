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

void GameGUI::handleMenuClick(int mx, int my, int button)
{
	// handle minimap
	if (my<128 && mx > (RIGHT_MENU_OFFSET) && mx < RIGHT_MENU_WIDTH - RIGHT_MENU_OFFSET)
	{
		if (putMark)
		{
			int markx, marky;
			minimapMouseToPos(globalContainer->gfx->getW() - RIGHT_MENU_WIDTH + mx, my, &markx, &marky, false);
			orderQueue.push_back(shared_ptr<Order>(new MapMarkOrder(localTeamNo, markx, marky)));
			globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_NORMAL);
			putMark = false;
		}
		else
		{
			miniMapPushed=true;
			int oldViewportX = viewportX;
			int oldViewportY = viewportY;
			minimapMouseToPos(globalContainer->gfx->getW() - RIGHT_MENU_WIDTH + mx, my, &viewportX, &viewportY, true);
			moveParticles(oldViewportX, viewportX, oldViewportY, viewportY);
		}
	}
	// Check if one of the panel buttons has been clicked
	else if (my<YPOS_BASE_DEFAULT)
	{
		if (!globalContainer->replaying)
		{
			int dec = (RIGHT_MENU_WIDTH-128)/2;
			int dm=(mx-dec)/32;
			if (!((1<<dm) & hiddenGUIElements))
			{
				if (dm < NB_VIEWS)
				{
					displayMode=DisplayMode(dm);
					clearSelection();
				}
			}
		}
		else
		{
			int dec = (RIGHT_MENU_WIDTH-96)/2;
			int dm=(mx-dec)/32;

			if (dm < RDM_NB_VIEWS)
			{
				replayDisplayMode=ReplayDisplayMode(dm);
				clearSelection();
			}
		}
	}
	else if (selectionMode==BUILDING_SELECTION)
	{
		handleMenuClickBuildingSelection(mx, my, button);
	}
	else if ((displayMode==CONSTRUCTION_VIEW && !globalContainer->replaying))
	{
		int xNum=mx/(RIGHT_MENU_WIDTH/2);
		int yNum=(my-YPOS_BASE_CONSTRUCTION)/46;
		int id=yNum*2+xNum;
		if (id<(int)buildingsChoiceName.size())
			if (buildingsChoiceState[id])
				setSelection(TOOL_SELECTION, (void *)buildingsChoiceName[id].c_str());
	}
	else if ((displayMode==FLAG_VIEW && !globalContainer->replaying))
	{
		int dec = (RIGHT_MENU_WIDTH - 128)/2;
		my -= YPOS_BASE_FLAG;
		int nmx = mx - dec;
		if (my > YOFFSET_BRUSH)
		{
			// set the selection
			setSelection(BRUSH_SELECTION);
			// change the brush type (forbidden, guard, clear) if necessary
			if (my < YOFFSET_BRUSH+40)
			{
				if (nmx < 44)
					toolManager.activateZoneTool(GameGUIToolManager::Forbidden);
				else if (nmx < 84)
					toolManager.activateZoneTool(GameGUIToolManager::Guard);
				else if(nmx < 124)
					toolManager.activateZoneTool(GameGUIToolManager::Clearing);
			}
			// anyway, update the tool
			brush.handleClick(mx-dec, my-YOFFSET_BRUSH-40);
			toolManager.activateZoneTool();
		}
		else
		{
			int xNum=mx / (RIGHT_MENU_WIDTH/3);
			int yNum=my / 46;
			int id=yNum*3+xNum;
			if (id<(int)flagsChoiceName.size())
				if (flagsChoiceState[id])
					setSelection(TOOL_SELECTION, (void*)flagsChoiceName[id].c_str());
		}
	}
	else if ((displayMode==STAT_GRAPH_VIEW && !globalContainer->replaying) || (replayDisplayMode==RDM_STAT_GRAPH_VIEW && globalContainer->replaying))
	{
		if(mx > 8 && mx < 24)
		{
			// In replays, this menu bar is 15 pixels lower than usual to show "Watching: player-name"
			int inc;

			if (globalContainer->replaying) inc = 15;
			else inc = 0;

			if(my > YPOS_BASE_STAT+140+inc+64 && my < YPOS_BASE_STAT+140+inc+80)
			{
				showDamagedMap=false;
				showDefenseMap=false;
				showFertilityMap=false;
				showStarvingMap=!showStarvingMap;
				overlay.compute(game, OverlayArea::Starving, localTeamNo);
			}

			if(my > YPOS_BASE_STAT+140+inc+88 && my < YPOS_BASE_STAT+140+inc+104)
			{
				showDamagedMap=!showDamagedMap;
				showStarvingMap=false;
				showDefenseMap=false;
				showFertilityMap=false;
				overlay.compute(game, OverlayArea::Damage, localTeamNo);
			}

			if(my > YPOS_BASE_STAT+140+inc+112 && my < YPOS_BASE_STAT+140+inc+128)
			{
				showDefenseMap=!showDefenseMap;
				showStarvingMap=false;
				showDamagedMap=false;
				showFertilityMap=false;
				overlay.compute(game, OverlayArea::Defence, localTeamNo);
			}

			if(my > YPOS_BASE_STAT+140+inc+136 && my < YPOS_BASE_STAT+140+inc+152)
			{
				showFertilityMap=!showFertilityMap;
				showDefenseMap=false;
				showStarvingMap=false;
				showDamagedMap=false;
				overlay.compute(game, OverlayArea::Fertility, localTeamNo);
			}
		}
	}
	else if (replayDisplayMode==RDM_REPLAY_VIEW && globalContainer->replaying)
	{
		int x = REPLAY_PANEL_XOFFSET;
		int y = REPLAY_PANEL_YOFFSET;
		int inc = REPLAY_PANEL_SPACE_BETWEEN_OPTIONS;

		if (mx > x && mx < x+20 && my > y+1*inc && my < y+1*inc + 20)
		{
			// Disable/show fog of war
			globalContainer->replayShowFog = !globalContainer->replayShowFog;

			if (globalContainer->replayShowFog) minimap.setMinimapMode( Minimap::ShowFOW );
			else minimap.setMinimapMode( Minimap::HideFOW );
		}
		if (mx > x && mx < x+20 && my > y+2*inc && my < y+2*inc + 20)
		{
			// Disable/enable combined vision
			if (globalContainer->replayVisibleTeams == 0xFFFFFFFF)
			{
				globalContainer->replayVisibleTeams = localTeam->me;
			}
			else
			{
				globalContainer->replayVisibleTeams = 0xFFFFFFFF;
			}
		}
		if (mx > x && mx < x+20 && my > y+3*inc && my < y+3*inc + 20)
		{
			// Show/hide player's areas
			globalContainer->replayShowAreas = !globalContainer->replayShowAreas;
		}
		if (mx > x && mx < x+20 && my > y+4*inc && my < y+4*inc + 20)
		{
			// Show/hide flags
			globalContainer->replayShowFlags = !globalContainer->replayShowFlags;
		}

		for (int i = 0; i < game.teamsCount(); i++)
		{
			if (mx > x && mx < x+20 && my > y+REPLAY_PANEL_PLAYERLIST_YOFFSET+(i+1)*inc && my < y+REPLAY_PANEL_PLAYERLIST_YOFFSET+(i+1)*inc + 20)
			{
				localTeamNo = i;

				// Update everything to match this team number
				adjustLocalTeam();

				// Update localPlayer to the first player of this team
				for (int j=0; j<game.gameHeader.getNumberOfPlayers(); j++)
				{
					if (game.players[j]->teamNumber == localTeamNo)
					{
						localPlayer = j;
						break;
					}
				}

				// Update the visible players unless all players are visible
				if (globalContainer->replayVisibleTeams != 0xFFFFFFFF)
				{
					globalContainer->replayVisibleTeams = localTeam->me;
				}
			}
		}
	}
}
