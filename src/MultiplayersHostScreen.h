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

#ifndef __MULTIPLAYERHOSTSCREEN_H
#define __MULTIPLAYERHOSTSCREEN_H

#include "PreparationGui.h"

class MultiplayersHostScreen:public MultiplayersCrossConnectableScreen
{
public:
	enum
	{
		START = 1,
		CANCEL = 5,

		STARTED=11
	};

	enum HostGlobalState
	{
		HGS_BAD=0,
		HGS_SHARING_SESSION_INFO=1,
		HGS_WAITING_CROSS_CONNECTIONS=2,
		HGS_ALL_PLAYERS_CROSS_CONNECTED=3,
		HGS_GAME_START_SENDED=4,
		HGS_PLAYING_COUNTER=5 // the counter 5-4-3-2-1-0 is playing
	};

	enum
	{
		SECONDS_BEFORE_START_GAME=5
	};
private:

	bool firstDraw;

	HostGlobalState hostGlobalState;

public:
	MultiplayersHostScreen(SessionInfo *sessionInfo);
	virtual ~MultiplayersHostScreen();
	void initHostGlobalState(void);
	void stepHostGlobalState(void);
	void removePlayer(int p);
	void removePlayer(char *data, int size, IPaddress ip);
	void newPlayer(char *data, int size, IPaddress ip);
	void newHostPlayer(void);
	void confirmPlayer(char *data, int size, IPaddress ip);
	void confirmStartCrossConnection(char *data, int size, IPaddress ip);
	void confirmStillCrossConnecting(char *data, int size, IPaddress ip);
	void confirmCrossConnectionAchieved(char *data, int size, IPaddress ip);
	void confirmPlayerStartGame(char *data, int size, IPaddress ip);
	void treatData(char *data, int size, IPaddress ip);
	void onTimer(Uint32 tick);
	void sendingTime();
	bool send(const int v);
	bool send(const int u, const int v);
	void stopHosting(void);
	void startGame(void);
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);

};

#endif