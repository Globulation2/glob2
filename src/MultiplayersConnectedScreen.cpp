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

#include "MultiplayersConnectedScreen.h"
#include "MultiplayersJoin.h"
#include <GUIButton.h>
#include <GUIText.h>
#include <GUITextInput.h>
#include <GUITextArea.h>
#include <GraphicContext.h>
#include <Toolkit.h>
#include <StringTable.h>

//MultiplayersConnectedScreen pannel part !!

MultiplayersConnectedScreen::MultiplayersConnectedScreen(MultiplayersJoin *multiplayersJoin)
{
	this->multiplayersJoin=multiplayersJoin;
	
	addWidget(new TextButton(440, 435, 180, 25, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[Disconnect]"), DISCONNECT));

	addWidget(new Text(20, 5, ALIGN_LEFT, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[awaiting players]"), 600, 0));

	startTimer=new Text(440, 300, ALIGN_LEFT, ALIGN_LEFT, "standard", "");
	addWidget(startTimer);

	chatWindow=new TextArea(20, 210, 400, 205, ALIGN_LEFT, ALIGN_LEFT, "standard");
	addWidget(chatWindow);
	textInput=new TextInput(20, 435, 400, 25, ALIGN_LEFT, ALIGN_LEFT, "standard", "", true, 256);
	addWidget(textInput);
	
	timeCounter=0;
	progress=0;
	lastProgress=0;
}

MultiplayersConnectedScreen::~MultiplayersConnectedScreen()
{
	//do not delete multiplayersJoin
}

void MultiplayersConnectedScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);

	multiplayersJoin->sessionInfo.draw(gfxCtx);

	addUpdateRect();
}


void MultiplayersConnectedScreen::onTimer(Uint32 tick)
{
	assert(multiplayersJoin);

	multiplayersJoin->onTimer(tick);

	if (multiplayersJoin->waitingState<MultiplayersJoin::WS_WAITING_FOR_PRESENCE)
	{
		multiplayersJoin->quitThisGame();
		printf("MultiplayersConnectScreen:DISCONNECTED!\n");
		endExecute(DISCONNECTED);
	}

	if ((timeCounter++ % 10)==0)
	{
		dispatchPaint(gfxCtx);
		if ((multiplayersJoin->waitingState>=MultiplayersJoin::WS_SERVER_START_GAME))
		{
			char s[128];
			snprintf(s, 128, "%s%d", Toolkit::getStringTable()->getString("[STARTING GAME ...]"), multiplayersJoin->startGameTimeCounter/20);
			printf("s=%s.\n", s);
			startTimer->setText(s);
		}
	}
	bool isFileMapDownload=multiplayersJoin->isFileMapDownload(progress);
	if (lastProgress!=progress)
	{
		lastProgress=progress;
		char s[128];
		if (isFileMapDownload)
		{
			int percent=(int)(100.0*progress);
			snprintf(s, 128, "%s%d%s", Toolkit::getStringTable()->getString("[downloaded at]"), percent, Toolkit::getStringTable()->getString("[percent]"));
		}
		else
			snprintf(s, 128, "%s", Toolkit::getStringTable()->getString("[download finished]"));
		startTimer->setText(s);
	}

	if (multiplayersJoin->waitingState==MultiplayersJoin::WS_SERVER_START_GAME)
	{
		if (multiplayersJoin->startGameTimeCounter<0)
		{
			printf("MultiplayersConnectScreen::STARTED!\n");
			endExecute(STARTED);
		}
	}

	if (multiplayersJoin->receivedMessages.size())
		for (std::list<MultiplayersCrossConnectable::Message>::iterator mit=multiplayersJoin->receivedMessages.begin(); mit!=multiplayersJoin->receivedMessages.end(); ++mit)
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
}

void MultiplayersConnectedScreen::onSDLEvent(SDL_Event *event)
{

}


void MultiplayersConnectedScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==DISCONNECT)
		{
			assert(multiplayersJoin);
			printf("MultiplayersConnectedScreen::par1==DISCONNECT\n");
			multiplayersJoin->quitThisGame();
			endExecute(DISCONNECT);
		}
		else if (par1==-1)
		{
			assert(multiplayersJoin);
			printf("MultiplayersConnectedScreen::par1==-1\n");
			multiplayersJoin->quitThisGame();
			endExecute(-1);
		}
		else
			assert(false);
	}
	else if (action==TEXT_VALIDATED)
	{
		assert(multiplayersJoin);
		multiplayersJoin->sendMessage(textInput->getText());
		textInput->setText("");
	}
}
