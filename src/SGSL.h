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
#include <vector>
#include <stdio.h>
#include "Marshaling.h"

struct Token
{
	enum TokenType
	{
		NIL=0,
		INT=1,
		STRING=2,
		//Units
		S_WORKER=101,
		S_EXPLORER=102,
		S_WARRIOR=103,
		//Buildings
		S_SWARM_B=201,
		S_FOOD_B=202,
		S_HEALTH_B=203,
		S_WALKSPEED_B=204,
		S_SWIMSPEED_B=205,
		S_ATTACK_B=206,
		S_SCIENCE_B=207,
		S_DEFENCE_B=208,
		//SGSL
		S_EQUAL=301,
		S_HIGHER=302,
		S_LOWER=303,
		S_EOF=304,
		S_WAIT=305,
		S_TIMER=306,
		S_SHOW=307,
		S_ACTIVATE=308,
		S_DEACTIVATE=309,
		S_FRIEND=310,
		S_ENEMY=311,
		S_DEAD=312,
		S_ALIVE=313,
		S_FLAG=314,
		S_YOU=315,
		S_NOENEMY=316,
		S_WIN=317,
		S_LOOSE=318,
		S_STORY=319,
		S_HIDE=320,
		S_MARK=321,
		S_GOBACKTO=322,
		S_SETFLAG=323,
		S_ALLY=324,
		S_SUMMON=345
	} type;
	
	struct TokenSymbolLookupTable
	{
		TokenType type;
		const char *name;
	};

	int value;
	std::string msg;
	
	//! Constructor, set logic default values
	Token() { type=NIL; value=0; }
	
	//! This table is a map table between token type and token names
	static TokenSymbolLookupTable table[];
	
	//! Returns the type of a given name (parsing phase)
	static TokenType getTypeByName(const char *name);
	
	//! Returns the name a of given type (debug & script recreation phase)
	static const char *getNameByType(TokenType type);
};

struct ErrorReport
{
	enum ErrorType
	{
		ET_OK=0,
		ET_INVALID_VALUE,
		ET_SYNTAX_ERROR,
		ET_INVALID_PLAYER,
		ET_NO_SUCH_FILE,
		ET_INVALID_FLAG_NAME,
		ET_UNKNOWN,
		ET_NB_ET,
	} type;
	
	unsigned line;
	unsigned col;
	unsigned pos;
	
	ErrorReport() { type=ET_UNKNOWN; line=0; col=0; pos=0; }
	ErrorReport(ErrorType et) { type=et; line=0; col=0; pos=0; }
	
	const char *getErrorString(void);
};

class Aquisition
{
public:
	Aquisition(void);
	virtual ~Aquisition(void);

public:
	const Token *getToken() { return &token; }
	void nextToken();
	bool newFile(const char*);
	unsigned getLine(void) { return lastLine; }
	unsigned getCol(void) { return lastCol; }
	unsigned getPos(void) { return lastPos; }
	
	virtual int getChar(void) = 0;
	virtual int ungetChar(char c) = 0;

private:
	Token token;
	unsigned actLine, actCol, actPos, lastLine, lastCol, lastPos;
};

class FileAquisition: public Aquisition
{
public:
	FileAquisition() { fp=NULL; }
	virtual ~FileAquisition() { if (fp) fclose(fp); }
	bool open(const char *filename);
	
	virtual int getChar(void) { return ::fgetc(fp); }
	virtual int ungetChar(char c) { return ::ungetc(c, fp); }
	
private:
	FILE *fp;
};

class StringAquisition: public Aquisition
{
public:
	StringAquisition();
	virtual ~StringAquisition();
	void open(const char *text);
	
	virtual int getChar(void);
	virtual int ungetChar(char c);
	
private:
	char *buffer;
	int pos;
};

class Mapscript;

class Story
{
public:
	Story(Mapscript *mapscript);
	virtual ~Story();

public:
	void step();
	std::deque<Token> line;
	bool hasWon, hasLost;
	
private:
	bool conditionTesterBuildings();
	bool conditionTesterGlobules();
	bool testCondition();
	int valueOfVariable(Token nameOfVariable,int numberOfPlayer, int level);
	int lineSelector;
	Mapscript *mapscript;
	int internTimer;
};

class Game;

struct Flag
{
	int x, y;
	std::string name;
};

class Mapscript
{
public:
	Mapscript();
	~Mapscript();
	
public:
	ErrorReport compileScript(Game *game);
	ErrorReport loadScript(const char *filename, Game *game);
	
	bool load(SDL_RWops *stream);
	void save(SDL_RWops *stream);
	void setSourceCode(const char *sourceCode);
	const char *getSourceCode(void) { return sourceCode; }
	
	void step();
	bool hasTeamWon(unsigned teamNumber);
	bool hasTeamLost(unsigned teamNumber);
	int getMainTimer(void) { return mainTimer; }
	
	bool isTextShown;
	std::string textShown;
	
private:
	friend class Story;
	
	ErrorReport parseScript(Aquisition *donnees, Game *game);
	void reset(void);
	bool testMainTimer(void);
	bool doesFlagExist(std::string name);
	bool getFlagPos(std::string name, int *x, int *y);
	
	int mainTimer;
	std::deque<Story> stories;
	std::vector<Flag> flags;
	Game *game;
	char *sourceCode;
};



#endif
