// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <iostream>


#include <FileManager.h>
#include <StringTable.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUILoadSave.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"

using std::shared_ptr;
using std::static_pointer_cast;

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
					inGameMenu=IGM_LOAD;
					if (globalContainer->replaying)
						gameMenuScreen.reset(new LoadSaveScreen("replays", "replay", true, std::string(Toolkit::getStringTable()->getString("[load replay]")), defaultGameSaveName.c_str(), glob2FilenameToName, glob2NameToFilename));
					else
						gameMenuScreen.reset(new LoadSaveScreen("games", "game", true, std::string(Toolkit::getStringTable()->getString("[load game]")), defaultGameSaveName.c_str(), glob2FilenameToName, glob2NameToFilename));
					return true;
				}
				break;
				case InGameMainScreen::SAVE_GAME:
				{
					inGameMenu=IGM_SAVE;
					gameMenuScreen.reset(new LoadSaveScreen("games", "game", false, std::string(Toolkit::getStringTable()->getString("[save game]")), defaultGameSaveName.c_str(), glob2FilenameToName, glob2NameToFilename));
					return true;
				}
				break;
				case InGameMainScreen::OPTIONS:
				{
					inGameMenu=IGM_OPTION;
					gameMenuScreen.reset(new InGameOptionScreen(this));
					return true;
				}
				break;
				case InGameMainScreen::RETURN_GAME:
				{
					inGameMenu=IGM_NONE;
					gameMenuScreen.reset();
					return true;
				}
				break;
				case InGameMainScreen::QUIT_GAME:
				{
					inGameMenu=IGM_NONE;
					gameMenuScreen.reset();
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
					playerMask[0]=((InGameAllianceScreen *)gameMenuScreen.get())->getAlliedMask();
					playerMask[1]=((InGameAllianceScreen *)gameMenuScreen.get())->getEnemyMask();
					playerMask[2]=((InGameAllianceScreen *)gameMenuScreen.get())->getExchangeVisionMask();
					playerMask[3]=((InGameAllianceScreen *)gameMenuScreen.get())->getFoodVisionMask();
					playerMask[4]=((InGameAllianceScreen *)gameMenuScreen.get())->getOtherVisionMask();
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
					chatMask=((InGameAllianceScreen *)gameMenuScreen.get())->getChatMask();
					inGameMenu=IGM_NONE;
					gameMenuScreen.reset();
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
				gameMenuScreen.reset();
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
				gameMenuScreen.reset();
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
					std::string locationName=((LoadSaveScreen *)gameMenuScreen.get())->getFileName();
					if (inGameMenu==IGM_LOAD)
					{
						toLoadGameFileName = locationName;
						orderQueue.push_back(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
						flushOutgoingAndExit=true;
					}
					else
					{
						defaultGameSaveName=((LoadSaveScreen *)gameMenuScreen.get())->getName();
						OutputStream *stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(locationName));
						if (stream->isEndOfStream())
						{
							std::cerr << "GGU : Can't save map " << locationName << std::endl;
						}
						else
						{
							const std::string name = ((LoadSaveScreen *)gameMenuScreen.get())->getName();
							assert(name.size());
							save(stream, name);
						}
						delete stream;
					}
				}

				case LoadSaveScreen::CANCEL:
				inGameMenu=IGM_NONE;
				gameMenuScreen.reset();
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
				gameMenuScreen.reset();
				return true;

				case InGameEndOfGameScreen::WATCH_AGAIN:
				assert(globalContainer->replaying);
				inGameMenu=IGM_NONE;
				gameMenuScreen.reset();
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
