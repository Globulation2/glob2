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
		Building* selBuild=selection.building;
		assert (selBuild);
		if (selBuild->owner->teamNumber!=localTeamNo)
			return;
		int ypos = YPOS_BASE_BUILDING +  YOFFSET_NAME + YOFFSET_ICON + YOFFSET_B_SEP;
		BuildingType *buildingType = selBuild->type;
		int lmx = mx - RIGHT_MENU_OFFSET; // local mx

		// working bar
		if (selBuild->type->maxUnitWorking)
		{
			if (((selBuild->owner->allies)&(1<<localTeamNo))
				&& my>ypos+YOFFSET_TEXT_BAR
				&& my<ypos+YOFFSET_TEXT_BAR+16
				&& selBuild->buildingState==Building::ALIVE
				&& lmx < 128)
			{
				int nbReq;
				if (lmx<18)
				{
					if(selBuild->maxUnitWorkingLocal>0)
					{
						nbReq=(selBuild->maxUnitWorkingLocal-=1);
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
				        defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
					}
				}
				else if (lmx<(128-18))
				{
					nbReq=selBuild->maxUnitWorkingLocal=((lmx-18)*MAX_UNIT_WORKING)/(128-36);
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
		        	defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
				}
				else
				{
					if(selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING)
					{
						nbReq=(selBuild->maxUnitWorkingLocal+=1);
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
			        	defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
					}
				}
			}
			ypos += YOFFSET_BAR + YOFFSET_B_SEP;
		}

		// priorities
		if(selBuild->type->maxUnitWorking)
		{
			ypos += YOFFSET_B_SEP;
			if (((selBuild->owner->allies)&(1<<localTeamNo))
				&& my>ypos+16
				&& my<ypos+16+12
				&& selBuild->buildingState==Building::ALIVE)
			{
				int width = (128 - 8)/3;

				if(lmx>=0 && lmx<=12)
				{
					orderQueue.push_back(shared_ptr<Order>(new OrderChangePriority(selBuild->gid, -1)));
					selBuild->priorityLocal = -1;
				}
				else if(lmx>=(width) && lmx<(width+12))
				{
					orderQueue.push_back(shared_ptr<Order>(new OrderChangePriority(selBuild->gid, 0)));
					selBuild->priorityLocal = 0;
				}
				else if(lmx>=(width*2) && lmx<=(width*2+12))
				{
					orderQueue.push_back(shared_ptr<Order>(new OrderChangePriority(selBuild->gid, 1)));
					selBuild->priorityLocal = 1;
				}
			}
			ypos += YOFFSET_BAR+YOFFSET_B_SEP;
		}

		// flag range bar
		if (buildingType->defaultUnitStayRange)
		{
			if (((selBuild->owner->allies)&(1<<localTeamNo))
				&& (my>ypos+YOFFSET_TEXT_BAR)
				&& (my<ypos+YOFFSET_TEXT_BAR+16)
				&& (lmx < 128))
			{
				int nbReq;
				if (lmx<18)
				{
					if(selBuild->unitStayRangeLocal>0)
					{
						nbReq=(selBuild->unitStayRangeLocal-=1);
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyFlag(selBuild->gid, nbReq)));
					}
				}
				else if (lmx<RIGHT_MENU_WIDTH-18)
				{
					nbReq=selBuild->unitStayRangeLocal=((lmx-18)*(unsigned)selBuild->type->maxUnitStayRange)/(128-36);
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyFlag(selBuild->gid, nbReq)));
				}
				else
				{
					// TODO : check in orderQueue to avoid useless orders.
					if (selBuild->unitStayRangeLocal < selBuild->type->maxUnitStayRange)
					{
						nbReq=(selBuild->unitStayRangeLocal+=1);
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyFlag(selBuild->gid, nbReq)));
					}
				}
			}
			ypos += YOFFSET_BAR+YOFFSET_B_SEP;
		}

		// flags specific options:
		if (((selBuild->owner->allies)&(1<<localTeamNo))
			&& lmx>10
			&& lmx<22)
		{

			// cleared ressources for clearing flags:
			if (buildingType->type == "clearingflag")
			{
				ypos+=YOFFSET_B_SEP+YOFFSET_TEXT_PARA;
				for (int i=0; i<BASIC_COUNT; i++)
					if (i!=STONE)
					{
						if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
						{
							selBuild->clearingRessourcesLocal[i]=!selBuild->clearingRessourcesLocal[i];
							orderQueue.push_back(shared_ptr<Order>(new OrderModifyClearingFlag(selBuild->gid, selBuild->clearingRessourcesLocal)));
						}

						ypos+=YOFFSET_TEXT_PARA;
					}
			}

			if (buildingType->type == "warflag")
			{
				ypos+=YOFFSET_B_SEP+YOFFSET_TEXT_PARA;
				for (int i=0; i<4; i++)
				{
					if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
					{
						selBuild->minLevelToFlagLocal=i;
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyMinLevelToFlag(selBuild->gid, selBuild->minLevelToFlagLocal)));
					}

					ypos+=YOFFSET_TEXT_PARA;
				}

			}

			if (buildingType->type == "explorationflag")
			{
				// we use minLevelToFlag as an int which says what magic effect at minimum an explorer
				// must be able to do to be accepted at this flag
				// 0 == any explorer
				// 1 == must be able to attack ground
				ypos+=YOFFSET_B_SEP+YOFFSET_TEXT_PARA;
				for (int i=0; i<2; i++)
				{
					if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
					{
						selBuild->minLevelToFlagLocal=i;
						orderQueue.push_back(shared_ptr<Order>(new OrderModifyMinLevelToFlag(selBuild->gid, selBuild->minLevelToFlagLocal)));
					}

					ypos+=YOFFSET_TEXT_PARA;
				}
			}
		}

		if (buildingType->armor)
			ypos+=YOFFSET_TEXT_LINE;
		if (buildingType->maxUnitInside)
			ypos += YOFFSET_INFOS;
		if (buildingType->shootDamage)
			ypos += YOFFSET_TOWER;
		ypos += YOFFSET_B_SEP;

		//Exchannge building
		//Exchanging as a feature is broken
		/*
		if (selBuild->type->canExchange && ((selBuild->owner->allies)&(1<<localTeamNo)))
		{
			int startY = ypos+YOFFSET_TEXT_PARA;
			int endY = startY+HAPPYNESS_COUNT*YOFFSET_TEXT_PARA;
			if ((my>startY) && (my<endY))
			{
				int r = (my-startY)/YOFFSET_TEXT_PARA;
				if ((lmx>92) && (lmx<104))
				{
					if (selBuild->receiveRessourceMask & (1<<r))
					{
						selBuild->receiveRessourceMaskLocal &= ~(1<<r);
					}
					else
					{
						selBuild->receiveRessourceMaskLocal |= (1<<r);
						selBuild->sendRessourceMaskLocal &= ~(1<<r);
					}
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyExchange(selBuild->gid, selBuild->receiveRessourceMaskLocal, selBuild->sendRessourceMaskLocal)));
				}

				if ((lmx>110) && (lmx<122))
				{
					if (selBuild->sendRessourceMask & (1<<r))
					{
						selBuild->sendRessourceMaskLocal &= ~(1<<r);
					}
					else
					{
						selBuild->receiveRessourceMaskLocal &= ~(1<<r);
						selBuild->sendRessourceMaskLocal |= (1<<r);
					}
					orderQueue.push_back(shared_ptr<Order>(new OrderModifyExchange(selBuild->gid, selBuild->receiveRessourceMaskLocal, selBuild->sendRessourceMaskLocal)));
				}
			}
		}
		*/
		// ressources in
		for (unsigned i=0; i<globalContainer->ressourcesTypes.size(); i++)
		{
			if (buildingType->maxRessource[i])
			{
				ypos += 11;
			}
		}
		if (buildingType->maxBullets)
		{
			ypos += 11;
		}
		ypos+=5;

		if (selBuild->type->unitProductionTime)
		{
			ypos+=15;
			for (int i=0; i<NB_UNIT_TYPE; i++)
			{
				if ((my>ypos+(i*20))&&(my<ypos+(i*20)+16)&&(lmx<128))
				{
					if (lmx<18)
					{
						if (selBuild->ratioLocal[i]>0)
						{
							selBuild->ratioLocal[i]--;
							orderQueue.push_back(shared_ptr<Order>(new OrderModifySwarm(selBuild->gid, selBuild->ratioLocal)));
						}
					}
					else if (lmx<(128-18))
					{
						selBuild->ratioLocal[i]=((lmx-18)*MAX_RATIO_RANGE)/(128-36);
						orderQueue.push_back(shared_ptr<Order>(new OrderModifySwarm(selBuild->gid, selBuild->ratioLocal)));
					}
					else
					{
						if (selBuild->ratioLocal[i]<MAX_RATIO_RANGE)
						{
							selBuild->ratioLocal[i]++;
							orderQueue.push_back(shared_ptr<Order>(new OrderModifySwarm(selBuild->gid, selBuild->ratioLocal)));
						}
					}
					//printf("ratioLocal[%d]=%d\n", i, selBuild->ratioLocal[i]);
				}
			}
		}

		if ((my>globalContainer->gfx->getH()-48) && (my<globalContainer->gfx->getH()-32))
		{
			if (selBuild->constructionResultState==Building::REPAIR)
			{
				int typeNum = selBuild->typeNum; //determines type of updated building
				int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum);
				orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(selBuild->gid, unitWorking)));
			}
			else if (selBuild->constructionResultState==Building::UPGRADE)
			{
				int typeNum = selBuild->typeNum; //determines type of updated building
				int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum - 1);
				orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(selBuild->gid, unitWorking)));
			}
			else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE))
			{
				repairAndUpgradeBuilding(selBuild, true, true);
			}
		}

		if ((my>globalContainer->gfx->getH()-24) && (my<globalContainer->gfx->getH()-8))
		{
			if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
			{
				orderQueue.push_back(shared_ptr<Order>(new OrderCancelDelete(selBuild->gid)));
			}
			else if (selBuild->buildingState==Building::ALIVE)
			{
				orderQueue.push_back(shared_ptr<Order>(new OrderDelete(selBuild->gid)));
			}
		}
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		Unit* selUnit=selection.unit;
		assert(selUnit);
		selUnit->verbose=!selUnit->verbose;
		printf("unit gid=(%d) verbose %d\n", selUnit->gid, selUnit->verbose);
		printf(" pos=(%d, %d)\n", selUnit->posX, selUnit->posY);
		printf(" needToRecheckMedical=%d\n", selUnit->needToRecheckMedical);
		printf(" medical=%d\n", selUnit->medical);
		printf(" activity=%d\n", selUnit->activity);
		printf(" displacement=%d\n", selUnit->displacement);
		printf(" movement=%d\n", selUnit->movement);
		printf(" action=%d\n", selUnit->action);

		if (selUnit->attachedBuilding)
			printf(" attachedBuilding bgid=%d\n", selUnit->attachedBuilding->gid);
		else
			printf(" attachedBuilding NULL\n");
		printf(" destinationPurpose=%d\n", selUnit->destinationPurpose);
		printf(" carriedRessource=%d\n", selUnit->carriedRessource);
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
