/*
  Copyright (C) 2001-2004 Stephane Magnenat, Luc-Olivier de Charrière
  and Martin S. Nyffenegger
  for any question or comment contact us at nct@ysagoon.com, nuage@ysagoon.com
  or barock@ysagoon.com

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

#ifndef SGSL_H
#define SGSL_H

#include <string>
#include <deque>
#include <vector>
#include <map>
#include <stdio.h>
#include "Marshaling.h"
#include "BuildingType.h"

struct Token
{
	enum TokenType
	{
		// Data Types
		NIL=0,
		INT,
		STRING,

		// Syntaxic token
		S_PAROPEN=20,
		S_PARCLOSE,
		S_SEMICOL,
		S_STORY,
		S_EOF,

		// Language keywords
		S_EQUAL=30,
		S_HIGHER,
		S_LOWER,
		S_NOT,

		// Functions
		S_WAIT=50,
		S_TIMER,
		S_SHOW,
		S_HIDE,
		S_ALLIANCE,
		S_GUIENABLE,
		S_GUIDISABLE,
		S_SUMMONUNITS,
		S_SUMMONFLAG,
		S_DESTROYFLAG,
		S_WIN,
		S_LOOSE,
		S_LABEL,
		S_JUMP,
		S_SETAREA,
		S_AREA,
		S_ISDEAD,
		S_ALLY,
		S_ENEMY,

		// Language code
		S_LANG_0,
		S_LANG_1,
		S_LANG_2,
		S_LANG_3,
		S_LANG_4,
		S_LANG_5,

		// Constants
		// Units
		S_WORKER=100,
		S_EXPLORER,
		S_WARRIOR,
		// Buildings & Flags
		S_SWARM_B,
		S_FOOD_B,
		S_HEALTH_B,
		S_WALKSPEED_B,
		S_SWIMSPEED_B,
		S_ATTACK_B,
		S_SCIENCE_B,
		S_DEFENCE_B,

		S_WALL_B=S_SWARM_B+BuildingType::STONE_WALL,
		S_WOODWALL_B,
		S_MARKET_B,

		S_EXPLOR_F,
		S_FIGHT_F,
		S_CLEARING_F,
		S_FORBIDDEN_F,

		// GUI elements that can be disabled or enabled
		S_BUILDINGTAB,
		S_FLAGTAB,
		S_TEXTSTATTAB,
		S_GFXSTATTAB,
		S_ALLIANCESCREEN,
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
	
	//! Constructor, create a token of type t
	Token(TokenType t) { type=t; value=0; }

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
		ET_INVALID_TEAM,
		ET_NO_SUCH_FILE,
		ET_UNDEFINED_AREA_NAME,
		ET_DUPLICATED_AREA_NAME,
		ET_UNDEFINED_LABEL,
		ET_MISSING_PAROPEN,
		ET_MISSING_PARCLOSE,
		ET_MISSING_SEMICOL,
		ET_MISSING_ARGUMENT,
		ET_INVALID_ALLIANCE_LEVEL,
		ET_NOT_VALID_LANG_ID,
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

// Text parser, returns tokens
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
	bool newLine;
};

// File parser
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

//String parser
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
class GameGUI;
class Game;

// Independant story line
class Story
{
public:
	Story(Mapscript *mapscript);
	virtual ~Story();

public:
	std::deque<Token> line;
	std::map<std::string, int> labels;

	void syncStep(GameGUI *gui);
	Sint32 checkSum() { return lineSelector; }

private:
	bool conditionTester(const Game *game, int pc, bool l);
	bool testCondition(GameGUI *gui);
	int valueOfVariable(const Game *game, Token::TokenType type, int teamNumber, int level);
	int lineSelector;
	Mapscript *mapscript;
	int internTimer;
};

struct Area
{
	int x, y, r;
};

class Building;

typedef std::map<std::string, Area> AreaMap;
typedef std::map<std::string, Building *> BuildingMap;

class Mapscript
{
public:
	Mapscript();
	~Mapscript();

public:
	ErrorReport compileScript(Game *game, const char *script);
	ErrorReport compileScript(Game *game);
	ErrorReport loadScript(const char *filename, Game *game);

	bool load(SDL_RWops *stream);
	void save(SDL_RWops *stream);
	void setSourceCode(const char *sourceCode);
	const char *getSourceCode(void) { return sourceCode; }

	void syncStep(GameGUI *gui);
	Sint32 checkSum();
	bool hasTeamWon(unsigned teamNumber);
	bool hasTeamLost(unsigned teamNumber);
	int getMainTimer(void) { return mainTimer; }

	void reset(void);
	bool isTextShown;
	std::string textShown;

private:
	friend class Story;

	ErrorReport parseScript(Aquisition *donnees, Game *game);
	bool testMainTimer(void);

	int mainTimer;
	std::vector<bool> hasWon, hasLost;

	std::deque<Story> stories;

	AreaMap areas;

	BuildingMap flags;

	char *sourceCode;
};



#endif
