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

#ifndef __ENGINE_H
#define __ENGINE_H

#include "Header.h"
#include "GameGUI.h"
#include <string>

class MultiplayersJoin;
class NetGame;

//! Engine is responsible for loading games and setting up players
class Engine
{
public:
	Engine();
	~Engine();
	
	int initCampain(const std::string &mapName);
	int initCustom();
	int initCustom(const std::string &gameName);
	int initLoadGame();
	void startMultiplayer(MultiplayersJoin *multiplayersJoin);
	int initMutiplayerHost(bool shareOnYOG);
	int initMutiplayerJoin();
	int initMutiplayerYOG();
	int run();

	enum EngineError
	{
		EE_NO_ERROR=1,
		EE_CANCEL=2,
		EE_CANT_LOAD_MAP=3,
		EE_CANT_FIND_PLAYER=4
	};

public:
	GameGUI gui;
	NetGame *net;
	
protected:
	//! Load a game. Return true on success
	bool loadGame(const std::string &filename);
	//! Do the final adjustements, like setting local teams and viewport, rendering minimap
	void finalAdjustements(void);

protected:
	int cpuStats[41];
	int ticksToWaitStats[41];
	FILE *logFile;
};

#endif
