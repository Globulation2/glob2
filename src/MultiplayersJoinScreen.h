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

#ifndef __MULTIPLAYERJOINSCREEN_H
#define __MULTIPLAYERJOINSCREEN_H

#include "PreparationGui.h"

class MultiplayersJoinScreen:public MultiplayersCrossConnectableScreen
{
public:
	enum
	{
		CONNECT = 1,
		QUIT = 5,

		STARTED=11
	};
	enum WaitingState
	{
		WS_BAD=0,
		WS_TYPING_SERVER_NAME,
		WS_WAITING_FOR_SESSION_INFO,
		WS_WAITING_FOR_CHECKSUM_CONFIRMATION,
		WS_OK,

		WS_CROSS_CONNECTING,
		WS_CROSS_CONNECTING_START_CONFIRMED,
		WS_CROSS_CONNECTING_ACHIEVED,

		WS_CROSS_CONNECTING_SERVER_HEARD,

		WS_SERVER_START_GAME
	};
private:
	//Sprite *arch;
	TextInput *serverName;
	TextInput *playerName;

	WaitingState waitingState;
	int waitingTimeout;
	int waitingTimeoutSize;
	int waitingTOTL;

public:
	MultiplayersJoinScreen();
	virtual ~MultiplayersJoinScreen();
	void dataSessionInfoRecieved(char *data, int size, IPaddress ip);
	void checkSumConfirmationRecieved(char *data, int size, IPaddress ip);
	void unCrossConnectSessionInfo(void);
	void tryCrossConnections(void);
	void startCrossConnections(void);
	void crossConnectionFirstMessage(char *data, int size, IPaddress ip);
	void checkAllCrossConnected(void);
	void crossConnectionSecondMessage(char *data, int size, IPaddress ip);
	void stillCrossConnectingConfirmation(IPaddress ip);
	void crossConnectionsAchievedConfirmation(IPaddress ip);
	void serverAskForBeginning(char *data, int size, IPaddress ip);
	void confirmPlayerStartGame(IPaddress ip);
	void treatData(char *data, int size, IPaddress ip);
	void onTimer(Uint32 tick);
	void sendingTime();
	void onSDLEvent(SDL_Event *event);
	bool sendSessionInfoRequest();
	bool sendSessionInfoConfirmation();
	bool send(const int v);
	bool send(const int u, const int v);
	bool tryConnection();
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);
};

#endif