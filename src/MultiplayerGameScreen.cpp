/*
  Copyright (C) 2007 Bradley Arsenault

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

#include "MultiplayerGameScreen.h"
#include "YOGClientLobbyScreen.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "NetConsts.h"
#include "Order.h"

#include <FormatableString.h>
#include <GUIText.h>
#include <GUITextArea.h>
#include <GUITextInput.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>

#include "IRC.h"
#include "YOGMessage.h"
#include "CustomGameOtherOptions.h"

MultiplayerGameScreen::MultiplayerGameScreen(TabScreen* parent, boost::shared_ptr<MultiplayerGame> game, boost::shared_ptr<YOGClient> client, boost::shared_ptr<IRCTextMessageHandler> ircChat)
	: TabScreenWindow(parent, Toolkit::getStringTable()->getString("[Game]")), game(game), gameChat(new YOGClientChatChannel(static_cast<unsigned int>(-1), client)), ircChat(ircChat)
{
	// we don't want to add AI_NONE
	for (size_t i=1; i<AI::SIZE; i++)
	{
		if(game->getMultiplayerMode() == MultiplayerGame::HostingGame)
		{
			TextButton *button = new TextButton(20, 400-30*(i-1), 180, 20, ALIGN_RIGHT, ALIGN_TOP, "standard", AI::getAIText(i).c_str(), ADD_AI+i);
			button->visible = false;
			addWidget(button);
			addAI.push_back(button);
		}
	}
	
	bool isHost = true;
	if(game->getMultiplayerMode() == MultiplayerGame::JoinedGame)
		isHost = false;
	
	startButton=new TextButton(20, 455, 180, 40, ALIGN_RIGHT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Start]"), START);
	startButton->visible=false;
	addWidget(startButton);

	gameStartWaitingText=new Text(20, (isHost ? 455 : 395), ALIGN_RIGHT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Waiting]"), 180, 30);
	addWidget(gameStartWaitingText);
	gameStartWaitingText->visible = false;

	notReadyText=new Text(20, (isHost ? 455 : 395), ALIGN_RIGHT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[not ready]"), 180, 30);
	notReadyText->visible=isActivated();
	addWidget(notReadyText);

	otherOptions = new TextButton(20, (isHost ? 425 : 475), 180, 20, ALIGN_RIGHT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Other Options]"), OTHEROPTIONS);
	addWidget(otherOptions);
	otherOptions->visible=false;
	
	if(game->getMultiplayerMode() == MultiplayerGame::JoinedGame)
	{
		isReadyText = new Text(50, 440, ALIGN_RIGHT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[ready?]"));
		isReady = new OnOffButton(20, 445, 20, 20, ALIGN_RIGHT, ALIGN_TOP, false, READY);
		addWidget(isReadyText);
		addWidget(isReady);
	}

	const char * cancelText;
	if(game->getMultiplayerMode() == MultiplayerGame::HostingGame)
	{
		cancelText = Toolkit::getStringTable()->getString("[Cancel]");
	}
	else
	{
		cancelText = Toolkit::getStringTable()->getString("[Leave Game]");
	}
	cancelButton = new TextButton(20, 505, 180, 40, ALIGN_RIGHT, ALIGN_TOP, "menu", cancelText, CANCEL);
	cancelButton->visible=false;
	addWidget(cancelButton);

	addWidget(new Text(0, 5, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[awaiting players]")));

	for (int i=0; i<MAX_NUMBER_OF_PLAYERS; i++)
	{
		int dx=320*(i/8);
		int dy=20*(i%8);
		color[i]=new ColorButton(22+dx, 112+dy, 16, 16, ALIGN_SCREEN_CENTERED, ALIGN_LEFT, COLOR_BUTTONS+i);
		for (int j=0; j<game->getMapHeader().getNumberOfTeams(); j++)
			color[i]->addColor(game->getMapHeader().getBaseTeam(j).color);
		addWidget(color[i]);
		text[i]=new Text(42+dx, 110+dy, ALIGN_SCREEN_CENTERED, ALIGN_LEFT, "standard",  Toolkit::getStringTable()->getString("[open]"));
		addWidget(text[i]);
		kickButton[i]=new TextButton(220+dx, 112+dy, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[kick]"), CLOSE_BUTTONS+i);
		addWidget(kickButton[i]);

		wasSlotUsed[i]=false;

		text[i]->visible=false;
		color[i]->visible=false;
		kickButton[i]->visible=false;
	}
	percentDownloaded=new Text(20, 420, ALIGN_RIGHT, ALIGN_TOP, "menu", "");
	addWidget(percentDownloaded);

	chatWindow=new TextArea(20, 280, 220, 135, ALIGN_FILL, ALIGN_FILL, "standard");
	addWidget(chatWindow);
	textInput=new TextInput(20, 90, 220, 25, ALIGN_FILL, ALIGN_BOTTOM, "standard", "", true, 256);
	addWidget(textInput);
	
	updateJoinedPlayers();
	
	game->addEventListener(this);
	gameChat->addListener(this);
}

MultiplayerGameScreen::~MultiplayerGameScreen()
{
	game->removeEventListener(this);
	gameChat->removeListener(this);
}

void MultiplayerGameScreen::onTimer(Uint32 tick)
{
	TabScreenWindow::onTimer(tick);
	game->update();
	if(ircChat)
		ircChat->update();
}



void MultiplayerGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	TabScreenWindow::onAction(source, action, par1, par2);
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1 == START)
		{
			//MultiplayerGame will send an event when the game is over
			updateVisibleButtons();
			game->startGame();
		}
		else if (par1 == CANCEL)
		{
			game->leaveGame();
			endExecute(Cancelled);
		}
		else if ((par1 >= ADD_AI) && (par1 < ADD_AI + AI::SIZE))
		{
			game->addAIPlayer((AI::ImplementitionID)(par1-ADD_AI));
		}
		else if ((par1>=CLOSE_BUTTONS)&&(par1<CLOSE_BUTTONS+MAX_NUMBER_OF_PLAYERS))
		{
			game->kickPlayer(par1 - CLOSE_BUTTONS);
		}
		else if(par1 == OTHEROPTIONS)
		{
			bool readOnly = true;
			if(game->getMultiplayerMode() == MultiplayerGame::HostingGame)
				readOnly = false;
			CustomGameOtherOptions settings(game->getGameHeader(), game->getMapHeader(), readOnly);
			int rc = settings.execute(globalContainer->gfx, 40);
			game->updateGameHeader();
		}
	}
	else if (action==BUTTON_STATE_CHANGED)
	{
		if(par1 == READY)
			game->setHumanReady(isReady->getState());
		else if(par1 > COLOR_BUTTONS)
			game->changeTeam(par1 - COLOR_BUTTONS, par2);
	}
	else if (action==TEXT_VALIDATED)
	{
		if(textInput->getText() != "")
		{
			boost::shared_ptr<YOGMessage> message(new YOGMessage(textInput->getText(), game->getUsername(), YOGNormalMessage));
			gameChat->sendMessage(message);
			textInput->setText("");
		}
	}
}



void MultiplayerGameScreen::recieveTextMessage(boost::shared_ptr<YOGMessage> message)
{
	chatWindow->addText(message->formatForReading());
	chatWindow->addText("\n");
	chatWindow->scrollToBottom();
}



void MultiplayerGameScreen::handleMultiplayerGameEvent(boost::shared_ptr<MultiplayerGameEvent> event)
{
	Uint8 type = event->getEventType();
	if(type == MGEPlayerListChanged)
	{
		updateJoinedPlayers();
		updateVisibleButtons();
	}
	else if(type == MGEReadyToStart)
	{
		updateVisibleButtons();
	}
	else if(type == MGENotReadyToStart)
	{
		updateVisibleButtons();
	}
	else if(type == MGEGameStarted)
	{
		if(ircChat)
			ircChat->stopIRC();
	}
	else if(type == MGEGameExit)
	{
		if(ircChat)
			ircChat->startIRC(game->getUsername());
		endExecute(-1);
		game->leaveGame();
	}
	else if(type == MGEGameEndedNormally)
	{
		if(ircChat)
			ircChat->startIRC(game->getUsername());
		endExecute(StartedGame);
		game->leaveGame();
	}
	else if(type == MGEGameRefused)
	{
		endExecute(GameRefused);
	}
	else if(type == MGEKickedByHost)
	{
		endExecute(Kicked);
	}
	else if(type == MGEHostCancelledGame)
	{
		endExecute(GameCancelled);
	}
	else if(type == MGEServerDisconnected)
	{
		endExecute(ServerDisconnected);
	}
	else if(type == MGEGameStartRefused)
	{
		updateVisibleButtons();
	}
	else if(type == MGEGameHostJoinAccepted)
	{
		updateVisibleButtons();
	}
	else if(type == MGEDownloadPercentUpdate)
	{
		shared_ptr<MGDownloadPercentUpdate> info = static_pointer_cast<MGDownloadPercentUpdate>(event);
		if(info->getPercentFinished() != 100)
		{
			percentDownloaded->setText(FormatableString(Toolkit::getStringTable()->getString("[downloaded %0]")).arg((int)info->getPercentFinished()));
		}
		updateVisibleButtons();
	}
	else if(type == MGEPlayerReadyStatusChanged)
	{
		shared_ptr<MGPlayerReadyStatusChanged> info = static_pointer_cast<MGPlayerReadyStatusChanged>(event);
		GameHeader& gh = game->getGameHeader();
		for (int i=0; i<MAX_NUMBER_OF_PLAYERS; i++)
		{
			BasePlayer& bp = gh.getBasePlayer(i);
			if(bp.playerID == info->getPlayerID())
			{
				if(!game->isReadyToStart(bp.playerID))
				{
					text[i]->setStyle(Font::Style(Font::STYLE_NORMAL, Color(255,64,64)));
				}
				else
				{
					text[i]->setStyle(Font::Style());
				}
			}
		}
		updateVisibleButtons();
	}
}



void MultiplayerGameScreen::updateJoinedPlayers()
{
	GameHeader& gh = game->getGameHeader();
	MapHeader& mh = game->getMapHeader();
	for (int i=0; i<MAX_NUMBER_OF_PLAYERS; i++)
	{
		color[i]->clearColors();
		for (int j=0; j<mh.getNumberOfTeams(); j++)
			color[i]->addColor(mh.getBaseTeam(j).color);
			
		if(game->getMultiplayerMode() == MultiplayerGame::JoinedGame)
			color[i]->setClickable(false);
		else
			color[i]->setClickable(true);

		BasePlayer& bp = gh.getBasePlayer(i);
		
		color[i]->setSelectedColor(bp.teamNumber);
		
		if(bp.type != BasePlayer::P_NONE)
		{
			text[i]->visible=isActivated();
			text[i]->setText(bp.name);
			color[i]->visible=isActivated();
			if(game->getMultiplayerMode() == MultiplayerGame::HostingGame && bp.number != game->getLocalPlayerNumber())
				kickButton[i]->visible=isActivated();
			else
				kickButton[i]->visible=false;
			if(!game->isReadyToStart(bp.playerID))
			{
				text[i]->setStyle(Font::Style(Font::STYLE_NORMAL, Color(255,64,64)));
			}
			else
			{
				text[i]->setStyle(Font::Style());
			}
		}
		else if(i < mh.getNumberOfTeams())
		{
			text[i]->visible=isActivated();
			text[i]->setText(Toolkit::getStringTable()->getString("[open]"));
			text[i]->setStyle(Font::Style());
			color[i]->visible=false;
			kickButton[i]->visible=false;
		}
		else
		{
			text[i]->visible=false;
			color[i]->visible=false;
			kickButton[i]->visible=false;
		}
	}
}


void MultiplayerGameScreen::updateVisibleButtons()
{
	if(game->isGameStarting())
	{
		gameStartWaitingText->visible=isActivated();
		startButton->visible=false;
		otherOptions->visible=false;
	}
	else
	{
		gameStartWaitingText->visible=false;
		startButton->visible=isActivated();
		otherOptions->visible=isActivated();
	}
	
	if(game->isGameReadyToStart())
	{
		if(game->getMultiplayerMode() == MultiplayerGame::HostingGame)
		{
			if(game->isGameStarting())
			{
				startButton->visible=false;
			}
			else
			{
				startButton->visible=isActivated();
			}
		}
		else
		{
			startButton->visible=false;
		}
		notReadyText->visible=false;
	}
	else
	{
		startButton->visible=false;
		notReadyText->visible=isActivated();
	}
	
	if(game->getGameJoinCreationState() == MultiplayerGame::ReadyToGo)
	{
		cancelButton->visible=isActivated();
		gameChat->setChannelID(game->getChatChannel());
		if(game->getMultiplayerMode() == MultiplayerGame::HostingGame)
		{
			for (size_t i=1; i<AI::SIZE; i++)
			{
				addAI[i-1]->visible=isActivated();
			}
		}
	}
	else
	{	
		cancelButton->visible=false;
		gameChat->setChannelID(game->getChatChannel());
		if(game->getMultiplayerMode() == MultiplayerGame::HostingGame)
		{
			for (size_t i=1; i<AI::SIZE; i++)
			{
				addAI[i-1]->visible=false;
			}
		}
	}
	if(game->percentageDownloadFinished() != 100)
	{
		percentDownloaded->visible=isActivated();
	}
	else
	{
		percentDownloaded->visible=false;
	}
}


void MultiplayerGameScreen::onActivated()
{
	updateJoinedPlayers();
	updateVisibleButtons();
}

