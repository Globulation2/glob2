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

#include <iostream>
#include <string>
#include <deque>
#include <vector>
#include <algorithm>
#include <string.h>
#include <math.h>
#include "SGSL.h"
#include "GameGUI.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include <Toolkit.h>
#include <StringTable.h>

Token::TokenSymbolLookupTable Token::table[] =
{
	{ INT, "int" },
	{ STRING, "string" },

	{ S_PAROPEN, "("},
	{ S_PARCLOSE, ")"},
	{ S_SEMICOL, ","},
	{ S_STORY, "story" },

	{ S_EQUAL, "=" },
	{ S_HIGHER, ">" },
	{ S_LOWER, "<" },
	{ S_NOT, "not" },

	{ S_WAIT, "wait" },
	{ S_TIMER, "timer" },
	{ S_SHOW, "show" },
	{ S_HIDE, "hide" },
	{ S_ALLIANCE, "alliance"},
	{ S_GUIENABLE, "guiEnable"},
	{ S_GUIDISABLE, "guiDisable"},
	{ S_SUMMONUNITS, "summonUnits" },
	{ S_SUMMONFLAG, "summonFlag" },
	{ S_DESTROYFLAG, "destroyFlag" },
	{ S_WIN, "win" },
	{ S_LOOSE, "loose" },
	{ S_LABEL, "label" },
	{ S_JUMP, "jump" },
	{ S_SETAREA, "setArea"},
	{ S_AREA, "area" },
	{ S_ISDEAD, "isdead" },
	{ S_ALLY, "ally" },
	{ S_ENEMY, "enemy" },

	{ S_LANG_0, "lang0" },
	{ S_LANG_1, "lang1" },
	{ S_LANG_2, "lang2" },
	{ S_LANG_3, "lang3" },
	{ S_LANG_4, "lang4" },

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
	{ S_WOODWALL_B, "WoodWall"},
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

	// NIL must be at the end because it is a stop condition... not very clean
	{ NIL, NULL },
};

Token::TokenType Token::getTypeByName(const char *name)
{
	int i = 0;
	TokenType type=NIL;

	std::cout << "Getting token for " << name << std::endl;

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
	this->mapscript=mapscript;
}

Story::~Story()
{

}

//get values from the game
int Story::valueOfVariable(const Game *game, Token::TokenType type, int teamNumber, int level)
{
	TeamStat *latestStat=game->teams[teamNumber]->stats.getLatestStat();
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
bool Story::conditionTester(const Game *game, int pc, bool l)
{
	Token::TokenType type, operation;
	int level, teamNumber, amount;

	type = line[pc++].type;
	if (l)
		level = line[pc++].value;
	teamNumber = line[pc++].value;
	operation = line[pc++].type;
	amount = line[pc].value;

	switch (operation)
	{
		case (Token::S_HIGHER):
		{
			return (valueOfVariable(game, type, teamNumber, level) > amount);
		}
		case (Token::S_LOWER):
		{
			return (valueOfVariable(game, type, teamNumber, level) < amount);
		}
		case (Token::S_EQUAL):
		{
			return (valueOfVariable(game, type, teamNumber, level) == amount);
		}
		default:
			return false;
	}
}


//main step-by-step machine
bool Story::testCondition(GameGUI *gui)
{
	Game *game = &gui->game;

	if (line.size())
		switch (line[lineSelector].type)
		{
			case (Token::S_SHOW):
			{
				unsigned lsInc=0;
				if ((line[lineSelector+2].type >= Token::S_LANG_0) ||
					(line[lineSelector+2].type <= Token::S_LANG_4))
				{
					unsigned langId = line[lineSelector+2].type - Token::S_LANG_0;
					if (langId != globalContainer->settings.defaultLanguage)
					{
						lineSelector += 2;
						return true;
					}
					lsInc = 1;
				}
				mapscript->isTextShown = true;
				mapscript->textShown = line[++lineSelector].msg;
				lineSelector += lsInc;
				return true;
			}

			case (Token::S_WIN):
			{
				mapscript->hasWon[lineSelector+1]=true;
				return false;
			}

			case (Token::S_LOOSE):
			{
				mapscript->hasLost[lineSelector+1]=true;
				return false;
			}

			case (Token::S_TIMER):
			{
				mapscript->mainTimer=line[++lineSelector].value;
				return true;
			}

			case (Token::S_ALLIANCE):
			{
				int team1 = line[++lineSelector].value;
				int team2 = line[++lineSelector].value;
				int level = line[++lineSelector].value;

				// Who do I thrust and don't fire on.
				Uint32 allies[4] = { 0, 0, 0, 1};
				// Who I don't thrust and fire on.
				Uint32 enemies[4] = { 1, 0, 0, 0};
				// Who does I share the vision of Exchange building to.
				Uint32 sharedVisionExchange[4] = { 0, 1, 1, 1};
				// Who does I share the vision of Food building to.
				Uint32 sharedVisionFood[4] = { 0, 0, 1, 1};
				// Who does I share the vision to.
				Uint32 sharedVisionOther[4] = { 0, 0, 0, 1};

				if (allies[level])
					gui->game.teams[team1]->allies |= 1<<team2;
				else
					gui->game.teams[team1]->allies &= ~(1<<team2);

				if (enemies[level])
					gui->game.teams[team1]->enemies |= 1<<team2;
				else
					gui->game.teams[team1]->enemies &= ~(1<<team2);

				if (sharedVisionExchange[level])
					gui->game.teams[team1]->sharedVisionExchange |= 1<<team2;
				else
					gui->game.teams[team1]->sharedVisionExchange &= ~(1<<team2);

				if (sharedVisionFood[level])
					gui->game.teams[team1]->sharedVisionFood |= 1<<team2;
				else
					gui->game.teams[team1]->sharedVisionFood &= ~(1<<team2);

				if (sharedVisionOther[level])
					gui->game.teams[team1]->sharedVisionOther |= 1<<team2;
				else
					gui->game.teams[team1]->sharedVisionOther &= ~(1<<team2);

				return true;
			}

			case (Token::S_LABEL):
			{
				lineSelector++;
				return true;
			}

			case (Token::S_JUMP):
			{
				lineSelector = labels[line[lineSelector+1].msg];
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

			case (Token::S_GUIENABLE):
			{
				Token::TokenType object = line[++lineSelector].type;
				if (object <= Token::S_WARRIOR)
				{
					// Units : TODO
				}
				else if (object <= Token::S_WALL_B)
				{
					gui->enableBuildingsChoice(object - Token::S_SWARM_B);
				}
				else if (object <= Token::S_FORBIDDEN_F)
				{
					gui->enableFlagsChoice(object - Token::S_EXPLOR_F);
				}
				else if (object <= Token::S_ALLIANCESCREEN)
				{
					gui->enableGUIElement(object - Token::S_BUILDINGTAB);
				}
				return true;
			}

			case (Token::S_GUIDISABLE):
			{
				Token::TokenType object = line[++lineSelector].type;
				if (object <= Token::S_WARRIOR)
				{
					// Units : TODO
				}
				else if (object <= Token::S_WALL_B)
				{
					gui->disableBuildingsChoice(object - Token::S_SWARM_B);
				}
				else if (object <= Token::S_FORBIDDEN_F)
				{
					gui->disableFlagsChoice(object - Token::S_EXPLOR_F);
				}
				else if (object <= Token::S_ALLIANCESCREEN)
				{
					gui->disableGUIElement(object - Token::S_BUILDINGTAB);
				}
				return true;
			}

			case (Token::S_SUMMONUNITS):
			{
				const std::string& areaName = line[++lineSelector].msg;
				int globulesAmount = line[++lineSelector].value;
				int type = line[++lineSelector].type - Token::S_WORKER;
				int level = line[++lineSelector].value;
				int team = line[++lineSelector].value;

				AreaMap::const_iterator fi;
				if ((fi = mapscript->areas.find(areaName)) != mapscript->areas.end())
				{
					int number = globulesAmount;
					int maxTest = number * 3;

					while ((number>0) && (maxTest>0))
					{
						int x = fi->second.x;
						int y = fi->second.y;
						int r = fi->second.r;
						int dx=(syncRand()%(2*r))+1;
						int dy=(syncRand()%(2*r))+1;
						dx-=r;
						dy-=r;

						if (dx*dx+dy*dy<r*r)
						{
							if (game->addUnit(x+dx, y+dy, team, type, level, 0, 0, 0))
							{
								number --;
							}
						}

						maxTest--;
					}
				}
				return true;
			}

			case Token::S_SUMMONFLAG:
			{
				const std::string& flagName = line[++lineSelector].msg;
				int x = line[++lineSelector].value;
				int y = line[++lineSelector].value;
				int r = line[++lineSelector].value;
				int unitCount = line[++lineSelector].value;
				int team = line[++lineSelector].value;

				int typeNum = globalContainer->buildingsTypes.getTypeNum(9, 0, false);

				Building *b = game->addBuilding(x, y, typeNum, team);

				b->unitStayRange = r;
				b->maxUnitWorking = unitCount;
				b->maxUnitWorkingPreferred = unitCount;
				b->maxUnitWorkingLocal = unitCount;
				b->update();

				mapscript->flags[flagName] = b;

				return true;
			}

			case Token::S_DESTROYFLAG:
			{
				const std::string& flagName = line[++lineSelector].msg;
				BuildingMap::iterator i;
				if ((i = mapscript->flags.find(flagName)) != mapscript->flags.end())
				{
					i->second->launchDelete();
					mapscript->flags.erase(i);
				}
				else
				{
					std::cerr << "SGSL : Unexistant flag " << flagName << " destroyed !" << std::endl;
				}

				return true;
			}

			case (Token::S_SETAREA):
			{
				Area flag;

				std::string name = line[++lineSelector].msg;
				flag.x = line[++lineSelector].value;
				flag.y = line[++lineSelector].value;
				flag.r = line[++lineSelector].value;

				mapscript->areas[name] = flag;

				return true;
			}

			case (Token::S_HIDE):
			{
				mapscript->isTextShown = false;
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
						if (!game->teams[line[execLine].value]->isAlive)
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
					case (Token::S_AREA):
					{
						int incL = 0;
						execLine++;

						AreaMap::const_iterator fi;
						if ((fi = mapscript->areas.find(line[execLine].msg)) == mapscript->areas.end())
							assert(false);

						Uint32 testMask;
						if (line[lineSelector+3+negate].type==Token::INT)
						{
							testMask = 1<<(line[lineSelector+3+negate].value);
						}
						else if (line[lineSelector+3+negate].type==Token::S_ENEMY)
						{
							testMask = game->teams[line[lineSelector+4+negate].value]->enemies;
							incL = 1;
						}
						else if (line[lineSelector+3+negate].type==Token::S_ALLY)
						{
							testMask = game->teams[line[lineSelector+4+negate].value]->allies;
							incL = 1;
						}
						else
							assert(false);

						int x = fi->second.x;
						int y = fi->second.y;
						int r = fi->second.r;
						int dx, dy;
						for (dy=y-r; dy<y+r; dy++)
							for (dx=x-r; dx<x+r; dx++)
							{
								Uint16 gid=game->map.getGroundUnit(dx, dy);
								if (gid!=NOGUID)
								{
									int team=Unit::GIDtoTeam(gid);
									if (team & testMask)
									{
										lineSelector += 3+negate+incL;
										return true;
									}
								}
							}
						return false;
					}
					case (Token::S_WORKER):
					case (Token::S_EXPLORER):
					case (Token::S_WARRIOR):
					{
						if (conditionTester(game, execLine, false))
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
						if (conditionTester(game, execLine, true))
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

			default:
				return false;
		}
	return false;
}

void Story::step(GameGUI *gui)
{
	int cycleLeft = 256;

	while (testCondition(gui) && cycleLeft)
	{
		lineSelector++;
		cycleLeft--;
	}

	if (!cycleLeft)
		std::cout << "Warning, story step took more than 256 cycles, perhaps you have infinite loop in your script" << std::endl;
}

using namespace std;

const char *ErrorReport::getErrorString(void)
{
	static const char *strings[]={
		"No error",
		"Invalid Value ",
		"Syntax error",
		"Invalid team",
		"No such file",
		"Area name not defined",
		"Area name already defined",
		"Label not defined",
		"Missing \"(\"",
		"Missing \")\"",
		"Missing \",\"",
		"Missing argument",
		"Invalid alliance level. Level must be between 0 and 3",
		"Not a valid language identifier",
		"Unknown error"
	};
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
			{
				c=this->getChar();
				HANDLE_ERROR_POS(c);
			}
		}
		newLine=false;
		//if (index(" \t\r\n().,", c)==NULL)
		if (index(" \t\r\n", c)==NULL)
		{
			this->ungetChar(c);
			break;
		}
		else if (c == '\n')
		{
			newLine=true;
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

void Mapscript::reset(void)
{
	isTextShown = false;
	mainTimer=0;
	stories.clear();
	areas.clear();
	flags.clear();

	// fill language map
	unsigned langCount = Toolkit::getStringTable()->getNumberOfLanguage();
	unsigned sgslLangCount = 5;
	assert(sgslLangCount == langCount);
	for (unsigned i=0; i<sgslLangCount; i++)
	{
		unsigned j=0;
		while (Token::table[j].type != (int)(Token::S_LANG_0+i))
			j++;
		Token::table[j].name = Toolkit::getStringTable()->getStringInLang("[language-code]", i);
	}
}

bool Mapscript::testMainTimer()
{
	return (mainTimer <= 0);
}

void Mapscript::step(GameGUI *gui)
{
	if (mainTimer)
		mainTimer--;
	for (std::deque<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
	{
		it->step(gui);
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

	// Set the size of the won/lost arrays and clear them
	hasWon.resize(game->session.numberOfTeam);
	std::fill(hasWon.begin(), hasWon.end(), false);
	hasLost.resize(game->session.numberOfTeam);
	std::fill(hasLost.begin(), hasLost.end(), false);

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
				// summonUnits(flag_name , globules_amount , globule_type , globule_level , team_int)
				case (Token::S_SUMMONUNITS):
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
					else if (areas.find(donnees->getToken()->msg) == areas.end())
					{
						er.type=ErrorReport::ET_UNDEFINED_AREA_NAME;
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
					NEXT_TOKEN; //<- team
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (donnees->getToken()->value >= game->session.numberOfTeam)
					{
						er.type=ErrorReport::ET_INVALID_TEAM;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

				// setArea(string name, int x , int y , int r)
				case (Token::S_SETAREA):
				{
					Area area;
					thisone.line.push_back(*donnees->getToken());

					CHECK_PAROPEN;
					NEXT_TOKEN; // <- string name
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::STRING)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (areas.find(donnees->getToken()->msg) != areas.end())
					{
						er.type=ErrorReport::ET_DUPLICATED_AREA_NAME;
						break;
					}
					std::string name=donnees->getToken()->msg;
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int x
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					area.x=donnees->getToken()->value;
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; //<- int y
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					area.y=donnees->getToken()->value;
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int r
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					area.r=donnees->getToken()->value;
					thisone.line.push_back(*donnees->getToken());

					areas[name] = area;

					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

				// summonFlag(string name, int x, int y, int r, int unitcount, int team)
				case (Token::S_SUMMONFLAG):
				{
					thisone.line.push_back(*donnees->getToken());

					CHECK_PAROPEN;
					NEXT_TOKEN; // <- string name
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::STRING)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int x
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int y
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int r
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int unitcount
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int team
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (donnees->getToken()->value >= game->session.numberOfTeam)
					{
						er.type=ErrorReport::ET_INVALID_TEAM;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

				// destroyFlag(string name)
				case (Token::S_DESTROYFLAG):
				{
					thisone.line.push_back(*donnees->getToken());

					CHECK_PAROPEN;
					NEXT_TOKEN; // <- string name
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

				// Alliance
				case (Token::S_ALLIANCE):
				{
					thisone.line.push_back(*donnees->getToken()); //<-SETAREA

					// team 1
					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					if (donnees->getToken()->value >= game->session.numberOfTeam)
					{
						er.type=ErrorReport::ET_INVALID_TEAM;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					// team 2
					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					if (donnees->getToken()->value >= game->session.numberOfTeam)
					{
						er.type=ErrorReport::ET_INVALID_TEAM;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					// level
					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					if (donnees->getToken()->value > 3)
					{
						er.type=ErrorReport::ET_INVALID_ALLIANCE_LEVEL;
						break;
					}
					thisone.line.push_back(*donnees->getToken());
				}
				break;

				// Show, Label, Jump
				case (Token::S_SHOW):
				case (Token::S_LABEL):
				case (Token::S_JUMP):
				{
					Token::TokenType type = donnees->getToken()->type;

					thisone.line.push_back(*donnees->getToken());
					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != Token::STRING)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}

					if (type == Token::S_LABEL)
					{
						// add label to table
						thisone.labels[donnees->getToken()->msg] = thisone.line.size();
					}
					if (type == Token::S_JUMP)
					{
						// complain if label doesn't exists
						if (thisone.labels.find(donnees->getToken()->msg) == thisone.labels.end())
						{
							er.type=ErrorReport::ET_UNDEFINED_LABEL;
							break;
						}
					}

					if (type == Token::S_SHOW)
					{
						thisone.line.push_back(*donnees->getToken());
						NEXT_TOKEN;
						if (donnees->getToken()->type != Token::S_PARCLOSE)
						{
							// This is a multilingual show
							if (donnees->getToken()->type != Token::S_SEMICOL)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							NEXT_TOKEN;
							CHECK_ARGUMENT;
							if ((donnees->getToken()->type < Token::S_LANG_0) ||
								(donnees->getToken()->type > Token::S_LANG_4))
							{
								er.type=ErrorReport::ET_NOT_VALID_LANG_ID;
								break;
							}
							thisone.line.push_back(*donnees->getToken());
							CHECK_PARCLOSE;
							NEXT_TOKEN;
						}
						else
						{
							NEXT_TOKEN;
						}
					}
					else
					{
						thisone.line.push_back(*donnees->getToken());
						CHECK_PARCLOSE;
						NEXT_TOKEN;
					}
				}
				break;

				// Wait | wait ( int) or wait ( isdead( teamNumber ) ) or wait( condition ) or wait( not( condition ) )
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

					// isdead( teamNumber )
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
							er.type=ErrorReport::ET_INVALID_TEAM;
							break;
						}
						thisone.line.push_back(*donnees->getToken());
						CHECK_PARCLOSE;
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
					// area ("areaname" , who*)
					if (donnees->getToken()->type == Token::S_AREA && !enter)
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
						else if (areas.find(donnees->getToken()->msg) == areas.end())
						{
							er.type=ErrorReport::ET_UNDEFINED_AREA_NAME;
							break;
						}
						thisone.line.push_back(*donnees->getToken());

						CHECK_SEMICOL;
						NEXT_TOKEN;
						CHECK_ARGUMENT;
						thisone.line.push_back(*donnees->getToken());
						if (donnees->getToken()->type != Token::INT)
						{
							if ((donnees->getToken()->type != Token::S_ENEMY)
								&& (donnees->getToken()->type != Token::S_ALLY))
								{
									er.type=ErrorReport::ET_SYNTAX_ERROR;
									break;
								}
							else
							{
								// we have enemy or ally
								CHECK_PAROPEN;
								NEXT_TOKEN;
								if (donnees->getToken()->type != Token::INT)
								{
									er.type=ErrorReport::ET_SYNTAX_ERROR;
									break;
								}
								thisone.line.push_back(*donnees->getToken());
								CHECK_PARCLOSE;
							}
						}
						CHECK_PARCLOSE;
					}
					//Comparaison| ( Variable( team , "flagName" ) cond value ) : variable = unit
					//( Variable( level , team , "flagName" ) cond value ) : variable = building
					//"flagName" can be omitted !
					if ((donnees->getToken()->type >= Token::S_WORKER) && (donnees->getToken()->type <= Token::S_MARKET_B) && !enter)
					{
						enter = true;

						thisone.line.push_back(*donnees->getToken());
						if (donnees->getToken()->type >= Token::S_SWARM_B)
						{
							// Buildings
							// level
							CHECK_PAROPEN;
							NEXT_TOKEN;
							CHECK_ARGUMENT;
							if (donnees->getToken()->type != Token::INT)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							else if ((donnees->getToken()->value < 0) || (donnees->getToken()->value > 2))
							{
								er.type=ErrorReport::ET_INVALID_VALUE;
								break;
							}
							thisone.line.push_back(*donnees->getToken());
							CHECK_SEMICOL;
							NEXT_TOKEN;
						}
						else
						{
							// Units
							CHECK_PAROPEN;
							NEXT_TOKEN;
							CHECK_ARGUMENT;
						}

						// team
						if (donnees->getToken()->type != Token::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						else if (donnees->getToken()->value >= game->session.numberOfTeam)
						{
							er.type=ErrorReport::ET_INVALID_TEAM;
							break;
						}
						thisone.line.push_back(*donnees->getToken());
						NEXT_TOKEN;
						//Optional "areaName"
						if (donnees->getToken()->type != Token::S_PARCLOSE)
						{
							NEXT_TOKEN;
							//there is a areaName
							if (donnees->getToken()->type != Token::STRING)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							else if (areas.find(donnees->getToken()->msg) == areas.end())
							{
								er.type=ErrorReport::ET_UNDEFINED_AREA_NAME;
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
						if ((donnees->getToken()->type < Token::S_EQUAL) || (donnees->getToken()->type > Token::S_LOWER))
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

				// Grammar of Timer( int )
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
					thisone.line.push_back(*donnees->getToken());

					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

				// Enable or disable a GUI element
				case (Token::S_GUIENABLE):
				case (Token::S_GUIDISABLE):
				{
					thisone.line.push_back(*donnees->getToken());

					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if ((donnees->getToken()->type < Token::S_WORKER) || (donnees->getToken()->type > Token::S_ALLIANCESCREEN))
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
						er.type=ErrorReport::ET_INVALID_TEAM;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

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
		// Debug code
		printf("SGSL : story loaded, %d tokens, dumping now :\n", (int)thisone.line.size());
		for (unsigned  i=0; i<thisone.line.size(); i++)
			cout << "Token type " << Token::getNameByType(thisone.line[i].type) << endl;
		NEXT_TOKEN;
	}
	return er;
}

bool Mapscript::hasTeamWon(unsigned teamNumber)
{
	if (testMainTimer())
	{
		return hasWon[teamNumber];
	}
	return false;
}

bool Mapscript::hasTeamLost(unsigned teamNumber)
{
	return hasLost[teamNumber];
}
