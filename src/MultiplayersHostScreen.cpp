/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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
#include "YOGScreen.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "SessionConnection.h"
#include "MultiplayersHost.h"
#include "MultiplayersJoin.h"
#include <GUIText.h>
#include <GUITextArea.h>
#include <GUITextInput.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>

MultiplayersHostScreen::MultiplayersHostScreen(SessionInfo *sessionInfo, bool shareOnYOG)
{
	addAINumbi=new TextButton(440, 285, 180, 30, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[AINumbi]"), ADD_AI_NUMBI);
	addWidget(addAINumbi);
	addAICastor=new TextButton(440, 335, 180, 30, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[AICastor]"), ADD_AI_CASTOR);
	addWidget(addAICastor);
	startButton=new TextButton(440, 385, 180, 30, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[Start]"), START);
	addWidget(new TextButton(440, 435, 180, 30, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL));

	startButton->visible=false;
	addWidget(startButton);
	notReadyText=new Text(440, 385, ALIGN_LEFT, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[not ready]"), 180, 30);
	notReadyText->visible=true;
	addWidget(notReadyText);
	gameFullText=new Text(440, 335, ALIGN_LEFT, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[game full]"), 180, 30);
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

	addWidget(new Text(20, 5, ALIGN_LEFT, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[awaiting players]"), 600, 0));

	for (int i=0; i<MAX_NUMBER_OF_PLAYERS; i++)
	{
		int dx=320*(i/8);
		int dy=20*(i%8);
		color[i]=new ColorButton(22+dx, 42+dy, 16, 16, ALIGN_LEFT, ALIGN_LEFT, COLOR_BUTTONS+i);
		for (int j=0; j<sessionInfo->numberOfTeam; j++)
			color[i]->addColor(sessionInfo->team[j].colorR, sessionInfo->team[j].colorG, sessionInfo->team[j].colorB);
		addWidget(color[i]);
		text[i]=new Text(42+dx, 42+dy, ALIGN_LEFT, ALIGN_LEFT, "standard",  Toolkit::getStringTable()->getString("[open]"));
		addWidget(text[i]);
		kickButton[i]=new TextButton(220+dx, 42+dy, 80, 18, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[close]"), CLOSE_BUTTONS+i);
		addWidget(kickButton[i]);

		wasSlotUsed[i]=false;

		text[i]->visible=false;
		color[i]->visible=false;
		kickButton[i]->visible=false;
	}
	startTimer=new Text(440, 210, ALIGN_LEFT, ALIGN_LEFT, "standard", "");
	addWidget(startTimer);

	timeCounter=0;

	chatWindow=new TextArea(20, 210, 400, 205, ALIGN_LEFT, ALIGN_LEFT, "standard");
	addWidget(chatWindow);
	textInput=new TextInput(20, 435, 400, 25, ALIGN_LEFT, ALIGN_LEFT, "standard", "", true, 256);
	addWidget(textInput);
	
	executionMode=START;
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
			int teamNumber=multiplayersHost->sessionInfo.players[i].teamNumber;
			char playerName[32];
			strncpy(playerName, multiplayersHost->sessionInfo.players[i].name, 32);
			char shownInfo[128];
			if (multiplayersHost->playerFileTra[i].wantsFile && !multiplayersHost->playerFileTra[i].receivedFile)
			{
				int percent=(100*multiplayersHost->playerFileTra[i].unreceivedIndex)/multiplayersHost->fileSize;
				snprintf(shownInfo, 128, "%s (%d)", playerName, percent);
			}
			else
				strncpy(shownInfo, playerName, 128);

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

				//text[i]->setText(Toolkit::getStringTable()->getString("[open]"));
				//color[i]->setSelectedColor(0);
			}
		}
	}

	if (multiplayersJoin==NULL)
	{
		multiplayersJoin=new MultiplayersJoin(shareOnYOG);
		assert(BasePlayer::MAX_NAME_LENGTH==32);
		strncpy(multiplayersJoin->playerName, globalContainer->getUsername(), 32);
		multiplayersJoin->playerName[31]=0;
		strncpy(multiplayersJoin->serverNickName, globalContainer->getUsername(), 32);
		multiplayersJoin->serverNickName[31]=0;
		
		strncpy(multiplayersJoin->serverName, "localhost", 256);
		
		multiplayersJoin->serverIP.host=SDL_SwapBE32(0x7F000001);
		multiplayersJoin->serverIP.port=SDL_SwapBE16(GAME_SERVER_PORT);
		multiplayersJoin->tryConnection(true);
	}

	if ((multiplayersJoin)&&(!multiplayersJoin->kicked))
		multiplayersJoin->onTimer(tick);

	if (((timeCounter++ % 10)==0)&&(multiplayersHost->hostGlobalState>=MultiplayersHost::HGS_PLAYING_COUNTER))
	{
		char s[128];
		snprintf(s, sizeof(s), "%s%d", Toolkit::getStringTable()->getString("[STARTING GAME ...]"), multiplayersHost->startGameTimeCounter/20);
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
				switch(mit->messageType)//set the text color
				{
				case MessageOrder::NORMAL_MESSAGE_TYPE:
					chatWindow->addText("<");
					chatWindow->addText(mit->userName);
					chatWindow->addText("> ");
					chatWindow->addText(mit->text);
					chatWindow->addText("\n");
					chatWindow->scrollToBottom();
				break;
				case MessageOrder::PRIVATE_MESSAGE_TYPE:
					chatWindow->addText("<");
					chatWindow->addText(Toolkit::getStringTable()->getString("[from:]"));
					chatWindow->addText(mit->userName);
					chatWindow->addText("> ");
					chatWindow->addText(mit->text);
					chatWindow->addText("\n");
					chatWindow->scrollToBottom();
				break;
				case MessageOrder::PRIVATE_RECEIPT_TYPE:
					chatWindow->addText("<");
					chatWindow->addText(Toolkit::getStringTable()->getString("[to:]"));
					chatWindow->addText(mit->userName);
					chatWindow->addText("> ");
					chatWindow->addText(mit->text);
					chatWindow->addText("\n");
					chatWindow->scrollToBottom();
				break;
				default:
					assert(false);
				break;
				}
				mit->guiPainted=true;
			}

	for (std::list<YOG::Message>::iterator mit=yog->receivedMessages.begin(); mit!=yog->receivedMessages.end(); ++mit)
		if (!mit->gameGuiPainted)
		{
			switch(mit->messageType)//set the text color
			{
				case YCMT_MESSAGE:
					// We don't want YOG messages to appear while in the game.
				break;
				case YCMT_PRIVATE_MESSAGE:
					chatWindow->addText("<");
					chatWindow->addText(Toolkit::getStringTable()->getString("[from:]"));
					chatWindow->addText(mit->userName);
					chatWindow->addText("> ");
					chatWindow->addText(mit->text);
					chatWindow->addText("\n");
					chatWindow->scrollToBottom();
				break;
				case YCMT_ADMIN_MESSAGE:
					chatWindow->addText("<");
					chatWindow->addText(mit->userName);
					chatWindow->addText("> ");
					chatWindow->addText(mit->text);
					chatWindow->addText("\n");
					chatWindow->scrollToBottom();
				break;
				case YCMT_PRIVATE_RECEIPT:
					chatWindow->addText("<");
					chatWindow->addText(Toolkit::getStringTable()->getString("[to:]"));
					chatWindow->addText(mit->userName);
					chatWindow->addText("> ");
					chatWindow->addText(mit->text);
					chatWindow->addText("\n");
					chatWindow->scrollToBottom();
				break;
				case YCMT_PRIVATE_RECEIPT_BUT_AWAY:
					chatWindow->addText("<");
					chatWindow->addText(Toolkit::getStringTable()->getString("[away:]"));
					chatWindow->addText(mit->userName);
					chatWindow->addText("> ");
					chatWindow->addText(mit->text);
					chatWindow->addText("\n");
					chatWindow->scrollToBottom();
				break;
				case YCMT_EVENT_MESSAGE:
					chatWindow->addText(mit->text);
					chatWindow->addText("\n");
					chatWindow->scrollToBottom();
				break;
				default:
					assert(false);
				break;
			}
			mit->gameGuiPainted=true;
		}
	
	if ((multiplayersHost->hostGlobalState>=MultiplayersHost::HGS_GAME_START_SENDED)&&(multiplayersHost->startGameTimeCounter<0))
		endExecute(STARTED);
	
	if (shareOnYOG && yog->unjoiningConfirmed)
		endExecute(executionMode);
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
			if (shareOnYOG)
				executionMode=par1;
			else
				endExecute(par1);
		break;
		case ADD_AI_NUMBI :
		case ADD_AI_CASTOR :
			if ((multiplayersHost->hostGlobalState<MultiplayersHost::HGS_GAME_START_SENDED)
				&&(multiplayersHost->sessionInfo.numberOfPlayer<MAX_NUMBER_OF_PLAYERS))
			{
				multiplayersHost->addAI((AI::ImplementitionID)(par1-ADD_AI_NUMBI+AI::NUMBI));
				if (multiplayersHost->sessionInfo.numberOfPlayer>=16)
				{
					addAINumbi->hide();
					addAICastor->hide();
					gameFullText->show();
				}
			}
		break;
		case -1:
			multiplayersHost->stopHosting();
			if (shareOnYOG)
				executionMode=par1;
			else
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
					addAINumbi->show();
					addAICastor->show();
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
		multiplayersHost->sendMessage(textInput->getText());
		textInput->setText("");
	}
}
