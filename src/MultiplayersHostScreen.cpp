/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#include "MultiplayersHostScreen.h"
#include "GlobalContainer.h"
#include "GAG.h"
#include "YOGScreen.h"
#include "Utilities.h"

MultiplayersHostScreen::MultiplayersHostScreen(SessionInfo *sessionInfo, bool shareOnYOG)
{	
	addAI=new TextButton(440, 345, 180, 25, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[Add AI]"), ADD_AI);
	startButton=new TextButton(440, 390, 180, 25, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[Start]"), START);
	addWidget(new TextButton(440, 435, 180, 25, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[Cancel]"), CANCEL));
	addWidget(addAI);

	startButton->visible=false;
	addWidget(startButton);
	notReadyText=new Text(440, 390, globalContainer->menuFont, globalContainer->texts.getString("[not ready]"), 180, 25);
	notReadyText->visible=true;
	addWidget(notReadyText);
	gameFullText=new Text(2, 420, globalContainer->standardFont, globalContainer->texts.getString("[game full]"), 180, 40);
	gameFullText->visible=false;
	addWidget(gameFullText);
	savedSessionInfo=NULL;
	
	if (!sessionInfo->fileIsAMap)
	{
		// We remember the sessionInfo at saving time.
		// This may be used to match player's current's names with old player's names.
		savedSessionInfo=new SessionInfo(*sessionInfo);
		// We erase players info.
		sessionInfo->numberOfPlayer=0;
	}
	multiplayersHost=new MultiplayersHost(sessionInfo, shareOnYOG, savedSessionInfo);
	multiplayersJoin=NULL;
	this->shareOnYOG=shareOnYOG;

	addWidget(new Text(20, 5, globalContainer->menuFont, globalContainer->texts.getString("[awaiting players]"), 600, 0));

	for (int i=0; i<MAX_NUMBER_OF_PLAYERS; i++)
	{
		int j;
		color[i]=new ColorButton(22, 42+i*20, 16, 16, COLOR_BUTTONS+i);
		for (j=0; j<sessionInfo->numberOfTeam; j++)
			color[i]->addColor(sessionInfo->team[j].colorR, sessionInfo->team[j].colorG, sessionInfo->team[j].colorB);
		addWidget(color[i]);
		text[i]=new Text(42, 42+i*20, globalContainer->standardFont,  globalContainer->texts.getString("[open]"));
		addWidget(text[i]);
		kickButton[i]=new TextButton(520, 42+i*20, 100, 18, NULL, -1, -1, globalContainer->standardFont, globalContainer->texts.getString("[close]"), CLOSE_BUTTONS+i);
		addWidget(kickButton[i]);
		
		wasSlotUsed[i]=false;
		
		text[i]->visible=false;
		color[i]->visible=false;
		kickButton[i]->visible=false;
	}
	startTimer=new Text(20, 400, globalContainer->standardFont, "");
	addWidget(startTimer);

	timeCounter=0;
	
	chatWindow=new TextArea(20, 210, 400, 205, globalContainer->standardFont);
	addWidget(chatWindow);
	textInput=new TextInput(20, 435, 400, 25, globalContainer->standardFont, "", true, 256);
	addWidget(textInput);
}

MultiplayersHostScreen::~MultiplayersHostScreen()
{
	delete multiplayersHost;
	if (multiplayersJoin)
		delete multiplayersJoin;
	if (savedSessionInfo)
		delete savedSessionInfo;
}

void MultiplayersHostScreen::onTimer(Uint32 tick)
{
	multiplayersHost->onTimer(tick, multiplayersJoin);
	
	// TODO : don't update this every step
	for (int i=0; i<MAX_NUMBER_OF_PLAYERS; i++)
	{
		if (multiplayersHost->sessionInfo.players[i].netState>BasePlayer::PNS_BAD)
		{
			int teamNumber;
			char playerInfo[128];
			char shownInfo[128];
			multiplayersHost->sessionInfo.getPlayerInfo(i, &teamNumber, playerInfo, savedSessionInfo, 128);
			if (multiplayersHost->playerFileTra[i].wantsFile && !multiplayersHost->playerFileTra[i].receivedFile)
			{
				int percent=(100*multiplayersHost->playerFileTra[i].unreceivedIndex)/multiplayersHost->fileSize;
				snprintf(shownInfo, 128, "%s (%d)", playerInfo, percent);
			}
			else
				strncpy(shownInfo, playerInfo, 128);

			if (strncmp(shownInfo, text[i]->getText(), 128))
			{
				text[i]->setText(shownInfo);
				color[i]->setSelectedColor(teamNumber);
			}
			if (!wasSlotUsed[i])
			{
				text[i]->show();
				color[i]->show();
				kickButton[i]->show();
			}
			wasSlotUsed[i]=true;
		}
		else
		{
			if (wasSlotUsed[i])
			{

				text[i]->hide();
				color[i]->hide();
				kickButton[i]->hide();
				wasSlotUsed[i]=false;

				//text[i]->setText(globalContainer->texts.getString("[open]"));
				//color[i]->setSelectedColor(0);
			}
		}
	}

	if (multiplayersJoin==NULL)
	{
		multiplayersJoin=new MultiplayersJoin(shareOnYOG);
		assert(BasePlayer::MAX_NAME_LENGTH==32);
		strncpy(multiplayersJoin->playerName, globalContainer->userName, 32);
		multiplayersJoin->playerName[31]=0;
		strncpy(multiplayersJoin->serverNickName, globalContainer->userName, 32);
		multiplayersJoin->serverNickName[31]=0;
		
		strncpy(multiplayersJoin->serverName, "localhost", 256);
		multiplayersJoin->serverIP.host=0x7F000001;
		if (multiplayersHost->serverIP.host)
			multiplayersJoin->serverIP.port=SDL_SwapBE16(multiplayersHost->serverIP.host);
		else
			multiplayersJoin->serverIP.port=SDL_SwapBE16(GAME_SERVER_PORT);
		multiplayersJoin->tryConnection(true);
	}

	if ((multiplayersJoin)&&(!multiplayersJoin->kicked))
		multiplayersJoin->onTimer(tick);

	if (((timeCounter++ % 10)==0)&&(multiplayersHost->hostGlobalState>=MultiplayersHost::HGS_PLAYING_COUNTER))
	{
		char s[128];
		snprintf(s, sizeof(s), "%s%d", globalContainer->texts.getString("[STARTING GAME ...]"), multiplayersHost->startGameTimeCounter/20);
		printf("s=%s.\n", s);
		startTimer->setText(s);
	}

	if (multiplayersHost->hostGlobalState>=MultiplayersHost::HGS_ALL_PLAYERS_CROSS_CONNECTED_AND_HAVE_FILE)
	{
		if (notReadyText->visible)
		{
			notReadyText->hide();
			startButton->show();
		}
	}
	else
	{
		if (!notReadyText->visible)
		{
			startButton->hide();
			notReadyText->show();
		}
	}
	
	if (multiplayersHost->receivedMessages.size())
		for (std::list<MultiplayersCrossConnectable::Message>::iterator mit=multiplayersHost->receivedMessages.begin(); mit!=multiplayersHost->receivedMessages.end(); ++mit)
			if (!mit->guiPainted)
			{
				chatWindow->addText("<");
				chatWindow->addText(mit->userName);
				chatWindow->addText("> ");
				chatWindow->addText(mit->text);
				chatWindow->addText("\n");
				chatWindow->scrollToBottom();
				mit->guiPainted=true;
			}

	if ((multiplayersHost->hostGlobalState>=MultiplayersHost::HGS_GAME_START_SENDED)&&(multiplayersHost->startGameTimeCounter<0))
		endExecute(STARTED);
}

void MultiplayersHostScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		switch (par1)
		{
		case START :
			multiplayersHost->startGame();
		break;
		case CANCEL :
			multiplayersHost->stopHosting();
			endExecute(par1);
		break;
		case ADD_AI :
			if ((multiplayersHost->hostGlobalState<MultiplayersHost::HGS_GAME_START_SENDED)
				&&(multiplayersHost->sessionInfo.numberOfPlayer<MAX_NUMBER_OF_PLAYERS))
			{
				multiplayersHost->addAI();
				if (multiplayersHost->sessionInfo.numberOfPlayer>=16)
				{
					addAI->hide();
					gameFullText->show();
				}
			}
		break;
		case -1:
			multiplayersHost->stopHosting();
			endExecute(par1);
		break;
		default:
		{
			if ((par1>=CLOSE_BUTTONS)&&(par1<CLOSE_BUTTONS+MAX_NUMBER_OF_PLAYERS))
			{
				multiplayersHost->kickPlayer(par1-CLOSE_BUTTONS);
				if (multiplayersHost->sessionInfo.numberOfPlayer<16)
				{
					gameFullText->hide();
					addAI->show();
				}
			}
		}
		break;
		}
	}
	else if (action==BUTTON_STATE_CHANGED)
	{
		if ((par1>=COLOR_BUTTONS)&&(par1<COLOR_BUTTONS+MAX_NUMBER_OF_PLAYERS))
				multiplayersHost->switchPlayerTeam(par1-COLOR_BUTTONS);
	}
	else if (action==TEXT_VALIDATED)
	{
		multiplayersHost->sendMessage(textInput->text);
		textInput->setText("");
	}
}