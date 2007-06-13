/*
  Copyright (C) 2007 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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
#include "YOGScreen.h"
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

MultiplayerGameScreen::MultiplayerGameScreen(boost::shared_ptr<MultiplayerGame> game, boost::shared_ptr<NetTextMessageHandler> textMessage)
	: game(game), textMessage(textMessage)
{
	// we don't want to add AI_NONE
	for (size_t i=1; i<AI::SIZE; i++)
	{
		TextButton *button = new TextButton(20, 330-30*(i-1), 180, 20, ALIGN_RIGHT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[AI]", i), ADD_AI+i);
		addWidget(button);
		addAI.push_back(button);
	}
	
	startButton=new TextButton(20, 385, 180, 40, ALIGN_RIGHT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Start]"), START);
	addWidget(new TextButton(20, 435, 180, 40, ALIGN_RIGHT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL));

	startButton->visible=false;
	addWidget(startButton);
	notReadyText=new Text(20, 385, ALIGN_RIGHT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[not ready]"), 180, 30);
	notReadyText->visible=true;
	addWidget(notReadyText);
	gameFullText=new Text(20, 335, ALIGN_RIGHT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[game full]"), 180, 30);
	gameFullText->visible=false;
	addWidget(gameFullText);


	addWidget(new Text(0, 5, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[awaiting players]")));

	for (int i=0; i<MAX_NUMBER_OF_PLAYERS; i++)
	{
		int dx=320*(i/8);
		int dy=20*(i%8);
		color[i]=new ColorButton(22+dx, 42+dy, 16, 16, ALIGN_SCREEN_CENTERED, ALIGN_LEFT, COLOR_BUTTONS+i);
		for (int j=0; j<game->getMapHeader().getNumberOfTeams(); j++)
			color[i]->addColor(game->getMapHeader().getBaseTeam(j).colorR, game->getMapHeader().getBaseTeam(j).colorG, game->getMapHeader().getBaseTeam(j).colorB);
		addWidget(color[i]);
		text[i]=new Text(42+dx, 40+dy, ALIGN_SCREEN_CENTERED, ALIGN_LEFT, "standard",  Toolkit::getStringTable()->getString("[open]"));
		addWidget(text[i]);
		kickButton[i]=new TextButton(220+dx, 42+dy, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[close]"), CLOSE_BUTTONS+i);
		addWidget(kickButton[i]);

		wasSlotUsed[i]=false;

		text[i]->visible=false;
		color[i]->visible=false;
		kickButton[i]->visible=false;
	}
	startTimer=new Text(20, 360, ALIGN_RIGHT, ALIGN_TOP, "standard", "");
	addWidget(startTimer);

	chatWindow=new TextArea(20, 210, 220, 65, ALIGN_FILL, ALIGN_FILL, "standard");
	addWidget(chatWindow);
	textInput=new TextInput(20, 20, 220, 25, ALIGN_FILL, ALIGN_BOTTOM, "standard", "", true, 256);
	addWidget(textInput);
	
	updateJoinedPlayers();
	
	textMessage->addTextMessageListener(this);
}

MultiplayerGameScreen::~MultiplayerGameScreen()
{
	textMessage->removeTextMessageListener(this);
}

void MultiplayerGameScreen::onTimer(Uint32 tick)
{
	game->update();
	if(game->hasPlayersChanged())
	{
		updateJoinedPlayers();
	}
	if(game->isGameReadyToStart())
	{
		if(game->getGameJoinCreationState() == MultiplayerGame::HostingGame)
			startButton->visible=true;
		else
			startButton->visible=false;
		notReadyText->visible=false;
	}
	else
	{
		startButton->visible=false;
		notReadyText->visible=true;
	}

	if(game->getGameJoinCreationState() == MultiplayerGame::NothingYet)
	{
		endExecute(Cancelled);
	}

	textMessage->update();
}



void MultiplayerGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1 == START)
		{
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
	}
	else if (action==BUTTON_STATE_CHANGED)
	{
		game->changeTeam(par1 - COLOR_BUTTONS, par2);
	}
	else if (action==TEXT_VALIDATED)
	{
		game->sendMessage(textInput->getText());
		boost::shared_ptr<IRC> irc = textMessage->getIRC();
		if(irc)
		{
			irc->sendCommand(textInput->getText());
		}
		textInput->setText("");
	}
}



void MultiplayerGameScreen::handleTextMessage(const std::string& message, NetTextMessageType type)
{
	chatWindow->addText(message);
	chatWindow->addText("\n");
	chatWindow->scrollToBottom();
}



void MultiplayerGameScreen::updateJoinedPlayers()
{
	GameHeader& gh = game->getGameHeader();
	MapHeader& mh = game->getMapHeader();
	for (int i=0; i<MAX_NUMBER_OF_PLAYERS; i++)
	{
		color[i]->clearColors();
		for (int j=0; j<mh.getNumberOfTeams(); j++)
			color[i]->addColor(mh.getBaseTeam(j).colorR, mh.getBaseTeam(j).colorG, mh.getBaseTeam(j).colorB);

		BasePlayer& bp = gh.getBasePlayer(i);
		
		color[i]->setSelectedColor(bp.teamNumber);
		
		if(bp.type != BasePlayer::P_NONE)
		{
			text[i]->visible=true;
			text[i]->setText(bp.name);
			color[i]->visible=true;
			if(game->getGameJoinCreationState() == MultiplayerGame::HostingGame)
				kickButton[i]->visible=true;
			else
				kickButton[i]->visible=false;
		}
		else if(i < mh.getNumberOfTeams())
		{
			text[i]->visible=true;
			text[i]->setText(Toolkit::getStringTable()->getString("[open]"));
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


