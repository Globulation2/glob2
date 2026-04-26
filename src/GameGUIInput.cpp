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

bool GameGUI::processScrollableWidget(SDL_Event *event)
{
	scrollableText->translateAndProcessEvent(event);
	return true;
}

bool GameGUI::processGameMenu(SDL_Event *event)
{
	gameMenuScreen->translateAndProcessEvent(event);
	switch (inGameMenu)
	{
		case IGM_MAIN:
		{
			switch (gameMenuScreen->endValue)
			{
				case InGameMainScreen::LOAD_GAME:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_LOAD;
					if (globalContainer->replaying)
						gameMenuScreen = new LoadSaveScreen("replays", "replay", true, std::string(Toolkit::getStringTable()->getString("[load replay]")), defualtGameSaveName.c_str(), glob2FilenameToName, glob2NameToFilename);
					else
						gameMenuScreen = new LoadSaveScreen("games", "game", true, false, defualtGameSaveName.c_str(), glob2FilenameToName, glob2NameToFilename);
					return true;
				}
				break;
				case InGameMainScreen::SAVE_GAME:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_SAVE;
					gameMenuScreen = new LoadSaveScreen("games", "game", false, false, defualtGameSaveName.c_str(), glob2FilenameToName, glob2NameToFilename);
					return true;
				}
				break;
				case InGameMainScreen::OPTIONS:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_OPTION;
					gameMenuScreen = new InGameOptionScreen(this);
					return true;
				}
				break;
				case InGameMainScreen::RETURN_GAME:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_NONE;
					gameMenuScreen=NULL;
					return true;
				}
				break;
				case InGameMainScreen::QUIT_GAME:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_NONE;
					gameMenuScreen=NULL;
					orderQueue.push_back(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
					flushOutgoingAndExit=true;
					return true;
				}
				break;
				default:
				return false;
			}
		}

		case IGM_ALLIANCE:
		{
			switch (gameMenuScreen->endValue)
			{
				case InGameAllianceScreen::OK :
				{
					Uint32 playerMask[5];
					Uint32 teamMask[5];
					playerMask[0]=((InGameAllianceScreen *)gameMenuScreen)->getAlliedMask();
					playerMask[1]=((InGameAllianceScreen *)gameMenuScreen)->getEnemyMask();
					playerMask[2]=((InGameAllianceScreen *)gameMenuScreen)->getExchangeVisionMask();
					playerMask[3]=((InGameAllianceScreen *)gameMenuScreen)->getFoodVisionMask();
					playerMask[4]=((InGameAllianceScreen *)gameMenuScreen)->getOtherVisionMask();
					teamMask[0]=teamMask[1]=teamMask[2]=teamMask[3]=teamMask[4]=0;

					// mask are for players, we need to convert them to team.
					for (int pi=0; pi<game.gameHeader.getNumberOfPlayers(); pi++)
					{
						int otherTeam=game.players[pi]->teamNumber;
						for (int mi=0; mi<5; mi++)
						{
							if (playerMask[mi]&(1<<pi))
							{
								// player is set, set team
								teamMask[mi]|=(1<<otherTeam);
							}
						}
					}

					// we have a special cases for uncontroled Teams:
					// FIXME : remove this
					for (int ti=0; ti<game.mapHeader.getNumberOfTeams(); ti++)
						if (game.teams[ti]->playersMask==0)
							teamMask[1]|=(1<<ti); // we want to hit them.

					orderQueue.push_back(shared_ptr<Order>(new SetAllianceOrder(localTeamNo,
						teamMask[0], teamMask[1], teamMask[2], teamMask[3], teamMask[4])));
					chatMask=((InGameAllianceScreen *)gameMenuScreen)->getChatMask();
					inGameMenu=IGM_NONE;
					delete gameMenuScreen;
					gameMenuScreen=NULL;
				}
				return true;

				default:
				return false;
			}
		}

		case IGM_OPTION:
		{
			if (gameMenuScreen->endValue == InGameOptionScreen::OK)
			{
				inGameMenu=IGM_NONE;
				delete gameMenuScreen;
				gameMenuScreen=NULL;
				return true;
			}
			else
			{
				return false;
			}
		}

		case IGM_OBJECTIVES:
		{
			if (gameMenuScreen->endValue == InGameObjectivesScreen::OK)
			{
				inGameMenu=IGM_NONE;
				delete gameMenuScreen;
				gameMenuScreen=NULL;
				return true;
			}
			else
			{
				return false;
			}
		}

		case IGM_LOAD:
		case IGM_SAVE:
		{
			switch (gameMenuScreen->endValue)
			{
				case LoadSaveScreen::OK:
				{
					std::string locationName=((LoadSaveScreen *)gameMenuScreen)->getFileName();
					if (inGameMenu==IGM_LOAD)
					{
						toLoadGameFileName = locationName;
						orderQueue.push_back(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
						flushOutgoingAndExit=true;
					}
					else
					{
						defualtGameSaveName=((LoadSaveScreen *)gameMenuScreen)->getName();
						OutputStream *stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(locationName));
						if (stream->isEndOfStream())
						{
							std::cerr << "GGU : Can't save map " << locationName << std::endl;
						}
						else
						{
							const std::string name = ((LoadSaveScreen *)gameMenuScreen)->getName();
							assert(name.size());
							save(stream, name);
						}
						delete stream;
					}
				}

				case LoadSaveScreen::CANCEL:
				inGameMenu=IGM_NONE;
				delete gameMenuScreen;
				gameMenuScreen=NULL;
				return true;

				default:
				return false;
			}
		}

		case IGM_END_OF_GAME:
		{
			switch (gameMenuScreen->endValue)
			{
				case InGameEndOfGameScreen::QUIT:
				orderQueue.push_back(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
				flushOutgoingAndExit=true;

				case InGameEndOfGameScreen::CONTINUE:
				inGameMenu=IGM_NONE;
				delete gameMenuScreen;
				gameMenuScreen=NULL;
				return true;

				case InGameEndOfGameScreen::WATCH_AGAIN:
				assert(globalContainer->replaying);
				inGameMenu=IGM_NONE;
				delete gameMenuScreen;
				gameMenuScreen=NULL;
				toLoadGameFileName = globalContainer->replayFileName;
				orderQueue.push_back(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
				flushOutgoingAndExit=true;
				return true;

				default:
				return false;
			}
		}

		default:
		return false;
	}
}

void GameGUI::processEvent(SDL_Event *event)
{
	// handle typing
	if (typingInputScreen)
	{
		if ((event->type==SDL_KEYDOWN) && (event->key.keysym.sym == SDLK_ESCAPE))
		{
			typingInputScreenInc=-TYPING_INPUT_BASE_INC;
			typingInputScreen->endValue=1;
		}

		typingInputScreen->translateAndProcessEvent(event);

		if (typingInputScreen->endValue==0)
		{
			//Interpret message
			std::string message = typingInputScreen->getText();
			Uint32 nchatMask = chatMask;
			if(message[0] == '/')
			{
				std::string name;
				for(int i=1; message[i]!=' '; ++i)
					name += message[i];
				message = message.substr(message.find(' ')+1);
				if(name=="a")
				{
					nchatMask = localTeam->allies;
				}
				else
				{
					for(int i=0; i<game.gameHeader.getNumberOfPlayers(); ++i)
					{
						if(name == game.gameHeader.getBasePlayer(i).name)
						{
							nchatMask = game.gameHeader.getBasePlayer(i).teamNumberMask | localTeam->me;
							break;
						}
					}
				}
			}

			if (!message.empty())
			{
				orderQueue.push_back(shared_ptr<Order>(new MessageOrder(nchatMask, MessageOrder::NORMAL_MESSAGE_TYPE, message.c_str())));
				typingInputScreen->setText("");
			}
			typingInputScreenInc=-TYPING_INPUT_BASE_INC;
			typingInputScreen->endValue=1;
			return;
		}
	}

	// the dump (debug) keys are always handled
	if (event->type == SDL_KEYDOWN)
		handleKeyDump(event->key);


	if (event->type==SDL_MOUSEBUTTONUP)
	{
		int button=event->button.button;
		if (button==SDL_BUTTON_MIDDLE)
		{
			panPushed=false;
		}
	}


	if (event->type == SDL_MOUSEBUTTONDOWN)
	{
		int butx = event->button.x;
		int buty = event->button.y;

		int leftEdge  = globalContainer->gfx->getW() - RIGHT_MENU_WIDTH - IGM_ICON_HEIGHT/2;
		int rightEdge = globalContainer->gfx->getW() - RIGHT_MENU_WIDTH + IGM_ICON_HEIGHT/2;
		int menu = -1;

		if (event->button.button == SDL_BUTTON_LEFT
			&& (butx > leftEdge)
			&& (butx < rightEdge))
		{
			if (buty < IGM_MAIN_MENU_ICON_Y + IGM_ICON_HEIGHT)
			{
				menu = IGM_MAIN;
			}
			if (!(hiddenGUIElements & HIDABLE_ALLIANCE)
				&& (buty > IGM_ALLIANCE_ICON_Y)
				&& (buty < IGM_ALLIANCE_ICON_Y + IGM_ICON_HEIGHT))
			{
				menu = IGM_ALLIANCE;
			}
			if ((buty > IGM_OBJECTIVES_ICON_Y)
				&& (buty < IGM_OBJECTIVES_ICON_Y + IGM_ICON_HEIGHT))
			{
				menu = IGM_OBJECTIVES;
			}

			if (menu != -1)
			{
				if (inGameMenu != IGM_NONE)
				{
					delete gameMenuScreen;
					gameMenuScreen = NULL;
				}
				if (inGameMenu == menu)
					inGameMenu = IGM_NONE;
				else
					inGameMenu = static_cast<InGameMenu>(menu);

				switch (menu)
				{
				case IGM_MAIN:
					gameMenuScreen = new InGameMainScreen(globalContainer->replaying);
					break;
				case IGM_ALLIANCE:
					gameMenuScreen = new InGameAllianceScreen(this);
					break;
				case IGM_OBJECTIVES:
					gameMenuScreen = new InGameObjectivesScreen(this, false);
					break;
				default:
					assert(false);
				}
			}
		}
	}


	// if there is a menu he get events first
	if (inGameMenu)
	{
		notmenu=true;
		processGameMenu(event);
	}
	else
	{
		notmenu=false;
		if (scrollableText)
		{
			processScrollableWidget(event);
		}
		if (event->type==SDL_KEYDOWN)
		{
			handleKey(event->key.keysym, true);
		}
		else if (event->type==SDL_KEYUP)
		{
			handleKey(event->key.keysym, false);
		}
		else if (event->type==SDL_MOUSEBUTTONDOWN)
		{
			int button=event->button.button;
			//int state=event->button.state;

			if (button==SDL_BUTTON_RIGHT)
			{
				handleRightClick();
			}
			else if (button==SDL_BUTTON_LEFT)
			{
				if (event->button.x>globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)
					handleMenuClick(event->button.x-globalContainer->gfx->getW()+RIGHT_MENU_WIDTH, event->button.y, event->button.button);
				else if (globalContainer->replaying && event->button.y >= REPLAY_BAR_Y)
					handleReplayProgressBarClick(event->button.x, event->button.y, event->button.button);
				else
					handleMapClick(event->button.x, event->button.y, event->button.button);
			}
			else if (button==SDL_BUTTON_MIDDLE)
			{
				if ((selectionMode==BUILDING_SELECTION) && (globalContainer->gfx->getW()-event->button.x<RIGHT_MENU_WIDTH))
				{
					Building* selBuild=selection.building;
					assert (selBuild);
//					selBuild->verbose=(selBuild->verbose+1)%5;
//					printf("building gid=(%d)\n", selBuild->gid);
//					if (selBuild->verbose==0)
//						printf(" verbose off\n");
//					else if (selBuild->verbose==1 || selBuild->verbose==2)
//						printf(" verbose global [%d]\n", selBuild->verbose&1);
//					else if (selBuild->verbose==3 || selBuild->verbose==4)
//						printf(" verbose local [%d]\n", selBuild->verbose&1);
//					else
//						assert(false);
//					printf(" pos=(%d, %d)\n", selBuild->posX, selBuild->posY);
//					printf(" dirtyLocalGradient=[%d, %d]\n", selBuild->dirtyLocalGradient[0], selBuild->dirtyLocalGradient[1]);
//					printf(" globalGradient=[%p, %p]\n", selBuild->globalGradient[0], selBuild->globalGradient[1]);
//					printf(" locked=[%d, %d]\n", selBuild->locked[0], selBuild->locked[1]);

				}
				else
				{
					// Enable panning
					panPushed=true;
					panMouseX=event->button.x;
					panMouseY=event->button.y;
					panViewX=viewportX;
					panViewY=viewportY;
				}
			}
			else if (button==4)
			{
				scrollWheelChanges += 1;

			}
			else if (button==5)
			{
				scrollWheelChanges -= 1;
			}
		}
		else if (event->type==SDL_MOUSEBUTTONUP)
		{
			int button=event->button.button;
			if ((button==SDL_BUTTON_LEFT) && (event->button.x < globalContainer->gfx->getW()-RIGHT_MENU_WIDTH))
			{
				if ((selectionMode==BUILDING_SELECTION) && selectionPushed && selection.building->type->isVirtual)
				{
					// update flag
					moveFlag(event->button.x, event->button.y, true);
				}
				// We send the order
				else if (selectionMode==BRUSH_SELECTION || selectionMode==TOOL_SELECTION)
				{
					toolManager.handleMouseUp(event->button.x, event->button.y, localTeamNo, viewportX, viewportY);
				}
			}
			miniMapPushed=false;
			selectionPushed=false;
			panPushed=false;
			// showUnitWorkingToBuilding=false;
		}
		else if (event->type==SDL_MOUSEWHEEL)
		{
			int factor = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
			scrollWheelChanges += event->wheel.y * factor;
		}
	}

	if (event->type==SDL_MOUSEMOTION)
	{
		handleMouseMotion(event->motion.x, event->motion.y, event->motion.state);
	}
	else if (event->type==SDL_WINDOWEVENT)
	{
		handleActivation(event->window.data1, event->window.data2);
	}
	else if (event->type==SDL_QUIT)
	{
		exitGlobCompletely=true;
		orderQueue.push_back(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
		flushOutgoingAndExit=true;
	}
	else if (event->type==SDL_WINDOWEVENT_RESIZED)
	{
		// FIXME: window resize is broken
		/*int newW=event->window.data1;
		int newH=event->window.data2;
		newW&=(~(0x1F));
		newH&=(~(0x1F));
		if (newW<640)
			newW=640;
		if (newH<480)
			newH=480;
		printf("New size : %dx%d\n", newW, newH);
		globalContainer->gfx->setRes(newW, newH);*/
	}
}

void GameGUI::handleActivation(Uint8 state, Uint8 gain)
{
	if (gain==0)
	{
		viewportSpeedX=viewportSpeedY=0;
	}
}

void GameGUI::handleRightClick(void)
{
	// We cycle between views:
	if (selectionMode==NO_SELECTION)
	{
		nextDisplayMode();
	}
	// We deselect all, we want no tools activated:
	else
	{
		clearSelection();
	}
}

void GameGUI::nextDisplayMode(void)
{
	if (globalContainer->replaying)
	{
		replayDisplayMode=ReplayDisplayMode((replayDisplayMode + 1) % RDM_NB_VIEWS);
		return;
	}

	int t=0;
	do
	{
		displayMode=DisplayMode((displayMode + 1) % NB_VIEWS);
		if ((t++)==4)
		{
			displayMode=NB_VIEWS;
			break;
		}
	} while ((1<<((int)displayMode)) & hiddenGUIElements);
}

void GameGUI::repairAndUpgradeBuilding(Building *building, bool repair, bool upgrade)
{
	BuildingType *buildingType = building->type;

	// building site can't be repaired nor upgraded
	if (buildingType->isBuildingSite)
		return;
	// we can upgrade or repair only building from our team
	if (building->owner->teamNumber != localTeamNo)
		return;
	int typeNum = building->typeNum + 1; //determines type of updated building
	int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum);
	int repairUnitWorking = defaultAssign.getDefaultAssignedUnits(building->typeNum - 1);
	int unitWorkingFuture = defaultAssign.getDefaultAssignedUnits(typeNum+1);
	if ((building->hp < buildingType->hpMax) && repair)
	{
		// repair
		if ((building->type->regenerationSpeed == 0) &&
			(building->isHardSpaceForBuildingSite(Building::REPAIR)) &&
			(localTeam->maxBuildLevel() >= buildingType->level))
			orderQueue.push_back(shared_ptr<Order>(new OrderConstruction(building->gid, repairUnitWorking, building->maxUnitWorkingLocal)));
	}
	else if (upgrade)
	{
		// upgrade
		if ((buildingType->nextLevel != -1) &&
			(building->isHardSpaceForBuildingSite(Building::UPGRADE)) &&
			(localTeam->maxBuildLevel() > buildingType->level))
			orderQueue.push_back(shared_ptr<Order>(new OrderConstruction(building->gid, unitWorking, unitWorkingFuture)));
	}
}

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
				if ((b->posXLocal==mapX) && (b->posYLocal==mapY))
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
							std::cerr << "Dump building " << selection.building->gid << " memory" << std::endl;
							selection.building->save(stream);
							selection.building->saveCrossRef(stream);
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
