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
	int initCustom(void);
	void startMultiplayer(SessionConnection *screen);
	int initMutiplayerHost(bool shareOnYOG);
	int initMutiplayerJoin(void);
	int initMutiplayerYOG(void);
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
