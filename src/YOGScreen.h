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

#ifndef __YOGSCREEN_H
#define __YOGSCREEN_H

#include "MultiplayersJoin.h"
#include "GAG.h"
#include <vector>
#include "YOG.h"

class YOGScreen:public Screen
{
public:
	enum
	{
		EXIT=-1,
		CANCEL=2,
		CREATE_GAME=3,
		UPDATE_LIST=4,
		
		STARTED=11,
		
	};

	enum
	{
		GAME_INFO_MAX_SIZE=1024
	};

public:
	//static TCPsocket socket;
	//static YOG yog;

public:
	MultiplayersJoin *multiplayersJoin;

protected:
	List *gameList;
	TextInput *textInput;
	TextArea *chatWindow;
	std::vector<Uint32> IPs;

	//static SDLNet_SocketSet socketSet;

	void updateList(void);

public:
	YOGScreen();
	virtual ~YOGScreen();
	virtual void onTimer(Uint32 tick);
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);
	void closeConnection(void);
	void createConnection(void);

	static void openYOG(void);
	static void closeYOG(void);
	static bool getString(TCPsocket socket, char data[GAME_INFO_MAX_SIZE]);
	static bool sendString(TCPsocket socket, char *data);
};

#endif
