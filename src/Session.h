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

#ifndef __SESSION_H
#define __SESSION_H

#include "GAG.h"
#include "Order.h"
#include "Team.h"
#include "Player.h"
#include "Map.h"

class SessionGame:public Order
{
public:
	SessionGame();
	virtual ~SessionGame(void) { }
	bool load(SDL_RWops *stream);
	void save(SDL_RWops *stream);

	virtual Uint8 getOrderType();
	virtual char *getData();
	bool setData(const char *data, int dataLength);
	virtual int getDataLength();
	
	virtual Sint32 checkSum();
	
public:
	Sint32 numberOfPlayer;
	Sint32 numberOfTeam;

	Sint32 gameTPF;//Tick per frame.
	Sint32 gameLatency;
private:
	//! Serialized form of SessionGame
	char data[16];
};

class SessionInfo:public SessionGame
{
public:
	SessionInfo();
	virtual ~SessionInfo(void) { }
	bool load(SDL_RWops *stream);
	void save(SDL_RWops *stream);
	
	Uint8 getOrderType();
	char *getData();
	bool setData(const char *data, int dataLength);
	int getDataLength();
	Sint32 checkSum();
	
	bool setLocal(int p);

	//! draw a list of players
	void draw(DrawableSurface *gfx);
	
public:
	BaseMap map;
	BasePlayer players[32];
	BaseTeam team[32];

private:
	//! Serialized form of SessionInfo
	char data[1968];
};

#endif 
 
