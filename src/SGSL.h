/*
    Copyright (C) 2002 Stephane Magnenat & Martin Nyffenegger
    for any question or comment contact us at nct@ysagoon.com or msnyffenegger@gmx.ch

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

#ifndef SGSL_H
#define SGSL_H

#include <string>
#include <deque>
#include <stdio.h>

struct Token
{
	enum TokenType
	{
		NIL,
		S_EQUAL,
		S_HIGHER,
		S_LOWER,
		S_EOF,
		S_WAIT,
		S_TIMER,
		S_SHOW,
		S_PLAYER,
		S_ACTIVATE,
		S_DEACTIVATE,
		S_FRIEND,
		S_ENEMY,
		S_DEAD,
		S_ALIVE,
		S_FLAG,
		S_YOU,
		S_NOENEMY,
		S_WIN,
		S_LOOSE,
		S_STORY,
		INT,
		STRING
	} type;

	int value;
	std::string msg;
};

class Aquisition
{
public:
	Aquisition(void);
	virtual ~Aquisition(void);

public:
	Token getToken() { return token; }
	void nextToken();
	bool newFile(const char*);

private:
	Token token;
	FILE *fp;
};


class Story
{
public:
	Story();
	virtual ~Story() { }

public:
	void step();
	std::deque<Token> line;
	bool hasWon, hasLost;
	
private:
	bool testCondition();
};

class Game;

class Mapscript
{
public:
	Mapscript();
	~Mapscript();
	
public:
	bool loadScript(const char *filename, Game *game);
	void step();
	bool hasTeamWon(unsigned teamNumber);
	bool hasTeamLost(unsigned teamNumber);
	
private:
	void reset(void);
	std::deque<Story> stories;
	Aquisition donnees;
	Game *game;
};



#endif
