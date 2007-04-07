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

#include "MultiplayersConnectedScreen.h"
#include "MultiplayersJoin.h"
#include "Order.h"

#include <FormatableString.h>
#include <GraphicContext.h>
#include <Toolkit.h>
#include <StringTable.h>
using namespace GAGCore;
#include <GUIButton.h>
#include <GUIText.h>
#include <GUITextInput.h>
#include <GUITextArea.h>
using namespace GAGGUI;

// Sutpid widget for color rectangles
class ColorRect: public RectangularWidget
{
protected:
	Uint8 r,g,b,a;

public:
	ColorRect(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign)
	{
		this->x=x;
		this->y=y;
		this->w=w;
		this->h=h;
	
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
		
		r=g=b=0;
		a=Color::ALPHA_OPAQUE;
	}
	
	void paint(void)
	{
		assert(parent);
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		assert(parent);
		assert(parent->getSurface());
		
		parent->getSurface()->drawFilledRect(x, y, w, h, r, g, b);
	}
	
	void setColor(Uint8 nr, Uint8 ng, Uint8 nb, Uint8 na = Color::ALPHA_OPAQUE)
	{
		if ((nr!=r) || (ng!=g) || (nb!=b) || (na!=a))
		{
			r=nr;
			g=ng;
			b=nb;
		}
	}
};
//MultiplayersConnectedScreen pannel part !!

MultiplayersConnectedScreen::MultiplayersConnectedScreen(MultiplayersJoin *multiplayersJoin)
{
	this->multiplayersJoin=multiplayersJoin;
	
	addWidget(new TextButton(20, 435, 180, 40, ALIGN_RIGHT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Disconnect]"), DISCONNECT));

	addWidget(new Text(0, 5, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[awaiting players]")));
	
	startTimer=new Text(20, 210, ALIGN_RIGHT, ALIGN_TOP, "standard", "");
	addWidget(startTimer);

	chatWindow=new TextArea(20, 210, 220, 65, ALIGN_FILL, ALIGN_FILL, "standard");
	addWidget(chatWindow);
	textInput=new TextInput(20, 20, 220, 25, ALIGN_FILL, ALIGN_BOTTOM, "standard", "", true, 256);
	addWidget(textInput);
	
	for (unsigned i=0; i<16; i++)
	{
		int dx=320*(i/8);
		int dy=20*(i%8);
		text[i] = new Text(42+dx, 40+dy, ALIGN_SCREEN_CENTERED, ALIGN_TOP, "standard");
		text[i]->visible = false;
		addWidget(text[i]);
		color[i] = new ColorRect(22+dx, 42+dy, 16, 16, ALIGN_SCREEN_CENTERED, ALIGN_TOP);
		color[i]->visible = false;
		addWidget(color[i]);
	}
	
	timeCounter=0;
	progress=0;
	lastProgress=0;
}

MultiplayersConnectedScreen::~MultiplayersConnectedScreen()
{
	//do not delete multiplayersJoin
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
		int i=0;
		for (;i<multiplayersJoin->sessionInfo.numberOfPlayer; i++)
		{
			text[i]->setText(multiplayersJoin->sessionInfo.players[i].name);
			text[i]->show();
			const BaseTeam &t = multiplayersJoin->sessionInfo.teams[multiplayersJoin->sessionInfo.players[i].teamNumber];
			color[i]->setColor(t.colorR, t.colorG, t.colorB);
			color[i]->show();
		}
		for (;i<16;i++)
		{
			text[i]->hide();
			color[i]->hide();
		}
		if ((multiplayersJoin->waitingState>=MultiplayersJoin::WS_SERVER_START_GAME))
		{
			std::string s;
			s = FormatableString("%0%1").arg(Toolkit::getStringTable()->getString("[STARTING GAME ...]")).arg(multiplayersJoin->startGameTimeCounter/20);
			if (verbose)
				printf("s=%s.\n", s.c_str());
			startTimer->setText(s);
		}
	}
	
	bool isFileMapDownload=multiplayersJoin->isFileMapDownload(progress);
	if (lastProgress!=progress)
	{
		lastProgress=progress;
		std::string s;
		if (isFileMapDownload)
		{
			int percent=(int)(100.0*progress);
			s = FormatableString("%0%1%2").arg(Toolkit::getStringTable()->getString("[downloaded at]")).arg(percent).arg(Toolkit::getStringTable()->getString("[percent]"));
		}
		else
			s = Toolkit::getStringTable()->getString("[download finished]");
		startTimer->setText(s);
	}

	if (multiplayersJoin->waitingState==MultiplayersJoin::WS_SERVER_START_GAME)
	{
		if (multiplayersJoin->startGameTimeCounter<0)
		{
			if (verbose)
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
		multiplayersJoin->sendMessage(textInput->getText().c_str());
		textInput->setText("");
	}
}
