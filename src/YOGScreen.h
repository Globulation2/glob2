/*
 * Globulation 2 YOGScreen gui
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __YOGSCREEN_H
#define __YOGSCREEN_H

#include "GAG.h"
#include <SDL_net.h>
#include <vector>

class YOGScreen:public Screen
{
public:
	enum
	{
		QUIT = 0,
		JOIN = 1,
		CREATE = 2,
		UPDATE_LOST=3
	};

	enum
	{
		GAME_INFO_MAX_SIZE=1024
	};

public:
	Uint32 IP;

private:
	List *gameList;
	TextInput *textInput;
	TextArea *chatWindow;
	vector<Uint32> IPs;
	TCPsocket socket;
	SDLNet_SocketSet socketSet;

public:
	YOGScreen();
	virtual ~YOGScreen();
	virtual void onTimer(Uint32 tick);
	void onAction(Widget *source, Action action, int par1, int par2);
	void paint(int x, int y, int w, int h);
	void closeConnection(void);

private:
	void createList(void);
	bool getString(TCPsocket socket, char data[GAME_INFO_MAX_SIZE]);
	bool sendString(TCPsocket socket, char *data);
};

#endif
