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

#ifndef __PREPARATIONGUI_H
#define __PREPARATIONGUI_H

#include "GAG.h"
#include "Race.h"
#include "Session.h"
#include "GUIMapPreview.h"

class MainMenuScreen:public Screen
{
public:
	enum
	{
		CAMPAIN = 1,
		CUSTOM = 2,
		MULTIPLAYERS = 3,
		GAME_SETUP = 4,
		QUIT = 5
	};
private:
	//Sprite *arch;
public:
	MainMenuScreen();
	virtual ~MainMenuScreen();
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);
	static int menu(void);
};

class MultiplayersOfferScreen:public Screen
{
public:
	enum
	{
		HOST = 1,
		JOIN = 4,
		QUIT = 5
	};
private:
	//Sprite *arch;
	//Font *font;
public:
	MultiplayersOfferScreen();
	virtual ~MultiplayersOfferScreen();
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);
	static int menu(void);
};

class TextInput;

class SessionConnection
{
protected:
	enum {MAX_PACKET_SIZE=4000};
public:
	SessionConnection();
	virtual ~SessionConnection();
	bool validSessionInfo;
	//Font *font;
	int crossPacketRecieved[32];
	int startGameTimeCounter;
protected:
	enum {hostiphost=0};
	enum {hostipport=0};
public:

	SessionInfo sessionInfo;
	Sint32 myPlayerNumber;
	UDPsocket socket;
	bool destroyNet;
	int channel;

};

class MultiplayersChooseMapScreen:public Screen
{
public:
	enum
	{
		OK = 1,
		CANCEL = 2
	};
	SessionInfo sessionInfo;

private:
	Button *ok, *cancel;
	List *fileList;
	MapPreview *mapPreview;
	Text *mapName, *mapInfo, *mapVersion, *mapSize;
	bool validSessionInfo;

public:
	MultiplayersChooseMapScreen();
	virtual ~MultiplayersChooseMapScreen();
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);
};


class MultiplayersCrossConnectable:public SessionConnection
{
public:
	MultiplayersCrossConnectable():SessionConnection() { }
	virtual ~MultiplayersCrossConnectable() { }
	void tryCrossConnections(void);
	int getFreeChannel();
	
public:
	IPaddress serverIP;
};

void raceMenu(Race *race);


#endif
