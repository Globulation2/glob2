/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat, Luc-Olivier de Charrière
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

/*
 TODO: Check for label/Jump coherency !

for the parser:
story: starts another parallel storyline, so multiple endings for a map are possible
*/

#include <iostream>
#include <string>
#include <deque>
#include <vector>
#include <string.h>
#include <math.h>
#include "SGSL.h"
#include "Game.h"
#include "Utilities.h"


Token::TokenSymbolLookupTable Token::table[] =
{
	{ S_WAIT, "wait" },
	{ S_EQUAL, "=" },
	{ S_HIGHER, ">" },
	{ S_LOWER, "<" },
	{ S_TIMER, "timer" },
	{ S_SHOW, "show" },
	
	{ S_FRIEND, "friend" },
	{ S_YOU, "you" },
	{ S_NOENEMY, "noenemy" },

	{ S_ENEMY, "enemy" },
	{ S_ISDEAD, "isdead" },
	{ S_FLAG, "flag" },
	{ S_WIN, "win" },
	{ S_LOOSE, "loose" },
	{ S_STORY, "story" },
	{ S_WORKER, "Worker" },
	{ S_EXPLORER, "Explorer" },
	{ S_WARRIOR, "Warrior" },
	{ S_SWARM_B, "Swarm" },
	{ S_FOOD_B, "Inn" },
	{ S_HEALTH_B, "Hospital" },
	{ S_WALKSPEED_B, "Racetrack" },
	{ S_SWIMSPEED_B, "Pool" },
	{ S_ATTACK_B, "Camp" },
	{ S_SCIENCE_B, "School" },
	{ S_DEFENCE_B, "Tower" },
	{ S_MARKET_B, "Market"},
	{ S_WALL_B, "Wall"},
	{ S_EXPLOR_F, "ExplorationFlag"},
	{ S_FIGHT_F, "WarFlag"},
	{ S_CLEARING_F, "ClearingFlag"},
	{ S_FORBIDDEN_F, "ForbiddenFlag"},
	{ S_ALLIANCESCREEN, "AllianceScreen"},
	{ S_BUILDINGTAB, "BuildingTab"},
	{ S_FLAGTAB, "FlagTab"},
	{ S_TEXTSTATTAB, "TextStatTab"},
	{ S_GFXSTATTAB, "GfxStatTab"},
	{ S_HIDE, "hide" },
	{ S_LABEL, "label" },
	{ S_JUMP, "jump" },
	{ S_SETFLAG, "setFlag"},
	{ S_ALLY, "ally" },
	{ S_SUMMON, "summon" },
	{ S_NOT, "not" },
	{ S_PAROPEN, "("},
	{ S_PARCLOSE, ")"},
	{ S_SEMICOL, ","},
	{ S_ALLIANCE, "alliance"},
	{ S_GUIENABLE, "guiEnable"},
	{ S_GUIDISABLE, "guiDisable"},
	{ INT, "int" },
	{ STRING, "string" },
	{ NIL, NULL }
};

Token::TokenType Token::getTypeByName(const char *name)
{
	int i = 0;
	TokenType type=NIL;

	if (name != NULL)
		while (table[i].type != NIL)
		{
			if (strcasecmp(name, table[i].name)==0)
			{
				type = table[i].type;
				break;
			}
			i++;
		}
	return type;
}

const char *Token::getNameByType(Token::TokenType type)
{
	int i = 0;
	const char *name=NULL;

	if (type != NIL)
		while (table[i].name != NULL)
		{
			if (type == table[i].type)
			{
				name = table[i].name;
				break;
			}
			i++;
		}
	return name;
}

Story::Story(Mapscript *mapscript)
{
	lineSelector = 0;
	internTimer=0;
	hasWon=false;
	hasLost=false;
	this->mapscript=mapscript;
}

Story::~Story()
{

}

//get values from the game
int Story::valueOfVariable(Token::TokenType type, int playerNumber, int level)
{
	TeamStat *latestStat=mapscript->game->teams[playerNumber]->stats.getLatestStat();
	switch(type)
	{
		case(Token::S_WORKER):
			return latestStat->numberUnitPerType[0];
		case(Token::S_EXPLORER):
			return latestStat->numberUnitPerType[1];
		case(Token::S_WARRIOR):
			return latestStat->numberUnitPerType[2];
		case(Token::S_SWARM_B):
			return latestStat->numberBuildingPerTypePerLevel[0][level];
		case(Token::S_FOOD_B):
			return latestStat->numberBuildingPerTypePerLevel[1][level];
		case(Token::S_HEALTH_B):
			return latestStat->numberBuildingPerTypePerLevel[2][level];
		case(Token::S_WALKSPEED_B):
			return latestStat->numberBuildingPerTypePerLevel[3][level];
		case(Token::S_SWIMSPEED_B):
			return latestStat->numberBuildingPerTypePerLevel[4][level];
		case(Token::S_ATTACK_B):
			return latestStat->numberBuildingPerTypePerLevel[5][level];
		case(Token::S_SCIENCE_B):
			return latestStat->numberBuildingPerTypePerLevel[6][level];
		case(Token::S_DEFENCE_B):
			return latestStat->numberBuildingPerTypePerLevel[7][level];
		default:
			assert(false);
			return 0;
	}
}

//code for testing conditions
bool Story::conditionTester(int pc, bool l)
{
	Token::TokenType type, operation;
	int level, playerNumber, amount;

	type = line[pc++].type;
	if (l)
		level = line[pc++].value;
	playerNumber = line[pc++].value;
	operation = line[pc++].type;
	amount = line[pc].value;

	switch (operation)
	{
		case (Token::S_HIGHER):
		{
			return (valueOfVariable(type, playerNumber, level) > amount);
		}
		case (Token::S_LOWER):
		{
			return (valueOfVariable(type, playerNumber, level) < amount);
		}
		case (Token::S_EQUAL):
		{
			return (valueOfVariable(type, playerNumber, level) == amount);
		}
		default:
			return false;
	}
}


//main step-by-step machine
bool Story::testCondition()
{
	/* Not up-to date */
	if (line.size())
		switch (line[lineSelector].type)
		{
			case (Token::S_SHOW):
			{
				mapscript->isTextShown = true;
				lineSelector++;
				mapscript->textShown = line[lineSelector].msg;
				return true;
			}

			case (Token::S_WIN):
			{
				hasWon=true;
				return false;
			}

			case (Token::S_LOOSE):
			{
				hasLost=true;
				return false;
			}

			case (Token::S_TIMER):
			{
				lineSelector++;
				mapscript->mainTimer=line[lineSelector].value;
				return true;
			}

			case (Token::S_WAIT):
			{
				bool negate = false;
				int execLine = lineSelector+1;

				if (line[execLine].type == Token::S_NOT)
				{
					negate = true;
					execLine++;
				}
				switch (line[execLine].type)
				{
					case (Token::INT):
					{
						// The idea is to put an int token on execution which stands for decrement and waiting
						internTimer = line[execLine].value;
						lineSelector = execLine;
						return false;
					}
					case (Token::S_ISDEAD):
					{
						execLine++;
						if (!mapscript->game->teams[line[execLine].value]->isAlive)
						{
							lineSelector = execLine;
							return true;
						}
						else
							return false;
					}
					case (Token::S_NOT):
						//Execution Error, this chouldn't happoen !!!
						assert(false);
						break;
					case (Token::S_FLAG):
					{
						int x, y ,r;

						execLine++;
						mapscript->getFlagPos(line[execLine].msg, &x, &y, &r);
						if (line[lineSelector+3+negate].type==Token::S_YOU)
						{
							int dx, dy;
							for (dy=y-5; dy<y+5; dy++)
								for (dx=x-5; dx<x+5; dx++)
								{
									Uint16 gid=mapscript->game->map.getGroundUnit(dx, dy);
									if (gid!=NOGUID)
									{
										int team=Unit::GIDtoTeam(gid);
										if (mapscript->game->teams[0]->teamNumber==team)//TODO XOR
										{
											lineSelector +=3+negate;
											return true;
										}
									}
								}
							return false;
						}
						else if (line[lineSelector+3+negate].type==Token::S_ALLY)
						{
							int dx, dy;
							for (dy=y-5; dy<y+5; dy++)
								for (dx=x-5; dx<x+5; dx++)
								{
									Uint16 gid=mapscript->game->map.getGroundUnit(dx, dy);
									if (gid!=NOGUID)
									{
										int team=Unit::GIDtoTeam(gid);
										Uint32 tm=1<<team;
										if (mapscript->game->teams[0]->allies & tm) //TODO XOR
										{
											lineSelector +=3+negate;
											return true;
										}
									}
								}

							return false;
						}
						else if (line[lineSelector+3+negate].type==Token::S_NOENEMY)
						{
							int dx, dy;
							for (dy=y-5; dy<y+5; dy++)
								for (dx=x-5; dx<x+5; dx++)
								{
									Uint16 gid;
									gid=mapscript->game->map.getGroundUnit(dx, dy);
									if (gid!=NOGUID)
									{
										int team=Unit::GIDtoTeam(gid);
										Uint32 tm=1<<team;
										if (mapscript->game->teams[0]->enemies & tm)//TODO XOR
											return false;
									}
									gid=mapscript->game->map.getBuilding(dx, dy);
									if (gid!=NOGBID)
									{
										int team=Building::GIDtoTeam(gid);
										Uint32 tm=1<<team;
										if (mapscript->game->teams[0]->enemies & tm)//TODO XOR
											return false;
									}
								}

							lineSelector +=3+negate;
							return true;
						}
						else
							assert(false);

					}
					case (Token::S_WORKER):
					case (Token::S_EXPLORER):
					case (Token::S_WARRIOR):
					{
						if (conditionTester(execLine, false))
						{
							lineSelector += execLine + 3;
							mapscript->isTextShown=false;
							return true;
						}
						else
							return false;
					}
					break;
					default: //Test conditions
					{
						if (conditionTester(execLine, true))
						{
							lineSelector += execLine + 4;
							mapscript->isTextShown=false;
							return true;
						}
						else
							return false;
					}
				}
			}
			case (Token::S_LABEL):
			{
				lineSelector++;
				return true;
			}
			case (Token::S_JUMP):
			{
				int newEmplacement;
				for (newEmplacement =lineSelector; newEmplacement > 0; newEmplacement--)
				{
					if ((line[lineSelector+1].msg == line[newEmplacement].msg) && (line[newEmplacement-1].type=Token::S_LABEL))
					{
						break;
					}
				}
				lineSelector = --newEmplacement;
				return true;
			}
			case (Token::INT):
			{
				internTimer--;
				if (internTimer==0)
					return true;
				else
					return false;
			}
			case (Token::S_FRIEND):
			{
				mapscript->game->teams[line[lineSelector+1].value]->allies |= mapscript->game->teams[line[lineSelector+2].value]->me;
				mapscript->game->teams[line[lineSelector+1].value]->enemies = ~ mapscript->game->teams[line[lineSelector+1].value]->allies;
				mapscript->game->teams[line[lineSelector+2].value]->allies |= mapscript->game->teams[line[lineSelector+1].value]->me;
				mapscript->game->teams[line[lineSelector+2].value]->enemies = ~ mapscript->game->teams[line[lineSelector+2].value]->allies;
				lineSelector +=2;
				return true;
			}
			case (Token::S_ENEMY):
			{

				mapscript->game->teams[line[lineSelector+1].value]->allies &= ~ mapscript->game->teams[line[lineSelector+2].value]->me;
				mapscript->game->teams[line[lineSelector+1].value]->enemies = ~ mapscript->game->teams[line[lineSelector+1].value]->allies;
				mapscript->game->teams[line[lineSelector+2].value]->allies &= ~ mapscript->game->teams[line[lineSelector+1].value]->me;
				mapscript->game->teams[line[lineSelector+2].value]->enemies = ~ mapscript->game->teams[line[lineSelector+2].value]->allies;
				lineSelector +=2;
				return true;
			}
			case (Token::S_SUMMON):
			{
				const std::string& flagName = line[++lineSelector].msg;
				int globulesAmount = line[++lineSelector].value;
				int type = line[++lineSelector].type;
				int level = line[++lineSelector].value;
				int team = line[++lineSelector].value;

				int x, y, r;
				if (mapscript->getFlagPos(flagName, &x, &y, &r))
				{
					int number = globulesAmount;
					int maxTest = number * 3;

					while ((number>0) && (maxTest>0))
					{
						int dx=(syncRand()%(2*r))+1;
						int dy=(syncRand()%(2*r))+1;
						dx-=r;
						dy-=r;

						if (dx*dx+dy*dy<r*r)
						{
							if (mapscript->game->addUnit(x+dx, y+dy, team, type, level, 0, 0, 0))
							{
								number --;
							}
						}

						maxTest--;
					}
				}
				return true;
			}
			case (Token::S_SETFLAG):
			{
				Flag flag;
				bool found=false;

				flag.name = line[++lineSelector].msg;
				flag.x = line[++lineSelector].value;
				flag.y = line[++lineSelector].value;
				flag.r  =line[++lineSelector].value;
				for (std::vector<Flag>::iterator it=mapscript->flags.begin(); it != mapscript->flags.end(); ++it)
				{
					if ((*it).name==flag.name)
					{
						(*it)=flag;
						found=true;
						break;
					}
				}
				if (!found)
					mapscript->flags.push_back(flag);

				return true;
			}
			case (Token::S_HIDE):
			{
				mapscript->isTextShown = false;
				return true;
			}
			default:
				return false;
		}
	return false;
}

void Story::step()
{
	while (testCondition())
	{
		lineSelector ++;
	}
}

using namespace std;

const char *ErrorReport::getErrorString(void)
{
	static const char *strings[]={ "No error", "Invalid Value ", "Syntax error", "Invalid player", "No such file", "Invalid Falg name", "Flag name already defined","Missing \"(\"","Missing \")\"", "Missing \",\"", "Missing argument", "Unknown error" };
	assert(type >= 0);
	assert(type < ET_NB_ET);
	assert(ET_NB_ET == sizeof(strings)/sizeof(const char *));
	return strings[(int)type];
}

//Text aquisition by the parser
Aquisition::~Aquisition(void)
{

}

Aquisition::Aquisition(void)
{
	token.type=Token::NIL;
	actLine=0;
	actCol=0;
	actPos=0;
	lastLine=0;
	lastCol=0;
	lastPos=0;
	newLine=true;
}

#define HANDLE_ERROR_POS(c) { actPos++; if (c=='\n') { actLine++; actCol=0; } else { actCol++; } }
#undef getc

#ifdef _MSC_VER
const char *index(const char *str, char f)
{
	for(const char *a=str;*a;a++)
	{
		if(*a==f)
			return a;
	}
	return NULL;
}
#endif

//Tokenizer
void Aquisition::nextToken()
{
	string mot;
	int c;
	lastCol=actCol;
	lastLine=actLine;
	lastPos=actPos;
	// eat empty char
	while(( c=this->getChar() )!=EOF)
	{
		if (c=='#' && newLine)
		{
			while(c!='\n')
				c=this->getChar();
		}
		newLine=false;
		//if (index(" \t\r\n().,", c)==NULL)
		if (index(" \t\r\n", c)==NULL)
		{
			if (c == '\n')
				newLine=true;
			this->ungetChar(c);
			break;
		}
		HANDLE_ERROR_POS(c);
	}

	if (c==EOF)
	{
		token.type=Token::S_EOF;
		return;
	}

	// push char in mot
	bool isInString=false;
	bool isInMot = false;
	while(( c=this->getChar() )!=EOF)
	{
		if ((char)c=='"')
			isInString=!isInString;
		if (isInString)
		{
			if (index("\t\r\n", c)!=NULL)
			{
				if (c == '\n')
					newLine=true;
				this->ungetChar(c);
				break;
			}
		}
		else
		{
			//if (index(" \t\r\n().,", c)!=NULL)
			if (index(" \t\r\n", c)!=NULL)
			{
				if (c == '\n')
					newLine=true;

				this->ungetChar(c);
				break;
			}
			else if (index("().,", c)!=NULL)
			{
				if (isInMot)
					this->ungetChar(c);
				else
				{
					//no need to come back
					HANDLE_ERROR_POS(c);
					mot+= (char)c;
				}
				break;
			}
			isInMot=true;
		}
		HANDLE_ERROR_POS(c);
		mot+= (char)c;
	}


	if (mot.size()>0)
	{
		if ((mot[0]>='0') && (mot[0]<='9'))
		{
			token.type = Token::INT;
			token.value = atoi(mot.c_str());
		}
		else if (mot[0]=='"')
		{
			string::size_type start=mot.find_first_of("\"");
			string::size_type end=mot.find_last_of("\"");
			if ((start!=string::npos) && (end!=string::npos))
			{
				token.type = Token::STRING;
				assert(end-start-1>=0);
				token.msg = mot.substr(start+1, end-start-1);
			}
			else
				token.type = Token::NIL;
		}
		else
		{
			token.type=Token::getTypeByName(mot.c_str());
		}
	}
	else
		token.type = Token::NIL;
}

bool FileAquisition::open(const char *filename)
{
	if (fp != NULL)
		fclose(fp);
	if ((fp = fopen(filename,"r")) == NULL)
	{
		fprintf(stderr,"SGSL : Can't open file %s\n", filename);
		return false;
	}
	return true;
}


StringAquisition::StringAquisition()
{
	buffer=NULL;
	pos=0;
}

StringAquisition::~StringAquisition()
{
	if (buffer)
		free(buffer);
}

void StringAquisition::open(const char *text)
{
	assert(text);

	if (buffer)
		free (buffer);

	size_t len=strlen(text);
	buffer=(char *)malloc(len+1);
	memcpy(buffer, text, len+1);
	pos=0;
}

int StringAquisition::getChar(void)
{
	if (buffer[pos])
	{
		return (buffer[pos++]);
	}
	else
		return EOF;
}

int StringAquisition::ungetChar(char c)
{
	if (pos)
	{
		buffer[--pos]=c;
	}
	return 0;
}

// Mapscript creation

Mapscript::Mapscript()
{
	reset();
	sourceCode=NULL;
	setSourceCode("");
}

Mapscript::~Mapscript(void)
{
	if (sourceCode)
		delete[] sourceCode;
}

bool Mapscript::load(SDL_RWops *stream)
{
	if (sourceCode)
		delete[] sourceCode;

	unsigned len=SDL_ReadBE32(stream);
	sourceCode=new char[len];
	SDL_RWread(stream, sourceCode, len, 1);
	return true;
}

void Mapscript::save(SDL_RWops *stream)
{
	unsigned len=strlen(sourceCode) +1;
	SDL_WriteBE32(stream, len);
	SDL_RWwrite(stream, sourceCode, len, 1);
}

void Mapscript::setSourceCode(const char *sourceCode)
{
	if (this->sourceCode)
		delete[] this->sourceCode;

	unsigned len=strlen(sourceCode)+1;
	this->sourceCode=new char[len];
	strcpy(this->sourceCode, sourceCode);
}

bool Mapscript::getFlagPos(string name, int *x, int *y, int *r)
{
	for (vector<Flag>::iterator it=flags.begin(); it != flags.end(); ++it)
	{
		if ((*it).name==name)
		{
			*x=(*it).x;
			*y=(*it).y;
			*r=(*it).r;
			return true;
		}
	}
	return false;
}

bool Mapscript::doesFlagExist(string name)
{
	for (vector<Flag>::iterator it=flags.begin(); it != flags.end(); ++it)
	{
		if ((*it).name==name)
		{
			return true;
		}
	}
	return false;
}

void Mapscript::reset(void)
{
	isTextShown = false;
	mainTimer=0;
	game=NULL;
	stories.clear();
	flags.clear();
}

bool Mapscript::testMainTimer()
{
	return (mainTimer <= 0);
}

void Mapscript::step()
{
	if (mainTimer)
		mainTimer--;
	for (std::deque<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
	{
		it->step();
	}
}

Sint32 Mapscript::checkSum()
{
	Sint32 cs=0;
	for (std::deque<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
	{
		cs^=it->checkSum();
		cs=(cs<<28)|(cs>>4);
	}
	return cs;
}


ErrorReport Mapscript::compileScript(Game *game, const char *script)
{
	StringAquisition aquisition;
	aquisition.open(script);
	return parseScript(&aquisition, game);
}

ErrorReport Mapscript::compileScript(Game *game)
{
	return compileScript(game, sourceCode);
}

ErrorReport Mapscript::loadScript(const char *filename, Game *game)
{
	FileAquisition aquisition;
	if (aquisition.open(filename))
		return parseScript(&aquisition, game);
	else
		return ErrorReport(ErrorReport::ET_NO_SUCH_FILE);
}

// Control of the syntax of the script
ErrorReport Mapscript::parseScript(Aquisition *donnees, Game *game)
{
	// Gets next token and sets right error position
	#define NEXT_TOKEN \
	{ \
		er.line=donnees->getLine(); \
		er.col=donnees->getCol(); \
		er.pos=donnees->getPos(); \
		donnees->nextToken(); \
	}

	// Check for open parentesis
	#define CHECK_PAROPEN \
	{ \
		NEXT_TOKEN; \
		if (donnees->getToken()->type != Token::S_PAROPEN) \
		{ \
			er.type=ErrorReport::ET_MISSING_PAROPEN; \
			break; \
		} \
	}

	// Check for closed parentesis
	#define CHECK_PARCLOSE \
	{ \
		NEXT_TOKEN; \
		if (donnees->getToken()->type != Token::S_PARCLOSE) \
		{ \
			er.type=ErrorReport::ET_MISSING_PARCLOSE; \
			break; \
		} \
	}

	// Checks for semicolon
	#define CHECK_SEMICOL \
	{ \
		NEXT_TOKEN; \
		if (donnees->getToken()->type != Token::S_SEMICOL) \
		{ \
			er.type=ErrorReport::ET_MISSING_SEMICOL; \
			break; \
		} \
	}

	// Cheks for right number of arguments
	#define CHECK_ARGUMENT \
	{ \
		if (donnees->getToken()->type == Token::S_PARCLOSE || donnees->getToken()->type == Token::S_SEMICOL) \
		{ \
			er.type=ErrorReport::ET_MISSING_ARGUMENT; \
			break; \
		} \
	}

	ErrorReport er;
	er.type=ErrorReport::ET_OK;

	reset();
	this->game=game;

	NEXT_TOKEN;
	while (donnees->getToken()->type != Token::S_EOF)
	{
		Story thisone(this);
		if (er.type != ErrorReport::ET_OK)
		{
			break;
		}
		while ((donnees->getToken()->type != Token::S_STORY) && (donnees->getToken()->type !=Token::S_EOF))
		{
			if (er.type != ErrorReport::ET_OK)
			{
				break;
			}
			// Grammar check
			switch (donnees->getToken()->type)
			{
				//Grammar for summon | summon(flag_name , globules_amount , globule_type , globule_level , player_int)
				case (Token::S_SUMMON):
				{
					//<-summon
					thisone.line.push_back(*donnees->getToken());
					CHECK_PAROPEN;
					NEXT_TOKEN; // <- flag_name
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::STRING)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (!doesFlagExist(donnees->getToken()->msg))
					{
						er.type=ErrorReport::ET_INVALID_FLAG_NAME;
						break;
					}
					thisone.line.push_back(*donnees->getToken());
					CHECK_SEMICOL;
					NEXT_TOKEN; //<- globules_amount
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (donnees->getToken()->value > 30) //Max number of globules to summon
					{
						er.type=ErrorReport::ET_INVALID_VALUE;
						break;
					}
					thisone.line.push_back(*donnees->getToken());
					CHECK_SEMICOL;
					NEXT_TOKEN; //<- globules type
					CHECK_ARGUMENT;
					if ((donnees->getToken()->type != Token::S_WARRIOR) && (donnees->getToken()->type != Token::S_WORKER) && (donnees->getToken()->type != Token::S_EXPLORER))
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());
					CHECK_SEMICOL;
					NEXT_TOKEN; //<- globules level
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (donnees->getToken()->value > 3)
					{
						er.type=ErrorReport::ET_INVALID_VALUE;
						break;
					}
					thisone.line.push_back(*donnees->getToken());
					CHECK_SEMICOL;
					NEXT_TOKEN; //<- player
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (donnees->getToken()->value >= game->session.numberOfTeam)
					{
						er.type=ErrorReport::ET_INVALID_PLAYER;
						break;
					}
					thisone.line.push_back(*donnees->getToken());
					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

				//Grammar for setflag | setflag("flag_name" , x , y , r )
				case (Token::S_SETFLAG):
				{
					Flag flag;
					bool found=false;
					thisone.line.push_back(*donnees->getToken()); //<-setflag
					CHECK_PAROPEN;
					NEXT_TOKEN;//<-"flagName"
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::STRING)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (doesFlagExist(donnees->getToken()->msg))
					{
						er.type=ErrorReport::ET_DOUBLE_FLAG_NAME;
						break;
					}
					flag.name=donnees->getToken()->msg;
					thisone.line.push_back(*donnees->getToken());
					CHECK_SEMICOL;
					NEXT_TOKEN; //<- x
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					flag.x=donnees->getToken()->value;
					thisone.line.push_back(*donnees->getToken());
					CHECK_SEMICOL;
					NEXT_TOKEN;//<- y
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					flag.y=donnees->getToken()->value;
					CHECK_SEMICOL;
					NEXT_TOKEN; //<- r
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					flag.r=donnees->getToken()->value;
					thisone.line.push_back(*donnees->getToken());
					for (vector<Flag>::iterator it=flags.begin(); it != flags.end(); ++it)
					{
						if ((*it).name==flag.name)
						{
							(*it)=flag;
							found=true;
							break;
						}
					}
					if (!found)
						flags.push_back(flag);
					thisone.line.push_back(*donnees->getToken());
					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;
				//Grammar of Show, Label, Jump
		    //TODO: !! No check if label name exist !!!
				case (Token::S_SHOW):
				case (Token::S_LABEL):
				case (Token::S_JUMP):
				{
					thisone.line.push_back(*donnees->getToken());
					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::STRING)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());
					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;
				//Grammar Of Wait | wait ( int) or wait ( isdead( playerid ) ) or wait( condition ) or wait( not( condition ) )
				case (Token::S_WAIT):
				{
					bool enter = false;
					bool negate = false;
					thisone.line.push_back(*donnees->getToken());
					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					// int
					if (donnees->getToken()->type == Token::INT  && !enter)
					{
						enter = true;
						if (donnees->getToken()->value <=0)
						{
							er.type=ErrorReport::ET_INVALID_VALUE;
							break;
						}
						thisone.line.push_back(*donnees->getToken());
					}

					// isdead( playerid )
					if (donnees->getToken()->type == Token::S_ISDEAD && !enter)
					{
						enter = true;
						thisone.line.push_back(*donnees->getToken());
						CHECK_PAROPEN;
						NEXT_TOKEN;
						CHECK_ARGUMENT;
						if (donnees->getToken()->type != Token::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						else if (donnees->getToken()->value >= game->session.numberOfTeam)
						{
							er.type=ErrorReport::ET_INVALID_PLAYER;
							break;
						}
						thisone.line.push_back(*donnees->getToken());
						CHECK_PARCLOSE;
						//CHECK_PARCLOSE;
						//NEXT_TOKEN;
						//break;
					}
					// following two arguments can be negated !
					if (donnees->getToken()->type == Token::S_NOT && !enter)
					{
						negate = true;
						thisone.line.push_back(*donnees->getToken());
						CHECK_PAROPEN;
						NEXT_TOKEN;
						CHECK_ARGUMENT;
					}
					// flag ("flagname" , who*)
					if (donnees->getToken()->type == Token::S_FLAG && !enter)
					{
						enter = true;
						thisone.line.push_back(*donnees->getToken());
						CHECK_PAROPEN;
						NEXT_TOKEN;
						CHECK_ARGUMENT;
						if (donnees->getToken()->type != Token::STRING)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						else if (!doesFlagExist(donnees->getToken()->msg))
						{
							er.type=ErrorReport::ET_INVALID_FLAG_NAME;
							break;
						}
						thisone.line.push_back(*donnees->getToken());
						CHECK_SEMICOL;
						NEXT_TOKEN;
						CHECK_ARGUMENT;
						if ((donnees->getToken()->type != Token::S_YOU) && (donnees->getToken()->type != Token::S_ENEMY)
						&& (donnees->getToken()->type != Token::S_ALLY) && (donnees->getToken()->type != Token::INT))
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						thisone.line.push_back(*donnees->getToken());
						CHECK_PARCLOSE;
					}
					//Comparaison| ( Variable( player , "flagName" ) cond value ) : variable = unit
					//( Variable( level , player , "flagName" ) cond value ) : variable = building
					//"flagName" can be omitted !
					if ((donnees->getToken()->type > 100) && (donnees->getToken()->type < 300) && !enter)
					{
						enter = true;
						//Buildings
						thisone.line.push_back(*donnees->getToken());
						if ((donnees->getToken()->type > 200) && (donnees->getToken()->type < 300))
						{
							//level
							CHECK_PAROPEN;
							NEXT_TOKEN;
							CHECK_ARGUMENT;
							if (donnees->getToken()->type != Token::INT)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							else if ((donnees->getToken()->value < 0) || (donnees->getToken()->value > 5))
							{
								er.type=ErrorReport::ET_INVALID_VALUE;
								break;
							}
							thisone.line.push_back(*donnees->getToken());
							CHECK_SEMICOL;
							NEXT_TOKEN;
						}
						else if ((donnees->getToken()->type > 100) && (donnees->getToken()->type < 200))
						{
							CHECK_PAROPEN;
							NEXT_TOKEN;
							CHECK_ARGUMENT;
						}
						else
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						//player
						if (donnees->getToken()->type != Token::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						else if (donnees->getToken()->value >= game->session.numberOfTeam)
						{
							er.type=ErrorReport::ET_INVALID_PLAYER;
							break;
						}
						thisone.line.push_back(*donnees->getToken());
						NEXT_TOKEN;
						//Optional "flagName"
						if (donnees->getToken()->type != Token::S_PARCLOSE)
						{
							NEXT_TOKEN;
							//there is a flagName
							if (donnees->getToken()->type != Token::STRING)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							else if (!doesFlagExist(donnees->getToken()->msg))
							{
								er.type=ErrorReport::ET_INVALID_FLAG_NAME;
								break;
							}
							thisone.line.push_back(*donnees->getToken());
							CHECK_PARCLOSE;
						}
						else
						{ // there was no flag name but a closing parenthesis
						}
						NEXT_TOKEN;
						CHECK_ARGUMENT;
						if ((donnees->getToken()->type < 301) || (donnees->getToken()->type > 303))
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						thisone.line.push_back(*donnees->getToken());
						NEXT_TOKEN;
						CHECK_ARGUMENT;
						if (donnees->getToken()->type != Token::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						thisone.line.push_back(*donnees->getToken());
					}
					if (!enter)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					if (negate)
					{
						negate = !negate;
						CHECK_PARCLOSE;
					}
					CHECK_PARCLOSE;
					NEXT_TOKEN;
					break;
				}
				break;
				case (Token::NIL):
				{
					//NEXT_TOKEN;
					er.type=ErrorReport::ET_UNKNOWN;
				}
				break;
					//friend/ennemy( player1 , player2 )
				case (Token::S_FRIEND):
				case (Token::S_ENEMY):
				{
					thisone.line.push_back(*donnees->getToken());
					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (donnees->getToken()->value >= game->session.numberOfTeam)
					{
						er.type=ErrorReport::ET_INVALID_PLAYER;
						break;
					}
					thisone.line.push_back(*donnees->getToken());
					CHECK_SEMICOL;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (donnees->getToken()->value >= game->session.numberOfTeam)
					{
						er.type=ErrorReport::ET_INVALID_PLAYER;
						break;
					}
					thisone.line.push_back(*donnees->getToken());
					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;
					//Timer( int )
				case (Token::S_TIMER):
				{
					thisone.line.push_back(*donnees->getToken());
					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					if (donnees->getToken()->value <=0)
					{
						er.type=ErrorReport::ET_INVALID_VALUE;
						break;
					}
					thisone.line.push_back(*donnees->getToken());
					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;
				case (Token::S_LOOSE):
				case (Token::S_WIN):
				case (Token::S_HIDE):
				{
					thisone.line.push_back(*donnees->getToken());
					NEXT_TOKEN;
				}
				break;
				case (Token::S_EOF):
				{
				}
				break;
				default:
					er.type=ErrorReport::ET_UNKNOWN;
					break;
			}
		}
		stories.push_back(thisone);
//Debug code
		printf("SGSL : story loaded, %d tokens, dumping now :\n", (int)thisone.line.size());
		for (unsigned  i=0; i<thisone.line.size(); i++)
			cout << "Token type " << Token::getNameByType(thisone.line[i].type) << endl;
		NEXT_TOKEN;
	}
	return er;
}

bool Mapscript::hasTeamWon(unsigned teamNumber)
{
	// Can make win or loose only player 0
	if (testMainTimer())
	{
		if (teamNumber==0)
		{
			for (std::deque<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
			{
				if ((*it).hasWon)
				return true;
			}
		}
	}
	return false;
}

bool Mapscript::hasTeamLost(unsigned teamNumber)
{
	// Can make win or loose only player 0
	if (teamNumber==0)
	{
		for (std::deque<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
		{
			if ((*it).hasLost)
				return true;
		}
	}
	return false;
}
