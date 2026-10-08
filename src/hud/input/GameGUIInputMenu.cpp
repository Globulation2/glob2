#include "hive/HiveDialog.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <iostream>
#include <stdio.h>

#include <BinaryStream.h>
#include <FormatableString.h>
#include <FileManager.h>
#include <Stream.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "SaveSnapshot.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUITouch.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "LoadSaveDialog.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"

using std::shared_ptr;
using std::static_pointer_cast;

Glob2UI::InGameDialog *GameGUI::activeDialog() const
{
	if (gameMenuScreen)
		return gameMenuScreen.get();
	if (typingInputScreen)
		return typingInputScreen.get();
	return scrollableText.get();
}

void GameGUI::openDialog(InGameMenu menu, std::unique_ptr<Glob2UI::InGameDialog> dialog)
{
    // Panel icons must not destroy an operation before durable persistence ends.
    if (inGameMenu == IGM_SAVE && gameMenuScreen &&
        static_cast<LoadSaveDialog*>(gameMenuScreen.get())->isPersisting()) return;
	if (touch)
		touch->cancel(true);
	inGameMenu = menu;
	gameMenuScreen = std::move(dialog);
	if (gameMenuScreen && !globalContainer->runNoX)
		gameMenuScreen->attach(*globalContainer->gfx);
}

void GameGUI::closeDialog()
{
    if (inGameMenu == IGM_SAVE && gameMenuScreen &&
        static_cast<LoadSaveDialog*>(gameMenuScreen.get())->isPersisting()) return;
	inGameMenu = IGM_NONE;
	gameMenuScreen.reset();
}

void GameGUI::openMainMenu()
{
	auto menu = std::make_unique<InGameMainScreen>(globalContainer->replaying, !globalContainer->isViewingGame(), gamePaused);
	menu->setNetworked(networkMatch.active);
	if (networkMatch.active)
	{
		auto &strings = *Toolkit::getStringTable();
		const PauseState state = pauseState ? pauseState() : PauseState();
		std::string label;
		if (gamePaused)
			label = strings.getString("[resume game]");
		else if (!state.limited)
			label = strings.getString("[pause game]");
		else if (pauseAvailable())
			label = GAGCore::FormattableString(strings.getString("[pause game left %0]")).arg(state.pausesLeft);
		else
			label = strings.getString("[pause none left]");
		menu->setPauseOffer(label, pauseAvailable());
	}
	openDialog(IGM_MAIN, std::move(menu));
}

std::unique_ptr<Glob2UI::InGameDialog> GameGUI::makeLeaveConfirmation() const
{
	auto &strings = *Toolkit::getStringTable();
	const char *body = !networkMatch.online   ? "[leave lan body]"
					   : networkMatch.rated   ? "[leave rated body]"
					   : networkMatch.fromRoom ? "[leave room body]"
											   : "[leave casual body]";
	return std::make_unique<InGameConfirmScreen>(strings.getString(networkMatch.online ? "[leave match title]" : "[leave lan title]"),
												 strings.getString(body), strings.getString("[leave match confirm]"),
												 strings.getString("[keep playing]"));
}

void GameGUI::openChat()
{
	if (typingInputScreen)
		return;
	if (touch)
		touch->cancel(true);
	typingInputScreen = std::make_unique<InGameTextInput>();
	if (!globalContainer->runNoX)
		typingInputScreen->attach(*globalContainer->gfx);
}

void GameGUI::openCommander()
{
 if(!hive || !hive->available() || !globalContainer->settings.hiveMindEnabled || typingInputScreen)return;
 if(touch)touch->cancel(true);
 typingCommander=true;
 typingInputScreen=std::make_unique<InGameTextInput>(true);
 typingInputScreen->setText(hive->commandDraft);
 if(!globalContainer->runNoX)typingInputScreen->attach(*globalContainer->gfx);
}

void GameGUI::closeChat()
{
	typingInputScreen.reset();
	typingCommander=false;
}

void GameGUI::toggleHistory()
{
	if (scrollableText)
	{
		scrollableText.reset();
		return;
	}
	if (touch)
		touch->cancel(true);
	scrollableText.reset(messageManager.createScrollableHistoryScreen());
	if (!globalContainer->runNoX)
		scrollableText->attach(*globalContainer->gfx);
}

void GameGUI::saveGameTo(LoadSaveDialog &dialog)
{
    const std::string locationName=glob2GzipWritePath(dialog.getFileName());
    const std::string name=dialog.getName();
    if(!autosaveWriter) autosaveWriter=std::make_unique<BackgroundFileWriter>(Toolkit::getFileManager());
    dialog.beginPersistence(std::make_unique<SaveOperation>(*autosaveWriter,locationName,
        [this,name]{
            BackgroundFileWriter::Encode encode;
            const auto capture=[&]{encode=captureSave([&](OutputStream* stream,DeferredGameSHA1* sha){save(stream,name,sha);});};
            if(!parkForClient(capture)) capture();
            return encode;
        },
        [this,name]{defaultGameSaveName=name;ownGameSaveName=name;}));
}

// Feed the event to the open dialog and act on its result. Returns true when
// the dialog consumed the event or completed.
bool GameGUI::processGameMenu(SDL_Event *event)
{
    bool boundaryResult=false;
    if (gameMenuScreen && parkForClient([&]{boundaryResult=processGameMenu(event);})) return boundaryResult;
	if (!gameMenuScreen)
		return false;
	bool consumed = false;
	if (event && event->type != SDL_EVENT_USER)
		consumed = gameMenuScreen->eventLogical(*event);
	if (!gameMenuScreen->finished())
		return consumed;
	const int result = gameMenuScreen->result();
	switch (inGameMenu)
	{
		case IGM_MAIN:
		{
			switch (result)
			{
				case InGameMainScreen::HIVE_MIND:
					if(hive)openDialog(IGM_HIVE,std::make_unique<Hive::Dialog>(hive));
					return true;
				case InGameMainScreen::LOAD_GAME:
				{
					if (globalContainer->replaying)
						openDialog(IGM_LOAD, std::make_unique<LoadSaveDialog>("replays", "replay", true, Toolkit::getStringTable()->getString("[load replay]"), defaultGameSaveName.c_str(), glob2FilenameToName, glob2NameToFilename));
					else
						openDialog(IGM_LOAD, std::make_unique<LoadSaveDialog>("games", "game", true, Toolkit::getStringTable()->getString("[load game]"), defaultGameSaveName.c_str(), glob2FilenameToName, glob2NameToFilename));
					return true;
				}
				case InGameMainScreen::SAVE_GAME:
				{
					auto save = std::make_unique<LoadSaveDialog>("games", "game", false, Toolkit::getStringTable()->getString("[save game]"), defaultGameSaveName.c_str(), glob2FilenameToName, glob2NameToFilename);
					// Re-saving this session's own save replaces it without asking.
					if (!ownGameSaveName.empty()) save->allowOverwriteOf(ownGameSaveName);
					openDialog(IGM_SAVE, std::move(save));
					return true;
				}
				case InGameMainScreen::AI_TELEMETRY:
					openDialog(IGM_TELEMETRY, std::make_unique<InGameAITelemetryScreen>(this));
					return true;

				case InGameMainScreen::OPTIONS:
				{
					openDialog(IGM_OPTION, std::make_unique<InGameOptionScreen>(this));
					return true;
				}
				case InGameMainScreen::PAUSE_GAME:
				{
					closeDialog();
					if (globalContainer->replaying)
						gamePaused = !gamePaused;
					else if (!globalContainer->isViewingGame())
						requestPause(!gamePaused);
					return true;
				}
				case InGameMainScreen::RETURN_GAME:
				{
					closeDialog();
					return true;
				}
				case InGameMainScreen::QUIT_GAME:
				{
					// Leaving a networked match costs the match: say so first.
					if (networkMatch.active)
					{
						openDialog(IGM_CONFIRM_LEAVE, makeLeaveConfirmation());
						return true;
					}
					closeDialog();
					enqueueOrder(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
					flushOutgoingAndExit=true;
					return true;
				}
				default:
				return false;
			}
		}

		case IGM_CONFIRM_LEAVE:
		{
			closeDialog();
			if (result == InGameConfirmScreen::CONFIRM)
			{
				enqueueOrder(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
				flushOutgoingAndExit = true;
			}
			else
				openMainMenu();
			return true;
		}

		case IGM_ALLIANCE:
		{
            if (!drawnScene().world.session) { closeDialog(); return true; }
			if (result != InGameAllianceScreen::OK)
				return false;
			auto *alliance = static_cast<InGameAllianceScreen *>(gameMenuScreen.get());
			Uint32 playerMask[5];
			Uint32 teamMask[5];
			playerMask[0]=alliance->getAlliedMask();
			playerMask[1]=alliance->getEnemyMask();
			playerMask[2]=alliance->getExchangeVisionMask();
			playerMask[3]=alliance->getFoodVisionMask();
			playerMask[4]=alliance->getOtherVisionMask();
			teamMask[0]=teamMask[1]=teamMask[2]=teamMask[3]=teamMask[4]=0;

			// mask are for players, we need to convert them to team.
			for (size_t pi=0; pi<drawnScene().world.session->players.size(); pi++)
			{
				int otherTeam=drawnScene().world.session->players[pi].teamNumber;
				for (int mi=0; mi<5; mi++)
				{
					if (playerMask[mi]&(1<<pi))
					{
						// player is set, set team
						teamMask[mi]|=(Team::teamNumberToMask(otherTeam));
					}
				}
			}

			// we have a special cases for uncontrolled Teams:
			// FIXME : remove this
			for (int ti=0; ti<drawnScene().entities.teamCount; ti++)
				if (drawnScene().entities.teams[ti].playersMask==0)
					teamMask[1]|=(1<<ti); // we want to hit them.

			enqueueOrder(shared_ptr<Order>(new SetAllianceOrder(localTeamNo,
				teamMask[0], teamMask[1], teamMask[2], teamMask[3], teamMask[4])));
			chatMask=alliance->getChatMask();
			closeDialog();
			return true;
		}

		case IGM_HIVE:
			closeDialog();return true;

		case IGM_TELEMETRY:
			closeDialog();
			return true;

		case IGM_OPTION:
		{
			if (result == InGameOptionScreen::OK)
			{
				closeDialog();
				return true;
			}
			return false;
		}

		case IGM_OBJECTIVES:
		{
			if (result == InGameObjectivesScreen::OK)
			{
				closeDialog();
				return true;
			}
			return false;
		}

		case IGM_LOAD:
		case IGM_SAVE:
		{
			auto *files = static_cast<LoadSaveDialog *>(gameMenuScreen.get());
			switch (result)
			{
				case LoadSaveDialog::OK:
				{
					std::string locationName = files->getFileName();
					if (inGameMenu==IGM_LOAD)
					{
						toLoadGameFileName = locationName;
						enqueueOrder(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
						flushOutgoingAndExit=true;
						closeDialog();
					}
					else
						saveGameTo(*files);
					return true;
				}

				case LoadSaveDialog::CANCEL:
				closeDialog();
				return true;

				default:
				return false;
			}
		}

		case IGM_END_OF_GAME:
		{
			switch (result)
			{
				case InGameEndOfGameScreen::QUIT:
				enqueueOrder(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
				flushOutgoingAndExit=true;
				closeDialog();
				return true;

				case InGameEndOfGameScreen::CONTINUE:
				closeDialog();
				return true;

				case InGameEndOfGameScreen::WATCH_AGAIN:
				assert(globalContainer->replaying);
				closeDialog();
				toLoadGameFileName = globalContainer->replayFileName;
				enqueueOrder(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
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
