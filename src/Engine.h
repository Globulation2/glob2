 /*
 * Globulation 2 Engine
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#ifndef __ENGINE_H
#define __ENGINE_H

#include "Header.h"
#include "Game.h"
#include "GameGUI.h"
#include "NetGame.h"
#include "PreparationGui.h"

class Engine
{
public:
	Engine() { }
	int init(void);
	int initCampain(void);
	void startMultiplayer(SessionScreen *screen);
	int initMutiplayerHost(void);
	int initMutiplayerJoin(void);
	int run(void);
	
	enum EngineError
	{
		NO_ERROR=1,
		CANCEL=2,
		CANT_LOAD_MAP=3,
		CANT_FIND_PLAYER=4
	};

public:
	GameGUI gui;
	NetGame *net;
};

#endif
