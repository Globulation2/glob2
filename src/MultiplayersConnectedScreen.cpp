/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "MultiplayersConnectedScreen.h"
#include "MultiplayersJoin.h"
#include "GlobalContainer.h"
#include "GAG.h"

//MultiplayersConnectedScreen pannel part !!

MultiplayersConnectedScreen::MultiplayersConnectedScreen(MultiplayersJoin *multiplayersJoin)
{
	this->multiplayersJoin=multiplayersJoin;

	addWidget(new TextButton(360, 350, 200, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[disconnect]"), DISCONNECT));
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
	multiplayersJoin->onTimer(tick);
	
	if (multiplayersJoin->waitingState<MultiplayersJoin::WS_WAITING_FOR_SESSION_INFO)
	{
		multiplayersJoin->quitThisGame();
		printf("MultiplayersConnectScreen:DISCONNECT!\n");
		endExecute(DISCONNECT);
	}
	
	
	if ((timeCounter++ % 10)==0)
		dispatchPaint(gfxCtx);
	
	if (multiplayersJoin->waitingState==MultiplayersJoin::WS_SERVER_START_GAME)
	{
		if (multiplayersJoin->startGameTimeCounter<0)
		{
			printf("MultiplayersConnectScreen::STARTED!\n");
			endExecute(STARTED);
		}
	}
}

void MultiplayersConnectedScreen::onSDLEvent(SDL_Event *event)
{

}


void MultiplayersConnectedScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==BUTTON_RELEASED)
	{
		if (par1==DISCONNECT)
		{
			multiplayersJoin->quitThisGame();
			endExecute(DISCONNECT);
		}
		else if (par1==-1)
		{
			multiplayersJoin->quitThisGame();
			endExecute(-1);
		}
		else
			assert(false);
	}
}
