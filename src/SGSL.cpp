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
/*
Types associations with int numbers.
11	wait(time in seconds): time to wait before exectuting the next command
12	timer(time in seconds): same thing as wait but is drawn on the screen
13	wait(condition)
        condition:  untit_or_building number_of_player condition number 
21	show(string): diplays a string on the screen until the next command is finished
1   activate: activates the given player (on by default)
2   deactivate: deactivate the given player
3   friend: makes a player become your friend
4   ennemy: makes a player become your ennemy (on by default)

5   dead number_of_player: return true if the player is dead
6   alive number_of_player: returns true if the player is alive
4	flag(number).order: give orders to flags
1   -you: returns true if one of your globule reaches the radius of the given flag
2   -noenemy: returns true if no enemy is in the radius of the given flag
3   -noally: returns true if no ally is in the radius of the given flag
51	win: obwiously makes you win the game
52	loose: bad for you...

//condition number
player xx -dead		xx0
player xx -alive	xx1
flag xx -you		xx5
flag xx -noennemy	xx6
flag xx -noally		xx7

for the parser:
story: starts another parallel storyline, so multiple endings for a map are possible
*/

#include <iostream>
#include <string>
#include <deque>
#include "SGSL.h"
#include "Game.h"

Token::TokenSymbolLookupTable Token::table[] =
{
	{ S_WAIT, "wait" },
	{ S_EQUAL, "=" },
	{ S_HIGHER, ">" },
	{ S_LOWER, "<" },
	{ S_TIMER, "timer" },
	{ S_SHOW, "show" },
	{ S_ACTIVATE, "activate" },
	{ S_DEACTIVATE, "deactivate" },
	{ S_FRIEND, "friend" },
	{ S_ENEMY, "enemy" },
	{ S_DEAD, "dead" },
	{ S_ALIVE, "alive" },
	{ S_FLAG, "flag" },
	{ S_YOU, "you" },
	{ S_NOENEMY, "noenemy" },
	{ S_WIN, "win" },
	{ S_LOOSE, "loose" },
	{ S_STORY, "story" },
	{ S_WORKER, "nbWorker" },
	{ S_EXPLORER, "nbExplorer" },
	{ S_WARRIOR, "nbWarrior" },
	{ S_SWARM_B, "nbSwarm" },
	{ S_FOOD_B, "nbGranary" },
	{ S_HEALTH_B, "nbHospital" },
	{ S_WALKSPEED_B, "nbRacetrack" },
	{ S_SWIMSPEED_B, "nbPool" },
	{ S_ATTACK_B, "nbCamp" },
	{ S_SCIENCE_B, "nbSchool" },
	{ S_DEFENCE_B, "nbTower" },
	{ S_HIDE, "hide" },
	{ S_MARK, "mark" },
	{ S_GOBACKTO, "gobackto" },
	{ S_SETFLAG, "setflag"},
	{ S_ALLY, "ally" },
	{ S_SUMMON, "summon" },
	{ INT, "int" },
	{ STRING, "string" },
	{ NIL, NULL }
};

Token::TokenType Token::getTypeByName(const char *name)
{
	int i = 0;
	TokenType type=NIL;
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

int Story::valueOfVariable(Token nameOfVariable,int numberOfPlayer,int level)
{

	TeamStat *latestStat=mapscript->game->teams[numberOfPlayer]->stats.getLatestStat();
	switch(nameOfVariable.type)
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
			return 0;
		}
}


bool Story::conditionTesterBuildings()
{
	switch (line[lineSelector+4].type)
	{
		case (Token::S_HIGHER):
		{
			return (valueOfVariable(line[lineSelector+1],line[lineSelector+3].value,line[lineSelector+2].value) > line[lineSelector+5].value);
		}
		case (Token::S_LOWER):
		{
			return (valueOfVariable(line[lineSelector+1],line[lineSelector+3].value,line[lineSelector+2].value) < line[lineSelector+5].value);
		}
		case (Token::S_EQUAL):
		{
			return (valueOfVariable(line[lineSelector+1],line[lineSelector+3].value,line[lineSelector+2].value) == line[lineSelector+5].value);
		}
		default:
			return false;		
	}
}

bool Story::conditionTesterGlobules()
{
	switch (line[lineSelector+3].type)
	{
		case (Token::S_HIGHER):
		{
			return (valueOfVariable(line[lineSelector+1],line[lineSelector+2].value,0) > line[lineSelector+4].value);
		}
		case (Token::S_LOWER):
		{
			return (valueOfVariable(line[lineSelector+1],line[lineSelector+2].value,0) < line[lineSelector+4].value);
		}
		case (Token::S_EQUAL):
		{
			return (valueOfVariable(line[lineSelector+1],line[lineSelector+2].value,0) == line[lineSelector+4].value);
		}
		default:
			return false;		
	}
}

bool Story::testCondition()
{

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
				switch (line[lineSelector+1].type)
				{
					case(Token::INT):
					{
						lineSelector++;
						internTimer=line[lineSelector].value;
						return false;
					}
					case (Token::S_DEAD):
					{				
						if (!mapscript->game->teams[line[lineSelector+2].value]->isAlive)
						{
							lineSelector+=2;
							return true;
						}
						else
							return false;
					}
					case (Token::S_FLAG):
					{
						int x, y;
						mapscript->getFlagPos(line[lineSelector+2].msg, &x, &y);
						if (line[lineSelector+3].type==Token::S_YOU)
						{
							int dx, dy;
							for (dy=y-5; dy<y+5; dy++)
								for (dx=x-5; dx<x+5; dx++)
								{
									Sint16 uid=mapscript->game->map.getUnit(dx, dy);
									if (uid>=0)
									{
										int team=Unit::UIDtoTeam(uid);
										if (mapscript->game->teams[0]->teamNumber==team)
										{
											lineSelector +=3;
											return true;
										}
									}
								}
							
							return false;
						}
						else if (line[lineSelector+3].type==Token::S_ALLY)
						{
							int dx, dy;
							for (dy=y-5; dy<y+5; dy++)
								for (dx=x-5; dx<x+5; dx++)
								{
									Sint16 uid=mapscript->game->map.getUnit(dx, dy);
									if (uid>=0)
									{
										int team=Unit::UIDtoTeam(uid);
										Uint32 tm=1<<team;
										if (mapscript->game->teams[0]->allies & tm)
										{
											lineSelector +=3;
											return true;
										}
									}
								}
							
							return false;
						}
						else if (line[lineSelector+3].type==Token::S_NOENEMY)
						{
						
							int dx, dy;
							for (dy=y-5; dy<y+5; dy++)
								for (dx=x-5; dx<x+5; dx++)
								{
									Sint16 uid=mapscript->game->map.getUnit(dx, dy);
									if (uid!=NOUID)
									{
										int team;
										if (uid<0)
											team=Building::UIDtoTeam(uid);
										else
											team=Unit::UIDtoTeam(uid);
										
										Uint32 tm=1<<team;
										if (mapscript->game->teams[0]->enemies & tm)
										{
											return false;
										}
									}
								}
						
							lineSelector +=3;
							return true;
						}
						else
							assert(false);

					}
					case (Token::S_WORKER):
					case (Token::S_EXPLORER):
					case (Token::S_WARRIOR):
					{
						if (conditionTesterGlobules())
						{
							lineSelector +=4;
							mapscript->isTextShown=false;
							return true;
						}
						else
							return false;
					}
					break;
					default: //Test conditions
					{
						if (conditionTesterBuildings())
						{
							lineSelector +=5;
							mapscript->isTextShown=false;
							return true;
						}
						else
							return false;
					}
				}
			}
			case (Token::S_MARK):
			{
				lineSelector += 1;
				return true;
			}
			case (Token::S_GOBACKTO):
			{
				int newEmplacement;
				for (newEmplacement = lineSelector; newEmplacement > 0; newEmplacement--)
				{
					if (line[lineSelector+1].msg == line[newEmplacement].msg)
						break;
				}
				lineSelector = newEmplacement;
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
				//TODO STEPH SUMMON
				//Grammar of summon |summon(globules_amount globule_type(player_int . level) flag_name
				//-> globules amount = line[lineSelector+1].value
				// ...
				//-> flag name = line[lineSelector+5].msg
				lineSelector +=5;
				return true;
			}
			case (Token::S_SETFLAG):
			{
				Flag flag;
				bool found=false;
				
				flag.name=line[lineSelector+2].msg;
				flag.x=line[lineSelector+2].value;
				flag.y=line[lineSelector+3].value;
				
				for (vector<Flag>::iterator it=mapscript->flags.begin(); it != mapscript->flags.end(); ++it)
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
				
				lineSelector+=3;
				return true;
			}
			case (Token::S_ACTIVATE):
			{
				lineSelector ++;
				return true;
			}
			case (Token::S_DEACTIVATE):
			{
				lineSelector ++;
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
	if (testCondition())
	{
		lineSelector ++;
	}
}

using namespace std;

const char *ErrorReport::getErrorString(void)
{
	static const char *strings[]={ "No error", "Syntax error", "Invalid player", "Unknown error" };
	return strings[(int)type];
}

//Text aquisition by the parser
Aquisition::~Aquisition(void)
{
	if (fp)
		fclose(fp);
}

Aquisition::Aquisition(void)
{
	fp=NULL;
	token.type=Token::NIL;
	actLine=0;
	actCol=0;
	lastLine=0;
	lastCol=0;
}

bool Aquisition::newFile(const char *filename)
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

#define HANDLE_ERROR_POS(c) if (c=='\n') { actLine++; actCol=0; } else { actCol++; }

void Aquisition::nextToken()
{
	string mot;
	int c;
	
	lastCol=actCol;
	lastLine=actLine;

	// eat empty char
	while((c=fgetc(fp))!=EOF)
	{
		if (index(" \t\r\n().,", c)==NULL)
		{
			ungetc(c, fp);
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
	while((c=fgetc(fp))!=EOF)
	{
		if ((char)c=='"')
			isInString=!isInString;
		if (isInString)
		{
			if (index("\t\r\n", c)!=NULL)
			{
				ungetc(c, fp);
				break;
			}
		}
		else
		{
			if (index(" \t\r\n().,", c)!=NULL)
			{
				ungetc(c, fp);
				break;
			}
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

// Mapscript creation

Mapscript::Mapscript()
{
	reset();
}

Mapscript::~Mapscript(void)
{}
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

bool Mapscript::getFlagPos(string name, int *x, int *y)
{
	for (vector<Flag>::iterator it=flags.begin(); it != flags.end(); ++it)
	{
		if ((*it).name==name)
		{
			*x=(*it).x;
			*y=(*it).y;
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
		(*it).step();
	}
}


ErrorReport Mapscript::loadScript(const char *filename, Game *game)
{
	ErrorReport er;
	er.type=ErrorReport::ET_OK;
	if (donnees.newFile(filename))
	{
		reset();
		this->game=game;
		
		donnees.nextToken();
		while (donnees.getToken().type != Token::S_EOF)
		{
			Story thisone(this);
			if (er.type != ErrorReport::ET_OK)
			{
				er.line=donnees.getLine();
				er.col=donnees.getCol();
				break;
			}
			while ((donnees.getToken().type != Token::S_STORY) && (donnees.getToken().type !=Token::S_EOF))
			{
				if (er.type != ErrorReport::ET_OK)
				{
					er.line=donnees.getLine();
					er.col=donnees.getCol();
					break;
				}
				// Grammar check
				switch (donnees.getToken().type)
				{
					//Grammar for summon |summon(globules_amount globule_type(player_int . level) flag_name
					case (Token::S_SUMMON):
					{
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if (donnees.getToken().type != Token::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						else if (donnees.getToken().value > 30)
						{
							er.type=ErrorReport::ET_INVALID_VALUE;
							break;
						}
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if ((donnees.getToken().type != Token::S_WARRIOR) && (donnees.getToken().type != Token::S_WORKER) && (donnees.getToken().type != Token::S_EXPLORER))
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if (donnees.getToken().type != Token::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if (donnees.getToken().type != Token::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						else if (donnees.getToken().value > 3)
						{
							er.type=ErrorReport::ET_INVALID_VALUE;
							break;
						}
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if (donnees.getToken().type != Token::STRING)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
					}
					//Grammar for setflag | setflag(flag_name)(x.y)
					case (Token::S_SETFLAG):
					{
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if (donnees.getToken().type != Token::STRING)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						for (int i =0; i<2; i++)
						{
							if (donnees.getToken().type != Token::INT)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
						}
					}
					break;
					//Grammar of Show Mark Gobackto
					case (Token::S_SHOW):
					case (Token::S_MARK):
					case (Token::S_GOBACKTO):
					{
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if (donnees.getToken().type != Token::STRING)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
					}
					break;
					//Grammar Of Wait
					case (Token::S_WAIT):
					{
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if ((donnees.getToken().type == Token::S_DEAD) || (donnees.getToken().type == Token::S_ALIVE))
						{
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
							if (donnees.getToken().type != Token::INT)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							else if (donnees.getToken().value > game->session.numberOfTeam)
							{
								er.type=ErrorReport::ET_INVALID_PLAYER;
								break;
							}
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
						}
						else if (donnees.getToken().type == Token::S_FLAG)
						{
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
							if (donnees.getToken().type != Token::STRING)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							else if (!doesFlagExist(donnees.getToken().msg))
							{
								er.type=ErrorReport::ET_INVALID_FLAG_NAME;
								break;
							}
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
							if ((donnees.getToken().type != Token::S_YOU) && (donnees.getToken().type != Token::S_NOENEMY) && (donnees.getToken().type != Token::S_ALLY))
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
						}
						else if (donnees.getToken().type == Token::INT)
						{
							if (donnees.getToken().value <=0)
							{
								er.type=ErrorReport::ET_INVALID_VALUE;
								break;
							}
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
							break;
						}
						else if ((donnees.getToken().type > 100) && (donnees.getToken().type < 300))
						{
							if ((donnees.getToken().type > 200) && (donnees.getToken().type < 300))
							{
								thisone.line.push_back(donnees.getToken());
								donnees.nextToken();
								if (donnees.getToken().type != Token::INT)
								{
									er.type=ErrorReport::ET_SYNTAX_ERROR;
									break;
								}
								else if ((donnees.getToken().value < 0) || (donnees.getToken().value > 5))
								{
									er.type=ErrorReport::ET_INVALID_VALUE;
									break;
								}
								thisone.line.push_back(donnees.getToken());
								donnees.nextToken();
							}
							else
							{
								thisone.line.push_back(donnees.getToken());
								donnees.nextToken();
							}
							if (donnees.getToken().type != Token::INT)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							else if (donnees.getToken().value > game->session.numberOfTeam)
							{
								er.type=ErrorReport::ET_INVALID_PLAYER;
								break;
							}
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
							if ((donnees.getToken().type < 301) && (donnees.getToken().type > 303))
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
							if (donnees.getToken().type != Token::INT)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
						}
						else
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
					}
					break;
					case (Token::NIL):
					{
						donnees.nextToken();
					}
					break;
					case (Token::S_FRIEND):
					case (Token::S_ENEMY):
					{
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if (donnees.getToken().type != Token::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						else if (donnees.getToken().value > game->session.numberOfTeam)
						{
							er.type=ErrorReport::ET_INVALID_PLAYER;
							break;
						}
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if (donnees.getToken().type != Token::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						else if (donnees.getToken().value > game->session.numberOfTeam)
						{
							er.type=ErrorReport::ET_INVALID_PLAYER;
							break;
						}
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
					}
					break;
					case (Token::S_TIMER):
					{
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if (donnees.getToken().type != Token::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						if (donnees.getToken().value <=0)
						{
							er.type=ErrorReport::ET_INVALID_VALUE;
							break;
						}
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
					}
					break;
					case (Token::S_ACTIVATE):
					case (Token::S_DEACTIVATE):
					{
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if (donnees.getToken().type != Token::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						else if (donnees.getToken().value > game->session.numberOfTeam)
						{
							er.type=ErrorReport::ET_INVALID_PLAYER;
							break;
						}
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
					}
					break;
					case (Token::S_LOOSE):
					case (Token::S_WIN):
					case (Token::S_HIDE):
					{
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
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
			printf("SGSL : story loaded, %d tokens, dumping now :\n", (int)thisone.line.size());
			for (unsigned  i=0; i<thisone.line.size(); i++)
				cout << "Token type " << Token::getNameByType(thisone.line[i].type) << endl;
			donnees.nextToken();
		}
	}
	else
	{
		er.type=ErrorReport::ET_NO_SUCH_FILE;
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
