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

void GameGUI::handleKey(SDL_Keysym key, bool pressed)
{
	if (typingInputScreen == NULL)
	{
		if(key.sym == SDLK_SPACE && pressed && swallowSpaceKey)
		{
			setIsSpaceSet(true);
		}
		else
		{
			Uint32 action_t = keyboardManager.getAction(KeyPress(key, pressed));
			switch(action_t)
			{
				case GameGUIKeyActions::DoNothing:
				{
				}
				break;
				case GameGUIKeyActions::ShowMainMenu:
				{
					if (inGameMenu==IGM_NONE)
					{
						gameMenuScreen=new InGameMainScreen(globalContainer->replaying);
						inGameMenu=IGM_MAIN;
					}
				}
				break;
				case GameGUIKeyActions::UpgradeBuilding:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild = selection.building;
						int typeNum = selBuild->typeNum; //determines type of updated building
						int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum - 1);
						if (selBuild->constructionResultState == Building::UPGRADE)
							orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(selBuild->gid, unitWorking)));
						else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE))
							repairAndUpgradeBuilding(selBuild, false, true);
					}
				}
				break;
				case GameGUIKeyActions::IncreaseUnitsWorking:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild=selection.building;
						if ((selBuild->owner->teamNumber==localTeamNo) && (selBuild->type->maxUnitWorking) && (selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING))
						{
							int nbReq=std::min(20, selBuild->maxUnitWorkingLocal+1);
							selBuild->maxUnitWorkingLocal = nbReq;
							orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
							defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
						}
					}
				}
				break;
				case GameGUIKeyActions::DecreaseUnitsWorking:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild=selection.building;
						if ((selBuild->owner->teamNumber==localTeamNo) && (selBuild->type->maxUnitWorking) && (selBuild->maxUnitWorkingLocal>0))
						{
							int nbReq=std::max(0, selBuild->maxUnitWorkingLocal-1);
							selBuild->maxUnitWorkingLocal = nbReq;
							orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
							defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
						}
					}
				}
				break;
				case GameGUIKeyActions::OpenChatBox:
				{
					typingInputScreen=new InGameTextInput(globalContainer->gfx);
					typingInputScreenInc=TYPING_INPUT_BASE_INC;
					typingInputScreenPos=0;
				}
				break;
				case GameGUIKeyActions::IterateSelection:
				{
					iterateSelection();
				}
				break;
				case GameGUIKeyActions::GoToEvent:
				{
					eventGoTypeIterator = eventGoType;
					int evX = eventGoPosX;
					int evY = eventGoPosY;

					int oldViewportX = viewportX;
					int oldViewportY = viewportY;

					int sw = globalContainer->gfx->getW();
					int sh = globalContainer->gfx->getH();
					viewportX = evX-((sw-RIGHT_MENU_WIDTH)>>6);
					viewportY = evY-(sh>>6);

					moveParticles(oldViewportX, viewportX, oldViewportY, viewportY);
				}
				break;
				case GameGUIKeyActions::GoToHome:
				{
					int evX = localTeam->startPosX;
					int evY = localTeam->startPosY;

					int oldViewportX = viewportX;
					int oldViewportY = viewportY;

				    int sw = globalContainer->gfx->getW();
					int sh = globalContainer->gfx->getH();
					viewportX = evX-((sw-RIGHT_MENU_WIDTH)>>6);
					viewportY = evY-(sh>>6);

					moveParticles(oldViewportX, viewportX, oldViewportY, viewportY);
				}
				break;
				case GameGUIKeyActions::PauseGame:
				{
					orderQueue.push_back(shared_ptr<Order>(new PauseGameOrder(!gamePaused)));
				}
				break;
				case GameGUIKeyActions::HardPause:
				{
					hardPause=!hardPause;
				}
				break;
				case GameGUIKeyActions::ToggleDrawUnitPaths:
				{
					drawPathLines=!drawPathLines;
				}
				break;
				case GameGUIKeyActions::DestroyBuilding:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild=selection.building;
						if (selBuild->owner->teamNumber==localTeamNo)
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
				}
				break;
				case GameGUIKeyActions::RepairBuilding:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild = selection.building;
						int typeNum = selBuild->typeNum; //determines type of updated building
						int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum);
						if (selBuild->constructionResultState == Building::REPAIR)
							orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(selBuild->gid, unitWorking)));
						else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE))
							repairAndUpgradeBuilding(selBuild, true, false);
					}
				}
				break;
				case GameGUIKeyActions::ToggleDrawInformation:
				{
					drawHealthFoodBar=!drawHealthFoodBar;
				}
				break;
				case GameGUIKeyActions::ToggleDrawAccessibilityAids:
				{
					drawAccessibilityAids = !drawAccessibilityAids;
				}
				break;
				case GameGUIKeyActions::MarkMap:
				{
					putMark=true;
					globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_MARK);
				}
				break;
				case GameGUIKeyActions::ToggleRecordingVoice:
				{
					if (globalContainer->voiceRecorder->recordingNow)
						globalContainer->voiceRecorder->stopRecording();
					else
						globalContainer->voiceRecorder->startRecording();
				}
				break;
				case GameGUIKeyActions::ViewHistory:
				{
					if ( ! scrollableText)
						scrollableText = messageManager.createScrollableHistoryScreen();
					else
					{
						delete scrollableText;
						scrollableText=NULL;
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructInn:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("inn")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("inn"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructSwarm:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("swarm")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("swarm"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructHospital:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("hospital")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("hospital"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructRacetrack:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("racetrack")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("racetrack"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructSwimmingPool:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("swimmingpool")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("swimmingpool"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructBarracks:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("barracks")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("barracks"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructSchool:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("school")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("school"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructDefenceTower:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("defencetower")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("defencetower"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructStoneWall:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("stonewall")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("stonewall"));
					}
				}
				break;
				case GameGUIKeyActions::SelectConstructMarket:
				{
					clearSelection();
					if (isBuildingEnabled(std::string("market")))
					{
						displayMode = CONSTRUCTION_VIEW;
						setSelection(TOOL_SELECTION, (void *)("market"));
					}
				}
				break;
				case GameGUIKeyActions::SelectPlaceExplorationFlag:
				{
					clearSelection();
					if (isFlagEnabled(std::string("explorationflag")))
					{
						displayMode = FLAG_VIEW;
						setSelection(TOOL_SELECTION, (void*)("explorationflag"));
					}
				}
				break;
				case GameGUIKeyActions::SelectPlaceWarFlag:
				{
					clearSelection();
					if (isFlagEnabled(std::string("warflag")))
					{
						displayMode = FLAG_VIEW;
						setSelection(TOOL_SELECTION, (void*)("warflag"));
					}
				}
				break;
				case GameGUIKeyActions::SelectPlaceClearingFlag:
				{
					clearSelection();
					if (isFlagEnabled(std::string("clearingflag")))
					{
						displayMode = FLAG_VIEW;
						setSelection(TOOL_SELECTION, (void*)("clearingflag"));
					}
				}
				break;
				case GameGUIKeyActions::SelectPlaceForbiddenArea:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool(GameGUIToolManager::Forbidden);
				}
				break;
				case GameGUIKeyActions::SelectPlaceGuardArea:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool(GameGUIToolManager::Guard);
				}
				break;
				case GameGUIKeyActions::SelectPlaceClearingArea:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool(GameGUIToolManager::Clearing);
				}
				break;
				case GameGUIKeyActions::SwitchToAddingAreas:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setType(BrushTool::MODE_ADD);
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToRemovingAreas:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setType(BrushTool::MODE_DEL);
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush1:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(0);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush2:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(1);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush3:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(2);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush4:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(3);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush5:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(4);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush6:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(5);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush7:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(6);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush8:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setFigure(7);
					if (brush.getType() == BrushTool::MODE_NONE)
					{
						brush.setType(BrushTool::MODE_ADD);
					}
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
			}
		}
	}
}

void GameGUI::handleKeyDump(SDL_KeyboardEvent key)
{
	if (key.keysym.sym == SDLK_PRINTSCREEN)
	{
		if ((key.keysym.mod & KMOD_SHIFT) != 0)
		{
			OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("glob2.dump.txt"));
			if (stream->isEndOfStream())
			{
				std::cerr << "Can't dump full game memory to file glob2.dump.txt" << std::endl;
			}
			else
			{
				std::cerr << "Dump full game memory" << std::endl;
				save(stream, "glob2.dump.txt");
			}
			delete stream;
		}
		else
		{
			globalContainer->gfx->printScreen("screenshot.bmp");
		}
	}
}

void GameGUI::handleKeyAlways(void)
{
	SDL_PumpEvents();
	const Uint8 *keystate = SDL_GetKeyboardState(NULL);
	if (notmenu == false)
	{
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

		if (keystate[SDL_SCANCODE_UP])
			viewportY -= yMotion;
		if (keystate[SDL_SCANCODE_KP_8])
			viewportY -= yMotion;
		if (keystate[SDL_SCANCODE_DOWN])
			viewportY += yMotion;
		if (keystate[SDL_SCANCODE_KP_2])
			viewportY += yMotion;
		if ((keystate[SDL_SCANCODE_LEFT]) && (typingInputScreen == NULL)) // we haave a test in handleKeyAlways, that's not very clean, but as every key check based on key states and not key events are here, it is much simpler and thus easier to understand and thus cleaner ;-)
			viewportX -= xMotion;
		if (keystate[SDL_SCANCODE_KP_4])
			viewportX -= xMotion;
		if ((keystate[SDL_SCANCODE_RIGHT]) && (typingInputScreen == NULL)) // we haave a test in handleKeyAlways, that's not very clean, but as every key check based on key states and not key events are here, it is much simpler and thus easier to understand and thus cleaner ;-)
			viewportX += xMotion;
		if (keystate[SDL_SCANCODE_KP_6])
			viewportX += xMotion;
		if (keystate[SDL_SCANCODE_KP_7])
		{
			viewportX -= xMotion;
			viewportY -= yMotion;
		}
		if (keystate[SDL_SCANCODE_KP_9])
		{
			viewportX += xMotion;
			viewportY -= yMotion;
		}
		if (keystate[SDL_SCANCODE_KP_1])
		{
			viewportX -= xMotion;
			viewportY += yMotion;
		}
		if (keystate[SDL_SCANCODE_KP_3])
		{
			viewportX += xMotion;
			viewportY += yMotion;
		}
	}
}
